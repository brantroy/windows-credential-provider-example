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
#include <wincred.h>
#include <windows.h>
#include <gdiplus.h>
#include <strsafe.h>
#include <thread>
#include <chrono>
#include <vector>

// ✅ 添加 WinHTTP
#include <winhttp.h>
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "winhttp.lib")

#include "CSampleCredential.h"
#include "guid.h"
#include "qrcodegen.h"
#include "LocalizedStrings.h"

using namespace Gdiplus;

// Helper: HTTP GET using WinHTTP
static HRESULT _HttpGet(PCWSTR pszUrl, PWSTR* ppwszResponse)
{
    if (!ppwszResponse) return E_INVALIDARG;
    *ppwszResponse = nullptr;

    HINTERNET hSession = WinHttpOpen(L"QRLoginCP", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return HRESULT_FROM_WIN32(GetLastError());

    // ✅ 设置超时（单位：毫秒）
    DWORD timeout = 10000; // 10秒超时
    WinHttpSetTimeouts(hSession, timeout, timeout, timeout, timeout);

    // 解析 URL
    URL_COMPONENTS urlComp = { 0 };
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwSchemeLength = (DWORD)-1;
    urlComp.dwHostNameLength = (DWORD)-1;
    urlComp.dwUrlPathLength = (DWORD)-1;
    urlComp.dwExtraInfoLength = (DWORD)-1;

    if (!WinHttpCrackUrl(pszUrl, 0, 0, &urlComp)) {
        WinHttpCloseHandle(hSession);
        return HRESULT_FROM_WIN32(GetLastError());
    }

    HINTERNET hConnect = WinHttpConnect(hSession, urlComp.lpszHostName, urlComp.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return HRESULT_FROM_WIN32(GetLastError());
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", urlComp.lpszUrlPath, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return HRESULT_FROM_WIN32(GetLastError());
    }

    BOOL bResult = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (bResult) {
        bResult = WinHttpReceiveResponse(hRequest, NULL);
    }

    if (bResult) {
        std::vector<BYTE> buffer;
        DWORD bytesRead = 0;
        do {
            char temp[4096];
            bResult = WinHttpReadData(hRequest, temp, sizeof(temp) - 1, &bytesRead);
            if (bResult && bytesRead > 0) {
                buffer.insert(buffer.end(), temp, temp + bytesRead);
            }
        } while (bResult && bytesRead > 0);

        if (bResult) {
            buffer.push_back(0);
            int len = MultiByteToWideChar(CP_UTF8, 0, (LPCCH)buffer.data(), -1, nullptr, 0);
            *ppwszResponse = (PWSTR)CoTaskMemAlloc(len * sizeof(WCHAR));
            if (*ppwszResponse) {
                MultiByteToWideChar(CP_UTF8, 0, (LPCCH)buffer.data(), -1, *ppwszResponse, len);
                bResult = TRUE;
            }
            else {
                bResult = FALSE;
            }
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (!bResult) {
        if (*ppwszResponse) {
            CoTaskMemFree(*ppwszResponse);
            *ppwszResponse = nullptr;
        }
        return E_FAIL;
    }

    return S_OK;
}

// Helper: Parse JSON for token and expire
static bool _ParseQRInfo(PCWSTR pszJson, PWSTR* ppszToken, DWORD* pdwExpireSeconds)
{
    if (!pszJson || !ppszToken || !pdwExpireSeconds) return false;

    *ppszToken = nullptr;
    *pdwExpireSeconds = 600; // default 10 minutes

    PCWSTR pToken = wcsstr(pszJson, L"\"token\":\"");
    if (!pToken) return false;
    pToken += wcslen(L"\"token\":\"");
    PCWSTR pEnd = wcschr(pToken, L'"');
    if (!pEnd) return false;

    size_t tokenLen = pEnd - pToken;
    *ppszToken = (PWSTR)CoTaskMemAlloc((tokenLen + 1) * sizeof(WCHAR));
    if (!*ppszToken) return false;
    wcsncpy_s(*ppszToken, tokenLen + 1, pToken, tokenLen);

    PCWSTR pExpire = wcsstr(pszJson, L"\"expire\":");
    if (pExpire) {
        pExpire += wcslen(L"\"expire\":");
        *pdwExpireSeconds = _wtoi(pExpire);
    }

    return true;
}

// CSampleCredential ////////////////////////////////////////////////////////

CSampleCredential::CSampleCredential() :
    _cRef(1),
    _pCredProvCredentialEvents(NULL),
    _hQRCodeBitmap(NULL),
    _pszToken(NULL),
    _bLoginSuccess(false),
    _bStopPolling(false)
{
    DllAddRef();
    InitializeCriticalSection(&_cs);

    ZeroMemory(_rgCredProvFieldDescriptors, sizeof(_rgCredProvFieldDescriptors));
    ZeroMemory(_rgFieldStatePairs, sizeof(_rgFieldStatePairs));
    ZeroMemory(_rgFieldStrings, sizeof(_rgFieldStrings));
}

CSampleCredential::~CSampleCredential()
{
    _StopPolling();

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

    if (_pszToken) {
        CoTaskMemFree(_pszToken);
        _pszToken = NULL;
    }

    _CleanupQRCodeBitmap();
    DeleteCriticalSection(&_cs);
    DllRelease();
}

HRESULT CSampleCredential::Initialize(
    __in CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus,
    __in const CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR* rgcpfd,
    __in const FIELD_STATE_PAIR* rgfsp,
    __in DWORD dwFlags,
    __in PCWSTR pwzUsername,
    __in PCWSTR pwzPassword
)
{
    UNREFERENCED_PARAMETER(pwzUsername);
    HRESULT hr = S_OK;
    _cpus = cpus;
    _dwFlags = dwFlags;

    for (DWORD i = 0; SUCCEEDED(hr) && i < ARRAYSIZE(_rgCredProvFieldDescriptors); i++)
    {
        _rgFieldStatePairs[i] = rgfsp[i];
        hr = FieldDescriptorCopy(rgcpfd[i], &_rgCredProvFieldDescriptors[i]);
    }

    if (SUCCEEDED(hr))
    {
        hr = SHStrDupW(IDS_USERNAME_LABEL, &_rgFieldStrings[SFI_USERNAME]);
    }
    if (SUCCEEDED(hr))
    {
        hr = SHStrDupW(pwzPassword ? pwzPassword : L"", &_rgFieldStrings[SFI_PASSWORD]);
    }
    if (SUCCEEDED(hr))
    {
        hr = SHStrDupW(IDS_SUBMIT_BUTTON, &_rgFieldStrings[SFI_SUBMIT_BUTTON]);
    }

    return S_OK;
}

HRESULT CSampleCredential::Advise(__in ICredentialProviderCredentialEvents* pcpce)
{
    if (_pCredProvCredentialEvents != NULL)
    {
        _pCredProvCredentialEvents->Release();
    }
    _pCredProvCredentialEvents = pcpce;
    _pCredProvCredentialEvents->AddRef();

    // Start QR flow when selected
    _FetchQRCodeInfoAsync();
    _StartPolling();

    return S_OK;
}

HRESULT CSampleCredential::UnAdvise()
{
    _StopPolling();

    if (_pCredProvCredentialEvents)
    {
        _pCredProvCredentialEvents->Release();
    }
    _pCredProvCredentialEvents = NULL;
    return S_OK;
}

HRESULT CSampleCredential::SetSelected(__out BOOL* pbAutoLogon)
{
    *pbAutoLogon = FALSE;
    return S_OK;
}

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

HRESULT CSampleCredential::GetFieldState(
    __in DWORD dwFieldID,
    __out CREDENTIAL_PROVIDER_FIELD_STATE* pcpfs,
    __out CREDENTIAL_PROVIDER_FIELD_INTERACTIVE_STATE* pcpfis
)
{
    if ((dwFieldID < ARRAYSIZE(_rgFieldStatePairs)) && pcpfs && pcpfis)
    {
        *pcpfs = _rgFieldStatePairs[dwFieldID].cpfs;
        *pcpfis = _rgFieldStatePairs[dwFieldID].cpfis;
        return S_OK;
    }
    return E_INVALIDARG;
}

HRESULT CSampleCredential::GetStringValue(
    __in DWORD dwFieldID,
    __deref_out PWSTR* ppwz
)
{
    if (dwFieldID < ARRAYSIZE(_rgCredProvFieldDescriptors) && ppwz)
    {
        return SHStrDupW(_rgFieldStrings[dwFieldID], ppwz);
    }
    return E_INVALIDARG;
}

HRESULT CSampleCredential::GetBitmapValue(
    __in DWORD dwFieldID,
    __out HBITMAP* phbmp
)
{
    if ((SFI_TILEIMAGE == dwFieldID) && phbmp)
    {
        HBITMAP hbmp = LoadBitmap(HINST_THISDLL, MAKEINTRESOURCE(IDB_TILE_IMAGE));
        if (hbmp != NULL)
        {
            *phbmp = hbmp;
            return S_OK;
        }
        return HRESULT_FROM_WIN32(GetLastError());
    }
    else if ((SFI_QRCODEIMAGE == dwFieldID) && phbmp)
    {
        if (!_hQRCodeBitmap)
        {
            // Should not happen normally, but safe fallback
            PWSTR pszURL = NULL;
            if (SUCCEEDED(_GetQRCodeURL(&pszURL)) && pszURL)
            {
                _GenerateQRCodeBitmap(pszURL);
                CoTaskMemFree(pszURL);
            }
        }

        if (_hQRCodeBitmap)
        {
            *phbmp = (HBITMAP)CopyImage(_hQRCodeBitmap, IMAGE_BITMAP, 0, 0, LR_COPYRETURNORG);
            return S_OK;
        }
        return E_FAIL;
    }
    return E_INVALIDARG;
}

HRESULT CSampleCredential::GetSubmitButtonValue(
    __in DWORD dwFieldID,
    __out DWORD* pdwAdjacentTo
)
{
    if ((SFI_SUBMIT_BUTTON == dwFieldID) && pdwAdjacentTo)
    {
        *pdwAdjacentTo = SFI_PASSWORD;
        return S_OK;
    }
    return E_INVALIDARG;
}

HRESULT CSampleCredential::SetStringValue(
    __in DWORD dwFieldID,
    __in PCWSTR pwz
)
{
    if (dwFieldID < ARRAYSIZE(_rgCredProvFieldDescriptors) &&
        (CPFT_EDIT_TEXT == _rgCredProvFieldDescriptors[dwFieldID].cpft ||
            CPFT_PASSWORD_TEXT == _rgCredProvFieldDescriptors[dwFieldID].cpft))
    {
        PWSTR* ppwzStored = &_rgFieldStrings[dwFieldID];
        CoTaskMemFree(*ppwzStored);
        return SHStrDupW(pwz, ppwzStored);
    }
    return E_INVALIDARG;
}

// Unimplemented methods
HRESULT CSampleCredential::GetCheckboxValue(__in DWORD, __out BOOL*, __deref_out PWSTR*) { return E_NOTIMPL; }
HRESULT CSampleCredential::GetComboBoxValueCount(__in DWORD, __out DWORD*, __out_range(< , *) DWORD*) { return E_NOTIMPL; }
HRESULT CSampleCredential::GetComboBoxValueAt(__in DWORD, __in DWORD, __deref_out PWSTR*) { return E_NOTIMPL; }
HRESULT CSampleCredential::SetCheckboxValue(__in DWORD, __in BOOL) { return E_NOTIMPL; }
HRESULT CSampleCredential::SetComboBoxSelectedValue(__in DWORD, __in DWORD) { return E_NOTIMPL; }
HRESULT CSampleCredential::CommandLinkClicked(__in DWORD) { return E_NOTIMPL; }

HRESULT CSampleCredential::GetSerialization(
    __out CREDENTIAL_PROVIDER_GET_SERIALIZATION_RESPONSE* pcpgsr,
    __out CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION* pcpcs,
    __deref_out_opt PWSTR* ppwzOptionalStatusText,
    __out CREDENTIAL_PROVIDER_STATUS_ICON* pcpsiOptionalStatusIcon
)
{
    UNREFERENCED_PARAMETER(ppwzOptionalStatusText);
    UNREFERENCED_PARAMETER(pcpsiOptionalStatusIcon);

    EnterCriticalSection(&_cs);
    bool bSuccess = _bLoginSuccess;
    LeaveCriticalSection(&_cs);

    if (!bSuccess)
    {
        *pcpgsr = CPGSR_NO_CREDENTIAL_NOT_FINISHED;
        return S_OK;
    }

    // Standard serialization (use stored username)
    HRESULT hr = S_OK;
    WCHAR wsz[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD cch = ARRAYSIZE(wsz);
    if (!GetComputerNameW(wsz, &cch))
        return HRESULT_FROM_WIN32(GetLastError());

    PWSTR pwzProtectedPassword = nullptr;
    hr = ProtectIfNecessaryAndCopyPassword(L"", _cpus, &pwzProtectedPassword); // no password

    KERB_INTERACTIVE_UNLOCK_LOGON kiul;
    hr = KerbInteractiveUnlockLogonInit(wsz, _rgFieldStrings[SFI_USERNAME], pwzProtectedPassword, _cpus, &kiul);
    if (SUCCEEDED(hr))
    {
        hr = KerbInteractiveUnlockLogonPack(kiul, &pcpcs->rgbSerialization, &pcpcs->cbSerialization);
    }

    if (SUCCEEDED(hr))
    {
        ULONG ulAuthPackage;
        hr = RetrieveNegotiateAuthPackage(&ulAuthPackage);
        if (SUCCEEDED(hr))
        {
            pcpcs->ulAuthenticationPackage = ulAuthPackage;
            pcpcs->clsidCredentialProvider = CLSID_CSample;
            *pcpgsr = CPGSR_RETURN_CREDENTIAL_FINISHED;
        }
    }

    CoTaskMemFree(pwzProtectedPassword);
    return hr;
}

HRESULT CSampleCredential::ReportResult(
    __in NTSTATUS ntsStatus,
    __in NTSTATUS ntsSubstatus,
    __deref_out_opt PWSTR* ppwzOptionalStatusText,
    __out CREDENTIAL_PROVIDER_STATUS_ICON* pcpsiOptionalStatusIcon
)
{
    *ppwzOptionalStatusText = NULL;
    *pcpsiOptionalStatusIcon = CPSI_NONE;

    static const struct {
        NTSTATUS status, substatus;
        PCWSTR msg;
        CREDENTIAL_PROVIDER_STATUS_ICON icon;
    } s_rg[] = {
        { STATUS_LOGON_FAILURE, STATUS_SUCCESS, L"Login failed, try again later!", CPSI_ERROR },
        { STATUS_ACCOUNT_RESTRICTION, STATUS_ACCOUNT_DISABLED, L"account has been disbaled", CPSI_WARNING },
    };

    for (auto& item : s_rg)
    {
        if (item.status == ntsStatus && item.substatus == ntsSubstatus)
        {
            SHStrDupW(item.msg, ppwzOptionalStatusText);
            *pcpsiOptionalStatusIcon = item.icon;
            break;
        }
    }

    if (!SUCCEEDED(HRESULT_FROM_NT(ntsStatus)) && _pCredProvCredentialEvents)
    {
        _pCredProvCredentialEvents->SetFieldString(this, SFI_PASSWORD, L"");
    }

    return S_OK;
}

// =============== QR Code Generation ===============

void CSampleCredential::_GenerateQRCodeBitmap(PCWSTR pszURL)
{
    _CleanupQRCodeBitmap();
    if (!pszURL || wcslen(pszURL) == 0) return;

    int len = WideCharToMultiByte(CP_UTF8, 0, pszURL, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return;

    std::vector<char> utf8(len);
    WideCharToMultiByte(CP_UTF8, 0, pszURL, -1, utf8.data(), len, nullptr, nullptr);

    uint8_t qrcode[qrcodegen_BUFFER_LEN_MAX];
    uint8_t tempBuffer[qrcodegen_BUFFER_LEN_MAX];
    bool ok = qrcodegen_encodeText(utf8.data(), tempBuffer, qrcode,
        qrcodegen_Ecc_MEDIUM,
        qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
        qrcodegen_Mask_AUTO, true);
    if (!ok) return;

    int size = qrcodegen_getSize(qrcode);
    if (size <= 0) return;

    const int scale = 6;
    const int width = size * scale;
    const int height = size * scale;

    HDC hdcScreen = GetDC(nullptr);
    if (!hdcScreen) return;

    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    if (!hdcMem) { ReleaseDC(nullptr, hdcScreen); return; }

    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HBITMAP hbm = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    if (!hbm) {
        DeleteDC(hdcMem);
        ReleaseDC(nullptr, hdcScreen);
        return;
    }

    uint32_t* pixels = static_cast<uint32_t*>(pBits);
    const uint32_t white = 0xFFFFFFFF;
    const uint32_t black = 0xFF000000;

    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            bool isBlack = qrcodegen_getModule(qrcode, x, y);
            uint32_t color = isBlack ? black : white;
            for (int dy = 0; dy < scale; dy++) {
                for (int dx = 0; dx < scale; dx++) {
                    int screenX = x * scale + dx;
                    int screenY = y * scale + dy;
                    if (screenX < width && screenY < height) {
                        pixels[screenY * width + screenX] = color;
                    }
                }
            }
        }
    }

    _hQRCodeBitmap = (HBITMAP)CopyImage(hbm, IMAGE_BITMAP, 0, 0, LR_COPYRETURNORG);

    DeleteObject(hbm);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
}

void CSampleCredential::_CleanupQRCodeBitmap()
{
    if (_hQRCodeBitmap)
    {
        DeleteObject(_hQRCodeBitmap);
        _hQRCodeBitmap = NULL;
    }
}

// =============== Network & Logic ===============

HRESULT CSampleCredential::_GetQRCodeURL(PWSTR* ppwszURL)
{
    // Fallback: should not be used in real flow
    PCWSTR staticURL = L"https://example.com/fallback";
    size_t cch = wcslen(staticURL) + 1;
    *ppwszURL = (PWSTR)CoTaskMemAlloc(cch * sizeof(WCHAR));
    if (*ppwszURL)
    {
        wcscpy_s(*ppwszURL, cch, staticURL);
        return S_OK;
    }
    return E_OUTOFMEMORY;
}

void CSampleCredential::_FetchQRCodeInfoAsync()
{
    PWSTR pszResponse = nullptr;
    HRESULT hr = _HttpGet(L"https://your-api.com/api/v1/login/qr", &pszResponse);
    if (FAILED(hr) || !pszResponse) {
        if (pszResponse) CoTaskMemFree(pszResponse);
        return;
    }

    PWSTR pszToken = nullptr;
    DWORD dwExpireSec = 600;
    if (!_ParseQRInfo(pszResponse, &pszToken, &dwExpireSec)) {
        CoTaskMemFree(pszResponse);
        return;
    }
    CoTaskMemFree(pszResponse);

    std::wstring qrUrl = L"https://auth.example.com?token=";
    qrUrl += pszToken;

    EnterCriticalSection(&_cs);
    if (_pszToken) CoTaskMemFree(_pszToken);
    _pszToken = pszToken;

    GetSystemTimeAsFileTime(&_ftExpireTime);
    ULARGE_INTEGER ul;
    ul.LowPart = _ftExpireTime.dwLowDateTime;
    ul.HighPart = _ftExpireTime.dwHighDateTime;
    ul.QuadPart += (ULONGLONG)dwExpireSec * 10000000ULL;
    _ftExpireTime.dwLowDateTime = ul.LowPart;
    _ftExpireTime.dwHighDateTime = ul.HighPart;

    _bLoginSuccess = false;
    _CleanupQRCodeBitmap();
    _GenerateQRCodeBitmap(qrUrl.c_str());
    LeaveCriticalSection(&_cs);

    // Notify UI
    if (_pCredProvCredentialEvents)
    {
        _pCredProvCredentialEvents->SetFieldBitmap(this, SFI_QRCODEIMAGE, _hQRCodeBitmap);
    }
}

void CSampleCredential::_PollingThread()
{
    while (!_bStopPolling)
    {
        try {
            EnterCriticalSection(&_cs);
            if (_bLoginSuccess || !_pszToken) {
                LeaveCriticalSection(&_cs);
                std::this_thread::sleep_for(std::chrono::seconds(2));
                continue;
            }

            FILETIME now;
            GetSystemTimeAsFileTime(&now);
            ULARGE_INTEGER nowUL, expireUL;
            nowUL.LowPart = now.dwLowDateTime; nowUL.HighPart = now.dwHighDateTime;
            expireUL.LowPart = _ftExpireTime.dwLowDateTime; expireUL.HighPart = _ftExpireTime.dwHighDateTime;

            if (nowUL.QuadPart >= expireUL.QuadPart) {
                LeaveCriticalSection(&_cs);
                _FetchQRCodeInfoAsync();
                std::this_thread::sleep_for(std::chrono::seconds(2));
                continue;
            }

            std::wstring statusUrl = L"https://your-api.com/api/v1/login/status?token=";
            statusUrl += _pszToken;
            LeaveCriticalSection(&_cs);

            PWSTR pszResponse = nullptr;
            HRESULT hr = _HttpGet(statusUrl.c_str(), &pszResponse);
            if (SUCCEEDED(hr) && pszResponse)
            {
                if (wcsstr(pszResponse, L"\"status\":\"success\""))
                {
                    PCWSTR pUser = wcsstr(pszResponse, L"\"username\":\"");
                    if (pUser) {
                        pUser += wcslen(L"\"username\":\"");
                        PCWSTR pEnd = wcschr(pUser, L'"');
                        if (pEnd) {
                            size_t len = pEnd - pUser;
                            PWSTR pszUsername = (PWSTR)CoTaskMemAlloc((len + 1) * sizeof(WCHAR));
                            if (pszUsername) {
                                wcsncpy_s(pszUsername, len + 1, pUser, len);

                                EnterCriticalSection(&_cs);
                                _bLoginSuccess = true;
                                CoTaskMemFree(_rgFieldStrings[SFI_USERNAME]);
                                _rgFieldStrings[SFI_USERNAME] = pszUsername;
                                LeaveCriticalSection(&_cs);

                                if (_pCredProvCredentialEvents) {
                                    _pCredProvCredentialEvents->SetFieldString(this, SFI_USERNAME, pszUsername);
                                }
                            }
                        }
                    }
                }
                CoTaskMemFree(pszResponse);
            }
        }
        catch (...) {
            // 捕获异常，避免线程崩溃
        }

        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

void CSampleCredential::_StartPolling()
{
    if (_pollingThread.joinable()) {
        _StopPolling();
    }
    _bStopPolling = false;
    _pollingThread = std::thread(&CSampleCredential::_PollingThread, this);
}

void CSampleCredential::_StopPolling()
{
    _bStopPolling = true;
    if (_pollingThread.joinable()) {
        _pollingThread.join();
    }
}