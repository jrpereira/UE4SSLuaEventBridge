cmake_minimum_required(VERSION 3.22)

get_filename_component(UE4SSLEB_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
include("${CMAKE_CURRENT_LIST_DIR}/cmake/BridgeBootstrap.cmake")
if(NOT DEFINED UE4SSLEB_PRODUCT_VERSION)
    bridge_read_product_version(UE4SSLEB_PRODUCT_VERSION "${UE4SSLEB_REPO_ROOT}/bridge-events/contract/Version.hpp")
endif()
project(UE4SSLEBBootstrap VERSION ${UE4SSLEB_PRODUCT_VERSION} LANGUAGES CXX)

option(UE4SSLEB_BUILD_TESTS "Build portable unit tests" ON)
if(UE4SSLEB_BUILD_TESTS AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    enable_testing()
endif()

# The event bridge's bootstrap. This call moves to the product's own
# CMakeLists.txt once the product folder exists.
configure_file("${UE4SSLEB_REPO_ROOT}/bridge-events/packaging/main.json.in" "${CMAKE_CURRENT_BINARY_DIR}/main.json" @ONLY)
add_bridge_bootstrap(
    TARGET UE4SSLEBBootstrap
    PRODUCT UE4SSLuaEventBridge
    DISPLAY_NAME "UE4SS Lua Event Bridge"
    MAGIC 0x4C454231
    CLAIM_NAME UE4SSLEB
    VERSION_FILE "${UE4SSLEB_REPO_ROOT}/bridge-events/contract/Version.hpp"
    DIST_FOLDER 0_ModCore_UE4SSLuaEventBridge
    SELECTOR_FILE "${CMAKE_CURRENT_BINARY_DIR}/main.json"
    ENABLED_FILE "${UE4SSLEB_REPO_ROOT}/bridge-events/packaging/enabled.txt"
)

if(UE4SSLEB_BUILD_TESTS)
    add_executable(BootstrapSelectionTests tests/BootstrapSelectionTests.cpp)
    target_compile_features(BootstrapSelectionTests PRIVATE cxx_std_20)
    target_include_directories(BootstrapSelectionTests PRIVATE include)
    add_test(NAME BootstrapSelectionTests COMMAND BootstrapSelectionTests)
endif()
