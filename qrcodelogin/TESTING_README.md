# QR Code Login Test Programs

This directory contains test programs to validate the QR code login functionality without needing to build and deploy the Windows Credential Provider DLL.

## Available Test Programs

### 1. Mock Test (Recommended for local testing)
- **File**: `test_qr_login_mock.cpp`
- **Executable**: `test_qr_login_mock`
- **Purpose**: Simulates the QR code login flow with random responses
- **Features**:
  - Generates random QR codes
  - Simulates login status (pending, success, expired)
  - Tests 10-minute timeout and QR code refresh
  - No external dependencies required

### 2. Real API Test
- **File**: `test_qr_login.cpp`
- **Executable**: `test_qr_login`
- **Purpose**: Tests with actual API endpoints
- **Requirements**:
  - Access to the real API endpoints
  - Network connectivity
  - Valid API endpoints

## How to Build

```bash
cd /workspace/qrcodelogin
mkdir -p build
cd build
cmake ..
make
```

This will create two executables:
- `test_qr_login` - for real API testing
- `test_qr_login_mock` - for local functionality testing

## How to Run

### Run the mock test (recommended for local development):
```bash
cd /workspace/qrcodelogin/build
./test_qr_login_mock
```

### Run the real API test (requires working endpoints):
```bash
cd /workspace/qrcodelogin/build
./test_qr_login
```

## Test Flow Simulation

The mock test program simulates the following behavior:
1. Fetches QR code data (simulated)
2. Displays the QR code URL
3. Checks login status every 2 seconds
4. Randomly returns different statuses:
   - 70% chance: "pending" (still waiting)
   - 20% chance: "success" (login successful)
   - 10% chance: "expired" (QR code expired)
5. After 10 minutes, refreshes the QR code
6. Continues until successful login or maximum checks reached

## Dependencies

For the real API test, the following packages are required:
- libcurl4-openssl-dev
- libjsoncpp-dev
- pkg-config

Install them with:
```bash
sudo apt-get install libcurl4-openssl-dev libjsoncpp-dev pkg-config
```

The mock test requires no external dependencies beyond standard C++ libraries.