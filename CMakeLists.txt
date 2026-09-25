cmake_minimum_required(VERSION 3.22)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/contract/Version.hpp" UE4SSLEB_VERSION_LINE REGEX "^#define UE4SSLEB_VERSION ")
if(NOT UE4SSLEB_VERSION_LINE MATCHES "\"([0-9]+\\.[0-9]+\\.[0-9]+)\"")
    message(FATAL_ERROR "Invalid product version in contract/Version.hpp")
endif()
set(UE4SSLEB_PRODUCT_VERSION "${CMAKE_MATCH_1}")
project(UE4SSLuaEventBridgeSuperbuild VERSION ${UE4SSLEB_PRODUCT_VERSION} LANGUAGES CXX)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/contract/Version.hpp")

option(UE4SSLEB_BUILD_TESTS "Build portable unit tests" ON)
option(UE4SSLEB_COMPONENT_LAYOUT_128 "Experimental UE5.5 component layout for isolated sample tests" OFF)

if(UE4SSLEB_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(bootstrap)
add_subdirectory(bridge)
