#pragma once

#include <windows.h>
#include <strsafe.h>
#include <intsafe.h>
#include <shlguid.h>
#include <propvarutil.h>
#include <propkey.h>
#include <propsys.h>
#include <wincred.h>
#include <ntsecapi.h>
#include <cryptuiapi.h>
#include <userenv.h>
#include <iomanip>
#include <sstream>

// Helper function to create a field descriptor
inline HRESULT CreateFieldDescriptor(const PROPERTYKEY& key, const PWSTR* pszDisplayName, CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR** ppcpfd)
{
    CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR cpfd = { 0 };
    HRESULT hr = S_OK;

    // Allocate memory for the field descriptor
    CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR* pcfd = (CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR*)CoTaskMemAlloc(sizeof(CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR));
    if (pcfd == nullptr)
    {
        hr = E_OUTOFMEMORY;
    }
    else
    {
        // Copy the key
        cpfd.pszwFieldName = nullptr;
        cpfd.guidFieldType = GUID_NULL;
        hr = StringCchCopyW(cpfd.pszwLabel, ARRAYSIZE(cpfd.pszwLabel), *pszDisplayName);
        if (SUCCEEDED(hr))
        {
            // Copy the key
            cpfd.cpft = CPFT_PASSWORD;
            cpfd.guidFieldType = GUID_NULL;
            CopyMemory(pcfd, &cpfd, sizeof(CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR));
        }
        else
        {
            CoTaskMemFree(pcfd);
            pcfd = nullptr;
        }
    }

    *ppcpfd = pcfd;
    return hr;
}

// Helper function to create a string value
inline HRESULT SHStrDupW(PCWSTR psz, PWSTR* ppsz)
{
    *ppsz = nullptr;
    if (psz)
    {
        size_t len;
        HRESULT hr = StringCchLengthW(psz, STRSAFE_MAX_CCH, &len);
        if (SUCCEEDED(hr))
        {
            PWSTR pszDup = (PWSTR)CoTaskMemAlloc((len + 1) * sizeof(WCHAR));
            if (!pszDup)
            {
                hr = E_OUTOFMEMORY;
            }
            else
            {
                hr = StringCchCopyW(pszDup, len + 1, psz);
                if (SUCCEEDED(hr))
                {
                    *ppsz = pszDup;
                }
                else
                {
                    CoTaskMemFree(pszDup);
                }
            }
        }
        return hr;
    }
    else
    {
        return E_INVALIDARG;
    }
}

// Helper function to create a field descriptor with specific type
inline HRESULT CreateFieldDescriptorWithType(DWORD dwFieldId, CREDENTIAL_PROVIDER_FIELD_TYPE cpft, PCWSTR pszLabel, const GUID* pguidFieldType, CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR* pcpfd)
{
    if (pszLabel)
    {
        HRESULT hr = StringCchCopyW(pcpfd->pszwLabel, ARRAYSIZE(pcpfd->pszwLabel), pszLabel);
        if (SUCCEEDED(hr))
        {
            pcpfd->dwFieldID = dwFieldId;
            pcpfd->cpft = cpft;
            pcpfd->guidFieldType = (pguidFieldType != nullptr) ? *pguidFieldType : GUID_NULL;
        }
        return hr;
    }
    else
    {
        return E_INVALIDARG;
    }
}

// Helper function to create a safe string
inline HRESULT SafeStringCoAllocString(PCWSTR pszSource, PWSTR* ppszDest)
{
    *ppszDest = nullptr;
    if (pszSource)
    {
        size_t len;
        HRESULT hr = StringCchLengthW(pszSource, STRSAFE_MAX_CCH, &len);
        if (SUCCEEDED(hr))
        {
            PWSTR pszDest = (PWSTR)CoTaskMemAlloc((len + 1) * sizeof(WCHAR));
            if (!pszDest)
            {
                hr = E_OUTOFMEMORY;
            }
            else
            {
                hr = StringCchCopyW(pszDest, len + 1, pszSource);
                if (SUCCEEDED(hr))
                {
                    *ppszDest = pszDest;
                }
                else
                {
                    CoTaskMemFree(pszDest);
                }
            }
        }
        return hr;
    }
    else
    {
        return E_INVALIDARG;
    }
}

// Helper function to create a bitmap from raw data
inline HBITMAP CreateBitmapFromRGBData(BYTE* pRGBData, int width, int height)
{
    HBITMAP hBitmap = NULL;
    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // Negative for top-down DIB
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 24;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC hdcScreen = GetDC(NULL);
    if (hdcScreen)
    {
        hBitmap = CreateDIBitmap(hdcScreen, &bmi.bmiHeader, CBM_INIT, pRGBData, &bmi, DIB_RGB_COLORS);
        ReleaseDC(NULL, hdcScreen);
    }

    return hBitmap;
}

// Helper function to duplicate a bitmap
inline HBITMAP DuplicateBitmap(HBITMAP hOriginalBitmap)
{
    if (!hOriginalBitmap)
        return NULL;

    HDC hdcScreen = GetDC(NULL);
    if (!hdcScreen)
        return NULL;

    HDC hdcMem1 = CreateCompatibleDC(hdcScreen);
    HDC hdcMem2 = CreateCompatibleDC(hdcScreen);
    
    if (!hdcMem1 || !hdcMem2)
    {
        ReleaseDC(NULL, hdcScreen);
        if (hdcMem1) DeleteDC(hdcMem1);
        if (hdcMem2) DeleteDC(hdcMem2);
        return NULL;
    }

    BITMAP bm;
    GetObject(hOriginalBitmap, sizeof(bm), &bm);

    HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, bm.bmWidth, bm.bmHeight);
    if (hBitmap)
    {
        HGDIOBJ hOld1 = SelectObject(hdcMem1, hOriginalBitmap);
        HGDIOBJ hOld2 = SelectObject(hdcMem2, hBitmap);

        BitBlt(hdcMem2, 0, 0, bm.bmWidth, bm.bmHeight, hdcMem1, 0, 0, SRCCOPY);

        SelectObject(hdcMem1, hOld1);
        SelectObject(hdcMem2, hOld2);
    }

    DeleteDC(hdcMem1);
    DeleteDC(hdcMem2);
    ReleaseDC(NULL, hdcScreen);

    return hBitmap;
}