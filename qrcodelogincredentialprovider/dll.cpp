//
// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//

#include <windows.h>
#include "dll.h"

HINSTANCE g_hinstThisDll = NULL;
LONG g_cRefThisDll = 0;

BOOL WINAPI DllMain(__in HINSTANCE hinstDll, __in DWORD dwReason, __in void*)
{
    switch (dwReason)
    {
    case DLL_PROCESS_ATTACH:
        // Hold the instance of this DLL module, so we can reference it to find resources.
        g_hinstThisDll = hinstDll;
        DisableThreadLibraryCalls(hinstDll);
        break;
    case DLL_PROCESS_DETACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    }
    return TRUE;
}

void DllAddRef()
{
    InterlockedIncrement(&g_cRefThisDll);
}

void DllRelease()
{
    InterlockedDecrement(&g_cRefThisDll);
}