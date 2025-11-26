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