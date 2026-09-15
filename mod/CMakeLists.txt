add_library(UE4SSLuaEventBridge SHARED
    src/BridgeMod.cpp
    src/EventRegistry.cpp
)

target_compile_features(UE4SSLuaEventBridge PRIVATE cxx_std_20)
target_include_directories(UE4SSLuaEventBridge PRIVATE include)
target_link_libraries(UE4SSLuaEventBridge PRIVATE UE4SS)

target_compile_definitions(UE4SSLuaEventBridge PRIVATE
    UE4SSLEB_VERSION="0.1.0-dev"
    UE4SSLEB_TARGET_UE4SS_COMMIT="97b7e501"
)

set_target_properties(UE4SSLuaEventBridge PROPERTIES
    OUTPUT_NAME "main"
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist/UE4SSLuaEventBridge/dlls"
)

