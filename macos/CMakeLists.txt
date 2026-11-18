cmake_minimum_required(VERSION 3.24)
# macOS (arm64) and iOS (CyPort) build of RedHttpClient. The Windows build is the top-level CMakeLists.txt, unchanged.
#   cmake -S macos -B build-mac && cmake --build build-mac            (macOS)
#   macos/ios-stage.sh                                                (iOS: build-ios/, signed deploy/)
# HTTP goes through NSURLSession (macos/cpr-apple: the part of the cpr API the sources use), not libcurl.
project(RedHttpClient VERSION 0.8.0 LANGUAGES CXX OBJCXX)

set(REDMAC_NAME RedHttpClient)
set(REDMAC_ENTRY redhttpclient_registerNatives)
set(REDMAC_RTTI_ID_BAND 0x7FE70000)
set(REDMAC_SERIAL_SLOT 3)
include("${CMAKE_CURRENT_SOURCE_DIR}/Common.cmake")

configure_file("${REDMAC_ROOT}/src/Config.h.in" Config/Config.h)

set(SRC "${REDMAC_ROOT}/src")
redmac_plugin(
    ${SRC}/main.cpp
    ${SRC}/AsyncHttpClient.cpp
    ${SRC}/HttpClient.cpp
    ${SRC}/HttpPlugin.cpp
)
# The C++ APIs of RedData and RedFileSystem (header only: they call those plugins' natives through the RTTI when they are loaded).
target_include_directories(RedHttpClient PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/cpr-apple"
    "${REDMAC_ROOT}/deps/RedData/include"
    "${REDMAC_ROOT}/deps/RedFileSystem/include")
target_compile_definitions(RedHttpClient PRIVATE HAS_REDDATA HAS_REDFILESYSTEM)

# NSURLSession. Its own static library: an Objective-C++ file without the plugin's force-included headers.
add_library(cpr_apple STATIC "${CMAKE_CURRENT_SOURCE_DIR}/cpr-apple/CprApple.mm")
target_include_directories(cpr_apple PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/cpr-apple")
target_compile_options(cpr_apple PRIVATE -fobjc-arc -fvisibility=hidden)
target_compile_definitions(cpr_apple PRIVATE "CPR_APPLE_USER_AGENT=\"RedHttpClient/${PROJECT_VERSION}\"")
set_target_properties(cpr_apple PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON)
target_link_libraries(RedHttpClient PRIVATE cpr_apple "-framework Foundation")
