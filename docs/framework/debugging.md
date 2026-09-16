# Framework: Debugging

[« Back to overview](../overview.md)

`WIIXL_LOG` is the one debug call; it reaches you differently per platform:

```cpp
WIIXL_LOG("player pos: %.2f %.2f %.2f", x, y, z);
```

| Platform | Where it goes |
|---|---|
| Switch | `svcOutputDebugString`, via exlaunch's logger. An emulator (Ryujinx) prints it in its console; on hardware it needs something listening for debug output. |
| Wii U (Aroma) | An on-screen notification toast (WUPS `NotificationModule`). |
| Cemu | A ring buffer in the mod's own memory, read out of process by `tools/ring_log_reader`. |

Same call site everywhere. `#if WIIXL_...` branches live inside
`WIIXL_LOG`'s implementation
([`debug_log.hpp`](../../include/wiixlaunch/debug_log.hpp)), not in your
code.

A `.wxlm` mod calls the same mechanism through `wiixl_core:Log` instead of
the macro. See [SDK: Getting started](../sdk/getting-started.md). Where the
text ends up is identical.

## Reading Cemu's ring log

Cemu's target process has no OS log access, so `WIIXL_LOG` writes free-form
text entries into a small ring buffer inside the mod. `tools/ring_log_reader`
finds the buffer by scanning process memory for a magic cookie; no
debugger, no symbols needed.

Build it:

```bash
# Windows
tools/ring_log_reader/build.bat

# Linux
cd tools/ring_log_reader && ./build.sh
```

Run it while Cemu is running your mod:

```bash
# Windows
tools/ring_log_reader/ring_log_reader.exe

# Linux
tools/ring_log_reader/ring_log_reader          # may need sudo
```

Looks for a process named `Cemu.exe`/`Cemu` by default; pass a name as the
first argument otherwise.

Reading another process's memory needs elevated privileges:

* Windows: run as Administrator if `OpenProcess` fails.
* Linux: run with `sudo` if opening `/proc/<pid>/mem` fails.

## The host's own log lines

The loader, hook manager, arena, and surface registry all log through
`WIIXL_LOG`, prefixed by component: `[loader:<id>]`, `Hook:`, `Arena:`,
`Surface:`, `Patch:`, `Tick:`. Read top-down; find the first missing or
wrong line. See
[SDK: Distribution and troubleshooting](../sdk/distribution.md#reading-the-boot-log).

## Wii U toasts vs. plugin status

The load-time toasts WUPS plugins show ("N hooks registered", etc., in
[`wiiu_plugin.cpp`](../../src/wiiu_plugin.cpp)) are a fixed set of
FunctionPatcher/hook-registration messages, not `WIIXL_LOG` output.
`WIIXL_LOG` calls show up as their own toasts alongside those.

## Limitations

A single `WIIXL_LOG` call is capped at 200 characters of text
(`kMaxLogTextLen` in
[`debug_log.hpp`](../../include/wiixlaunch/debug_log.hpp)).

The formatter is hand-rolled ([`format.hpp`](../../include/wiixlaunch/format.hpp)),
since there's no `printf` on a bare code cave. Supports `%d`/`%i`, `%u`,
`%x`/`%X`, `%p`, `%s`, `%f`, with field width, zero padding (`%02X`, `%8d`),
and precision on `%f` (`%.2f`). `tools/format_test` guards it.

## BotW::OSLog is a different thing

`WiiXLaunch::BotW::OSLog(...)` (in `gx2.hpp`, `fs.hpp`) is a separate,
Cemu-only logger calling Cemu's own `OSReport` directly, used by the
module's internals. It doesn't write to the ring buffer;
`tools/ring_log_reader` won't show it. Look for it in Cemu's log window
with CoreInit logging enabled under Debug > Logging. Use `WIIXL_LOG` (or
`wiixl_core:Log` from a `.wxlm`) for your own code.
