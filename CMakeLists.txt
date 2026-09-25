cmake_minimum_required(VERSION 3.22)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/mod/include/UE4SSLuaEventBridge/Version.hpp" UE4SSLEB_VERSION_LINE REGEX "^#define UE4SSLEB_VERSION ")
if(NOT UE4SSLEB_VERSION_LINE MATCHES "\"([0-9]+\\.[0-9]+\\.[0-9]+)(-[A-Za-z0-9.-]+)?\"")
    message(FATAL_ERROR "Invalid product version in Version.hpp")
endif()
set(UE4SSLEB_PRODUCT_VERSION "${CMAKE_MATCH_1}${CMAKE_MATCH_2}")
project(UE4SSLuaEventBridge VERSION ${CMAKE_MATCH_1} LANGUAGES CXX)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/mod/include/UE4SSLuaEventBridge/Version.hpp")

option(UE4SSLEB_BUILD_TESTS "Build portable unit tests" ON)
option(UE4SSLEB_COMPONENT_LAYOUT_128 "Experimental UE5.5 component layout for isolated sample tests" OFF)

set(UE4SSLEB_LUA_API_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/mod/lua/bridge_api.lua")
set(UE4SSLEB_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(UE4SSLEB_GENERATED_LUA_API
    "${UE4SSLEB_GENERATED_INCLUDE_DIR}/UE4SSLuaEventBridge/EmbeddedLuaAPI.hpp")
file(MAKE_DIRECTORY "${UE4SSLEB_GENERATED_INCLUDE_DIR}/UE4SSLuaEventBridge")
file(READ "${UE4SSLEB_LUA_API_SOURCE}" UE4SSLEB_LUA_API)
string(SUBSTRING "${UE4SSLEB_LUA_API}" 0 7000 UE4SSLEB_LUA_API_1)
string(SUBSTRING "${UE4SSLEB_LUA_API}" 7000 7000 UE4SSLEB_LUA_API_2)
string(SUBSTRING "${UE4SSLEB_LUA_API}" 14000 -1 UE4SSLEB_LUA_API_3)
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
    if(UE4SSLEB_COMPONENT_LAYOUT_128)
        target_compile_definitions(UE4SSLuaEventBridge PRIVATE UE4SSLEB_COMPONENT_LAYOUT_128=1)
    endif()
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
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist/_UE4SSLuaEventBridge/dlls"
    )
    add_custom_command(TARGET UE4SSLuaEventBridge POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/mod/enabled.txt"
            "${CMAKE_BINARY_DIR}/dist/_UE4SSLuaEventBridge/enabled.txt"
    )
endif()

if(UE4SSLEB_BUILD_TESTS)
    enable_testing()
    if(WIN32)
        add_executable(NativeBackendLifecycleTests tests/NativeBackendLifecycleTests.cpp)
        target_compile_features(NativeBackendLifecycleTests PRIVATE cxx_std_20)
        target_include_directories(NativeBackendLifecycleTests PRIVATE mod/include)
        target_compile_options(NativeBackendLifecycleTests PRIVATE /W4 /WX)
        add_test(NAME NativeBackendLifecycleTests COMMAND NativeBackendLifecycleTests)
        set_tests_properties(NativeBackendLifecycleTests PROPERTIES TIMEOUT 20)
    endif()
    add_executable(DispatchBudgetTests tests/DispatchBudgetTests.cpp)
    target_compile_features(DispatchBudgetTests PRIVATE cxx_std_20)
    target_include_directories(DispatchBudgetTests PRIVATE mod/include)
    add_test(NAME DispatchBudgetTests COMMAND DispatchBudgetTests)
    add_executable(QueueDispatchScheduleTests tests/QueueDispatchScheduleTests.cpp)
    target_compile_features(QueueDispatchScheduleTests PRIVATE cxx_std_20)
    target_include_directories(QueueDispatchScheduleTests PRIVATE mod/include)
    add_test(NAME QueueDispatchScheduleTests COMMAND QueueDispatchScheduleTests)
    add_executable(QueueBuffersTests tests/QueueBuffersTests.cpp)
    target_compile_features(QueueBuffersTests PRIVATE cxx_std_20)
    target_include_directories(QueueBuffersTests PRIVATE mod/include)
    add_test(NAME QueueBuffersTests COMMAND QueueBuffersTests)
    add_executable(WeakObjectPtrTests tests/WeakObjectPtrTests.cpp)
    target_compile_features(WeakObjectPtrTests PRIVATE cxx_std_20)
    target_include_directories(WeakObjectPtrTests PRIVATE mod/include)
    add_test(NAME WeakObjectPtrTests COMMAND WeakObjectPtrTests)
    add_executable(BindingSnapshotTests tests/BindingSnapshotTests.cpp)
    target_compile_features(BindingSnapshotTests PRIVATE cxx_std_20)
    target_include_directories(BindingSnapshotTests PRIVATE mod/include)
    add_test(NAME BindingSnapshotTests COMMAND BindingSnapshotTests)
    add_executable(LegacyInstallMigrationTests tests/LegacyInstallMigrationTests.cpp)
    target_compile_features(LegacyInstallMigrationTests PRIVATE cxx_std_20)
    target_include_directories(LegacyInstallMigrationTests PRIVATE mod/include)
    add_test(NAME LegacyInstallMigrationTests COMMAND LegacyInstallMigrationTests)
    find_package(Threads REQUIRED)
    target_link_libraries(QueueBuffersTests PRIVATE Threads::Threads)
    set_tests_properties(QueueBuffersTests PROPERTIES TIMEOUT 20)
    add_executable(SessionAliasIndexTests tests/SessionAliasIndexTests.cpp)
    target_compile_features(SessionAliasIndexTests PRIVATE cxx_std_20)
    target_include_directories(SessionAliasIndexTests PRIVATE mod/include)
    target_link_libraries(SessionAliasIndexTests PRIVATE Threads::Threads)
    add_test(NAME SessionAliasIndexTests COMMAND SessionAliasIndexTests)
    add_executable(LifecycleRegistryTests tests/LifecycleRegistryTests.cpp)
    target_compile_features(LifecycleRegistryTests PRIVATE cxx_std_20)
    target_include_directories(LifecycleRegistryTests PRIVATE mod/include)
    target_link_libraries(LifecycleRegistryTests PRIVATE Threads::Threads)
    add_test(NAME LifecycleRegistryTests COMMAND LifecycleRegistryTests)
endif()
