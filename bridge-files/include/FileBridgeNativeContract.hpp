#pragma once

// UE4SSLuaFileBridge: internal contract between the native DLL and the
// embedded Lua layer (bridge-files/lua/files_api.lua).
//
// Status: draft for Gate G-spec. It implements the public API in
// bridge-files/docs/API.md. Nothing here is visible to mods: mods see only the
// `UE4SSLuaFileBridge` table that the Lua layer builds. Changes after G-spec go
// through K and reach F, L and D together.
//
// This header is portable (no Win32, no UE4SS includes) so the portable core,
// its tests and the Lua-layer fake-native tests can share one description.
//
// ---------------------------------------------------------------------------
// 1. Why the boundary looks like this (pinned UE4SS ABI 97b7e501)
// ---------------------------------------------------------------------------
// The bridge links only the LuaMadeSimple exports in
// bridge-events/abi/UE4SS-97b7e501.def (the file bridge uses the same pin):
// register_function, execute_string, registry().make_ref/get_function_ref,
// is_function, get_string, get_integer, set_nil/bool/integer/number/string,
// call_function. That fixes four rules:
//
//   R1  Scalars only. Natives can neither read nor build Lua tables. Every
//       argument is an integer or a string; every result is nil, boolean,
//       integer, number or string. Lists and records cross as one string in
//       the record format below (section 6).
//   R2  Fixed arity, Lua-checked types. There is no lua_gettop and no type
//       query. get_integer/get_string consume argument 1 and shift the rest,
//       so natives read arguments strictly left to right. get_string on a
//       non-string is undefined behaviour (lua_tostring returns NULL). The Lua
//       layer therefore passes exactly the listed arguments with exactly the
//       listed types, every time ("" for an absent string, 0 for an absent
//       integer). The natives are not safe to call by hand; see R5.
//   R3  Inbound strings stop at the first NUL. get_string is
//       lua_tostring + strlen, and it removes the value from the stack, so the
//       native copies the view into a std::string immediately, before any other
//       Lua API call. Paths and names never contain NUL (the Lua layer returns
//       `invalid` first). File content that may contain NUL crosses with the
//       data escape in section 5. Outbound strings are binary-safe
//       (set_string is lua_pushlstring).
//   R4  Natives never throw into Lua and never raise Lua errors. Every native
//       body is wrapped; an escaping C++ exception becomes
//       `nil, "io", "<operation>: internal error: <what>"`.
//   R5  Natives are registered as globals (register_function has no other
//       target). The Lua layer captures every `UE4SSLuaFileBridge_*` global
//       into a local table at load and sets each global to nil, so only the
//       helper layer can reach them. (LEB leaves its natives global; see open
//       point 1.)
//
// ---------------------------------------------------------------------------
// 2. Start-up per Lua environment (on_lua_start)
// ---------------------------------------------------------------------------
// UE4SS calls on_lua_start(mod_name, lua, main_lua, async_lua, hook_lua) once
// for every Lua mod; mod_name is the mod's folder name under Mods/
// (LuaMod::get_name()). For each call the native side:
//
//   1. Creates a Session: id (uint64, process-unique, never reused), the
//      mod name (UTF-16 and UTF-8), the per-session preset paths, the safe-set
//      policy, and empty stream/subscription/policy tables. The `mod` preset is
//      bound here: <Mods>/<mod_name>, where <Mods> is three levels above the
//      bridge's own versioned DLL (Mods/0_ModCore_UE4SSLuaFileBridge/dlls/
//      versions/*.dll), checked to lie inside the game root; if that check
//      fails it falls back to <game>/Dawnwalker/Binaries/Win64/ue4ss/Mods.
//      `moddata` and `temp` are bound from the same name. A mod name that is
//      not a single valid path segment leaves `mod`, `moddata` and `temp`
//      unbound: they still appear in Locations with exists=false, and any
//      path through them is `invalid`.
//   2. Registers every function in `native_functions` on `lua` under
//      `UE4SSLuaFileBridge_<Name>`.
//   3. Executes `<session_global> = <id>` and then the embedded
//      files_api.lua (embedded exactly like bridge-events: CMake splits it into
//      7000-byte raw-string chunks in a generated EmbeddedLuaAPI.hpp, delimiter
//      `)UE4SSLFB_LUA`). The chunk reads and clears the session global, keeps
//      the id as an upvalue, captures and clears the native globals (R5),
//      defines the global `UE4SSLuaFileBridge`, and RETURNS ONE FUNCTION: the
//      dispatcher (section 7).
//   4. Stores registry().make_ref() of that dispatcher in the Session.
//   5. Only after all of that succeeds, publishes the session and binds the
//      lua_State aliases of lua/main_lua/async_lua/hook_lua to it (same
//      pattern as bridge-events SessionAliasIndex). On any failure nothing is
//      published and the error is logged; that mod simply has no
//      UE4SSLuaFileBridge.
//
// Every native call that acts for a mod takes the session id as its first
// argument. A session id that is unknown, belongs to a stopped session, or
// does not match the calling lua_State's session is
// `nil, "invalid", "Lua session is not registered or is stopping"`.
//
// ---------------------------------------------------------------------------
// 3. Access environments as native policies
// ---------------------------------------------------------------------------
// A Lua environment table carries one policy id (int64 > 0). Policies are
// native, immutable and owned by the session that created them.
//
//   * SafePolicy(session) returns the session's safe-set policy (same id on
//     every call): game ro, user ro, mod rw, temp rw + delete.
//   * AddPath(session, parent, path, mode, extensions, flags) validates the
//     arguments, resolves `path`, and returns a NEW policy id whose grant list
//     is the parent's list with this grant added. A grant whose resolved path
//     equals (case-insensitively) an existing grant's path replaces it in the
//     new list. The parent is never modified. Lua turns a failure into a Lua
//     error ("invalid: ..." / "outside_root: ..."), as API.md requires.
//   * ReleasePolicy(session, policy) drops one id. The Lua layer calls it from
//     the environment table's __gc. Releasing does not affect policies derived
//     from it (each holds its own flattened grant list, sharing storage
//     internally), nor streams or subscriptions opened through it.
//   * All remaining policies are freed when the session stops.
//   * Limits: 256 grants per policy, 4096 live policies per session; beyond
//     either, AddPath fails with `invalid`.
//
// Grant fields (native representation, `Grant` below):
//   path        resolved real path (UTF-16, final-path form), stored with the
//               location-relative or absolute text it was given for messages
//   mode        stat | ro | wo | rw | ao | inherit
//   extensions  inherit | list (possibly empty = covers no files); lowercase,
//               no dot, compared case-insensitively
//   delete      bool, never inherited (default false)
//   recursive   bool (default true); false covers the path and its direct
//               children only
//
// The check (one per path an operation touches; `check_steps` below):
//   1. Normalize (API.md "Paths"). Syntax faults: `invalid`; `..` above the
//      location or root, device/UNC/reserved names: `outside_root`.
//   2. Resolve the real path: open the deepest existing ancestor (following
//      links), GetFinalPathNameByHandleW, append the non-existing rest. For
//      Remove, RemoveTree, Move (both ends) and the .bak/temporary siblings
//      the LAST segment is not followed (the parent is resolved, the leaf
//      appended): those operations act on the link itself. Everything else
//      follows the leaf too.
//   3. Containment: the real path must equal or lie under the final path of
//      `game` or `user`; otherwise `outside_root`.
//   4. Coverage: grant paths are resolved the same way at check time (memoized
//      for the duration of one call). A grant covers the target when the target
//      equals the grant path or lies under it (only direct children when
//      recursive=false) AND, if the target is a file (or is being created as
//      one), its extension is in the grant's effective extensions. Folders are
//      always covered. Non-covering grants are skipped.
//   5. Winner: the covering grant with the most path segments. Paths are
//      unique within a policy, so there are no ties.
//   6. Effective mode/extensions of a grant with `inherit`: taken from the most
//      specific OTHER grant that covers the grant's own path (as a folder),
//      recursively. The safe set's root grants have explicit values, so this
//      always terminates.
//   7. The operation's required permissions (`required_permissions`) must be a
//      subset of `permissions_for(mode, delete)`; otherwise `denied`.
//   8. Fixed rules regardless of grants, `denied`: the delete floor
//      (any location's own path, any drive root, the user profile root) for
//      DeleteEntry; `moddata/.owner` for anything but stat/read.
//   9. Fixed rules, `invalid`: under `savegames`, non-atomic write and
//      truncating Open.
// The check is not atomic with the operation (a junction could be swapped in
// between). The sandbox prevents mistakes; it is not a security boundary.
//
// ---------------------------------------------------------------------------
// 4. Errors
// ---------------------------------------------------------------------------
// Every native returns its success values first. On failure it returns
// exactly three values: nil, code, message.
//   code     one of `error_code_names` (stable, for program logic)
//   message  English, "<Operation> <path>: <reason>", plus "(win32 <n>)" for
//            an operating-system failure. Wording is not part of the contract.
// Success values are never nil, so `if r == nil` is the failure test. The Lua
// layer passes `nil, code, message` through unchanged (AddPath: raises).
// Win32 mapping is in `win32_error_mapping`.
//
// ---------------------------------------------------------------------------
// 5. Data escape for inbound content (R3)
// ---------------------------------------------------------------------------
// Content written by WriteText, Append and stream Write crosses as
// (byte_length:int, escaped:string). The Lua layer escapes only bytes 0 and 1:
//     \0 -> \1\2      \1 -> \1\1
//     escaped = text:find("[%z\1]") and text:gsub("[%z\1]", ESC) or text
// The native decodes with `decode_escaped` and fails with `invalid` if the
// escape is malformed or the decoded length differs from byte_length. Content
// without NUL or \1 bytes (all text) crosses unchanged and is not copied
// twice. Outbound content (ReadText/ReadBytes, Tail) is returned raw.
//
// ---------------------------------------------------------------------------
// 6. Record format for structured results (R1)
// ---------------------------------------------------------------------------
// One string: records separated by '\n', fields by '\t'. Windows forbids
// control characters in file names, and paths/names are checked UTF-8, so
// neither separator can occur inside a field. Booleans are "1"/"0"; numbers
// use Lua-readable decimal (integers as integers, times with up to 7 fraction
// digits). Field order is fixed by the `*_fields` arrays below. An empty
// result is the empty string.
//
// ---------------------------------------------------------------------------
// 7. Threads, delivery and lifetime
// ---------------------------------------------------------------------------
// Synchronous natives run on whatever thread calls them (game thread,
// LoopAsync thread, hooks). Session, policy, stream and subscription tables
// are guarded by one mutex per session; file I/O runs outside it. A single
// stream is serialized by its own mutex. Natives never require the game
// thread and never call into Unreal.
//
// Tail delivery uses the shared dispatch code that E extracts to
// shared/dispatch/ (QueueDispatchSchedule, DispatchBudget, DispatchBacklog,
// QueueBuffers): in on_update, when the schedule is due, the bridge polls each
// active subscription's file (size via the held handle; identity by path at
// most every `tail_identity_check_ms`), reads at most what the budget allows,
// splits lines, and calls the session's dispatcher on that session's `lua`
// state:
//     dispatcher(kind, token, text, offset, flags)
// `kind` is a DispatchKind; `token` is the integer the Lua layer chose in
// TailOpen; `text` is the line/chunk (or the error message); `offset` is the
// byte offset of `text` in the file; `flags` has DeliveryFlags bits. The
// dispatcher returns nothing. If it raises, the subscription is closed, the
// error is written to UE4SS.log, and no further kinds are delivered for it.
// Unread data stays in the file (the file is the buffer), so nothing is
// queued beyond the current pass: only the held partial line (<= 1 MiB) is
// buffered natively. Budget defaults: 256 deliveries and 2000 us per pass,
// overridable with UE4SSLFB_MAX_EVENTS_PER_PASS / UE4SSLFB_MAX_DISPATCH_US.
//
// on_lua_stop (and a reload) for a session, in order: mark the session
// inactive (natives then fail with `invalid`); close every subscription (no
// further dispatch); flush and close every stream; free every policy; unbind
// the lua_State aliases. The Session record itself stays until bridge
// destruction (ids are never reused), as in bridge-events. The Lua layer may
// also clear its own callback tables from an optional
// `__UE4SSLuaFileBridge_Stop` global, called on the game thread only.
//
// Bridge start (before the first on_lua_start): resolve game/user roots and
// the shared presets, then empty `temp` in every <user>/Saved/ModData/*
// folder that has an `.owner` marker.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace UE4SSLuaFileBridge::NativeContract
{
// --- Identity -------------------------------------------------------------

inline constexpr int64_t api_version = 1;
inline constexpr std::string_view lua_global = "UE4SSLuaFileBridge";
inline constexpr std::string_view native_prefix = "UE4SSLuaFileBridge_";
inline constexpr std::string_view session_global = "__UE4SSLuaFileBridge_SessionId";
inline constexpr std::string_view stop_hook_global = "__UE4SSLuaFileBridge_Stop";
inline constexpr std::string_view target_ue4ss_commit = "97b7e501";
inline constexpr std::string_view embedded_lua_delimiter = ")UE4SSLFB_LUA";

// --- Limits ---------------------------------------------------------------

inline constexpr int64_t max_read_bytes = 64LL * 1024 * 1024;
inline constexpr int64_t max_grants_per_policy = 256;
inline constexpr int64_t max_policies_per_session = 4096;
inline constexpr int64_t max_streams_per_session = 64;
inline constexpr int64_t max_subscriptions_per_session = 64;
inline constexpr int64_t stream_buffer_bytes = 64 * 1024;
inline constexpr int64_t tail_max_line_bytes = 1024 * 1024;
inline constexpr int64_t tail_identity_check_ms = 250;
inline constexpr uint32_t default_max_deliveries_per_pass = 256;
inline constexpr uint32_t default_max_dispatch_us = 2000;

// --- Error codes ----------------------------------------------------------

enum class ErrorCode : uint8_t
{
    not_found,
    denied,
    exists,
    outside_root,
    read_only,
    busy,
    io,
    invalid,
};

inline constexpr std::array<std::string_view, 8> error_code_names{
    "not_found", "denied", "exists", "outside_root", "read_only", "busy", "io", "invalid"};

constexpr std::string_view to_string(ErrorCode code)
{
    return error_code_names[static_cast<std::size_t>(code)];
}

// Win32 error -> code. Unlisted errors are `io`. ERROR_ACCESS_DENIED on a file
// whose read-only attribute is set is reported as read_only (the backend
// checks the attribute before choosing).
struct Win32ErrorMapping
{
    uint32_t win32;
    ErrorCode code;
};

inline constexpr std::array<Win32ErrorMapping, 11> win32_error_mapping{{
    {2, ErrorCode::not_found},    // ERROR_FILE_NOT_FOUND
    {3, ErrorCode::not_found},    // ERROR_PATH_NOT_FOUND
    {5, ErrorCode::denied},       // ERROR_ACCESS_DENIED
    {19, ErrorCode::read_only},   // ERROR_WRITE_PROTECT
    {32, ErrorCode::busy},        // ERROR_SHARING_VIOLATION
    {33, ErrorCode::busy},        // ERROR_LOCK_VIOLATION
    {80, ErrorCode::exists},      // ERROR_FILE_EXISTS
    {145, ErrorCode::invalid},    // ERROR_DIR_NOT_EMPTY
    {183, ErrorCode::exists},     // ERROR_ALREADY_EXISTS
    {267, ErrorCode::invalid},    // ERROR_DIRECTORY (a file where a folder was expected)
    {1224, ErrorCode::busy},      // ERROR_USER_MAPPED_FILE
}};

// --- Modes and permissions ------------------------------------------------

enum class Mode : uint8_t
{
    inherit,
    stat,
    ro,
    wo,
    rw,
    ao,
};

// Wire form of AddPath's `mode` argument: "" means inherit.
constexpr std::optional<Mode> parse_mode(std::string_view text)
{
    if (text.empty()) return Mode::inherit;
    if (text == "stat") return Mode::stat;
    if (text == "ro") return Mode::ro;
    if (text == "wo") return Mode::wo;
    if (text == "rw") return Mode::rw;
    if (text == "ao") return Mode::ao;
    return std::nullopt;
}

namespace Permission
{
inline constexpr uint32_t stat = 1u << 0;     // Exists, Stat, List
inline constexpr uint32_t read = 1u << 1;     // ReadText, ReadBytes, Tail, Copy source
inline constexpr uint32_t make_dir = 1u << 2; // MakeDir (each folder it creates)
inline constexpr uint32_t append = 1u << 3;   // Append, Open append=true
inline constexpr uint32_t write = 1u << 4;    // WriteText, Open append=false, Copy/Move destination, Move source
inline constexpr uint32_t remove = 1u << 5;   // Remove, RemoveTree (every entry), Move source; needs `delete`
}

// API.md "What each mode allows": every mode includes stat.
constexpr uint32_t permissions_for(Mode mode, bool delete_allowed)
{
    using namespace Permission;
    uint32_t bits = 0;
    switch (mode)
    {
    case Mode::stat: bits = stat; break;
    case Mode::ro: bits = stat | read; break;
    case Mode::wo: bits = stat | make_dir | append | write; break;
    case Mode::rw: bits = stat | read | make_dir | append | write; break;
    case Mode::ao: bits = stat | make_dir | append; break;
    case Mode::inherit: bits = 0; break; // never effective; resolved first
    }
    // `delete` is meaningful only together with write (AddPath rejects it with
    // explicit ro/stat/ao; an inherited mode without write ignores it).
    if (delete_allowed && (bits & write) != 0) bits |= remove;
    return bits;
}

// Required permissions per checked path role.
enum class Access : uint8_t
{
    stat,          // Exists, Stat, List
    read,          // ReadText, ReadBytes, Tail, Copy source
    make_dir,      // each folder MakeDir would create
    append,        // Append, Open append=true
    write,         // WriteText, Open append=false, Copy/Move destination
    delete_entry,  // Remove, each RemoveTree entry
    move_source,   // Move source: write + remove
};

constexpr uint32_t required_permissions(Access access)
{
    using namespace Permission;
    switch (access)
    {
    case Access::stat: return stat;
    case Access::read: return read;
    case Access::make_dir: return make_dir;
    case Access::append: return append;
    case Access::write: return write;
    case Access::delete_entry: return write | remove;
    case Access::move_source: return write | remove;
    }
    return ~0u;
}

// --- Grants (native representation; the backend stores paths as UTF-16) ---

struct Grant
{
    std::u16string real_path;        // resolved final path, set at AddPath and re-resolved at check
    std::string given_path;          // what the mod passed, for messages
    Mode mode{Mode::inherit};
    bool extensions_inherited{true}; // false: `extensions` is authoritative (may be empty)
    std::string extensions;          // ';'-joined lowercase, no dots; "" with !inherited covers no files
    bool delete_allowed{false};
    bool recursive{true};
};

// AddPath `flags` argument.
namespace AddPathFlag
{
inline constexpr int64_t delete_allowed = 1 << 0;
inline constexpr int64_t non_recursive = 1 << 1;  // recursive=false
inline constexpr int64_t extensions_set = 1 << 2; // `extensions` given (else inherit)
inline constexpr int64_t known = delete_allowed | non_recursive | extensions_set;
}

inline constexpr char extension_separator = ';';

// --- Other flag arguments -------------------------------------------------

namespace WriteFlag
{
inline constexpr int64_t non_atomic = 1 << 0; // WriteText atomic=false
inline constexpr int64_t known = non_atomic;
}

namespace CopyMoveFlag
{
inline constexpr int64_t overwrite = 1 << 0;
inline constexpr int64_t known = overwrite;
}

namespace OpenFlag
{
inline constexpr int64_t truncate = 1 << 0;     // append=false
inline constexpr int64_t no_create = 1 << 1;    // create=false
inline constexpr int64_t manual_flush = 1 << 2; // flush="manual"
inline constexpr int64_t known = truncate | no_create | manual_flush;
}

namespace TailFlag
{
inline constexpr int64_t chunks = 1 << 0;     // chunks=true
inline constexpr int64_t from_start = 1 << 1; // from="start"
inline constexpr int64_t known = chunks | from_start;
}

// Unknown bits in any flags argument are `invalid`.

// --- Tail dispatch --------------------------------------------------------

enum class DispatchKind : int64_t
{
    data = 1,   // text = line (no terminator) or chunk; offset = its byte offset
    closed = 2, // the native side closed the subscription (stop, file error);
                // text = "<code>\t<message>" or "" ; the Lua layer drops the callback
};

namespace DeliveryFlag
{
inline constexpr int64_t reset = 1 << 0;   // first delivery after truncation/rotation
inline constexpr int64_t partial = 1 << 1; // a piece of a line longer than tail_max_line_bytes
}

// --- Record formats -------------------------------------------------------

inline constexpr char record_separator = '\n';
inline constexpr char field_separator = '\t';

// Locations(session): one record per location, in this order of records:
// game, user, mod, moddata, temp, savegames.
inline constexpr std::array<std::string_view, 4> location_fields{"name", "path", "root", "exists"};
inline constexpr std::array<std::string_view, 6> location_names{
    "game", "user", "mod", "moddata", "temp", "savegames"};

// List(session, policy, path): one record per entry, sorted by name (bytes).
// type is "file", "directory" or "other".
inline constexpr std::array<std::string_view, 5> list_fields{"name", "type", "size", "modified", "link"};

// --- Data escape (section 5) ----------------------------------------------

inline constexpr char escape_byte = '\1';

// Decodes `escaped` into `out`. Returns false on a malformed escape or a
// length mismatch (the caller reports `invalid`).
inline bool decode_escaped(std::string_view escaped, int64_t byte_length, std::string& out)
{
    if (byte_length < 0) return false;
    out.clear();
    out.reserve(static_cast<std::size_t>(byte_length));
    for (std::size_t i = 0; i < escaped.size(); ++i)
    {
        const char c = escaped[i];
        if (c != escape_byte)
        {
            out.push_back(c);
            continue;
        }
        if (++i == escaped.size()) return false;
        if (escaped[i] == '\1') out.push_back('\1');
        else if (escaped[i] == '\2') out.push_back('\0');
        else return false;
    }
    return static_cast<int64_t>(out.size()) == byte_length;
}

// --- Native function table ------------------------------------------------
//
// Each entry is registered as native_prefix + name. `arguments` and `results`
// use: s=session id (int), p=policy id (int), path=string (UTF-8),
// int, str, num, bool, data=(byte_length:int, escaped:str). `results` is the
// success shape; failure is always (nil, code, message). `thread`: any = any
// Lua thread; any thread is also fine for the stream/subscription calls.

struct NativeFunction
{
    std::string_view name;
    std::string_view arguments;
    std::string_view results;
    std::string_view backs; // the public API.md function(s) it serves
};

inline constexpr std::array<NativeFunction, 29> native_functions{{
    // Session, version, capabilities, locations
    {"GetVersion", "", "version:str", "GetVersion"},
    {"GetCapabilities", "",
     "api:int, environments:bool, append_only:bool, streams:bool, tail:bool, "
     "max_read_bytes:int, target_ue4ss_commit:str",
     "GetCapabilities (locations list and deferred=false keys are added by Lua)"},
    {"SessionInfo", "s", "mod_name:str, mod_bound:bool", "diagnostics, Lua-layer messages"},
    {"Locations", "s", "records:str (location_fields)", "Roots"},

    // Paths (pure: no grant, no file-system access except reading cached roots)
    {"Normalize", "s, path", "absolute:str", "Normalize, Path (Lua joins, then Normalize)"},
    {"LocationOf", "s, path", "location:str, relative:str", "messages; which preset rules apply"},

    // Policies
    {"SafePolicy", "s", "policy:int", "UE4SSLuaFileBridge()"},
    {"AddPath", "s, p, path, mode:str, extensions:str, flags:int", "policy:int", "env.AddPath"},
    {"ReleasePolicy", "s, p", "true", "environment __gc"},
    {"Check", "s, p, path, access:int (Access)", "true",
     "Lua-side preflight and tests; operations check by themselves"},

    // Queries
    {"Exists", "s, p, path", "exists:bool", "env.Exists"},
    {"Stat", "s, p, path",
     "type:str, size:int, modified:num, created:num, readonly:bool, link:bool", "env.Stat"},
    {"List", "s, p, path", "records:str (list_fields)", "env.List"},

    // Reading
    {"ReadText", "s, p, path", "content:str (BOM removed)", "env.ReadText"},
    {"ReadBytes", "s, p, path, offset:int, length:int (-1 = to end)", "content:str", "env.ReadBytes"},

    // Writing
    {"MakeDir", "s, p, path", "true", "env.MakeDir"},
    {"WriteText", "s, p, path, data, flags:int (WriteFlag)", "true", "env.WriteText"},
    {"Append", "s, p, path, data", "true", "env.Append"},

    // Removing, copying, moving
    {"Remove", "s, p, path", "true", "env.Remove"},
    {"RemoveTree", "s, p, path", "true", "env.RemoveTree"},
    {"Copy", "s, p, from:path, to:path, flags:int (CopyMoveFlag)", "true", "env.Copy"},
    {"Move", "s, p, from:path, to:path, flags:int (CopyMoveFlag)", "true", "env.Move"},

    // Streams (checked once, at open)
    {"StreamOpen", "s, p, path, flags:int (OpenFlag)", "stream:int, normalized:str", "env.Open"},
    {"StreamWrite", "s, stream:int, data", "true", "stream:Write (Lua concatenates the arguments)"},
    {"StreamFlush", "s, stream:int", "true", "stream:Flush"},
    {"StreamClose", "s, stream:int", "true (also for an already closed id of this session)",
     "stream:Close, stream __gc"},

    // Tail (checked once, at subscribe)
    {"TailOpen", "s, p, path, token:int (>0, chosen by Lua), flags:int (TailFlag)",
     "subscription:int, normalized:str", "env.Tail"},
    {"TailClose", "s, subscription:int", "true (idempotent)", "sub:Close"},

    // Delivery
    {"GetDispatchStats", "", "subscriptions:int, delivered:int, budget_exhausted_passes:int",
     "diagnostics only"},
}};

// --- Check pipeline (section 3), for tests and reviewers ------------------

inline constexpr std::array<std::string_view, 9> check_steps{
    "normalize (invalid / outside_root)",
    "resolve real path (leaf unfollowed for Remove, RemoveTree, Move)",
    "containment in game or user (outside_root)",
    "coverage: path prefix, recursive, effective extensions (files only)",
    "winner: most path segments",
    "inherit mode/extensions from the most specific grant covering the grant's path",
    "required_permissions subset of permissions_for (denied)",
    "fixed denials: delete floor, moddata/.owner (denied)",
    "fixed savegames rules: no non-atomic write, no truncating Open (invalid)",
};

} // namespace UE4SSLuaFileBridge::NativeContract

