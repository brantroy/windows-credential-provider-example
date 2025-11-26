//
// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//

#ifndef WIN32_NO_STATUS
#include <ntstatus.h>
#define WIN32_NO_STATUS
#endif
#include <unknwn.h>
#include "CQRCodeLoginCredential.h"
#include "guid.h"

// For QR Code generation
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

// Function to generate QR code bitmap
HBITMAP GenerateQRCodeBitmap(PCWSTR pwzURL)
{
    // Initialize GDI+
    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    // Create a memory DC and bitmap for drawing the QR code
    HDC hScreenDC = GetDC(NULL);
    HDC hMemDC = CreateCompatibleDC(hScreenDC);
    
    // Create a bitmap (simplified - in real implementation we would generate actual QR code)
    HBITMAP hBitmap = CreateCompatibleBitmap(hScreenDC, 200, 200);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemDC, hBitmap);

    // Fill background with white
    RECT rect = {0, 0, 200, 200};
    HBRUSH hBrush = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(hMemDC, &rect, hBrush);
    DeleteObject(hBrush);

    // Draw a simple QR code representation (in real implementation, you'd use a QR code library)
    // For now, we'll draw a simple grid pattern to represent a QR code
    HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
    HPEN hOldPen = (HPEN)SelectObject(hMemDC, hPen);
    
    // Draw a simple pattern to represent a QR code
    for (int i = 0; i < 20; i++) {
        for (int j = 0; j < 20; j++) {
            // Simple pattern - in real implementation, you'd encode the URL properly
            if ((i + j) % 3 == 0) {
                HBRUSH hBlackBrush = CreateSolidBrush(RGB(0, 0, 0));
                RECT cellRect = {i * 10, j * 10, (i + 1) * 10, (j + 1) * 10};
                FillRect(hMemDC, &cellRect, hBlackBrush);
                DeleteObject(hBlackBrush);
            }
        }
    }
    
    // Draw border
    Rectangle(hMemDC, 0, 0, 200, 200);
    
    // Cleanup
    SelectObject(hMemDC, hOldPen);
    DeleteObject(hPen);
    
    // Add text with URL
    SetTextColor(hMemDC, RGB(0, 0, 0));
    SetBkMode(hMemDC, TRANSPARENT);
    RECT textRect = {0, 205, 200, 220};
    DrawTextW(hMemDC, pwzURL, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    
    SelectObject(hMemDC, hOldBitmap);
    DeleteDC(hMemDC);
    ReleaseDC(NULL, hScreenDC);
    
    GdiplusShutdown(gdiplusToken);
    
    return hBitmap;
}

// CQRCodeLoginCredential ////////////////////////////////////////////////////////

CQRCodeLoginCredential::CQRCodeLoginCredential():
    _cRef(1),
    _pCredProvCredentialEvents(NULL),
    _hQRCodeBitmap(NULL),
    _pwzQRCodeURL(NULL)
{
    DllAddRef();

    ZeroMemory(_rgCredProvFieldDescriptors, sizeof(_rgCredProvFieldDescriptors));
    ZeroMemory(_rgFieldStatePairs, sizeof(_rgFieldStatePairs));
    ZeroMemory(_rgFieldStrings, sizeof(_rgFieldStrings));
    
    // Set default QR code URL
    SHStrDupW(L"https://example.com/login", &_pwzQRCodeURL);
    
    // Generate QR code bitmap
    _hQRCodeBitmap = GenerateQRCodeBitmap(_pwzQRCodeURL);
}

CQRCodeLoginCredential::~CQRCodeLoginCredential()
{
    if (_rgFieldStrings[QRFI_PASSWORD])
    {
        // CoTaskMemFree (below) deals with NULL, but StringCchLength does not.
        size_t lenPassword = lstrlen(_rgFieldStrings[QRFI_PASSWORD]);
        SecureZeroMemory(_rgFieldStrings[QRFI_PASSWORD], lenPassword * sizeof(*_rgFieldStrings[QRFI_PASSWORD]));
    }
    for (int i = 0; i < ARRAYSIZE(_rgFieldStrings); i++)
    {
        CoTaskMemFree(_rgFieldStrings[i]);
        CoTaskMemFree(_rgCredProvFieldDescriptors[i].pszLabel);
    }

    // Clean up QR code bitmap
    if (_hQRCodeBitmap)
    {
        DeleteObject(_hQRCodeBitmap);
        _hQRCodeBitmap = NULL;
    }
    
    // Clean up QR code URL
    CoTaskMemFree(_pwzQRCodeURL);
    
    DllRelease();
}

