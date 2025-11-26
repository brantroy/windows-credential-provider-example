# QR Code Login Credential Provider

This is a Windows Credential Provider that implements QR code-based authentication for the Windows login/unlock screen. The provider displays a QR code on the login screen that users can scan with their mobile device to authenticate.

## Features

- Displays a QR code on the Windows login screen
- The QR code contains a static URL that can be customized
- Implements the standard Windows Credential Provider interface
- Compatible with both logon and unlock scenarios

## Directory Structure

```
qrcodelogincredentialprovider/
├── CQRCodeLoginCredential.h/cpp    # Main credential implementation
├── CSampleProvider.cpp             # Credential provider implementation
├── common.h                        # Field definitions and constants
├── dll.h/cpp                       # DLL entry points and helpers
├── guid.h/cpp                      # GUID definitions
├── resource.h                      # Resource identifiers
├── resources.rc                    # Resource script
├── QrCodeLoginCredentialProvider.def # Export definitions
├── QrCodeLoginCredentialProvider.vcxproj # Visual Studio project
└── README.md                       # This file
```

## Implementation Details

The credential provider creates a tile with the following fields:
- QR code image (displayed as a bitmap)
- QR code label
- Submit button

The QR code is generated dynamically using GDI+ and contains a URL that can be customized. In a real implementation, this URL would point to an authentication service that handles the login process.

## Next Steps

To complete the full authentication flow as requested:
1. Integrate with an API to get the dynamic URL
2. Implement polling mechanism to check authentication status
3. Add proper QR code library (like libqrencode) for real QR code generation
4. Implement the authentication completion logic

## Building

To build this credential provider, open the .vcxproj file in Visual Studio and build for your target platform (x64 recommended for modern Windows systems).

## Installation

After building, register the DLL using regsvr32 and update the Windows authentication policies to include this credential provider.