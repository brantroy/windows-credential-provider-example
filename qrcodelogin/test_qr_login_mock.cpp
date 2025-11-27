#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>

// Structure to hold API response data
struct QRAuthResponse {
    std::string qrCodeId;
    std::string authUrl;
    std::string token;
    std::string qrCodeContent;
    bool isValid = false;
};

// Function to generate a random string
std::string generateRandomString(int length) {
    const std::string charset = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, charset.size() - 1);
    
    std::string result;
    for (int i = 0; i < length; ++i) {
        result += charset[dis(gen)];
    }
    return result;
}

// Function to generate a random token
std::string generateRandomToken() {
    return generateRandomString(32);
}

// Function to generate a random QR code ID
std::string generateRandomQRCodeId() {
    return generateRandomString(16);
}

// Mock function to simulate getting QR auth data
QRAuthResponse getQRAuthData() {
    QRAuthResponse result;
    
    // Simulate network delay
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Generate mock response data
    result.qrCodeId = generateRandomQRCodeId();
    result.authUrl = "https://example.com/login?token=" + generateRandomToken();
    result.token = generateRandomToken();
    result.qrCodeContent = "https://qrcode.example.com/" + result.qrCodeId;
    result.isValid = true;
    
    return result;
}

// Mock function to simulate getting login result
std::string getLoginResult(const std::string& qrCodeId, const std::string& token) {
    // Simulate network delay
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Randomly decide the status for simulation
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1, 100);
    
    int randomValue = dis(gen);
    
    // For simulation purposes:
    // 70% of the time return "pending" (still waiting)
    // 20% of the time return "success" (login successful)
    // 10% of the time return "expired" (QR code expired)
    if (randomValue <= 20) {
        return "{\"status\":\"success\",\"userId\":\"user123\",\"userName\":\"Test User\"}";
    } else if (randomValue <= 30) {
        return "{\"status\":\"expired\"}";
    } else {
        return "{\"status\":\"pending\"}";
    }
}

// Simple function to simulate QR code generation (just print the URL)
void generateQRCode(const std::string& url) {
    std::cout << "\n=== QR Code Generated ===" << std::endl;
    std::cout << "QR Code URL: " << url << std::endl;
    std::cout << "QR Code ID: " << url.substr(url.find_last_of('/') + 1) << std::endl;
    std::cout << "Scan this QR code with your mobile device to login." << std::endl;
    std::cout << "=========================\n" << std::endl;
}

// Function to check login status
bool checkLoginStatus(const std::string& qrCodeId, const std::string& token) {
    std::string result = getLoginResult(qrCodeId, token);
    
    // Simple parsing of the mock JSON response
    if (result.find("\"status\":\"success\"") != std::string::npos) {
        std::cout << "Login Status: success" << std::endl;
        std::cout << "Login successful!" << std::endl;
        return true;
    } else if (result.find("\"status\":\"pending\"") != std::string::npos) {
        std::cout << "Login Status: pending" << std::endl;
        std::cout << "Waiting for user to scan QR code..." << std::endl;
        return false;
    } else if (result.find("\"status\":\"expired\"") != std::string::npos) {
        std::cout << "Login Status: expired" << std::endl;
        std::cout << "QR code has expired. Generating new QR code..." << std::endl;
        return false; // Return false to indicate we need to regenerate
    } else {
        std::cout << "Login Status: unknown" << std::endl;
        std::cout << "Unknown status, continuing to wait..." << std::endl;
        return false;
    }
}

int main() {
    std::cout << "QR Code Login Test Program (Mock Version)" << std::endl;
    std::cout << "=========================================" << std::endl;
    
    // Get initial QR auth data
    QRAuthResponse authData = getQRAuthData();
    
    if (!authData.isValid) {
        std::cerr << "Failed to get QR auth data" << std::endl;
        return 1;
    }
    
    // Determine the QR code URL from available fields
    std::string qrCodeUrl = authData.qrCodeContent.empty() ? authData.authUrl : authData.qrCodeContent;
    
    // Generate QR code (simulated)
    generateQRCode(qrCodeUrl);
    
    // Record start time for 10-minute timeout
    auto startTime = std::chrono::steady_clock::now();
    const auto tenMinutes = std::chrono::minutes(10);
    
    // Counter to eventually break the loop for demo purposes
    int checkCount = 0;
    const int maxChecks = 50; // Maximum number of checks before forcing exit
    
    // Main loop for checking login status
    while (true) {
        // Check if 10 minutes have passed
        auto currentTime = std::chrono::steady_clock::now();
        if (currentTime - startTime >= tenMinutes) {
            std::cout << "\n10 minutes have passed. Refreshing QR code..." << std::endl;
            
            // Get new auth data
            authData = getQRAuthData();
            if (!authData.isValid) {
                std::cerr << "Failed to get new QR auth data" << std::endl;
                break;
            }
            
            qrCodeUrl = authData.qrCodeContent.empty() ? authData.authUrl : authData.qrCodeContent;
            generateQRCode(qrCodeUrl);
            startTime = currentTime; // Reset timer
        }
        
        // Check login status
        bool loginSuccess = checkLoginStatus(authData.qrCodeId, authData.token);
        
        if (loginSuccess) {
            std::cout << "\nLogin successful! Exiting..." << std::endl;
            break;
        }
        
        // Increment check counter
        checkCount++;
        if (checkCount >= maxChecks) {
            std::cout << "\nMaximum checks reached. Exiting for demo purposes..." << std::endl;
            std::cout << "In a real scenario, this would continue indefinitely waiting for login." << std::endl;
            break;
        }
        
        // Wait 2 seconds before next check
        std::cout << "Checking again in 2 seconds...\n" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    
    std::cout << "\nTest completed." << std::endl;
    return 0;
}