// (This file is copied from original samplecreduicredentialprovider with no changes.)
//
// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//

#include <credentialprovider.h>
#include "CSampleProvider.h"
#include "CSampleCredential.h"
#include "guid.h"
#include <wincred.h>

// CSampleProvider ////////////////////////////////////////////////////////

CSampleProvider::CSampleProvider():
    _cRef(1),
    _pkiulSetSerialization(NULL),
    _dwCredUIFlags(0),
    _bRecreateEnumeratedCredentials(true),
    _bAutoSubmitSetSerializationCred(false),
    _bDefaultToFirstCredential(false)
{
    DllAddRef();

    ZeroMemory(_rgpCredentials, sizeof(_rgpCredentials));
}

CSampleProvider::~CSampleProvider()
{
    _ReleaseEnumeratedCredentials();
    DllRelease();
}

void CSampleProvider::_ReleaseEnumeratedCredentials()
{
    for (int i = 0; i < ARRAYSIZE(_rgpCredentials); i++)
    {
        if (_rgpCredentials[i] != NULL)
        {
            _rgpCredentials[i]->Release();
        }
    }
}


// SetUsageScenario is the provider's cue that it's going to be asked for tiles
// in a subsequent call.
HRESULT CSampleProvider::SetUsageScenario(
    __in CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus,
    __in DWORD dwFlags
    )
{
    HRESULT hr;

    _cpus = cpus;
    if (cpus == CPUS_CREDUI)
    {
        _dwCredUIFlags = dwFlags;  // currently the only flags ever passed in are only valid for the credui scenario
    }
    _bRecreateEnumeratedCredentials = true;

    switch (cpus)
    {
    case CPUS_LOGON:
    case CPUS_UNLOCK_WORKSTATION:
    case CPUS_CREDUI:
        hr = S_OK;
        break;

    case CPUS_CHANGE_PASSWORD:
        hr = E_NOTIMPL;
        break;

    default:
        hr = E_INVALIDARG;
        break;
    }

    return hr;
}

