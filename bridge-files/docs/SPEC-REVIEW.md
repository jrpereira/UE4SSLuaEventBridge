# UE4SSLuaFileBridge spec review (L, Gate G-spec step 3)

2026-10-10. Reviewer: L (Lua helpers). Inputs:

- D: `bridge-files/docs/API.md` at `00fff90` (`feature/xb-files-docs`), open points 1-27.
- F: `bridge-files/include/FileBridgeNativeContract.hpp` at `05bb4de` (`feature/xb-files`),
  rules R1-R5, open points F1-F13.

Verdict: **a thin Lua layer can connect the two.** Every public function maps to a native
or to pure Lua. Fifteen mismatches need a one-line resolution each (section 2). Four
items need K (section 5).

UE4SS references below are to RE-UE4SS at the pinned commit
(`git -C work/RE-UE4SS show 97b7e501:<path>`; the checkout's HEAD is newer). The
installed `UE4SS.dll` is that build (`UE4SS.log`: "Git SHA #97b7e501").

## 1. F's ABI claims, checked

| Claim | Result | Evidence |
|---|---|---|
| R3: inbound strings stop at the first NUL | **Confirmed** | `LuaMadeSimple.cpp:754-759`: `std::string_view string = lua_tostring(...)` (a `const char*`, so `strlen`), then `lua_remove`. `Table::get_string_field` does the same (`:373-394`). No `lua_tolstring` wrapper exists |
| R3: outbound strings are binary-safe | **Confirmed** | `set_string(std::string_view)` is `lua_pushlstring` (`:771-774`) |
| R3: copy before the next Lua API call | **Confirmed** | The value is removed from the stack before the view is returned (`:757`) |
| R2: `get_string` on a non-string crashes | **Confirmed, narrower** | `lua_tostring` returns NULL for nil, boolean, table, function and a missing argument, so `strlen(NULL)`. A number converts in place, so it's no crash. `get_integer` on a non-integer returns `0` (`:810-815`). It's wrong, but it doesn't crash |
| R2: no argument count or type query | **Refuted for UE4SS, true for LEB's `.def`** | `get_stack_size`, `is_string`, `is_integer`, `is_nil`, `is_bool` are `RC_LMS_API` (`LuaMadeSimple.hpp:516,532,535,548,552`) and **are exported by the installed `UE4SS.dll`** (`objdump -p`). `bridge-events/abi/UE4SS-97b7e501.def` just doesn't list them |
| R1: natives can't build or read tables | **Refuted for UE4SS, true for LEB's `.def`** | `prepare_new_table` (`hpp:573`, `cpp:992-997`) and `Table::fuse_pair` (`cpp:300-303`, `lua_rawset(-3)`) are exported. So are `is_table` and `Table::get_*_field`. `prepare_new_table`, then `set_string(key)`, `set_X(value)` and `fuse_pair()` builds a table. `add_pair` is inline and calls `lua_*` directly, so it can't be used. No `lua_*` symbol is exported (LuaRaw is static) |

**Recommendation (needs K, see K1).** Add `get_stack_size`, `is_string` and `is_integer` to
the file bridge's import `.def`. Each native can then check its arity and types and return
`invalid` instead of crashing. This adds defence in depth: R5 still hides the natives.
Keep the record strings (R1/F3) anyway. They are simpler to fake in the Lua tests, and
building tables natively buys nothing the Lua layer can't do in a few lines.

## 2. Mismatches between D and F, with resolutions

| # | Mismatch | Resolution |
|---|---|---|
| M1 | **Links.** API.md "Links" follows links for every check except `RemoveTree`. F (F4) leaves the leaf unfollowed for `Remove`, `RemoveTree`, `Move` and siblings | Take F's rule. API.md: "`Remove`, `RemoveTree` and `Move` act on a link itself. The check uses the link's own path" |
| M2 | **Tail `partial`.** F has `DeliveryFlag.partial` for pieces of lines over 1 MiB. API.md's `info` has only `offset`, `reset` | Add `info.partial` (boolean) to API.md. The Lua layer maps the bits |
| M3 | **Native-side close.** F's `DispatchKind.closed` (file error, session stop) has no public surface. The subscriber can't tell its subscription died | Lua marks the subscription closed, drops the callback and prints `<path>: <code> <message>` once. Exposing it publicly (an `on_close` option or `sub.closed`) is K2 |
| M4 | **Limits.** 256 grants, 4096 policies and 64 subscriptions are not in API.md. Subscriptions over the limit are `invalid`, streams over it are `io` | Document all four limits in API.md. Use `io` for every capacity limit (it's exhaustion, not a bad argument and not another holder) |
| M5 | **Extension text.** F joins extensions with `;`, but API.md only forbids `.`, `/`, `\` | API.md also forbids `;`, control characters and `< > : " \| ? *` in an extension. Lua checks before the call |
| M6 | **Unbound `mod`** (F10: a mod name that isn't one valid segment). API.md `Roots()` always has a `path` string | `path = nil, exists = false` for unbound locations. API.md says so |
| M7 | **Stop hook.** F's `__UE4SSLuaFileBridge_Stop` is a mod-visible global. API.md's "Isolation" implies nothing extra is global | Drop the global. Add `DispatchKind.stop = 3` and call the existing dispatcher ref. Same effect, no global |
| M8 | **Session check by `lua_State`.** F rejects a session id whose `lua_State` isn't one of the four aliases. `hook_lua` is created lazily on the first `RegisterHook` (`LuaMod.cpp:760-770`), after `on_lua_start` has fired (`:6006`). So calls from hook callbacks would fail | Check the session id only, as LEB does (`bridge-events/src/BridgeMod.cpp` `consume_session` / `session_for_id`). The id is an upvalue that mods can't reach |
| M9 | **Coroutines.** API.md "Threads" says operations may be called from any Lua thread. `process_lua_function` raises for any `lua_State` not registered through `Lua::new_thread` (`LuaMadeSimple.cpp:1178-1181`), so it raises for every `coroutine.create`/`wrap` | API.md: "Not available inside coroutines the mod creates itself; there they return `nil, "invalid", message`." Lua `pcall`s each native call and maps a raised error to `invalid`. (This applies to LEB too; nobody has tested it in the game) |
| M10 | **Safe policy release.** `SafePolicy` returns the same id on every call, and `ReleasePolicy` runs from `__gc`. The first collected safe environment would free it for everyone | Native: `ReleasePolicy` on the safe id is a no-op returning `true`. Lua never queues it either |
| M11 | **GC timing.** API.md says streams close "when the stream object is garbage-collected". A native call from `__gc` is unsafe: the finalizer may run on an unregistered coroutine (M9), and it re-enters natives during any Lua allocation | Lua's `__gc` only queues the id. The queue drains at the start of the next helper call in that Lua environment and at session stop. API.md wording: "closed after it is collected, at the latest when its Lua environment stops." F: never hold a session or stream mutex across a Lua API call (`set_string` and number-to-string `get_string` allocate) |
| M12 | **Record safety.** NT-API-created names can contain `\t` or `\n`, which would break F's records | The native skips such names in `List`, as in D's point 27 |
| M13 | **Natives with no public use:** `Check` (F13), `LocationOf`, `SessionInfo`, `GetDispatchStats` | Drop `Check`, `LocationOf` and `SessionInfo`. L doesn't need them, and each is extra surface. `GetDispatchStats` is K3 |
| M14 | **Name.** API.md `env.Roots()` returns every location. F's native is `Locations` | Rename it to `env.Locations()`. "Roots" for a table of mostly presets misleads (D3) |
| M15 | **Callback thread wording.** API.md says the callback runs "on the subscriber's Lua thread". F calls the dispatcher on the session's root `lua` from `on_update` | API.md: "on the mod's root Lua state, from the bridge's update callback (not the game thread)". F/E: confirm this is the same thread and locking discipline LEB uses for its dispatcher today |

## 3. How the Lua layer connects them (no spec change needed)

- **Load:** read and clear `__UE4SSLuaFileBridge_SessionId` (keep it as an upvalue), as
  LEB's `bridge_api.lua:1-2` does. Capture natives from a **fixed name list** that matches
  `native_functions`, `assert` that each one is present, then nil each global (F1). Return
  the dispatcher.
- **Arguments:** every native call goes through one helper that passes exactly the listed
  arity. It sends strings as `type == "string"` with no NUL (`find("\0", 1, true)`), and
  integers as `math.type == "integer"` (integral floats converted with `math.tointeger`).
  Absent values are `""`, `0` or `-1` as F lists them. Wrong types return
  `nil, "invalid", msg` before any native call.
- **Errors:** `nil, code, message` passes through unchanged. Success is tested with
  `~= nil`, never truthiness (`Exists` returns `false`).
- **AddPath:** Lua validates the option table completely: string keys only, unknown keys
  rejected, value types, a proper sequence for `extensions` with M5's characters,
  `delete` with explicit `ro`/`stat`/`ao`. It builds `mode`, the `;`-joined extensions
  and the flags, then raises with `error("<code>: <msg>", 2)`, so the caller's line is
  reported. If the native reports the 4096-policy limit, Lua runs one
  `collectgarbage()`, drains the release queue and retries once. Garbage environments
  otherwise pin policies until the next GC cycle.
- **Environments:** a proxy table whose `__index` returns bound closures (cached),
  with `__newindex` raising. Every closure captures a small **holder** object that
  carries the policy id and the `__gc`. So `local read = env.ReadText; env = nil` keeps
  the policy alive as long as `read` lives. `rawset` can still write to the proxy but
  can't change a grant, so in practice the environment is immutable.
- **Streams and subscriptions:** tables with a metatable. Colon methods, a read-only
  `path` and a queued `__gc` (M11). Closed state is tracked in Lua, so `Close` is
  idempotent without a native call.
- **Tail:** Lua picks increasing integer tokens and keeps `token -> {fn, sub}`. The
  dispatcher handles `data` by building `info` from `offset` and the flags, and `pcall`s
  `fn`. If `fn` errors it drops the entry and re-raises, so the native closes the
  subscription and logs (F's path). `closed` drops the entry (M3), and `stop` (M7) clears
  everything.
- **Records:** split on `\n`, then `\t`, keeping empty fields. Numbers go through
  `tonumber`, booleans through `== "1"`. Empty input gives `{}`. `Stat` is positional,
  so Lua builds the table directly.
- **Content:** the escape works on Lua 5.4: `("a\0b\1c"):gsub("[%z\1]", ESC)` was checked
  with `lua5.4`. `stream:Write(...)` uses `tostring` on numbers, concatenates the
  arguments, escapes once and makes one native call. `Write()` with no arguments makes
  no native call.
- **Pure Lua:** `Join`, `Parent`, `Name`, `Stem`, `Extension`, and `Path` (joins, then
  native `Normalize`), plus `GetCapabilities` merging `locations` and the deferred keys,
  and `API_VERSION`. The lexical helpers treat `\` as `/` and return `/`.

## 4. Positions on the open points

**D's 27** (A = agree, D = disagree, K = needs K):

| # | | Note |
|---|---|---|
| 1 | K | `mods`. L leans toward adding it: one location name, read-only through `game` anyway, and file sockets between mods are a 0.1.0 goal. Same as F11 |
| 2 | A | |
| 3 | A | Rename it to `Locations()` (M14) |
| 4 | A | Helpers accept `\` (section 3) |
| 5 | A | |
| 6 | A | |
| 7 | A | |
| 8 | A | |
| 9 | A | Add F6's "inherited mode without write makes `delete` inert" to the text |
| 10 | A | |
| 11 | A | |
| 12 | A | Lua raises at level 2 |
| 13 | A | |
| 14 | A | |
| 15 | A | |
| 16 | A | |
| 17 | A | No alias |
| 18 | A | |
| 19 | A | |
| 20 | A | |
| 21 | A | |
| 22 | A | |
| 23 | A | 64-stream limit stays `io` (M4). Change the GC wording (M11) |
| 24 | A | Plus `info.partial` (M2) |
| 25 | D | Accept both forms. A path is never a table, so `rawequal(arg1, env)` identifies a colon call without ambiguity, at no cost. Streams and subscriptions stay colon-only, with a clear `invalid` message |
| 26 | A | |
| 27 | A | Also skip names with `\t`/`\n` (M12) |

**F's 13:**

| # | | Note |
|---|---|---|
| F1 | A | Fixed list plus `assert`, so a mismatch fails at load and not at first use |
| F2 | A | The NUL truncation is confirmed (section 1) |
| F3 | A | Keep records even if the `.def` grows (section 1) |
| F4 | A | M1 |
| F5 | A | Only F's reading is well-defined. With the target-based reading, whether an `extensions`-inheriting grant covers a file depends on what it inherits, and what it inherits depends on which grants cover that file: a circular definition. F's reading resolves each grant once per policy, and Lua can mirror it in the fake natives |
| F6 | A | Document it in API.md (D9) |
| F7 | A | E's concern. No effect on Lua |
| F8 | A | M3 for the public side |
| F9 | A/D | Agree on the limits. Disagree on `busy` for streams, and subscriptions should use `io` too (M4) |
| F10 | A | M6 |
| F11 | K | = D1 |
| F12 | K | L agrees: it costs little and blocks `RemoveTree("user/Saved")` taking out every mod's data. But it changes the delete-floor rule, so K decides |
| F13 | A | Drop `Check` (M13) |

## 5. Needs K

- **K1. Grow the import `.def`** with `get_stack_size`, `is_string` and `is_integer`, all
  exported by the pinned build. Natives would then refuse bad calls instead of crashing.
  This touches the ABI-pin surface E and F share.
- **K2. Native-side subscription close (M3):** log only (L's default), or add a public
  signal (an `on_close` option or `sub.closed`).
- **K3. `GetDispatchStats`:** expose it as `UE4SSLuaFileBridge.GetDispatchStats()` for
  parity with LEB, or drop it.
- **K4. D1/F11 `mods` location** (already parked for Jorge), and **F12 ancestor delete
  floor**.

## 6. What the Lua layer can't do as specified

- **Synchronous close on GC** (M11): it's deferred to the next helper call or to session stop.
- **Calls from mod-created coroutines** (M9): they can only fail cleanly, as `invalid`.
- **True immutability:** `rawset` writes to the proxy. Grants still can't change,
  because closures hold the policy id.
- Nothing else in API.md is out of reach with F's contract plus M1-M15.