// Initializes one credential with the field information passed in.
HRESULT CQRCodeLoginCredential::Initialize(
    __in CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus,
    __in const CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR* rgcpfd,
    __in const FIELD_STATE_PAIR* rgfsp,
    __in PCWSTR pwzUsername,
    __in PCWSTR pwzPassword
    )
{
    HRESULT hr = S_OK;

    _cpus = cpus;

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
        hr = SHStrDupW(pwzUsername ? pwzUsername : L"", &_rgFieldStrings[QRFI_USERNAME]);
    }
    if (SUCCEEDED(hr))
    {
        hr = SHStrDupW(pwzPassword ? pwzPassword : L"", &_rgFieldStrings[QRFI_PASSWORD]);
    }
    if (SUCCEEDED(hr))
    {
        hr = SHStrDupW(L"QR Code Login", &_rgFieldStrings[QRFI_QR_CODE_LABEL]);
    }
    if (SUCCEEDED(hr))
    {
        hr = SHStrDupW(L"Scan QR Code to Login", &_rgFieldStrings[QRFI_SUBMIT_BUTTON]);
    }

    return S_OK;
}

// LogonUI calls this in order to give us a callback in case we need to notify it of anything.
HRESULT CQRCodeLoginCredential::Advise(
    __in ICredentialProviderCredentialEvents* pcpce
    )
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
HRESULT CQRCodeLoginCredential::UnAdvise()
{
    if (_pCredProvCredentialEvents)
    {
        _pCredProvCredentialEvents->Release();
    }
    _pCredProvCredentialEvents = NULL;
    return S_OK;
}

// LogonUI calls this function when our tile is selected (zoomed).
HRESULT CQRCodeLoginCredential::SetSelected(__out BOOL* pbAutoLogon)  
{
    *pbAutoLogon = FALSE;  

    return S_OK;
}

// Similarly to SetSelected, LogonUI calls this when your tile was selected
// and now no longer is. 
HRESULT CQRCodeLoginCredential::SetDeselected()
{
    HRESULT hr = S_OK;
    if (_rgFieldStrings[QRFI_PASSWORD])
    {
        size_t lenPassword = lstrlen(_rgFieldStrings[QRFI_PASSWORD]);
        SecureZeroMemory(_rgFieldStrings[QRFI_PASSWORD], lenPassword * sizeof(*_rgFieldStrings[QRFI_PASSWORD]));
    
        CoTaskMemFree(_rgFieldStrings[QRFI_PASSWORD]);
        hr = SHStrDupW(L"", &_rgFieldStrings[QRFI_PASSWORD]);
        if (SUCCEEDED(hr) && _pCredProvCredentialEvents)
        {
            _pCredProvCredentialEvents->SetFieldString(this, QRFI_PASSWORD, _rgFieldStrings[QRFI_PASSWORD]);
        }
    }

    return hr;
}

