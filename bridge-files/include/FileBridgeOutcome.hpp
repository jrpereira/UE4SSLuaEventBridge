#pragma once

// A value or a (code, message) failure, as natives report them to Lua.

#include <FileBridgeNativeContract.hpp>

#include <cstdint>
#include <string>
#include <utility>
#include <variant>

namespace UE4SSLuaFileBridge::Core
{
using NativeContract::ErrorCode;

struct Failure
{
    ErrorCode code{ErrorCode::io};
    std::string message;
};

struct Done
{
};

template<class T>
class Outcome
{
public:
    Outcome(T value) : state_(std::move(value)) {}
    Outcome(Failure failure) : state_(std::move(failure)) {}

    [[nodiscard]] bool ok() const { return state_.index() == 0; }
    explicit operator bool() const { return ok(); }
    T& value() { return std::get<0>(state_); }
    const T& value() const { return std::get<0>(state_); }
    const Failure& failure() const { return std::get<1>(state_); }

private:
    std::variant<T, Failure> state_;
};

using Status = Outcome<Done>;

inline Failure fail(ErrorCode code, std::string message)
{
    return Failure{code, std::move(message)};
}

// Win32 error code -> contract code (FileBridgeNativeContract.hpp
// win32_error_mapping); anything unlisted is `io`.
constexpr ErrorCode map_win32_error(uint32_t win32)
{
    for (const auto& entry : NativeContract::win32_error_mapping)
    {
        if (entry.win32 == win32) return entry.code;
    }
    return ErrorCode::io;
}
}
