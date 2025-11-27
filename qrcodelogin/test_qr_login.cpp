#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <regex>
#include <curl/curl.h>
#include <json/json.h> // You may need to install libjsoncpp-dev

// Structure to hold API response data
struct QRAuthResponse {
    std::string qrCodeId;
    std::string authUrl;
    std::string token;
    std::string qrCodeContent;
    bool isValid = false;
};

// Callback function for curl to write response data
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* response) {
    size_t totalSize = size * nmemb;
    response->append((char*)contents, totalSize);
    return totalSize;
}

// Function to make HTTP request and get QR auth data
QRAuthResponse getQRAuthData() {
    QRAuthResponse result;
    
    CURL* curl;
    CURLcode res;
    std::string response;

    curl = curl_easy_init();
    if(curl) {
        // Set URL for the authentication endpoint
        curl_easy_setopt(curl, CURLOPT_URL, "https://ehcloud-gw-ehtest.dxchi.com/oauth/auth/xxx");
        
        // Set callback function to capture response
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        
        // Follow redirects
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        
        // Perform the request
        res = curl_easy_perform(curl);
        
        // Check for errors
        if(res != CURLE_OK) {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
            curl_easy_cleanup(curl);
            return result;
        }
        
        // Get HTTP response code
        long response_code;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        if(response_code != 200) {
            std::cerr << "HTTP request failed with code: " << response_code << std::endl;
            curl_easy_cleanup(curl);
            return result;
        }
        
        curl_easy_cleanup(curl);
    } else {
        std::cerr << "Failed to initialize curl" << std::endl;
        return result;
    }

    // Parse JSON response
    Json::Value root;
    Json::Reader reader;
    
    if (!reader.parse(response, root)) {
        std::cerr << "Failed to parse JSON response: " << reader.getFormattedErrorMessages() << std::endl;
        return result;
    }

    // Extract required fields from JSON
    if (root.isMember("qrCodeId")) {
        result.qrCodeId = root["qrCodeId"].asString();
    }
    if (root.isMember("authUrl")) {
        result.authUrl = root["authUrl"].asString();
    }
    if (root.isMember("token")) {
        result.token = root["token"].asString();
    }
    if (root.isMember("qrCodeContent")) {
        result.qrCodeContent = root["qrCodeContent"].asString();
    }
    
    result.isValid = true;
    return result;
}

// Function to get login result
std::string getLoginResult(const std::string& qrCodeId, const std::string& token) {
    CURL* curl;
    CURLcode res;
    std::string response;

    curl = curl_easy_init();
    if(curl) {
        // Construct URL with parameters
        std::string url = "https://ehcloud-gw-ehtest.dxchi.com/oauth/loginResult?qrCodeId=" + 
                         qrCodeId + "&token=" + token;
        
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        
        res = curl_easy_perform(curl);
        
        if(res != CURLE_OK) {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
            curl_easy_cleanup(curl);
            return "";
        }
        
        long response_code;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        if(response_code != 200) {
            std::cerr << "HTTP request failed with code: " << response_code << std::endl;
            curl_easy_cleanup(curl);
            return "";
        }
        
        curl_easy_cleanup(curl);
    }

    return response;
}

// Simple function to simulate QR code generation (just print the URL)
void generateQRCode(const std::string& url) {
    std::cout << "\n=== QR Code Generated ===" << std::endl;
    std::cout << "QR Code URL: " << url << std::endl;
    std::cout << "Scan this QR code with your mobile device to login." << std::endl;
    std::cout << "=========================\n" << std::endl;
}

// Function to check login status
bool checkLoginStatus(const std::string& qrCodeId, const std::string& token) {
    std::string result = getLoginResult(qrCodeId, token);
    
    if (result.empty()) {
        std::cout << "Failed to get login result." << std::endl;
        return false;
    }
    
    Json::Value root;
    Json::Reader reader;
    
    if (!reader.parse(result, root)) {
        std::cout << "Failed to parse login result JSON: " << reader.getFormattedErrorMessages() << std::endl;
        return false;
    }
    
    // Assuming the API returns a status field
    if (root.isMember("status")) {
        std::string status = root["status"].asString();
        std::cout << "Login Status: " << status << std::endl;
        
        if (status == "success") {
            std::cout << "Login successful!" << std::endl;
            return true;
        } else if (status == "pending") {
            std::cout << "Waiting for user to scan QR code..." << std::endl;
            return false;
        } else if (status == "expired") {
            std::cout << "QR code has expired. Generating new QR code..." << std::endl;
            return false; // Return false to indicate we need to regenerate
        } else {
            std::cout << "Unknown status: " << status << std::endl;
            return false;
        }
    }
    
    // If no status field, assume failure
    std::cout << "No status field in response, assuming pending..." << std::endl;
    return false;
}

int main() {
    std::cout << "QR Code Login Test Program" << std::endl;
    std::cout << "===========================" << std::endl;
    
    // Initialize curl globally
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    // Get initial QR auth data
    QRAuthResponse authData = getQRAuthData();
    
    if (!authData.isValid) {
        std::cerr << "Failed to get QR auth data" << std::endl;
        curl_global_cleanup();
        return 1;
    }
    
    // Determine the QR code URL from available fields
    std::string qrCodeUrl = authData.qrCodeContent.empty() ? authData.authUrl : authData.qrCodeContent;
    
    // Generate QR code (simulated)
    generateQRCode(qrCodeUrl);
    
    // Record start time for 10-minute timeout
    auto startTime = std::chrono::steady_clock::now();
    const auto tenMinutes = std::chrono::minutes(10);
    
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
        
        // Wait 2 seconds before next check
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    
    // Cleanup curl
    curl_global_cleanup();
    
    std::cout << "\nTest completed." << std::endl;
    return 0;
}