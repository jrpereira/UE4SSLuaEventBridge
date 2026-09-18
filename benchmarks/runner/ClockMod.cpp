// Test-only clock. Never ship this mod in the production bridge package.
#include <UE4SSLuaEventBridge/UE4SSABI.hpp>
#include <chrono>
#include <windows.h>
#include <fstream>
#include <iomanip>
#include <cstring>
using Lua = RC::LuaMadeSimple::Lua;
static int clock_seconds(const Lua& lua) {
    LARGE_INTEGER counter{}, frequency{};
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    lua.set_number(static_cast<double>(counter.QuadPart) / static_cast<double>(frequency.QuadPart));
    return 1;
}
// OS execution accounting for the calling thread, not elapsed wall time.
static int thread_cpu(const Lua& lua) {
    FILETIME created{}, exited{}, kernel{}, user{};
    ULONG64 cycles{};
    if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user) ||
        !QueryThreadCycleTime(GetCurrentThread(), &cycles)) {
        lua.set_nil(); lua.set_string("Windows thread CPU accounting failed"); return 2;
    }
    const auto ticks = [](FILETIME value) -> uint64_t {
        return (static_cast<uint64_t>(value.dwHighDateTime) << 32) | value.dwLowDateTime;
    };
    lua.set_integer(static_cast<int64_t>(cycles));
    lua.set_integer(static_cast<int64_t>(ticks(kernel) + ticks(user)));
    lua.set_integer(static_cast<int64_t>(GetCurrentThreadId()));
    return 3;
}
// All threads in the Unreal process, including UE routing, UE4SS and graphics.
static int process_cpu(const Lua& lua) {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        lua.set_nil(); lua.set_string("Windows process CPU accounting failed"); return 2;
    }
    const auto ticks = [](FILETIME v) -> uint64_t {
        return (static_cast<uint64_t>(v.dwHighDateTime) << 32) | v.dwLowDateTime;
    };
    lua.set_integer(static_cast<int64_t>(ticks(kernel) + ticks(user)));
    return 1;
}
// Test-only raw state read; Tap recognition remains in the baseline Lua code.
static int key_down(const Lua& lua) {
    const auto virtual_key = static_cast<int>(lua.get_integer(1));
    if (virtual_key < 1 || virtual_key > 0xff) {
        lua.set_nil(); lua.set_string("Invalid virtual-key code"); return 2;
    }
    DWORD foreground{};
    GetWindowThreadProcessId(GetForegroundWindow(), &foreground);
    lua.set_bool(foreground == GetCurrentProcessId() && (GetAsyncKeyState(virtual_key) & 0x8000) != 0);
    return 1;
}
// Read-only, one-shot layout evidence captured before any native binding.
static int inspect_layout(const Lua& lua) {
    std::wofstream out("C:/TestResults/component-layout.txt");
    const auto raw=lua.get_string(1);
    const std::wstring path(raw.begin(),raw.end());
    for(int index=2;index<=4;++index){
        auto* type=reinterpret_cast<RC::Unreal::UStruct*>(static_cast<uintptr_t>(lua.get_integer(1)));
        if(type)out<<"reflected_type_arg="<<index<<" path="<<type->GetPathName()<<" properties_size="<<type->GetPropertiesSize()<<"\n";
    }
    std::vector<RC::Unreal::UObject*> components;
    RC::Unreal::UObjectGlobals::FindAllOf(L"EnhancedInputComponent",components);
    for(auto* obj:components){
        if(obj->GetPathName()!=path)continue;
        unsigned char bytes[0x160]{}; SIZE_T got{};
        if(!ReadProcessMemory(GetCurrentProcess(),obj,bytes,sizeof(bytes),&got) || got!=sizeof(bytes))break;
        out<<"component="<<obj<<"\n";
        for(size_t i=0;i<sizeof(bytes);i+=8){
            uint64_t value{};std::memcpy(&value,bytes+i,8);
            out<<std::hex<<"offset="<<i<<" value="<<value<<"\n";
        }
    }
    lua.set_bool(out.good());return 1;
}
class ClockMod final : public RC::CppUserModBase {
public:
    ClockMod() { ModName=L"BridgeBenchmarkClock"; ModVersion=L"1"; }
    void on_lua_start(RC::StringViewType, Lua& lua, Lua&, Lua&, Lua*) override {
        lua.register_function("BridgeBenchmarkSeconds", &clock_seconds);
        lua.register_function("BridgeBenchmarkThreadCpu", &thread_cpu);
        lua.register_function("BridgeBenchmarkProcessCpu", &process_cpu);
        lua.register_function("BridgeBenchmarkKeyDown", &key_down);
        lua.register_function("BridgeBenchmarkInspectLayout", &inspect_layout);
    }
};
extern "C" __declspec(dllexport) RC::CppUserModBase* start_mod() { return new ClockMod; }
extern "C" __declspec(dllexport) void uninstall_mod(RC::CppUserModBase* mod) { delete mod; }