HRESULT CSampleProvider::SetSerialization(
    __in const CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION* pcpcs
    )
{
    HRESULT hr = E_INVALIDARG;

    if ((CLSID_CSample == pcpcs->clsidCredentialProvider) || (CPUS_CREDUI == _cpus))
    {
        ULONG ulNegotiateAuthPackage;
        hr = RetrieveNegotiateAuthPackage(&ulNegotiateAuthPackage);

        if (SUCCEEDED(hr))
        {
            if (CPUS_CREDUI == _cpus)
            {
                if (CREDUIWIN_IN_CRED_ONLY & _dwCredUIFlags)
                {
                    hr = E_INVALIDARG;
                }
                else if (_dwCredUIFlags & CREDUIWIN_AUTHPACKAGE_ONLY)
                {
                    if (ulNegotiateAuthPackage == pcpcs->ulAuthenticationPackage)
                    {
                        hr = S_FALSE;
                    }
                    else
                    {
                        hr = E_INVALIDARG;
                    }
                }
            }

            if ((ulNegotiateAuthPackage == pcpcs->ulAuthenticationPackage) &&
                (0 < pcpcs->cbSerialization && pcpcs->rgbSerialization))
            {
                KERB_INTERACTIVE_UNLOCK_LOGON* pkil = (KERB_INTERACTIVE_UNLOCK_LOGON*) pcpcs->rgbSerialization;
                if (KerbInteractiveLogon == pkil->Logon.MessageType)
                {
                    if (0 < pkil->Logon.UserName.Length && pkil->Logon.UserName.Buffer)
                    {
                        if ((CPUS_CREDUI == _cpus) && (CREDUIWIN_PACK_32_WOW & _dwCredUIFlags))
                        {
                            BYTE* rgbNativeSerialization;
                            DWORD cbNativeSerialization;
                            if (SUCCEEDED(KerbInteractiveUnlockLogonRepackNative(pcpcs->rgbSerialization, pcpcs->cbSerialization, &rgbNativeSerialization, &cbNativeSerialization)))
                            {
                                KerbInteractiveUnlockLogonUnpackInPlace((PKERB_INTERACTIVE_UNLOCK_LOGON)rgbNativeSerialization, cbNativeSerialization);

                                _pkiulSetSerialization = (PKERB_INTERACTIVE_UNLOCK_LOGON)rgbNativeSerialization;
                                hr = S_OK;
                            }
                        }
                        else
                        {
                            BYTE* rgbSerialization;
                            rgbSerialization = (BYTE*)HeapAlloc(GetProcessHeap(), 0, pcpcs->cbSerialization);
                            HRESULT hrCreateCred = rgbSerialization ? S_OK : E_OUTOFMEMORY;

                            if (SUCCEEDED(hrCreateCred))
                            {
                                CopyMemory(rgbSerialization, pcpcs->rgbSerialization, pcpcs->cbSerialization);
                                KerbInteractiveUnlockLogonUnpackInPlace((KERB_INTERACTIVE_UNLOCK_LOGON*)rgbSerialization,pcpcs->cbSerialization);

                                if (_pkiulSetSerialization)
                                {
                                    HeapFree(GetProcessHeap(), 0, _pkiulSetSerialization);
                                }
                                _pkiulSetSerialization = (KERB_INTERACTIVE_UNLOCK_LOGON*)rgbSerialization;
                                if (SUCCEEDED(hrCreateCred))
                                {
                                    hr = hrCreateCred;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    return hr;
}

HRESULT CSampleProvider::Advise(
    __in ICredentialProviderEvents* pcpe,
    __in UINT_PTR upAdviseContext
    )
{
    UNREFERENCED_PARAMETER(pcpe);
    UNREFERENCED_PARAMETER(upAdviseContext);

    return E_NOTIMPL;
}

HRESULT CSampleProvider::UnAdvise()
{
    return E_NOTIMPL;
}

HRESULT CSampleProvider::GetFieldDescriptorCount(
    __out DWORD* pdwCount
    )
{
    *pdwCount = SFI_NUM_FIELDS;

    return S_OK;
}

HRESULT CSampleProvider::GetFieldDescriptorAt(
    __in DWORD dwIndex, 
    __deref_out CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR** ppcpfd
    )
{    
    HRESULT hr;

    if ((dwIndex < SFI_NUM_FIELDS) && ppcpfd)
    {
        hr = FieldDescriptorCoAllocCopy(s_rgCredProvFieldDescriptors[dwIndex], ppcpfd);
    }
    else
    { 
        hr = E_INVALIDARG;
    }

    return hr;
}

HRESULT CSampleProvider::GetCredentialCount(
    __out DWORD* pdwCount,
    __out_range(<,*pdwCount) DWORD* pdwDefault,
    __out BOOL* pbAutoLogonWithDefault
    )
{
    HRESULT hr = E_FAIL;
    if (_bRecreateEnumeratedCredentials)
    {
        _ReleaseEnumeratedCredentials();
        hr = _CreateEnumeratedCredentials();
        _bRecreateEnumeratedCredentials = false;
    }

    *pdwCount = 0;
    *pdwDefault = (_bDefaultToFirstCredential && _rgpCredentials[0]) ? 0 : CREDENTIAL_PROVIDER_NO_DEFAULT;
    *pbAutoLogonWithDefault = FALSE;

    if (SUCCEEDED(hr))
    {
        DWORD dwNumCreds = 0;
        for (int i = 0; i < MAX_CREDENTIALS; i++)
        {
            if (_rgpCredentials[i] != NULL)
            {
                dwNumCreds++;
            }
        }

        switch(_cpus)
        {
        case CPUS_LOGON:
            if (_bAutoSubmitSetSerializationCred)
            {
                *pdwCount = 1;
                *pbAutoLogonWithDefault = TRUE;
            }
            else
            {
                *pdwCount = dwNumCreds;
            }
            hr = S_OK;
            break;

        case CPUS_UNLOCK_WORKSTATION:
            *pdwCount = dwNumCreds;
            hr = S_OK;
            break;

        case CPUS_CREDUI:
            {
                *pdwCount = dwNumCreds;
                hr = S_OK;
            }
            break;

        default:
            hr = E_INVALIDARG;
            break;
        }
    }

    return hr;
}

HRESULT CSampleProvider::GetCredentialAt(
    __in DWORD dwIndex, 
    __deref_out ICredentialProviderCredential** ppcpc
    )
{
    HRESULT hr;

    if((dwIndex < ARRAYSIZE(_rgpCredentials)) && _rgpCredentials[dwIndex] != NULL && ppcpc)
    {
        hr = _rgpCredentials[dwIndex]->QueryInterface(IID_ICredentialProviderCredential, reinterpret_cast<void**>(ppcpc));
    }
    else
    {
        hr = E_INVALIDARG;
    }

    return hr;
}

HRESULT CSampleProvider::_EnumerateOneCredential(
    __in DWORD dwCredentialIndex,
    __in PCWSTR pwzUsername
    )
{
    HRESULT hr;

    CSampleCredential* ppc = new CSampleCredential();

    if (ppc)
    {
        hr = ppc->Initialize(_cpus,s_rgCredProvFieldDescriptors, s_rgFieldStatePairs, _dwCredUIFlags, pwzUsername);

        if (SUCCEEDED(hr))
        {
            _rgpCredentials[dwCredentialIndex] = ppc;
        }
        else
        {
            ppc->Release();
        }
    }
    else
    {
        hr = E_OUTOFMEMORY;
    }

    return hr;
}

HRESULT CSampleProvider::_CreateEnumeratedCredentials()
{
    HRESULT hr = E_INVALIDARG;
    switch(_cpus)
    {
    case CPUS_LOGON:
        if (_pkiulSetSerialization)
        {
            hr = _EnumerateSetSerialization();
        }
        else
        {
            hr = _EnumerateCredentials();
        }
        break;

    case CPUS_CHANGE_PASSWORD:
        break;

    case CPUS_UNLOCK_WORKSTATION:
        hr = _EnumerateCredentials();  
        break;

    case CPUS_CREDUI:
        _bDefaultToFirstCredential = true;

        if (_pkiulSetSerialization)
        {
            hr = _EnumerateSetSerialization();
        }
        if (_dwCredUIFlags & CREDUIWIN_ENUMERATE_ADMINS)
        {
            // not handled in this sample
        }
        else if (!(_dwCredUIFlags & CREDUIWIN_IN_CRED_ONLY))
        {
            if (_pkiulSetSerialization && SUCCEEDED(hr))
            {
                hr = _EnumerateCredentials(true);
            }
            else
            {
                hr = _EnumerateCredentials(false);
            }
        }
        break;

    default:
        break;
    }
    return hr;
}

HRESULT CSampleProvider::_EnumerateCredentials(__in bool bAlreadyHaveSetSerializationCred)
{
    DWORD dwStart = bAlreadyHaveSetSerializationCred ? 1 : 0;
    HRESULT hr = _EnumerateOneCredential(dwStart++, L"Administrator");
    if (SUCCEEDED(hr))
    {
        hr = _EnumerateOneCredential(dwStart++, L"Guest");
    }
    return hr;
}

HRESULT CSampleProvider::_EnumerateSetSerialization()
{
    KERB_INTERACTIVE_LOGON* pkil = &_pkiulSetSerialization->Logon;

    _bAutoSubmitSetSerializationCred = false;
    _bDefaultToFirstCredential = false;

    WCHAR wszUsername[MAX_PATH] = {0};
    WCHAR wszPassword[MAX_PATH] = {0};

    HRESULT hr = StringCbCopyNW(wszUsername, sizeof(wszUsername), pkil->UserName.Buffer, pkil->UserName.Length);

    if (SUCCEEDED(hr))
    {
        hr = StringCbCopyNW(wszPassword, sizeof(wszPassword), pkil->Password.Buffer, pkil->Password.Length);

        if (SUCCEEDED(hr))
        {
            CSampleCredential* pCred = new CSampleCredential();

            if (pCred)
            {
                hr = pCred->Initialize(_cpus, s_rgCredProvFieldDescriptors, s_rgFieldStatePairs, _dwCredUIFlags, wszUsername, wszPassword);

                if (SUCCEEDED(hr))
                {
                    _rgpCredentials[0] = pCred;
                    _bDefaultToFirstCredential = true;  
                }
            }
            else
            {
                hr = E_OUTOFMEMORY;
            }

            if (SUCCEEDED(hr) && (0 < wcslen(wszPassword)))
            {
                _bAutoSubmitSetSerializationCred = true;
            }
        }
    }

    return hr;
}

HRESULT CSample_CreateInstance(__in REFIID riid, __deref_out void** ppv)
{
    HRESULT hr;

    CSampleProvider* pProvider = new CSampleProvider();

    if (pProvider)
    {
        hr = pProvider->QueryInterface(riid, ppv);
        pProvider->Release();
    }
    else
    {
        hr = E_OUTOFMEMORY;
    }

    return hr;
}