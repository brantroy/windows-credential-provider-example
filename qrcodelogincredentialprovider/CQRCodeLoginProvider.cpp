//
// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//

#include <credentialprovider.h>
#include <helpers.h>
#include "CQRCodeLoginCredential.h"
#include "guid.h"

// This class is responsible for the communication between LogonUI and our credential.
// It implements ICredentialProvider and is instantiated by LogonUI.
class CQRCodeLoginProvider : public ICredentialProvider
{
public:
    CQRCodeLoginProvider(): _cRef(1), _pCredential(NULL)
    {
        DllAddRef();
    }

    // IUnknown
    IFACEMETHODIMP_(ULONG) AddRef()
    {
        return ++_cRef;
    }

    IFACEMETHODIMP_(ULONG) Release()
    {
        LONG cRef = --_cRef;
        if (!cRef)
        {
            delete this;
        }
        return cRef;
    }

    IFACEMETHODIMP QueryInterface(__in REFIID riid, __deref_out void** ppv)
    {
        static const QITAB qit[] =
        {
            QITABENT(CQRCodeLoginProvider, ICredentialProvider), // IID_ICredentialProvider
            {0},
        };
        return QISearch(this, qit, riid, ppv);
    }

    IFACEMETHODIMP SetUsageScenario(__in CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus, __in_opt HWND hwnd)
    {
        HRESULT hr;
        // We only support the logon and unlock scenarios.
        // These scenarios are documented in the credential provider SDK.
        switch (cpus)
        {
        case CPUS_LOGON:
        case CPUS_UNLOCK:
            _cpus = cpus;
            hr = S_OK;
            break;
        case CPUS_CREDUI:
        case CPUS_CHANGE_PASSWORD:
        default:
            hr = E_NOTIMPL;
            break;
        }

        return hr;
    }

    IFACEMETHODIMP SetSerialization(__in_opt CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION* pcpcs)
    {
        return E_NOTIMPL;
    }

    // LogonUI calls this function to see if the credential provider has a 
    // credential available for the user.
    IFACEMETHODIMP GetCredentialCount(__out DWORD* pdwCount,
                                      __out_range(<,*pdwCount) DWORD* pdwDefault,
                                      __out BOOL* pbAutoLogonWithDefault
                                      )
    {
        *pdwCount = 1;
        *pdwDefault = 0;
        *pbAutoLogonWithDefault = FALSE;
        return S_OK;
    }

    // LogonUI calls this to get a specific credential. This credential provider 
    // only provides a single credential, so regardless of the index, it returns
    // the same credential.
    IFACEMETHODIMP GetCredentialAt(__in DWORD dwIndex,
                                   __deref_out ICredentialProviderCredential** ppcpc
                                   )
    {
        HRESULT hr;
        if (!_pCredential)
        {
            CQRCodeLoginCredential* pNewCredential = new CQRCodeLoginCredential();
            if (pNewCredential)
            {
                // The first parameter of Initialize is the usage scenario which is stored in our instance
                // variable.  The rest of the parameters are taken from the sample provided with the SDK.
                hr = pNewCredential->Initialize(_cpus, s_rgQRCodeCredProvFieldDescriptors, s_rgQRCodeFieldStatePairs);
                if (SUCCEEDED(hr))
                {
                    _pCredential = pNewCredential;
                    pNewCredential = NULL;
                }
            }
            else
            {
                hr = E_OUTOFMEMORY;
            }
            if (pNewCredential)
            {
                pNewCredential->Release();
            }
        }
        else
        {
            hr = S_OK;
        }

        if (SUCCEEDED(hr))
        {
            hr = _pCredential->QueryInterface(IID_ICredentialProviderCredential, reinterpret_cast<void**>(ppcpc));
        }

        return hr;
    }

private:
    ~CQRCodeLoginProvider()
    {
        if (_pCredential)
        {
            _pCredential->Release();
        }
        DllRelease();
    }

    LONG                                    _cRef;
    CREDENTIAL_PROVIDER_USAGE_SCENARIO      _cpus;
    CQRCodeLoginCredential*                 _pCredential;
};

// Simulate QR code generation by creating a simple bitmap with a pattern
// In a real implementation, you would use a proper QR code library like libqrencode
HBITMAP GenerateQRCodeBitmap(PCWSTR pwzURL)
{
    // Implementation is in CQRCodeLoginCredential.cpp
    return NULL;
}

// The other functions necessary to implement a credential provider are 
// helper functions that are not relevant to the UI aspects of the credential.
// These are standard implementations that can be reused from the sample code.

// Provider implementation
extern "C" HRESULT CALLBACK DllGetClassObject(__in REFCLSID rclsid, __in REFIID riid, __deref_out void** ppv)
{
    *ppv = NULL;
    HRESULT hr = E_OUTOFMEMORY;
    CQRCodeLoginProvider* pProvider = new CQRCodeLoginProvider();  // construct the CP
    if (pProvider)  // successfully created provider object
    {
        hr = pProvider->QueryInterface(riid, ppv);  // look for the requested interface
        pProvider->Release();
    }
    return hr;
}

extern "C" HRESULT CALLBACK DllCanUnloadNow()
{
    return g_cRefThisDll ? S_FALSE : S_OK;
}