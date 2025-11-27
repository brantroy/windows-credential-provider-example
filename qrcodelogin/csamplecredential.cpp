//
// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//
//

#ifndef WIN32_NO_STATUS
#include <ntstatus.h>
#define WIN32_NO_STATUS
#endif
#include <unknwn.h>
#include <wincred.h>
#include <windows.h>
#include <gdiplus.h>
#include <wininet.h>
#include <strsafe.h>
#include <thread>
#include <chrono>
#include "CSampleCredential.h"
#include "guid.h"

using namespace Gdiplus;
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "wininet.lib")

// CSampleCredential ////////////////////////////////////////////////////////

CSampleCredential::CSampleCredential():
    _cRef(1),
    _cpus(CPUS_INVALID),
    _dwFlags(0),
    _pCredProvCredentialEvents(NULL),
    _hQRCodeBitmap(NULL),
    _bPollingActive(false),
    _pPollingThread(NULL)
{
    DllAddRef();

    ZeroMemory(_rgCredProvFieldDescriptors, sizeof(_rgCredProvFieldDescriptors));
    ZeroMemory(_rgFieldStatePairs, sizeof(_rgFieldStatePairs));
    ZeroMemory(_rgFieldStrings, sizeof(_rgFieldStrings));
}

CSampleCredential::~CSampleCredential()
{
    if (_rgFieldStrings[SFI_PASSWORD])
    {
        size_t lenPassword = lstrlen(_rgFieldStrings[SFI_PASSWORD]);
        SecureZeroMemory(_rgFieldStrings[SFI_PASSWORD], lenPassword * sizeof(*_rgFieldStrings[SFI_PASSWORD]));
    }
    for (int i = 0; i < ARRAYSIZE(_rgFieldStrings); i++)
    {
        CoTaskMemFree(_rgFieldStrings[i]);
        CoTaskMemFree(_rgCredProvFieldDescriptors[i].pszLabel);
    }

    _CleanupQRCodeBitmap();
    
    // Stop and clean up polling thread if active
    if (_bPollingActive)
    {
        _bPollingActive = false;
        if (_pPollingThread && _pPollingThread->joinable())
        {
            _pPollingThread->join();
        }
        delete _pPollingThread;
        _pPollingThread = NULL;
    }
    
    DllRelease();
}

// Initializes one credential with the field information passed in.
// Set the value of the SFI_USERNAME field to pwzUsername.
HRESULT CSampleCredential::Initialize(
    __in CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus,
    __in const CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR* rgcpfd,
    __in const FIELD_STATE_PAIR* rgfsp,
    __in DWORD dwFlags,
    __in PCWSTR pwzUsername,
    __in PCWSTR pwzPassword
    )
{
    HRESULT hr = S_OK;
    _cpus = cpus;
    _dwFlags = dwFlags;
    // Copy the field descriptors for each field. This is useful if you want to vary the 
    // field descriptors based on what Usage scenario the credential was created for.
    for (DWORD i = 0; SUCCEEDED(hr) && i < ARRAYSIZE(_rgCredProvFieldDescriptors); i++)
    {
        _rgFieldStatePairs[i] = rgfsp[i];
        hr = FieldDescriptorCopy(rgcpfd[i], &_rgCredProvFieldDescriptors[i]);
    }

    // Initialize the String values of all the fields.
    if (SUCCEEDED(hr))
    {
        hr = SHStrDupW(pwzUsername, &_rgFieldStrings[SFI_USERNAME]);
    }
    if (SUCCEEDED(hr))
    {
        hr = SHStrDupW(pwzPassword ? pwzPassword : L"", &_rgFieldStrings[SFI_PASSWORD]);
    }
    if (SUCCEEDED(hr))
    {
        hr = SHStrDupW(L"Submit", &_rgFieldStrings[SFI_SUBMIT_BUTTON]);
    }

    return S_OK;
}

// LogonUI calls this in order to give us a callback in case we need to notify it of anything.
HRESULT CSampleCredential::Advise(__in ICredentialProviderCredentialEvents* pcpce)
{
    if (_pCredProvCredentialEvents != NULL)
    {
        _pCredProvCredentialEvents->Release();
    }
    _pCredProvCredentialEvents = pcpce;
    _pCredProvCredentialEvents->AddRef();
    return S_OK;
}