// Gets info for a particular field of a tile. Called by logonUI to get information to 
// display the tile.
HRESULT CQRCodeLoginCredential::GetFieldState(
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

// Sets ppwsz to the string value of the field at the index dwFieldID.
HRESULT CQRCodeLoginCredential::GetStringValue(
    __in DWORD dwFieldID, 
    __deref_out PWSTR* ppwsz
    )
{
    HRESULT hr;

    // Check to make sure dwFieldID is a legitimate index.
    if (dwFieldID < ARRAYSIZE(_rgCredProvFieldDescriptors) && ppwsz) 
    {
        // Make a copy of the string and return that. The caller
        // is responsible for freeing it.
        hr = SHStrDupW(_rgFieldStrings[dwFieldID], ppwsz);
    }
    else
    {
        hr = E_INVALIDARG;
    }

    return hr;
}

// Gets the image to show in the user tile (this would be the QR code).
HRESULT CQRCodeLoginCredential::GetBitmapValue(
    __in DWORD dwFieldID, 
    __out HBITMAP* phbmp
    )
{
    HRESULT hr;
    if ((QRFI_QR_CODE_IMAGE == dwFieldID) && phbmp)
    {
        if (_hQRCodeBitmap != NULL)
        {
            *phbmp = _hQRCodeBitmap;
            hr = S_OK;
        }
        else
        {
            // Fallback: load a default image if QR code generation failed
            HBITMAP hbmp = LoadBitmap(HINST_THISDLL, MAKEINTRESOURCE(IDB_TILE_IMAGE));
            if (hbmp != NULL)
            {
                *phbmp = hbmp;
                hr = S_OK;
            }
            else
            {
                hr = HRESULT_FROM_WIN32(GetLastError());
            }
        }
    }
    else
    {
        hr = E_INVALIDARG;
    }

    return hr;
}

// Sets pdwAdjacentTo to the index of the field the submit button should be 
// adjacent to.
HRESULT CQRCodeLoginCredential::GetSubmitButtonValue(
    __in DWORD dwFieldID,
    __out DWORD* pdwAdjacentTo
    )
{
    HRESULT hr;

    // Validate parameters.
    if ((QRFI_SUBMIT_BUTTON == dwFieldID) && pdwAdjacentTo)
    {
        // pdwAdjacentTo is a pointer to the fieldID you want the submit button to appear next to.
        *pdwAdjacentTo = QRFI_QR_CODE_IMAGE;
        hr = S_OK;
    }
    else
    {
        hr = E_INVALIDARG;
    }
    return hr;
}

// Sets the value of a field which can accept a string as a value.
HRESULT CQRCodeLoginCredential::SetStringValue(
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
        PWSTR* ppwszStored = &_rgFieldStrings[dwFieldID];
        CoTaskMemFree(*ppwszStored);
        hr = SHStrDupW(pwz, ppwszStored);
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
HRESULT CQRCodeLoginCredential::GetCheckboxValue(
    __in DWORD dwFieldID, 
    __out BOOL* pbChecked,
    __deref_out PWSTR* ppwszLabel
    )
{
    UNREFERENCED_PARAMETER(dwFieldID);
    UNREFERENCED_PARAMETER(pbChecked);
    UNREFERENCED_PARAMETER(ppwszLabel);

    return E_NOTIMPL;
}

HRESULT CQRCodeLoginCredential::GetComboBoxValueCount(
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

HRESULT CQRCodeLoginCredential::GetComboBoxValueAt(
    __in DWORD dwFieldID, 
    __in DWORD dwItem,
    __deref_out PWSTR* ppwszItem
    )
{
    UNREFERENCED_PARAMETER(dwFieldID);
    UNREFERENCED_PARAMETER(dwItem);
    UNREFERENCED_PARAMETER(ppwszItem);
    return E_NOTIMPL;
}

HRESULT CQRCodeLoginCredential::SetCheckboxValue(
    __in DWORD dwFieldID, 
    __in BOOL bChecked
    )
{
    UNREFERENCED_PARAMETER(dwFieldID);
    UNREFERENCED_PARAMETER(bChecked);

    return E_NOTIMPL;
}

HRESULT CQRCodeLoginCredential::SetComboBoxSelectedValue(
    __in DWORD dwFieldId,
    __in DWORD dwSelectedItem
    )
{
    UNREFERENCED_PARAMETER(dwFieldId);
    UNREFERENCED_PARAMETER(dwSelectedItem);
    return E_NOTIMPL;
}

HRESULT CQRCodeLoginCredential::CommandLinkClicked(__in DWORD dwFieldID)
{
    UNREFERENCED_PARAMETER(dwFieldID);
    return E_NOTIMPL;
}
//------ end of methods for controls we don't have in our tile ----//

// Collect the username and password into a serialized credential for the correct usage scenario 
// (logon/unlock is what's demonstrated in this sample).  LogonUI then passes these credentials 
// back to the system to log on.
HRESULT CQRCodeLoginCredential::GetSerialization(
    __out CREDENTIAL_PROVIDER_GET_SERIALIZATION_RESPONSE* pcpgsr,
    __out CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION* pcpcs, 
    __deref_out_opt PWSTR* ppwszOptionalStatusText, 
    __out CREDENTIAL_PROVIDER_STATUS_ICON* pcpsiOptionalStatusIcon
    )
{
    UNREFERENCED_PARAMETER(ppwszOptionalStatusText);
    UNREFERENCED_PARAMETER(pcpsiOptionalStatusIcon);

    HRESULT hr;

    WCHAR wsz[MAX_COMPUTERNAME_LENGTH+1];
    DWORD cch = ARRAYSIZE(wsz);
    if (GetComputerNameW(wsz, &cch))
    {
        PWSTR pwzProtectedPassword;

        hr = ProtectIfNecessaryAndCopyPassword(_rgFieldStrings[QRFI_PASSWORD], _cpus, &pwzProtectedPassword);

        if (SUCCEEDED(hr))
        {
            KERB_INTERACTIVE_UNLOCK_LOGON kiul;

            // Initialize kiul with weak references to our credential.
            hr = KerbInteractiveUnlockLogonInit(wsz, _rgFieldStrings[QRFI_USERNAME], pwzProtectedPassword, _cpus, &kiul);

            if (SUCCEEDED(hr))
            {
                // We use KERB_INTERACTIVE_UNLOCK_LOGON in both unlock and logon scenarios.  It contains a
                // KERB_INTERACTIVE_LOGON to hold the creds plus a LUID that is filled in for us by Winlogon
                // as necessary.
                hr = KerbInteractiveUnlockLogonPack(kiul, &pcpcs->rgbSerialization, &pcpcs->cbSerialization);

                if (SUCCEEDED(hr))
                {
                    ULONG ulAuthPackage;
                    hr = RetrieveNegotiateAuthPackage(&ulAuthPackage);
                    if (SUCCEEDED(hr))
                    {
                        pcpcs->ulAuthenticationPackage = ulAuthPackage;
                        pcpcs->clsidCredentialProvider = CLSID_CQRCodeLogin;
 
                        // At this point the credential has created the serialized credential used for logon
                        // By setting this to CPGSR_RETURN_CREDENTIAL_FINISHED we are letting logonUI know
                        // that we have all the information we need and it should attempt to submit the 
                        // serialized credential.
                        *pcpgsr = CPGSR_RETURN_CREDENTIAL_FINISHED;
                    }
                }
            }

            CoTaskMemFree(pwzProtectedPassword);
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
    { STATUS_LOGON_FAILURE, STATUS_SUCCESS, L"QR Code authentication failed.", CPSI_ERROR, },
    { STATUS_ACCOUNT_RESTRICTION, STATUS_ACCOUNT_DISABLED, L"The account is disabled.", CPSI_WARNING },
};

// ReportResult is completely optional.  Its purpose is to allow a credential to customize the string
// and the icon displayed in the case of a logon failure.  
HRESULT CQRCodeLoginCredential::ReportResult(
    __in NTSTATUS ntsStatus, 
    __in NTSTATUS ntsSubstatus,
    __deref_out_opt PWSTR* ppwszOptionalStatusText, 
    __out CREDENTIAL_PROVIDER_STATUS_ICON* pcpsiOptionalStatusIcon
    )
{
    *ppwszOptionalStatusText = NULL;
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
        if (SUCCEEDED(SHStrDupW(s_rgLogonStatusInfo[dwStatusInfo].pwzMessage, ppwszOptionalStatusText)))
        {
            *pcpsiOptionalStatusIcon = s_rgLogonStatusInfo[dwStatusInfo].cpsi;
        }
    }

    // If we failed the logon, try to erase the password field.
    if (!SUCCEEDED(HRESULT_FROM_NT(ntsStatus)))
    {
        if (_pCredProvCredentialEvents)
        {
            _pCredProvCredentialEvents->SetFieldString(this, QRFI_PASSWORD, L"");
        }
    }

    // Since NULL is a valid value for *ppwszOptionalStatusText and *pcpsiOptionalStatusIcon
    // this function can't fail.
    return S_OK;
}