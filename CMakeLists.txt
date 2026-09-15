cmake_minimum_required(VERSION 3.22)

project(UE4SSLuaEventBridge VERSION 0.1.0 LANGUAGES CXX)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/RE-UE4SS/CMakeLists.txt")
    message(FATAL_ERROR
        "RE-UE4SS is missing. Initialize the submodule and check it out at "
        "commit 97b7e501 before configuring this project.")
endif()

add_subdirectory(RE-UE4SS)
add_subdirectory(mod)