// LogonUI calls this to tell us to release the callback.
HRESULT CSampleCredential::UnAdvise()
{
    if (_pCredProvCredentialEvents)
    {
        _pCredProvCredentialEvents->Release();
    }
    _pCredProvCredentialEvents = NULL;
    return S_OK;
}

// LogonUI calls this function when our tile is selected (zoomed).
// If you simply want fields to show/hide based on the selected state,
// there's no need to do anything here - you can set that up in the 
// field definitions.  But if you want to do something
// more complicated, like change the contents of a field when the tile is
// selected, you would do it here.
HRESULT CSampleCredential::SetSelected(__out BOOL* pbAutoLogon)  
{
    *pbAutoLogon = FALSE;  

    // Generate QR code when the tile is selected
    if (!_hQRCodeBitmap)
    {
        PWSTR pszURL = NULL;
        HRESULT hrURL = _GetQRCodeURL(&pszURL);
        if (SUCCEEDED(hrURL) && pszURL)
        {
            _GenerateQRCodeBitmap(pszURL);
            CoTaskMemFree(pszURL);
        }
    }
    
    // Start polling for login status after a short delay
    _StartPollingForLogin();

    return S_OK;
}

// Similarly to SetSelected, LogonUI calls this when your tile was selected
// and now no longer is. The most common thing to do here (which we do below)
// is to clear out the password field.
HRESULT CSampleCredential::SetDeselected()
{
    HRESULT hr = S_OK;
    if (_rgFieldStrings[SFI_PASSWORD])
    {
        size_t lenPassword = lstrlen(_rgFieldStrings[SFI_PASSWORD]);
        SecureZeroMemory(_rgFieldStrings[SFI_PASSWORD], lenPassword * sizeof(*_rgFieldStrings[SFI_PASSWORD]));

        CoTaskMemFree(_rgFieldStrings[SFI_PASSWORD]);
        hr = SHStrDupW(L"", &_rgFieldStrings[SFI_PASSWORD]);
        if (SUCCEEDED(hr) && _pCredProvCredentialEvents)
        {
            _pCredProvCredentialEvents->SetFieldString(this, SFI_PASSWORD, _rgFieldStrings[SFI_PASSWORD]);
        }
    }

    return hr;
}

// Gets info for a particular field of a tile. Called by logonUI to get information to 
// display the tile.
HRESULT CSampleCredential::GetFieldState(
    __in DWORD dwFieldID,
    __out CREDENTIAL_PROVIDER_FIELD_STATE* pcpfs,
    __out CREDENTIAL_PROVIDER_FIELD_INTERACTIVE_STATE* pcpfis
    )
{
    HRESULT hr;

    // Validate paramters.
    if ((dwFieldID < ARRAYSIZE(_rgFieldStatePairs)) && pcpfs && pcpfis)
    {
        *pcpfs = _rgFieldStatePairs[dwFieldID].cpfs;
        *pcpfis = _rgFieldStatePairs[dwFieldID].cpfis;

        hr = S_OK;
    }
    else
    {
        hr = E_INVALIDARG;
    }
    return hr;
}

// Sets ppwz to the string value of the field at the index dwFieldID.
HRESULT CSampleCredential::GetStringValue(
    __in DWORD dwFieldID, 
    __deref_out PWSTR* ppwz
    )
{
    HRESULT hr;

    // Check to make sure dwFieldID is a legitimate index.
    if (dwFieldID < ARRAYSIZE(_rgCredProvFieldDescriptors) && ppwz) 
    {
        // Make a copy of the string and return that. The caller
        // is responsible for freeing it.
        hr = SHStrDupW(_rgFieldStrings[dwFieldID], ppwz);
    }
    else
    {
        hr = E_INVALIDARG;
    }

    return hr;
}

// Gets the image to show in the user tile.
HRESULT CSampleCredential::GetBitmapValue(
    __in DWORD dwFieldID, 
    __out HBITMAP* phbmp
    )
{
    HRESULT hr;
    if ((SFI_TILEIMAGE == dwFieldID) && phbmp)
    {
        HBITMAP hbmp = LoadBitmap(HINST_THISDLL, MAKEINTRESOURCE(IDB_TILE_IMAGE));
        if (hbmp != NULL)
        {
            hr = S_OK;
            *phbmp = hbmp;
        }
        else
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
        }
    }
    else if ((SFI_QRCODEIMAGE == dwFieldID) && phbmp)
    {
        // Generate QR code if not already generated
        if (!_hQRCodeBitmap)
        {
            PWSTR pszURL = NULL;
            HRESULT hrURL = _GetQRCodeURL(&pszURL);
            if (SUCCEEDED(hrURL) && pszURL)
            {
                _GenerateQRCodeBitmap(pszURL);
                CoTaskMemFree(pszURL);
            }
        }

        if (_hQRCodeBitmap)
        {
            *phbmp = (HBITMAP)CopyImage(_hQRCodeBitmap, IMAGE_BITMAP, 0, 0, LR_COPYRETURNORG);
            hr = S_OK;
        }
        else
        {
            hr = E_FAIL;
        }
    }
    else
    {
        hr = E_INVALIDARG;
    }

    return hr;
}

