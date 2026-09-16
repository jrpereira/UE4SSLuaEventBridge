cmake_minimum_required(VERSION 3.22)

project(UE4SSLuaEventBridge VERSION 0.3.0 LANGUAGES CXX)

option(UE4SSLEB_BUILD_TESTS "Build portable unit tests" ON)

set(UE4SSLEB_LUA_API_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/mod/lua/bridge_api.lua")
set(UE4SSLEB_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(UE4SSLEB_GENERATED_LUA_API
    "${UE4SSLEB_GENERATED_INCLUDE_DIR}/UE4SSLuaEventBridge/EmbeddedLuaAPI.hpp")
file(MAKE_DIRECTORY "${UE4SSLEB_GENERATED_INCLUDE_DIR}/UE4SSLuaEventBridge")
file(READ "${UE4SSLEB_LUA_API_SOURCE}" UE4SSLEB_LUA_API)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbeddedLuaAPI.hpp.in"
    "${UE4SSLEB_GENERATED_LUA_API}"
    @ONLY
)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${UE4SSLEB_LUA_API_SOURCE}")

if(WIN32)
    if(NOT MSVC)
        message(FATAL_ERROR "The ABI-pinned UE4SS target must be built with MSVC.")
    endif()
    find_program(MSVC_LIB_EXE lib.exe REQUIRED)
    set(UE4SS_IMPORT_LIB "${CMAKE_CURRENT_BINARY_DIR}/UE4SS-97b7e501.lib")
    add_custom_command(
        OUTPUT "${UE4SS_IMPORT_LIB}"
        COMMAND "${MSVC_LIB_EXE}" /nologo /machine:x64
            "/def:${CMAKE_CURRENT_SOURCE_DIR}/abi/UE4SS-97b7e501.def"
            "/out:${UE4SS_IMPORT_LIB}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/abi/UE4SS-97b7e501.def"
        VERBATIM
    )
    add_custom_target(UE4SSImportLibrary DEPENDS "${UE4SS_IMPORT_LIB}")

    add_library(UE4SSLuaEventBridge SHARED
        mod/src/BridgeMod.cpp
        mod/src/EnhancedInputBackend.cpp
        "${UE4SSLEB_GENERATED_LUA_API}"
    )
    add_dependencies(UE4SSLuaEventBridge UE4SSImportLibrary)
    target_compile_features(UE4SSLuaEventBridge PRIVATE cxx_std_20)
    target_include_directories(UE4SSLuaEventBridge PRIVATE
        "${UE4SSLEB_GENERATED_INCLUDE_DIR}"
        mod/include
    )
    target_link_libraries(UE4SSLuaEventBridge PRIVATE "${UE4SS_IMPORT_LIB}" kernel32)
    target_compile_options(UE4SSLuaEventBridge PRIVATE /W4 /WX /GR- /permissive-)
    set_property(TARGET UE4SSLuaEventBridge PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
    set_target_properties(UE4SSLuaEventBridge PROPERTIES
        OUTPUT_NAME main
        PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist/UE4SSLuaEventBridge/dlls"
    )
    add_custom_command(TARGET UE4SSLuaEventBridge POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/mod/enabled.txt"
            "${CMAKE_BINARY_DIR}/dist/UE4SSLuaEventBridge/enabled.txt"
    )
endif()

if(UE4SSLEB_BUILD_TESTS)
    enable_testing()
    add_executable(EventRegistryTests mod/src/EventRegistry.cpp tests/EventRegistryTests.cpp)
    target_compile_features(EventRegistryTests PRIVATE cxx_std_20)
    target_include_directories(EventRegistryTests PRIVATE mod/include)
    add_test(NAME EventRegistryTests COMMAND EventRegistryTests)
endif()
