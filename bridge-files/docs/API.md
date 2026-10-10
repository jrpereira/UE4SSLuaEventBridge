# UE4SSLuaFileBridge Lua API (0.1.0)

Status: **draft for review (Gate G-spec).** This file is the contract the native
product and the Lua helpers implement. Once K approves it, it is frozen, and any
change goes through K to the native, Lua and documentation tracks together.
Points that the plan left open carry a concrete proposal in the text and are
listed again under [Open points for review](#open-points-for-review).

- [Conventions](#conventions)
- [Entry point and version](#entry-point-and-version)
- [Locations: roots and presets](#locations-roots-and-presets)
- [Paths](#paths)
- [Access environments](#access-environments)
- [Operations](#operations)
- [Streams: `Open`](#streams-open)
- [File sockets: `Tail`](#file-sockets-tail)
- [Error codes](#error-codes)
- [Deferred and out of scope](#deferred-and-out-of-scope)
- [Internal native contract](#internal-native-contract)
- [Open points for review](#open-points-for-review)

## Conventions

- **Names.** Functions are PascalCase, like UE4SSLuaEventBridge. Functions of an
  environment are called with a dot (`env.ReadText(path)`), because each one is
  bound to its environment. Methods of a stream or a subscription are called
  with a colon (`stream:Write(text)`, `sub:Close()`).
- **Results.** Every operation returns its value on success. On failure it
  returns `nil, code, message`:
  - `code` is one of the [error codes](#error-codes). It is stable and meant for
    program logic.
  - `message` is English text for logs. It names the operation and the path. Its
    wording isn't part of the contract.
  - An operation with no natural value returns `true`.
- **Argument errors** in operations (wrong type, unknown option key, bad option
  value) return `nil, "invalid", message`. They are never ignored. `AddPath` is
  the exception: it raises a Lua error (see [AddPath](#envaddpathpath-options)).
- **Options** are always an optional last table argument. An unknown key or a
  value of the wrong type or range is `invalid`.
- **Strings** are Lua byte strings. Paths are UTF-8. File contents are bytes,
  passed through unchanged unless an operation says otherwise.
- **Threads.** File operations are synchronous and run on the calling Lua thread.
  They may be called from any Lua thread, including the game thread, but large
  reads and writes there stall the frame. Callbacks (`Tail`) run on the
  subscriber's Lua thread from the UE4SS update callback, not on the game thread;
  schedule Unreal work with `ExecuteInGameThread`.
- **Isolation.** Every Lua environment (each mod's Lua state) receives its own
  `UE4SSLuaFileBridge`. Environments, streams and subscriptions belong to the Lua
  environment that created them and are closed when it stops or reloads.

## Entry point and version

```lua
local env = UE4SSLuaFileBridge()           -- the safe environment
local version = UE4SSLuaFileBridge.GetVersion()        -- "0.1.0"
local caps = UE4SSLuaFileBridge.GetCapabilities()
local api = UE4SSLuaFileBridge.API_VERSION             -- 1
```

`UE4SSLuaFileBridge` is a table with a `__call` metamethod. Calling it returns
the [safe environment](#the-safe-set) for the calling mod. Each call returns an
equivalent environment; whether it is the same table is unspecified.

`GetVersion()` returns the product version string. `API_VERSION` is the API
contract number, `1` for 0.1.0. Every environment also has `env.GetVersion` and
`env.GetCapabilities`, identical to the global ones.

### `GetCapabilities()`

Returns a new table each call:

```lua
{
    api = 1,
    environments = true,     -- AddPath and access checks
    append_only = true,      -- mode "ao"
    streams = true,          -- Open
    tail = true,             -- Tail
    locations = { "game", "user", "mod", "moddata", "temp", "savegames" },
    lock = false,            -- deferred
    hash = false,            -- deferred
    temp_file = false,       -- deferred
    max_read_bytes = 67108864,
    target_ue4ss_commit = "97b7e501",
}
```

Gate optional features on these fields, not on the version number. Later
releases add keys; they don't change the meaning of existing ones.

## Locations: roots and presets

A **location** is a named folder. Paths start from a location (see
[Paths](#paths)). There are two kinds.

**Roots** bound every path the bridge accepts. They are not presets, but their
names can start a path.

| Location | Resolves to |
|---|---|
| `game` | The game install folder, which holds `Dawnwalker/` and `Engine/` (for Steam, `…/steamapps/common/The Blood of Dawnwalker`). Worked out from the running executable, `<game>/Dawnwalker/Binaries/Win64/Dawnwalker.exe` |
| `user` | The game's user data folder, `%LOCALAPPDATA%\Dawnwalker`, from `SHGetKnownFolderPath(FOLDERID_LocalAppData)` plus the project name (the folder that holds `Binaries/Win64`). It holds saves, user config and logs |

**Presets** are worked out once when the bridge starts (`mod` when the Lua
environment starts). 0.1.0 ships exactly these:

| Preset | Resolves to | Rules |
|---|---|---|
| `mod` | `<game>/Dawnwalker/Binaries/Win64/ue4ss/Mods/<calling mod>` | The calling mod's own folder, bound per Lua environment |
| `moddata` | `<user>/Saved/ModData/<calling mod>` | Per-mod data that outlives the mod folder. Created on first use, with an [`.owner` marker](#the-owner-marker) |
| `temp` | `<moddata>/temp` | Scratch space. [Emptied when the bridge starts](#temp) |
| `savegames` | `<user>/Saved/SaveGames` | `*.sav`, `*.meta`, `*.png`. [Every overwrite keeps one `.bak`](#savegames). Expect `busy` |

`<calling mod>` is the mod's folder name under `Mods/`, as UE4SS reports it in
`on_lua_start`.

### `env.Path(name, ...)`

```lua
local p, code, msg = env.Path("savegames", "Autosave0.sav")
-- "C:/Users/me/AppData/Local/Dawnwalker/Saved/SaveGames/Autosave0.sav"
```

Joins the given parts onto the location `name` and returns the
[normalized](#envnormalizepath) absolute path. Parts are strings and may
themselves contain `/`. Pure: it doesn't touch the file system and needs no
grant. The folder need not exist.

Errors: `invalid` (unknown location, bad part, malformed result),
`outside_root` (a `..` part climbs out of the location).

### `env.Roots()`

```lua
local locations = env.Roots()
-- {
--   game      = { path = "C:/…/The Blood of Dawnwalker", root = true,  exists = true },
--   user      = { path = "C:/…/AppData/Local/Dawnwalker", root = true, exists = true },
--   mod       = { path = "C:/…/Mods/MyMod",              root = false, exists = true },
--   moddata   = { path = "C:/…/Saved/ModData/MyMod",     root = false, exists = false },
--   temp      = { ... }, savegames = { ... },
-- }
```

Returns a new table describing every location: its absolute path, whether it is
one of the two roots, and whether its folder exists right now. A location whose
folder doesn't exist is still returned, with `exists = false`. Needs no grant.
Never fails.

### The `.owner` marker

When the bridge creates `moddata` for a mod, it writes `moddata/.owner`, a UTF-8
text file of `key=value` lines:

```text
folder=MyMod
first_write=2026-10-10T12:00:00Z
last_write=2026-10-10T12:00:00Z
bridge_version=0.1.0
```

`last_write` and `bridge_version` are refreshed by the first write under
`moddata` in each bridge session (not on every write). Times are UTC, ISO 8601.
Mods may read `.owner`. The bridge refuses to let a mod write, append to, move or
remove it (`denied`), whatever the grants say. It exists so leftover folders can
be traced to a mod later.

### `temp`

`temp` lives inside `moddata`, so using it creates `moddata` (and `.owner`) if
needed. When the bridge starts, before any Lua environment starts, it empties
`temp` in every `<user>/Saved/ModData/*` folder that has an `.owner` marker.
Folders without a marker are left alone. A Lua reload doesn't empty `temp`.

### `savegames`

- **Every overwrite keeps one backup.** Any operation that replaces an existing
  file under `savegames` (`WriteText`, `Copy` or `Move` with `overwrite = true`)
  goes through `ReplaceFileW` and leaves the previous content in
  `<name>.bak` (for example `Autosave0.sav.bak`), replacing any older `.bak`.
  This is a fixed rule, not an option. The `.bak` is written by the bridge and
  isn't checked against the grant's `extensions`.
- **Operations that can't keep a backup are refused** with `invalid`:
  `WriteText` with `atomic = false`, and `Open` with `append = false` on an
  existing file. `Append` and appending `Open` don't overwrite, so they are
  allowed if granted.
- **`busy`:** the game may hold a save open while writing it. A sharing
  violation returns `busy` at once; the bridge doesn't wait or retry.
- Steam Cloud may sync these files (not checked). A bad write can reach the
  cloud copy.
- The safe set can only read `savegames`. Writing needs an explicit grant.

## Paths

### Accepted forms

Every path argument accepts either form:

- **Location-relative:** a location name, optionally followed by `/` and a
  relative path: `"mod"`, `"mod/cache/state.json"`, `"savegames/Autosave0.sav"`.
  The first segment must be a location name (exact, lowercase). Any other
  relative path is `invalid`; there is no current directory.
- **Absolute:** a drive-letter path inside one of the two roots:
  `"C:/Users/me/AppData/Local/Dawnwalker/Saved/x.txt"`. `\` and `/` are both
  accepted.

### Rules

- **Encoding.** Paths are UTF-8 in Lua. Invalid UTF-8 or a NUL byte is
  `invalid`. The native side converts to UTF-16 and uses long-path (`\\?\`)
  forms internally; these never appear in Lua.
- **Returned paths** are absolute, with forward slashes, an uppercase drive
  letter, no `.` segments, no doubled or trailing slash. Case is kept as given;
  the bridge doesn't fold case or look up on-disk spelling.
- **Comparison** with grants and locations ignores case (Windows ordinal,
  case-insensitive).
- **`.` segments** are dropped.
- **`..` segments** are resolved lexically. A location-relative path whose `..`
  climbs above its location (`"mod/../OtherMod"`) is `outside_root`. An absolute
  path whose `..` climbs above its root is `outside_root`.
- **Outside the roots:** an absolute path not inside `game` or `user` is
  `outside_root`.
- **Device and UNC paths** are `outside_root`: `\\server\share\…`, `//…`,
  `\\.\…`, `\\?\…`, `\??\…`, and any segment that is a reserved device name
  (`CON`, `PRN`, `AUX`, `NUL`, `COM1`–`COM9`, `LPT1`–`LPT9`, with or without an
  extension). Named pipes are therefore unreachable.
- **Malformed paths** are `invalid`: empty string, drive-relative (`C:foo`), a
  segment containing `< > : " | ? *` or a control character (this also rules
  out alternate data streams), a segment ending in a space or a dot.
- **Links.** Symbolic links, junctions and mount points inside the roots are
  followed. Every access check is made against the **resolved real path**. A
  path whose resolution leaves both roots is `outside_root`, checked at the time
  of each call. `RemoveTree` never follows links; it removes the link itself.

### Lexical helpers

These work on strings only. They don't touch the file system, need no grant and
don't resolve locations, except `Normalize`.

| Function | Returns | Notes |
|---|---|---|
| `env.Join(a, b, ...)` | string | Joins parts with `/` and collapses separators. `Join("mod", "cache", "x.json")` → `"mod/cache/x.json"`. Doesn't resolve `..` or locations |
| `env.Normalize(path)` | string | The absolute form described under "Returned paths", with the location resolved. Errors: `invalid`, `outside_root` |
| `env.Parent(path)` | string | Everything before the last segment. `Parent("mod/a/b.txt")` → `"mod/a"`. A path with one segment (`"mod"`, `"C:"`) returns `nil, "invalid"` |
| `env.Name(path)` | string | Last segment. `"b.txt"` |
| `env.Stem(path)` | string | Last segment without its extension. `"b"`; `"a.tar.gz"` → `"a.tar"`; `".owner"` → `".owner"` |
| `env.Extension(path)` | string | Text after the last dot of the last segment, without the dot. `"txt"`; `"a.tar.gz"` → `"gz"`; none or a leading dot only (`".owner"`) → `""` |

A trailing slash is ignored by all of them. A non-string argument is `invalid`.

## Access environments

The sandbox prevents mistakes; it isn't a security boundary. Every UE4SS Lua mod
already has the full standard library (`io`, `os`, `package.loadlib`) and can
write anywhere the user can. The bridge checks only what goes through it.

### The safe set

`UE4SSLuaFileBridge()` returns an environment with these grants:

| Path | Mode | delete |
|---|---|---|
| `game` | `ro` | `false` |
| `user` | `ro` | `false` |
| `mod` | `rw` | `false` |
| `temp` | `rw` | `true` (proposed) |

Writing to `moddata`, `savegames` or anything else must be granted explicitly.

### `env.AddPath(path, options?)`

```lua
local env = UE4SSLuaFileBridge()
    .AddPath("mod/cache", { delete = true })                    -- inherits rw from mod
    .AddPath("savegames", { mode = "rw", extensions = { "sav" } })
    .AddPath("mod/logs",  { mode = "wo", extensions = { "log" } })
```

Returns a **new** environment: this one plus the grant. The original is
unchanged, so variants derived from one base can't interfere. There is no
global switch and nothing to reset: an environment's **scope is wherever the
variable holding it is visible**.

`path` is a location or a location-relative path (or an absolute path inside the
roots). It needn't exist. A grant on a file covers that file; a grant on a
folder covers the folder and, with `recursive`, everything below it.

| Option | Type | Default | Meaning |
|---|---|---|---|
| `mode` | `"ro"`, `"wo"`, `"rw"`, `"ao"`, `"stat"` | inherited | What the grant allows (table below) |
| `extensions` | list of strings | inherited | File extensions the grant covers, without the dot, compared ignoring case. `{}` covers no files. Folders are always covered |
| `delete` | boolean | `false` | Allows `Remove`, `RemoveTree` and being the source of `Move` |
| `recursive` | boolean | `true` | `false` covers the path itself and its direct children only |

**Inheritance.** A missing `mode` or `extensions` is taken from the most specific
grant that covers the path, worked out at check time from specificity, not from
chain order. There always is one, because the safe set covers both roots. A
default never widens access: `delete` defaults to `false`, and `extensions`
inherits rather than defaulting to "any" (the safe set's root grants cover any
extension).

**Validation.** `AddPath` raises a Lua error, with a message starting
`invalid:` or `outside_root:`, when:

- an option key is unknown, or a value has the wrong type or an unknown value;
- an `extensions` entry is empty or contains `.`, `/` or `\`;
- `delete = true` is combined with an explicit `mode` of `"ro"`, `"stat"` or
  `"ao"` (rotation of an `ao` log needs a separate grant with `delete`);
- the path is malformed or outside the roots.

It raises rather than returning `nil, code, message` so that a bad grant fails
at the line that built it, not later as "attempt to index a nil value" in the
middle of a chain.

**Re-granting a path.** `AddPath` on a path that already has a grant in the
environment (compared after resolution, ignoring case) **replaces** that grant
in the new environment. This is how `AddPath("mod", { delete = true })` changes
a safe-set grant.

### What each mode allows

| Operation | `stat` | `ro` | `wo` | `rw` | `ao` | Also needs |
|---|:-:|:-:|:-:|:-:|:-:|---|
| `Exists`, `Stat`, `List` | yes | yes | yes | yes | yes | |
| `ReadText`, `ReadBytes`, `Tail` | | yes | | yes | | |
| `MakeDir` | | | yes | yes | yes | |
| `WriteText` | | | yes | yes | | |
| `Append`, `Open` (append) | | | yes | yes | yes | |
| `Open` with `append = false` | | | yes | yes | | |
| `Remove`, `RemoveTree` | | | yes | yes | | `delete` |
| `Copy` source | | yes | | yes | | |
| `Copy` destination | | | yes | yes | | |
| `Move` source | | | yes | yes | | `delete` |
| `Move` destination | | | yes | yes | | |

The plan lists `MakeDir`, writing and appending for `wo`; this spec adds `stat`
to `wo` and `ao` (proposed), so a writer can check what it is about to touch.
`ao` never truncates, overwrites or deletes, so logs keep their history.

### How a call is checked

For each path an operation touches:

1. The path is normalized and resolved to its real path (links followed).
   Outside both roots: `outside_root`.
2. Among the environment's grants that **cover** the real path, the most
   specific wins (the longest path, by segments). A grant covers a path when the
   path is the grant's path or below it (direct children only when
   `recursive = false`), and, for a file, when its extension is in the grant's
   effective `extensions`. A grant that doesn't cover the path is skipped, so
   the next enclosing grant decides. Grants are never combined.
3. The winning grant's effective `mode` (and `delete`) must allow the operation.
   Otherwise: `denied`.
4. The [delete floor](#delete-floor) and the [`.owner`](#the-owner-marker)
   rule apply regardless of grants: `denied`.

Checks are made natively: each environment carries a policy id, and the native
side checks every call against the resolved real path (normalization,
junctions, Unicode).

### Delete floor

Whatever the grants say, `Remove`, `RemoveTree` and `Move` (as source) refuse,
with `denied`:

- any location itself (`game`, `user`, `mod`, `moddata`, `temp`, `savegames`);
- a drive root;
- the user profile root (`%USERPROFILE%`).

Their contents can be removed when granted.

## Operations

All paths below accept either [form](#accepted-forms). "Mode" is what the
winning grant must allow (see the [table](#what-each-mode-allows)). Errors list
the codes specific to the operation; `invalid`, `outside_root`, `denied` and
`io` can come from any operation that touches the file system.

### `env.Exists(path)`

Returns `true` or `false`. A missing file or folder is `false`, not an error.
Mode: `stat`. Errors: `denied`, `outside_root`, `invalid`, `io`.

### `env.Stat(path)`

```lua
{
    type = "file",          -- "file", "directory" or "other"
    size = 1234,            -- bytes; 0 for a directory
    modified = 1791640800,  -- seconds since the Unix epoch (UTC), may have a fraction
    created = 1791640000,
    readonly = false,       -- the read-only attribute
    link = false,           -- the path's last segment is a symlink, junction or mount point
}
```

Follows links; `link` reports whether the final segment was one. Mode: `stat`.
Errors: `not_found`.

### `env.List(path)`

Returns an array of the folder's entries, sorted by name (byte order), without
`.` and `..`:

```lua
{ { name = "a.json", type = "file", size = 10, modified = 1791640800, link = false }, ... }
```

Not recursive in 0.1.0. Hidden and system entries are included. Mode: `stat` on
the folder. Errors: `not_found`, `invalid` (path is a file).

### `env.MakeDir(path)`

Creates the folder and any missing parents. Returns `true`, also when the folder
already exists. Every folder it would create must be allowed by its own winning
grant; this is checked before anything is created. Mode: `wo`, `rw` or `ao`.
Errors: `exists` (a file is in the way), `not_found` (the drive or root is
missing), `read_only`.

### `env.ReadText(path)`

Returns the file's contents. A leading UTF-8 BOM is removed. Nothing else is
changed: no newline conversion, no UTF-8 validation. Mode: `ro`. Errors:
`not_found`, `invalid` (a directory, or larger than `max_read_bytes`), `busy`.

### `env.ReadBytes(path, options?)`

Returns the bytes exactly as stored. Options: `offset` (integer ≥ 0, default
`0`), `length` (integer ≥ 0, default to the end). Reading past the end returns
the bytes available (possibly `""`). A single read may not exceed
`max_read_bytes`. Mode: `ro`. Errors: `not_found`, `invalid`, `busy`.

### `env.WriteText(path, text, options?)`

Writes `text` (any Lua string, so it also serves binary content) to the file,
creating or replacing it. It adds no BOM and converts no newlines. Returns
`true`. The parent folder must exist. Mode: `wo` or `rw`.

Options: `atomic` (boolean, default `true`).

**Atomic write (default).** Readers see the old content or the new content,
never a mix, and a failure leaves the target unchanged:

1. Create a temporary sibling `<name>.xbtmp-<pid>-<n>` in the same folder
   (`CREATE_NEW`, not shared).
2. Write all bytes, then `FlushFileBuffers`, then close.
3. If the target exists: `ReplaceFileW(target, temp, backup)`, with `backup`
   = `<name>.bak` under `savegames` and none elsewhere. The target's
   attributes, ACL and creation time are kept. Otherwise:
   `MoveFileExW(temp, target, MOVEFILE_WRITE_THROUGH)`.
4. On any failure, delete the temporary file and return the error.

The temporary sibling isn't checked against `extensions`. A crash can leave one
behind; its name makes it recognizable.

**Non-atomic** (`atomic = false`): opens the target with truncation and writes in
place. A failure can leave a partial file. Refused under `savegames`
(`invalid`).

Errors: `not_found` (parent folder missing), `busy` (the target is open without
write/delete sharing), `read_only` (the target has the read-only attribute),
`invalid` (target is a directory).

### `env.Append(path, text)`

Appends `text` to the end of the file, creating it if missing. Each call is one
`FILE_APPEND_DATA` write, so concurrent appenders don't overwrite each other.
Returns `true`. The parent folder must exist. Mode: `wo`, `rw` or `ao`. Errors:
`not_found`, `busy`, `read_only`, `invalid`.

### `env.Remove(path)`

Removes a file or an empty folder. Returns `true`. Mode: `wo` or `rw`, plus
`delete`. Errors: `not_found`, `denied` (including the
[delete floor](#delete-floor)), `invalid` (a folder that isn't empty; use
`RemoveTree`), `busy` (the file is open), `read_only`.

### `env.RemoveTree(path)`

Removes a folder and everything in it. Returns `true`. Links inside the tree are
removed as links; their targets are never entered. Before removing anything it
checks every entry against the environment; if any entry is denied, nothing is
removed and it returns `denied`. A failure part-way (for example `busy`)
returns that code and leaves the rest of the tree. Mode: `wo` or `rw`, plus
`delete`, on every entry. Errors: `not_found`, `denied`, `invalid` (path is a
file), `busy`, `read_only`.

### `env.Copy(from, to, options?)`

Copies a file. Options: `overwrite` (boolean, default `false`). The destination
is written like an atomic `WriteText` (temporary sibling, then replace; `.bak`
under `savegames`). Folders are not copied in 0.1.0. Returns `true`. Mode:
`ro` on `from`, `wo` or `rw` on `to`. Errors: `not_found`, `exists`
(destination exists and `overwrite` is false), `invalid` (a folder), `busy`,
`read_only`.

### `env.Move(from, to, options?)`

Moves or renames a file. Options: `overwrite` (boolean, default `false`). Within
one volume it is a rename (`MoveFileExW`, or `ReplaceFileW` with `.bak` when
overwriting under `savegames`). Across volumes (the two roots may be on
different drives) it copies, flushes, then removes the source. Folders are not
moved in 0.1.0. Returns `true`. Mode: `wo` or `rw` plus `delete` on `from`;
`wo` or `rw` on `to`. Errors: `not_found`, `exists`, `invalid`, `busy`,
`read_only`, `denied` (including the delete floor on `from`).

## Streams: `Open`

```lua
local log = assert(env.Open("mod/logs/session.log"))
log:Write("started\n")
log:Close()
```

### `env.Open(path, options?)`

Opens a file for writing and returns a stream. The environment is checked when
the stream is opened, not on each write. Streams are write-only in 0.1.0.

| Option | Type | Default | Meaning |
|---|---|---|---|
| `append` | boolean | `true` | `true` writes at the end. `false` truncates the file on open (needs `wo` or `rw`; refused under `savegames`) |
| `create` | boolean | `true` | Create the file if missing. `false` returns `not_found` instead |
| `flush` | `"write"`, `"manual"` | `"write"` | `"write"` hands every `Write` to the operating system at once, so readers and `Tail` see it immediately. `"manual"` buffers up to 64 KiB until `Flush`, `Close` or a full buffer |

Mode: `wo`, `rw` or `ao` (with `append = true` only). The parent folder must
exist. The file is opened with read and delete sharing
(`FILE_SHARE_READ | FILE_SHARE_DELETE`): others can read it, and it can be
renamed for rotation, while it is open. A second writer gets `busy`. Errors:
`not_found`, `busy`, `read_only`, `invalid`, and `io` when the Lua environment
already has 64 open streams.

### Stream methods

| Method | Returns | Notes |
|---|---|---|
| `stream:Write(text, ...)` | `true` | Writes each argument in order. Strings and numbers are accepted (numbers as `tostring`); anything else is `invalid` and nothing is written |
| `stream:Flush()` | `true` | Hands buffered data to the operating system. It doesn't force it to disk (`FlushFileBuffers`) |
| `stream:Close()` | `true` | Flushes and closes. Calling it again returns `true` |
| `stream.path` | string | The normalized path, read-only |

After `Close`, `Write` and `Flush` return `nil, "invalid", "stream is closed"`.
Streams close automatically when their Lua environment stops or reloads, and
when the stream object is garbage-collected. Close streams explicitly when you
are done; collection timing is not a schedule.

## File sockets: `Tail`

`Open` and `Tail` together make a **file socket**: one mod holds a file open and
writes; another is called back whenever the file grows. Mods can talk through
a path without sharing a Lua state.

```lua
-- Mod A
local out = assert(env.Open("mod/outbox.txt"))
out:Write("hello\n")

-- Mod B
local sub = assert(env.Tail("game/Dawnwalker/Binaries/Win64/ue4ss/Mods/ModA/outbox.txt",
    function(line, info)
        print("ModA says: " .. line)
    end))
-- later
sub:Close()
```

### `env.Tail(path, fn, options?)`

Subscribes `fn` to data appended to the file. Returns a subscription. Mode:
`ro` (or `rw`).

| Option | Type | Default | Meaning |
|---|---|---|---|
| `chunks` | boolean | `false` | `false` delivers complete lines; `true` delivers raw chunks as read |
| `from` | `"end"`, `"start"` | `"end"` | Where reading starts: at the current end of the file, or at its start |

**Callback.** `fn(text, info)`:

- Lines mode: `text` is one line without its terminator (`\n`, or `\r\n`). A
  partial line is held until its newline arrives.
- Chunks mode: `text` is the bytes read, unchanged.
- `info` is `{ offset = <byte offset of text in the file>, reset = <boolean> }`.
  `reset` is `true` on the first delivery after a truncation or rotation.

**Truncation and rotation.** When the file becomes shorter than the read
position (truncated), or its identity (volume serial and file index) changes
(rotated, deleted and recreated), the read position goes back to `0` and a held
partial line is discarded.

**Missing file.** If the file doesn't exist yet, `Tail` still succeeds provided
its folder exists; delivery starts from offset `0` when the file appears.
Folder missing: `not_found`.

**Delivery.** The bridge checks subscribed files from its update callback, on
the dispatch schedule it shares with UE4SSLuaEventBridge, and calls `fn` on the
subscriber's Lua thread within the per-pass dispatch budget. Text left over when
the budget runs out is delivered on the next pass, in order. The file itself is
the buffer: the bridge reads only what it delivers, so a slow subscriber falls
behind but loses nothing (unless the file is truncated or rotated first). Expect
delivery in the next frame or so after the writer's flush. A line longer than
1 MiB is delivered in 1 MiB pieces.

**Errors in `fn`.** A callback that raises an error closes its subscription.
The bridge writes the path and the error to UE4SS.log.

**Subscription.** `sub:Close()` stops delivery and returns `true`; calling it
again returns `true`. A callback already running is not interrupted.
Subscriptions close automatically when their Lua environment stops or reloads.
`sub.path` is the normalized path.

Errors from `Tail`: `not_found`, `denied`, `outside_root`, `invalid` (`fn` is not
a function, the path is a folder).

## Error codes

| Code | When |
|---|---|
| `not_found` | The file or folder doesn't exist, or a parent folder an operation needs is missing (`ERROR_FILE_NOT_FOUND`, `ERROR_PATH_NOT_FOUND`) |
| `denied` | The environment doesn't grant the operation (mode, extension, `delete`); the [delete floor](#delete-floor) or the `.owner` rule; or Windows refuses access (`ERROR_ACCESS_DENIED`, for example under `Program Files` without elevation). The message says which |
| `exists` | The destination exists and the operation won't replace it: `Copy` or `Move` without `overwrite`, or `MakeDir` where a file is in the way |
| `outside_root` | The path, after normalization or link resolution, is outside both roots; a `..` climbs out of its location or root; a device, UNC or reserved-name path |
| `read_only` | The target file has the read-only attribute, or the volume is write-protected (`ERROR_WRITE_PROTECT`). The bridge never clears the attribute |
| `busy` | Another process or handle holds the file in a conflicting way (`ERROR_SHARING_VIOLATION`, `ERROR_LOCK_VIOLATION`), for example the game writing a save. Returned at once, without waiting or retrying |
| `io` | Any other operating-system failure: disk full, a device error, a failed step of an atomic replace that `ReplaceFileW` reports, the open-stream limit |
| `invalid` | A bad argument or option, a malformed path, an unknown location, the wrong kind of target (folder vs file), a folder not empty for `Remove`, a read larger than `max_read_bytes`, an operation that would bypass the `savegames` backup, or using a closed stream |

Policy refusals and Windows refusals both use `denied`; they differ in the
message. An operation that fails leaves no partial result unless its section
says otherwise (non-atomic `WriteText`, `RemoveTree`).

## Deferred and out of scope

**Deferred** (not in 0.1.0; `GetCapabilities()` reports `false`):

- `Lock`: advisory or byte-range locking between writers.
- `Hash`: content hashes of files.
- `TempFile`: unique temporary files with automatic cleanup.

**Out of scope** (decided 2026-10-10):

- **Queuing:** brokers, delivery guarantees, distribution among receivers.
  `Tail` delivers what is in the file; it doesn't acknowledge or redeliver.
- **Whiteboard messaging:** shared state between mods.
- **Windows named pipes** (`\\.\pipe\…`). They would need an explicit exception
  to the ban on device paths.

Also not in 0.1.0: reading streams, recursive `List`, copying or moving
folders, file watching beyond `Tail`, presets beyond the four above.

## Internal native contract

*To be drafted by F (Phase 1, step 2):* the table the DLL hands to Lua, the
policy id carried by each environment, and the shape of each native call behind
the functions above.

## Open points for review

Each is a proposal written into the spec above; approve or change before G-spec.

1. **`mods` location.** The plan's own AddPath example uses `"mods"`, which isn't
   one of the four presets. Without it, a `Tail` on another mod's file needs the
   long `game/Dawnwalker/Binaries/Win64/ue4ss/Mods/<mod>/…` form. Proposal:
   keep 0.1.0 to the four presets and fix the example; alternatively add `mods`
   (read-only by the safe set) now, since file sockets between mods need it.
2. **Root names** `game` and `user` as path prefixes (`"game/…"`, `"user/…"`)
   and as keys of `Roots()`.
3. **`Roots()` returns all locations**, roots and presets, with `root` and
   `exists`, rather than only the two roots. The `exists = false` rule in the
   plan needs somewhere to appear.
4. **Path forms:** every path argument accepts location-relative or absolute
   inside the roots; returned paths are absolute. Lexical helpers (`Join`,
   `Parent`, `Name`, `Stem`, `Extension`) don't resolve locations.
5. **`..` handling:** resolved lexically, `outside_root` only when it climbs above
   its location or root. The stricter alternative is to reject every `..`
   segment.
6. **`extensions` inherits** like `mode` instead of defaulting to "any".
   Otherwise `AddPath("savegames/sub", {})` would widen an `extensions = {"sav"}`
   grant, contradicting "a default never widens access".
7. **Uncovered files fall through.** A file whose extension a grant doesn't list,
   or a deeper path under a `recursive = false` grant, is decided by the next
   enclosing grant (usually the safe set's `ro`), not denied outright. This
   keeps `savegames/*.png` readable after granting `rw` on `*.sav`.
8. **Every mode includes `stat`** (the plan lists only `MakeDir`, writing and
   appending for `wo`).
9. **`delete` with explicit `ro`, `stat` or `ao` is an `AddPath` error;** at check
   time `Remove` also needs an effective `wo` or `rw`.
10. **Re-granting the same path replaces** the earlier grant, so
    `AddPath("mod", { delete = true })` can widen a safe-set grant.
11. **`temp` has `delete = true`** in the safe set; scratch space without delete
    is awkward. The plan only says "read-write on `mod` and `temp`".
12. **`AddPath` raises a Lua error** on bad input instead of returning
    `nil, "invalid", message`, so chains fail at the faulty line.
13. **`read_only` means the file system's read-only attribute** or a
    write-protected volume; every policy refusal is `denied`. Alternative: use
    `read_only` for "the grant allows reading but not this write".
14. **`temp` cleanup** happens once at bridge start, for every `ModData/*` folder
    with an `.owner` marker; Lua reloads don't empty it.
15. **`.owner`** format (`key=value` lines), refreshed once per bridge session,
    and protected from writes and removal by mods.
16. **`savegames` refuses** non-atomic `WriteText` and truncating `Open`
    (`invalid`) rather than silently forcing a backup. `Copy` and `Move` with
    `overwrite` keep the `.bak` too, reading "every overwrite" literally.
17. **No `WriteBytes`.** `WriteText` writes any Lua string unchanged. Adding
    `WriteBytes` as an identical name for symmetry with `ReadBytes` is cheap if
    wanted.
18. **`ReadText`** strips a UTF-8 BOM and nothing else (no validation, no newline
    conversion). **`max_read_bytes`** is 64 MiB per read.
19. **Parent folders are not created** by `WriteText`, `Append`, `Open`, `Copy`
    or `Move` (`not_found`); `MakeDir` creates parents.
20. **`Copy` and `Move` are file-only** in 0.1.0; `List` is not recursive.
21. **`Remove`** on a missing path returns `not_found` (not idempotent) and on a
    non-empty folder `invalid`. **`RemoveTree`** checks every entry first and
    removes nothing if any is denied.
22. **Times** are seconds since the Unix epoch, possibly fractional, comparable
    with `os.time()`.
23. **Streams:** option names `append`, `create`, `flush`; sharing is read plus
    delete (the plan says share-read; delete sharing allows rotation by
    rename); `Flush` doesn't force to disk; 64 open streams per Lua environment,
    `io` beyond that; streams also close on garbage collection.
24. **`Tail` callback** receives `(text, info)` with `offset` and `reset`; a
    missing file is accepted if its folder exists; a callback error closes the
    subscription; lines over 1 MiB are split.
25. **Dot calls for environments:** `env.ReadText(p)`, as in the plan's examples.
    A colon call (`env:ReadText(p)`) passes the environment as the path and gets
    `invalid`. The Lua helpers could instead detect and accept both forms.
26. **`GetVersion` and `GetCapabilities`** exist on both the global and every
    environment; `API_VERSION` starts at `1`.
27. **Names in `List`** that aren't valid UTF-16 (unpaired surrogates, which
    Windows permits) can't be returned as UTF-8. Proposal: skip them in 0.1.0
    and log once; `RemoveTree` still removes them natively.
