//
// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//
// This file contains some global variables that describe what our
// QR code login tile looks like.  It defines what fields a tile has 
// and which fields show in which states of LogonUI.

#pragma once
#include <helpers.h>

// The indexes of each of the fields in our QR code credential provider's tiles.
enum QR_CODE_FIELD_ID 
{
    QRFI_TILEIMAGE       = 0,      // User tile image
    QRFI_USERNAME        = 1,      // Username field (may be hidden in QR code flow)
    QRFI_QR_CODE_LABEL   = 2,      // Label for QR code
    QRFI_QR_CODE_IMAGE   = 3,      // QR code image
    QRFI_SUBMIT_BUTTON   = 4,      // Submit button
    QRFI_NUM_FIELDS      = 5,      // Note: if new fields are added, keep NUM_FIELDS last.  This is used as a count of the number of fields
};

// The first value indicates when the tile is displayed (selected, not selected)
// the second indicates things like whether the field is enabled, whether it has key focus, etc.
struct FIELD_STATE_PAIR
{
    CREDENTIAL_PROVIDER_FIELD_STATE cpfs;
    CREDENTIAL_PROVIDER_FIELD_INTERACTIVE_STATE cpfis;
};

// These two arrays are seperate because a credential provider might
// want to set up a credential with various combinations of field state pairs 
// and field descriptors.

// The field state value indicates whether the field is displayed
// in the selected tile, the deselected tile, or both.
// The Field interactive state indicates when 
static const FIELD_STATE_PAIR s_rgQRCodeFieldStatePairs[] = 
{
    { CPFS_DISPLAY_IN_BOTH, CPFIS_NONE },                   // QRFI_TILEIMAGE
    { CPFS_HIDDEN, CPFIS_NONE },                            // QRFI_USERNAME (hidden for QR code login)
    { CPFS_DISPLAY_IN_SELECTED_TILE, CPFIS_NONE },          // QRFI_QR_CODE_LABEL
    { CPFS_DISPLAY_IN_SELECTED_TILE, CPFIS_NONE },          // QRFI_QR_CODE_IMAGE
    { CPFS_DISPLAY_IN_SELECTED_TILE, CPFIS_NONE    },       // QRFI_SUBMIT_BUTTON   
};

// Field descriptors for QR code unlock and logon.
// The first field is the index of the field.
// The second is the type of the field.
// The third is the name of the field, NOT the value which will appear in the field.
static const CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR s_rgQRCodeCredProvFieldDescriptors[] =
{
    { QRFI_TILEIMAGE, CPFT_TILE_IMAGE, L"User Image" },
    { QRFI_USERNAME, CPFT_LARGE_TEXT, L"Username" },
    { QRFI_QR_CODE_LABEL, CPFT_SMALL_TEXT, L"QR Code Label" },
    { QRFI_QR_CODE_IMAGE, CPFT_TILE_IMAGE, L"QR Code Image" },
    { QRFI_SUBMIT_BUTTON, CPFT_SUBMIT_BUTTON, L"Submit" },
};