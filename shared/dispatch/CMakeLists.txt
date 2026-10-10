# Queue and dispatch code shared by the bridge products. Header-only.
# Link UE4SSXBDispatch and include <dispatch/Name.hpp>; names live in UE4SSXB.
add_library(UE4SSXBDispatch INTERFACE)
target_include_directories(UE4SSXBDispatch INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(UE4SSXBDispatch INTERFACE cxx_std_20)
