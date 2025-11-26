Sample QR Code Login credential provider
=======================================

This sample is based on the Windows Credential Provider sample.

Purpose:
- Display a QR code (static URL https://www.baidu.com) in the credential tile image.
- QR bitmap is generated at runtime and sized to 300x300.

Notes:
- Project file is configured with PlatformToolset v140 (VS2015) to be compatible with VS2016.
- For more robust QR generation replace qrcodegen.cpp with the full implementation from:
  https://github.com/nayuki/QR-Code-generator (C++ version).
- Register sampleqrcodelogin by importing Register.reg (requires admin). Test in a VM.
- To unregister, import Unregister.reg.