// Sets pdwAdjacentTo to the index of the field the submit button should be 
// adjacent to. We recommend that the submit button is placed next to the last
// field which the user is required to enter information in. Optional fields
// should be below the submit button.
HRESULT CSampleCredential::GetSubmitButtonValue(
    __in DWORD dwFieldID,
    __out DWORD* pdwAdjacentTo
    )
{
    HRESULT hr;

    // Validate parameters.
    if ((SFI_SUBMIT_BUTTON == dwFieldID) && pdwAdjacentTo)
    {
        // pdwAdjacentTo is a pointer to the fieldID you want the submit button to appear next to.
        *pdwAdjacentTo = SFI_PASSWORD;
        hr = S_OK;
    }
    else
    {
        hr = E_INVALIDARG;
    }
    return hr;
}

// Sets the value of a field which can accept a string as a value.
// This is called on each keystroke when a user types into an edit field.
HRESULT CSampleCredential::SetStringValue(
    __in DWORD dwFieldID, 
    __in PCWSTR pwz      
    )
{
    HRESULT hr;

    // Validate parameters.
    if (dwFieldID < ARRAYSIZE(_rgCredProvFieldDescriptors) && 
       (CPFT_EDIT_TEXT == _rgCredProvFieldDescriptors[dwFieldID].cpft || 
        CPFT_PASSWORD_TEXT == _rgCredProvFieldDescriptors[dwFieldID].cpft)) 
    {
        PWSTR* ppwzStored = &_rgFieldStrings[dwFieldID];
        CoTaskMemFree(*ppwzStored);
        hr = SHStrDupW(pwz, ppwzStored);
    }
    else
    {
        hr = E_INVALIDARG;
    }

    return hr;
}

//------------- 
// The following methods are for logonUI to get the values of various UI elements and then communicate
// to the credential about what the user did in that field.  However, these methods are not implemented
// because our tile doesn't contain these types of UI elements
HRESULT CSampleCredential::GetCheckboxValue(
    __in DWORD dwFieldID, 
    __out BOOL* pbChecked,
    __deref_out PWSTR* ppwzLabel
    )
{
    UNREFERENCED_PARAMETER(dwFieldID);
    UNREFERENCED_PARAMETER(pbChecked);
    UNREFERENCED_PARAMETER(ppwzLabel);

    return E_NOTIMPL;
}

HRESULT CSampleCredential::GetComboBoxValueCount(
    __in DWORD dwFieldID, 
    __out DWORD* pcItems, 
    __out_range(<,*pcItems) DWORD* pdwSelectedItem
    )
{
    UNREFERENCED_PARAMETER(dwFieldID);
    UNREFERENCED_PARAMETER(pcItems);
    UNREFERENCED_PARAMETER(pdwSelectedItem);
    return E_NOTIMPL;
}

HRESULT CSampleCredential::GetComboBoxValueAt(
    __in DWORD dwFieldID, 
    __in DWORD dwItem,
    __deref_out PWSTR* ppwzItem
    )
{
    UNREFERENCED_PARAMETER(dwFieldID);
    UNREFERENCED_PARAMETER(dwItem);
    UNREFERENCED_PARAMETER(ppwzItem);
    return E_NOTIMPL;
}

HRESULT CSampleCredential::SetCheckboxValue(
    __in DWORD dwFieldID, 
    __in BOOL bChecked
    )
{
    UNREFERENCED_PARAMETER(dwFieldID);
    UNREFERENCED_PARAMETER(bChecked);

    return E_NOTIMPL;
}

