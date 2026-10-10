#pragma once

// The part of the pinned UE4SS 97b7e501 ABI the file bridge uses. Imports
// come from bridge-files/abi/UE4SS-97b7e501.def: the event bridge's set plus
// Lua::get_stack_size, Lua::is_string and Lua::is_integer (contract R2).
// CppUserModBase must match UE4SS's virtual table exactly; it is copied from
// bridge-events/include/UE4SSABI.hpp.

#ifdef _WIN32

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#ifndef UE4SS_IMPORT
#define UE4SS_IMPORT __declspec(dllimport)
#endif

struct lua_State;

namespace RC
{
using StringType = std::wstring;
using StringViewType = std::wstring_view;

namespace GUI
{
class GUITab;
}

namespace Output
{
UE4SS_IMPORT void send(StringViewType content);
}

namespace LuaMadeSimple
{
class Lua
{
public:
    using LuaFunction = int (*)(const Lua&);

    class Registry
    {
    public:
        UE4SS_IMPORT int32_t make_ref() const;
        UE4SS_IMPORT void get_function_ref(int32_t registry_index) const;
    };

    UE4SS_IMPORT const Registry& registry() const;
    UE4SS_IMPORT lua_State* get_lua_state() const;
    UE4SS_IMPORT void register_function(const std::string& name, const LuaFunction& function) const;
    UE4SS_IMPORT void execute_string(std::string_view source) const;
    UE4SS_IMPORT int32_t get_stack_size() const;
    UE4SS_IMPORT bool is_string(int32_t index = 1) const;
    UE4SS_IMPORT bool is_integer(int32_t index = 1) const;
    UE4SS_IMPORT bool is_function(int32_t index = 1) const;
    UE4SS_IMPORT std::string_view get_string(int32_t index = 1) const;
    UE4SS_IMPORT int64_t get_integer(int32_t index = 1) const;
    UE4SS_IMPORT void set_nil() const;
    UE4SS_IMPORT void set_bool(bool value) const;
    UE4SS_IMPORT void set_integer(int64_t value) const;
    UE4SS_IMPORT void set_number(double value) const;
    UE4SS_IMPORT void set_string(std::string_view value) const;
    UE4SS_IMPORT void call_function(int32_t parameter_count, int32_t return_count) const;
};
}

class CppUserModBase
{
protected:
    std::vector<std::shared_ptr<GUI::GUITab>> GUITabs{};

public:
    StringType ModName{};
    StringType ModVersion{};
    StringType ModDescription{};
    StringType ModAuthors{};
    StringType ModIntendedSDKVersion{};

    UE4SS_IMPORT CppUserModBase();
    UE4SS_IMPORT virtual ~CppUserModBase();

    virtual void on_update() {}
    virtual void on_unreal_init() {}
    virtual void on_ui_init() {}
    virtual void on_program_start() {}
    virtual void on_lua_start(
        StringViewType,
        LuaMadeSimple::Lua&,
        LuaMadeSimple::Lua&,
        LuaMadeSimple::Lua&,
        std::vector<LuaMadeSimple::Lua*>&) {}
    virtual void on_lua_start(
        LuaMadeSimple::Lua&,
        LuaMadeSimple::Lua&,
        LuaMadeSimple::Lua&,
        std::vector<LuaMadeSimple::Lua*>&) {}
    virtual void on_lua_stop(
        StringViewType,
        LuaMadeSimple::Lua&,
        LuaMadeSimple::Lua&,
        LuaMadeSimple::Lua&,
        std::vector<LuaMadeSimple::Lua*>&) {}
    virtual void on_lua_stop(
        LuaMadeSimple::Lua&,
        LuaMadeSimple::Lua&,
        LuaMadeSimple::Lua&,
        std::vector<LuaMadeSimple::Lua*>&) {}
    virtual void on_dll_load(StringViewType) {}
    virtual void render_tab() {}
    virtual void on_lua_start(
        StringViewType,
        LuaMadeSimple::Lua&,
        LuaMadeSimple::Lua&,
        LuaMadeSimple::Lua&,
        LuaMadeSimple::Lua*) {}
    virtual void on_lua_start(
        LuaMadeSimple::Lua&,
        LuaMadeSimple::Lua&,
        LuaMadeSimple::Lua&,
        LuaMadeSimple::Lua*) {}
    virtual void on_lua_stop(
        StringViewType,
        LuaMadeSimple::Lua&,
        LuaMadeSimple::Lua&,
        LuaMadeSimple::Lua&,
        LuaMadeSimple::Lua*) {}
    virtual void on_lua_stop(
        LuaMadeSimple::Lua&,
        LuaMadeSimple::Lua&,
        LuaMadeSimple::Lua&,
        LuaMadeSimple::Lua*) {}
    virtual void on_cpp_mods_loaded() {}
};

namespace Unreal
{
UE4SS_IMPORT bool IsInGameThread();
}
}

#endif
