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

## Building the Project

### Prerequisites
- Visual Studio 2019 or later
- Windows SDK
- C++ build tools

### Steps
1. Open the solution file `QrCodeLoginCredentialProvider.sln` in Visual Studio
2. Select the target platform (x64 or x86) and configuration (Debug or Release)
3. Build the solution (Ctrl+Shift+B)

The resulting DLL will be named `QrCodeLoginCredentialProvider.dll` and located in the output directory:
- Debug builds: `x64/Debug/` or `x86/Debug/`
- Release builds: `x64/Release/` or `x86/Release/`

### Manual Build with MSBuild (Command Line)
```cmd
# For x64 Release build
msbuild QrCodeLoginCredentialProvider.sln /p:Configuration=Release /p:Platform=x64

# For x86 Release build  
msbuild QrCodeLoginCredentialProvider.sln /p:Configuration=Release /p:Platform=Win32
```

## Installation

After building, register the DLL using regsvr32 and update the Windows authentication policies to include this credential provider.

## Architecture
- `CQRCodeLoginCredential.h/cpp`: Implements the ICredentialProviderCredential interface with QR code display
- `CQRCodeLoginProvider.cpp`: Implements the ICredentialProvider interface
- `dll.cpp/h`: DLL entry points and COM class factory
- `guid.cpp/h`: GUID definitions for COM registration
- `common.h`: Field definitions and constants
- `resources.rc`: Resource definitions
- `QrCodeLoginCredentialProvider.def`: Export definitions