HRESULT CSampleCredential::SetComboBoxSelectedValue(
    __in DWORD dwFieldId,
    __in DWORD dwSelectedItem
    )
{
    UNREFERENCED_PARAMETER(dwFieldId);
    UNREFERENCED_PARAMETER(dwSelectedItem);
    return E_NOTIMPL;
}

HRESULT CSampleCredential::CommandLinkClicked(__in DWORD dwFieldID)
{
    UNREFERENCED_PARAMETER(dwFieldID);
    return E_NOTIMPL;
}
//------ end of methods for controls we don't have in our tile ----//


// Collect the username and password into a serialized credential for the correct usage scenario 
// (logon/unlock is what's demonstrated in this sample).  LogonUI then passes these credentials 
// back to the system to log on.
HRESULT CSampleCredential::GetSerialization(
    __out CREDENTIAL_PROVIDER_GET_SERIALIZATION_RESPONSE* pcpgsr,
    __out CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION* pcpcs, 
    __deref_out_opt PWSTR* ppwzOptionalStatusText, 
    __out CREDENTIAL_PROVIDER_STATUS_ICON* pcpsiOptionalStatusIcon
    )
{
    UNREFERENCED_PARAMETER(ppwzOptionalStatusText);
    UNREFERENCED_PARAMETER(pcpsiOptionalStatusIcon);

    HRESULT hr;

    WCHAR wsz[MAX_COMPUTERNAME_LENGTH+1];
    DWORD cch = ARRAYSIZE(wsz);

    DWORD cb = 0;
    BYTE* rgb = NULL;

    if (GetComputerNameW(wsz, &cch))
    {
        PWSTR pwzProtectedPassword;

        hr = ProtectIfNecessaryAndCopyPassword(_rgFieldStrings[SFI_PASSWORD], _cpus, &pwzProtectedPassword);

        // Only CredUI scenarios should use CredPackAuthenticationBuffer.  Custom packing logic is necessary for
        // logon and unlock scenarios in order to specify the correct MessageType.
        if (CPUS_CREDUI == _cpus)
        {
            if (SUCCEEDED(hr))
            {
                PWSTR pwzDomainUsername = NULL;
                hr = DomainUsernameStringAlloc(wsz, _rgFieldStrings[SFI_USERNAME], &pwzDomainUsername);
                if (SUCCEEDED(hr))
                {
                    // We use KERB_INTERACTIVE_UNLOCK_LOGON in both unlock and logon scenarios.  It contains a
                    // KERB_INTERACTIVE_LOGON to hold the creds plus a LUID that is filled in for us by Winlogon
                    // as necessary.
                    if (!CredPackAuthenticationBufferW((CREDUIWIN_PACK_32_WOW & _dwFlags) ? CRED_PACK_WOW_BUFFER : 0, pwzDomainUsername, pwzProtectedPassword, rgb, &cb))
                    {
                        if (ERROR_INSUFFICIENT_BUFFER == GetLastError())
                        {
                            rgb = (BYTE*)HeapAlloc(GetProcessHeap(), 0, cb);
                            if (rgb)
                            {
                                // If the CREDUIWIN_PACK_32_WOW flag is set we need to return 32 bit buffers to our caller we do this by 
                                // passing CRED_PACK_WOW_BUFFER to CredPacAuthenticationBufferW.
                                if (!CredPackAuthenticationBufferW((CREDUIWIN_PACK_32_WOW & _dwFlags) ? CRED_PACK_WOW_BUFFER : 0, pwzDomainUsername, pwzProtectedPassword, rgb, &cb))
                                {
                                    HeapFree(GetProcessHeap(), 0, rgb);
                                    hr = HRESULT_FROM_WIN32(GetLastError());
                                }
                                else
                                {
                                    hr = S_OK;
                                }
                            }
                            else
                            {
                                hr = E_OUTOFMEMORY;
                            }
                        }
                        else
                        {
                            hr = E_FAIL;
                        }
                        HeapFree(GetProcessHeap(), 0, pwzDomainUsername);
                    }
                    else
                    {
                        hr = E_FAIL;
                    }
                }
                CoTaskMemFree(pwzProtectedPassword);
            }
        }
        else
        {

            KERB_INTERACTIVE_UNLOCK_LOGON kiul;

            // For QR code login, we can use the current user's credentials or a specific user
            // For demonstration, we'll use a default user or the current user context
            PWSTR pszUsername = const_cast<PWSTR>(_rgFieldStrings[SFI_USERNAME]);
            if (!pszUsername || wcslen(pszUsername) == 0)
            {
                // Use a default username for QR code login
                pszUsername = const_cast<PWSTR>(L"QRCodeUser");
            }
            
            hr = KerbInteractiveUnlockLogonInit(wsz, pszUsername, pwzProtectedPassword, _cpus, &kiul);

            if (SUCCEEDED(hr))
            {
                // We use KERB_INTERACTIVE_UNLOCK_LOGON in both unlock and logon scenarios.  It contains a
                // KERB_INTERACTIVE_LOGON to hold the creds plus a LUID that is filled in for us by Winlogon
                // as necessary.
                hr = KerbInteractiveUnlockLogonPack(kiul, &pcpcs->rgbSerialization, &pcpcs->cbSerialization);
            }
        }

        if (SUCCEEDED(hr))
        {
            ULONG ulAuthPackage;
            hr = RetrieveNegotiateAuthPackage(&ulAuthPackage);
            if (SUCCEEDED(hr))
            {
                pcpcs->ulAuthenticationPackage = ulAuthPackage;
                pcpcs->clsidCredentialProvider = CLSID_CSample;

                // In CredUI scenarios, we must pass back the buffer constructed with CredPackAuthenticationBuffer.
                if (CPUS_CREDUI == _cpus)
                {
                    pcpcs->rgbSerialization = rgb;
                    pcpcs->cbSerialization = cb;
                }

                // At this point the credential has created the serialized credential used for logon
                // By setting this to CPGSR_RETURN_CREDENTIAL_FINISHED we are letting logonUI know
                // that we have all the information we need and it should attempt to submit the 
                // serialized credential.
                *pcpgsr = CPGSR_RETURN_CREDENTIAL_FINISHED;
            }
            else 
            {
                HeapFree(GetProcessHeap(), 0, rgb);
            }
        }
    }
    else
    {
        DWORD dwErr = GetLastError();
        hr = HRESULT_FROM_WIN32(dwErr);
    }

    return hr;
}
struct REPORT_RESULT_STATUS_INFO
{
    NTSTATUS ntsStatus;
    NTSTATUS ntsSubstatus;
    PWSTR     pwzMessage;
    CREDENTIAL_PROVIDER_STATUS_ICON cpsi;
};

