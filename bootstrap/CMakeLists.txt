cmake_minimum_required(VERSION 3.22)

get_filename_component(UE4SSLEB_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(NOT DEFINED UE4SSLEB_PRODUCT_VERSION)
    file(STRINGS "${UE4SSLEB_REPO_ROOT}/contract/Version.hpp" UE4SSLEB_VERSION_LINE REGEX "^#define UE4SSLEB_VERSION ")
    if(NOT UE4SSLEB_VERSION_LINE MATCHES "\"([0-9]+\\.[0-9]+\\.[0-9]+)\"")
        message(FATAL_ERROR "Invalid product version in contract/Version.hpp")
    endif()
    set(UE4SSLEB_PRODUCT_VERSION "${CMAKE_MATCH_1}")
endif()
project(UE4SSLEBBootstrap VERSION ${UE4SSLEB_PRODUCT_VERSION} LANGUAGES CXX)

option(UE4SSLEB_BUILD_TESTS "Build portable unit tests" ON)
if(UE4SSLEB_BUILD_TESTS AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    enable_testing()
endif()

configure_file("${UE4SSLEB_REPO_ROOT}/packaging/main.json.in" "${CMAKE_CURRENT_BINARY_DIR}/main.json" @ONLY)

if(WIN32)
    if(NOT MSVC)
        message(FATAL_ERROR "The bootstrap must be built with MSVC.")
    endif()
    enable_language(RC)
    set(UE4SSLEB_BOOTSTRAP_RESOURCE "${CMAKE_CURRENT_BINARY_DIR}/Version.rc")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/resources/Version.rc.in" "${UE4SSLEB_BOOTSTRAP_RESOURCE}" @ONLY)
    add_library(UE4SSLEBBootstrap SHARED src/Bootstrap.cpp "${UE4SSLEB_BOOTSTRAP_RESOURCE}")
    target_compile_features(UE4SSLEBBootstrap PRIVATE cxx_std_20)
    target_include_directories(UE4SSLEBBootstrap PRIVATE include "${UE4SSLEB_REPO_ROOT}/contract")
    target_link_libraries(UE4SSLEBBootstrap PRIVATE kernel32 version)
    target_compile_options(UE4SSLEBBootstrap PRIVATE /W4 /WX /permissive-)
    set_property(TARGET UE4SSLEBBootstrap PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
    set_target_properties(UE4SSLEBBootstrap PROPERTIES
        OUTPUT_NAME main
        PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist/_ModCore_UE4SSLuaEventBridge/dlls"
    )
    add_custom_command(TARGET UE4SSLEBBootstrap POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_BINARY_DIR}/main.json"
            "${CMAKE_BINARY_DIR}/dist/_ModCore_UE4SSLuaEventBridge/dlls/main.json"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${UE4SSLEB_REPO_ROOT}/packaging/enabled.txt"
            "${CMAKE_BINARY_DIR}/dist/_ModCore_UE4SSLuaEventBridge/enabled.txt"
    )
endif()

if(UE4SSLEB_BUILD_TESTS)
    add_executable(BootstrapSelectionTests tests/BootstrapSelectionTests.cpp)
    target_compile_features(BootstrapSelectionTests PRIVATE cxx_std_20)
    target_include_directories(BootstrapSelectionTests PRIVATE include)
    add_test(NAME BootstrapSelectionTests COMMAND BootstrapSelectionTests)
endif()