// ---------------------------------------------------------------------------
// Open points for review (F). Numbers refer to API.md's list where relevant.
// ---------------------------------------------------------------------------
// F1  Natives hidden from mods (R5): the Lua layer nils the
//     UE4SSLuaFileBridge_* globals after capturing them, because a hand call
//     with a wrong type crashes the game (R2). LEB keeps its natives global;
//     this deliberately differs.
// F2  Content crosses with a NUL/\1 escape plus an explicit byte length
//     (section 5) rather than LEB's packed-integer words, which cap at 512
//     bytes and cost one stack slot per 8 bytes.
// F3  Structured results (Stat is positional; List and Locations are record
//     strings) because the pinned ABI cannot build tables.
// F4  Disagree with API.md "Links": D follows links for every check except
//     RemoveTree. F proposes the leaf is NOT followed for Remove and Move too:
//     DeleteFileW and MoveFileExW act on the link, so checking the target's
//     grant would check the wrong path. Stat still follows (and reports link).
// F5  API.md 7/6: inherited mode and extensions are taken from the most
//     specific grant covering the GRANT's path, not the target's; results are
//     the same in every example, but this makes a grant's effective values
//     independent of the file being checked. Please confirm.
// F6  `delete` with an inherited mode that lacks write is silently inert
//     (permissions_for). API.md 9 only rejects explicit ro/stat/ao.
// F7  Tail reads the file in on_update (as API.md "Delivery" says) instead of
//     a watcher thread: no cross-thread queue is needed, so of shared/dispatch
//     F uses QueueDispatchSchedule and DispatchBudget (and DispatchBacklog for
//     the per-pass delivery list), not QueueBuffers. Rotation is detected by
//     re-opening the path at most every 250 ms. E's extraction must keep those
//     headers usable without the Enhanced Input types.
// F8  Callback error closes the subscription (API.md 24); F adds a `closed`
//     dispatch kind so the native side can also close a subscription (file
//     error, session stop) and the Lua layer drops its callback.
// F9  Limits not in API.md: 256 grants per policy, 4096 live policies and 64
//     subscriptions per Lua environment (`invalid` beyond). API.md says the
//     64-stream limit is `io`; F would prefer `busy` there but implements `io`.
// F10 `mod` is derived from the bridge's own DLL location (three folders up),
//     with the plan's fixed path as fallback, so a UE4SS layout with Mods/
//     beside the exe still works. A mod name that is not one valid segment
//     leaves mod/moddata/temp unbound (`invalid`).
// F11 API.md 1: F favours adding `mods` now (read-only via the game root
//     anyway; it is just a named location, not a grant). File sockets between
//     mods are a 0.1.0 goal, and the long form is error-prone. Delete floor
//     would include it.
// F12 Delete floor: API.md lists locations themselves. F proposes RemoveTree
//     also refuses an ANCESTOR of a location (e.g. user/Saved holds savegames
//     and ModData) unless the grant is on that ancestor explicitly; today only
//     the safe set's ro on roots prevents it.
// F13 Check() exists for tests and the Lua layer's preflight; it is not needed
//     by the public API. Drop it if L doesn't need it.