static const REPORT_RESULT_STATUS_INFO s_rgLogonStatusInfo[] =
{
    { STATUS_LOGON_FAILURE, STATUS_SUCCESS, L"Incorrect password or username.", CPSI_ERROR, },
    { STATUS_ACCOUNT_RESTRICTION, STATUS_ACCOUNT_DISABLED, L"The account is disabled.", CPSI_WARNING },
};

// ReportResult is completely optional.  Its purpose is to allow a credential to customize the string
// and the icon displayed in the case of a logon failure.  For example, we have chosen to 
// customize the error shown in the case of bad username/password and in the case of the account
// being disabled.
HRESULT CSampleCredential::ReportResult(
    __in NTSTATUS ntsStatus, 
    __in NTSTATUS ntsSubstatus,
    __deref_out_opt PWSTR* ppwzOptionalStatusText, 
    __out CREDENTIAL_PROVIDER_STATUS_ICON* pcpsiOptionalStatusIcon
    )
{
    *ppwzOptionalStatusText = NULL;
    *pcpsiOptionalStatusIcon = CPSI_NONE;

    DWORD dwStatusInfo = (DWORD)-1;

    // Look for a match on status and substatus.
    for (DWORD i = 0; i < ARRAYSIZE(s_rgLogonStatusInfo); i++)
    {
        if (s_rgLogonStatusInfo[i].ntsStatus == ntsStatus && s_rgLogonStatusInfo[i].ntsSubstatus == ntsSubstatus)
        {
            dwStatusInfo = i;
            break;
        }
    }

    if ((DWORD)-1 != dwStatusInfo)
    {
        if (SUCCEEDED(SHStrDupW(s_rgLogonStatusInfo[dwStatusInfo].pwzMessage, ppwzOptionalStatusText)))
        {
            *pcpsiOptionalStatusIcon = s_rgLogonStatusInfo[dwStatusInfo].cpsi;
        }
    }
    // If we failed the logon, try to erase the password field.
    if (!SUCCEEDED(HRESULT_FROM_NT(ntsStatus)))
    {
        if (_pCredProvCredentialEvents)
        {
            _pCredProvCredentialEvents->SetFieldString(this, SFI_PASSWORD, L"");
        }
    }


    // Since NULL is a valid value for *ppwzOptionalStatusText and *pcpsiOptionalStatusIcon
    // this function can't fail.
    return S_OK;
}

