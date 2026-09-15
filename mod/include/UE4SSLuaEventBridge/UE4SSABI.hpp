#pragma once

#ifdef _WIN32

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#define UE4SS_IMPORT __declspec(dllimport)

namespace RC
{
using StringType = std::wstring;
using StringViewType = std::wstring_view;

namespace GUI
{
class GUITab;
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
    UE4SS_IMPORT void register_function(const std::string& name, const LuaFunction& function) const;
    UE4SS_IMPORT void execute_string(std::string_view source) const;
    UE4SS_IMPORT bool is_function(int32_t index = 1) const;
    UE4SS_IMPORT std::string_view get_string(int32_t index = 1) const;
    UE4SS_IMPORT int64_t get_integer(int32_t index = 1) const;
    UE4SS_IMPORT void set_nil() const;
    UE4SS_IMPORT void set_bool(bool value) const;
    UE4SS_IMPORT void set_float(float value) const;
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
class UObject;
class UFunction;
class UClass;

class UObjectBase
{
};

class UObject : public UObjectBase
{
public:
    UE4SS_IMPORT std::wstring GetPathName(UObject* stop_outer = nullptr) const;
    UE4SS_IMPORT void* GetValuePtrByPropertyNameInChain(const wchar_t* property_name);
};

class UStruct : public UObject
{
public:
    UE4SS_IMPORT int32_t& GetPropertiesSize();
};

class UClass : public UStruct
{
};

struct FWeakObjectPtr
{
    int32_t object_index{-1};
    int32_t object_serial_number{0};

    UE4SS_IMPORT FWeakObjectPtr();
    UE4SS_IMPORT explicit FWeakObjectPtr(const UObject* object);
    UE4SS_IMPORT UObject* Get() const;
};

class FMemory
{
public:
    UE4SS_IMPORT static void* Malloc(std::size_t size, uint32_t alignment = 0);
    UE4SS_IMPORT static void* Realloc(void* allocation, std::size_t size, uint32_t alignment = 0);
    UE4SS_IMPORT static void Free(void* allocation);
};

class FUObjectCreateListener
{
public:
    UE4SS_IMPORT FUObjectCreateListener();
    UE4SS_IMPORT virtual ~FUObjectCreateListener();
    virtual void NotifyUObjectCreated(const UObjectBase* object, int32_t index) = 0;
    virtual void OnUObjectArrayShutdown() = 0;
};

class FUObjectDeleteListener
{
public:
    UE4SS_IMPORT FUObjectDeleteListener();
    UE4SS_IMPORT virtual ~FUObjectDeleteListener();
    virtual void NotifyUObjectDeleted(const UObjectBase* object, int32_t index) = 0;
    virtual void OnUObjectArrayShutdown() = 0;
};

class FUObjectArray
{
public:
    UE4SS_IMPORT static void AddUObjectCreateListener(FUObjectCreateListener* listener);
    UE4SS_IMPORT static void AddUObjectDeleteListener(FUObjectDeleteListener* listener);
    UE4SS_IMPORT static void RemoveUObjectCreateListener(FUObjectCreateListener* listener);
    UE4SS_IMPORT static void RemoveUObjectDeleteListener(FUObjectDeleteListener* listener);
};

namespace UObjectGlobals
{
UE4SS_IMPORT void FindAllOf(const wchar_t* class_name, std::vector<UObject*>& objects);
}

UE4SS_IMPORT bool IsInGameThread();

namespace Hook
{
using ProcessEventCallback = std::function<void(UObject*, UFunction*, void*)>;
UE4SS_IMPORT void RegisterProcessEventPostCallback(ProcessEventCallback callback);
}
}
}

#endif
