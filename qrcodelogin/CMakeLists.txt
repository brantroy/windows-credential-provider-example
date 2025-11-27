cmake_minimum_required(VERSION 3.10)
project(qr_login_test)

set(CMAKE_CXX_STANDARD 11)

# Find required packages
find_package(PkgConfig REQUIRED)
find_package(CURL REQUIRED)

# If pkg-config is available, try to find jsoncpp
if(PKG_CONFIG_FOUND)
    pkg_check_modules(JSONCPP jsoncpp)
endif()

# If jsoncpp wasn't found via pkg-config, try find_package
if(NOT JSONCPP_FOUND)
    find_package(jsoncpp QUIET)
    if(jsoncpp_FOUND)
        set(JSONCPP_INCLUDE_DIRS ${jsoncpp_INCLUDE_DIRS})
        set(JSONCPP_LIBRARIES ${jsoncpp_LIBRARIES})
    endif()
endif()

# If still not found, we'll need to manually set paths (common on some systems)
if(NOT JSONCPP_FOUND AND NOT jsoncpp_FOUND)
    # Common locations for jsoncpp
    find_path(JSONCPP_INCLUDE_DIR json/json.h
        /usr/include
        /usr/local/include
        /opt/local/include
        /usr/include/jsoncpp
    )
    
    find_library(JSONCPP_LIBRARY
        NAMES jsoncpp
        PATHS /usr/lib /usr/local/lib /opt/local/lib
    )
    
    if(JSONCPP_INCLUDE_DIR AND JSONCPP_LIBRARY)
        set(JSONCPP_FOUND TRUE)
        set(JSONCPP_INCLUDE_DIRS ${JSONCPP_INCLUDE_DIR})
        set(JSONCPP_LIBRARIES ${JSONCPP_LIBRARY})
    endif()
endif()

# Add executables
add_executable(test_qr_login test_qr_login.cpp)
add_executable(test_qr_login_mock test_qr_login_mock.cpp)

# Link libraries for real test
if(CURL_FOUND)
    target_link_libraries(test_qr_login ${CURL_LIBRARIES})
    target_include_directories(test_qr_login PRIVATE ${CURL_INCLUDE_DIRS})
endif()

if(JSONCPP_FOUND)
    target_link_libraries(test_qr_login ${JSONCPP_LIBRARIES})
    target_include_directories(test_qr_login PRIVATE ${JSONCPP_INCLUDE_DIRS})
else()
    message(WARNING "jsoncpp library not found. You may need to install it (e.g., 'sudo apt-get install libjsoncpp-dev')")
endif()

# Mock test doesn't need external libraries
target_link_libraries(test_qr_login_mock)

# Additional compiler flags
target_compile_features(test_qr_login PRIVATE cxx_std_11)