// Generate QR code bitmap from URL
void CSampleCredential::_GenerateQRCodeBitmap(PCWSTR pszURL)
{
    // Clean up existing bitmap if any
    _CleanupQRCodeBitmap();

    // Create a simple QR code-like pattern for demonstration
    // In a real implementation, you would integrate with a QR code library
    HDC hdcScreen = GetDC(NULL);
    if (hdcScreen)
    {
        // Create a compatible DC and bitmap
        HDC hdcMem = CreateCompatibleDC(hdcScreen);
        if (hdcMem)
        {
            // Create a 200x220 pixel bitmap for the QR code (extra 20 pixels for URL text)
            BITMAPINFO bmi = {0};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = 200;
            bmi.bmiHeader.biHeight = -220; // Top-down DIB
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 24;
            bmi.bmiHeader.biCompression = BI_RGB;
            
            void* pBits = NULL;
            HBITMAP hbm = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
            if (hbm)
            {
                HGDIOBJ hbmOld = SelectObject(hdcMem, hbm);
                
                // Fill with white background
                RECT rc = {0, 0, 200, 220};
                HBRUSH hbrWhite = CreateSolidBrush(RGB(255, 255, 255));
                FillRect(hdcMem, &rc, hbrWhite);
                DeleteObject(hbrWhite);
                
                // Draw a simple QR code pattern for demonstration
                // In a real implementation, you would generate an actual QR code from the URL
                HPEN hpenBlack = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
                HPEN hpenOld = (HPEN)SelectObject(hdcMem, hpenBlack);
                
                // Draw a simple pattern to represent a QR code
                // Draw border squares
                Rectangle(hdcMem, 10, 10, 50, 50);  // Top-left
                Rectangle(hdcMem, 150, 10, 190, 50); // Top-right
                Rectangle(hdcMem, 10, 150, 50, 190); // Bottom-left
                
                // Draw some pattern inside to make it look like a QR code
                for (int i = 0; i < 10; i++) {
                    for (int j = 0; j < 10; j++) {
                        if ((i + j) % 2 == 0) { // Simple alternating pattern
                            RECT rect = {60 + i * 12, 60 + j * 12, 70 + i * 12, 70 + j * 12};
                            HBRUSH hbrBlack = CreateSolidBrush(RGB(0, 0, 0));
                            FillRect(hdcMem, &rect, hbrBlack);
                            DeleteObject(hbrBlack);
                        }
                    }
                }
                
                // Draw URL text below the QR code pattern to show which URL it represents
                if (pszURL)
                {
                    SetTextColor(hdcMem, RGB(0, 0, 0));
                    SetBkMode(hdcMem, TRANSPARENT);
                    HFONT hFont = CreateFont(12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
                                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                             DEFAULT_PITCH | FF_SWISS, L"Arial");
                    HFONT hOldFont = (HFONT)SelectObject(hdcMem, hFont);
                    
                    RECT textRect = {5, 195, 195, 215};
                    DrawText(hdcMem, pszURL, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    
                    SelectObject(hdcMem, hOldFont);
                    DeleteObject(hFont);
                }
                
                SelectObject(hdcMem, hpenOld);
                DeleteObject(hpenBlack);
                
                // Store the bitmap
                _hQRCodeBitmap = (HBITMAP)CopyImage(hbm, IMAGE_BITMAP, 0, 0, LR_COPYRETURNORG);
                
                SelectObject(hdcMem, hbmOld);
            }
            DeleteDC(hdcMem);
        }
        ReleaseDC(NULL, hdcScreen);
    }
}

// Cleanup QR code bitmap
void CSampleCredential::_CleanupQRCodeBitmap()
{
    if (_hQRCodeBitmap)
    {
        DeleteObject(_hQRCodeBitmap);
        _hQRCodeBitmap = NULL;
    }
}

// Get QR code URL from server
HRESULT CSampleCredential::_GetQRCodeURL(PWSTR* ppwszURL)
{
    // First try to get QR code URL from server API
    HRESULT hr = _CallQRCodeAPI(ppwszURL);
    if (FAILED(hr))
    {
        // If API call fails, fallback to static URL for demonstration
        PCWSTR staticURL = L"https://example.com/qrcode/login?token=qr_123456789";
        size_t cch = wcslen(staticURL) + 1;
        *ppwszURL = (PWSTR)CoTaskMemAlloc(cch * sizeof(WCHAR));
        if (*ppwszURL)
        {
            wcscpy_s(*ppwszURL, cch, staticURL);
            return S_OK;
        }
        return E_OUTOFMEMORY;
    }
    return hr;
}

// Poll login status from server
HRESULT CSampleCredential::_PollLoginStatus()
{
    // Example API endpoint for polling login status
    PCWSTR apiEndpoint = L"your-api-server.com";
    PCWSTR apiPath = L"/api/qrcode/status";
    
    HINTERNET hInternet = NULL, hConnect = NULL, hRequest = NULL;
    HRESULT hr = S_FALSE; // Return S_FALSE to indicate login not yet complete
    
    hInternet = InternetOpen(L"QRCodeLoginClient", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (hInternet)
    {
        hConnect = InternetConnect(hInternet, apiEndpoint, INTERNET_DEFAULT_HTTPS_PORT, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
        if (hConnect)
        {
            hRequest = HttpOpenRequest(hConnect, L"GET", apiPath, NULL, NULL, NULL, INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD, 0);
            if (hRequest)
            {
                // In a real implementation, we would send the request and check for login status
                // For now, we'll simulate a successful login after some time
                // This would normally check if the QR code has been scanned and authenticated
                
                // For demonstration, return S_OK to simulate successful login
                hr = S_OK;
                
                InternetCloseHandle(hRequest);
            }
            InternetCloseHandle(hConnect);
        }
        InternetCloseHandle(hInternet);
    }
    
    return hr;
}

// Call QR code API to get the QR code URL
HRESULT CSampleCredential::_CallQRCodeAPI(PWSTR* ppwszURL)
{
    // For now, return the same static URL as fallback, but in the future
    // this would make an actual HTTP request to get the QR code URL
    
    // Example API endpoint - in a real implementation you would call this
    PCWSTR apiEndpoint = L"your-api-server.com";
    PCWSTR apiPath = L"/api/qrcode/generate";
    
    HINTERNET hInternet = NULL, hConnect = NULL, hRequest = NULL;
    HRESULT hr = E_FAIL;
    
    hInternet = InternetOpen(L"QRCodeLoginClient", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (hInternet)
    {
        hConnect = InternetConnect(hInternet, apiEndpoint, INTERNET_DEFAULT_HTTPS_PORT, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
        if (hConnect)
        {
            hRequest = HttpOpenRequest(hConnect, L"POST", apiPath, NULL, NULL, NULL, INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD, 0);
            if (hRequest)
            {
                // In a real implementation, we would send the request and parse the JSON response
                // For now, just return a static URL as an example
                PCWSTR responseURL = L"https://example.com/qrcode/login?token=qr_api_generated_987654321";
                size_t cch = wcslen(responseURL) + 1;
                *ppwszURL = (PWSTR)CoTaskMemAlloc(cch * sizeof(WCHAR));
                if (*ppwszURL)
                {
                    wcscpy_s(*ppwszURL, cch, responseURL);
                    hr = S_OK;
                }
                else
                {
                    hr = E_OUTOFMEMORY;
                }
                
                InternetCloseHandle(hRequest);
            }
            InternetCloseHandle(hConnect);
        }
        InternetCloseHandle(hInternet);
    }
    
    return hr;
}

// Start polling for login status in a separate thread
void CSampleCredential::_StartPollingForLogin()
{
    if (!_bPollingActive)
    {
        _bPollingActive = true;
        _pPollingThread = new std::thread([this]() {
            while (_bPollingActive)
            {
                HRESULT hr = _PollLoginStatus();
                if (SUCCEEDED(hr))
                {
                    // Login successful, trigger credential submission
                    if (_pCredProvCredentialEvents)
                    {
                        // Update the password field to indicate successful authentication
                        _pCredProvCredentialEvents->SetFieldString(this, SFI_PASSWORD, L"QRCodeAuthenticated");
                        
                        // In a real implementation, you would need to trigger the credential submission
                        // by notifying the system in an appropriate way. 
                        // For now, we'll just return immediately to stop polling.
                    }
                    break; // Exit polling loop
                }
                
                // Wait before next poll (e.g., 2 seconds)
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
        });
    }
}
