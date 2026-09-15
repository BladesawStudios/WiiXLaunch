# Framework: wiixl.net, sockets that belong to somebody

[« Back to overview](../overview.md)

Design of the `wiixl.net` surface. To use TCP from a mod, see
[SDK: Imports and versioning](../sdk/imports-and-versioning.md) and
`include/wiixlaunch/imports/wiixl_net.h` for the calls themselves.

`wiixl.net` is the first surface where a mod holds a resource with a
lifetime. Hooks and patches are install-once; the host owns the result
forever. A socket a mod opens and never closes holds a system handle for
the whole session, with no owner attached to the eventual report.

So the host tracks sockets and hands out its own handles rather than raw
descriptors.

## Why not raw descriptors

A raw descriptor turns use-after-close into silent cross-mod corruption:

1. mod A closes fd 5
2. mod B opens a socket, the OS hands it fd 5 (descriptors get reused)
3. mod A, still holding a stale `5`, sends its response down mod B's
   connection

Nothing crashes. B's client receives A's data intermittently, and it reads
like a host bug. A `Handle` carries a generation alongside the slot index;
the generation moves every time the slot is reused, and a stale handle
comes back `STALE-HANDLE` instead of reaching B's socket.

```
bits  0..15   slot index + 1     (so 0 is never a valid handle)
bits 16..31   generation         (moves on every acquisition)
```

16-bit generation: a slot would need 65536 recycles before a stale handle
could alias a live one. A bound, not an impossibility.

## Why per-module quotas

Without per-module attribution the only cap possible is global, and a
global cap means the leaking mod exhausts the pool while whichever mods
happen to ask next get refused. The failure lands on innocent modules.

With a per-module cap (`kMaxPerModule = 8`, `kMaxSockets = 24`), the leak is
contained to its owner: it hits its own ceiling, is refused by name, every
other mod keeps working. `tools/net_test` asserts that a second module is
unaffected.

An accepted connection goes through the quota too. A server that accepts
every frame and forgets to close is the real leak vector. When an accept is
refused, the host closes the descriptor the platform already handed over
rather than leaving it untracked.

## What tracking does not buy

| Case | What happens |
|---|---|
| A leak within a session | Contained. The quota stops it at 8; `LogState` names who holds what. |
| A hang inside a tick | Attribution only. Nothing reclaims anything since nothing runs. Tick's in-flight record names the module. |
| Shutdown | The process owns the handles. No module-unload path exists. |

`CloseAllFor` exists; its only caller today is a module closing its own
sockets through the surface.

## Two different ways there is no network

* The surface is not registered: no socket implementation at all
  (Switch). A mod importing any `wiixl.net` symbol has `wiixl.net` as a
  required surface, refused at load, by name.
* `UNAVAILABLE` from a call: sockets exist but couldn't be reached here
  (Cemu, a title whose process has no network stack).

## The static-import rule

Base may only statically import what the host can guarantee is present.
Anything else is resolved at runtime and degrades to an `UNAVAILABLE`
value.

### Why

On Cemu, WiiXLaunch ships as a graphic pack, and Cemu fails the entire pack
when any patch import cannot be resolved. Not the feature that needed it:
the pack. A single unresolvable import in base means WiiXLaunch doesn't
load at all, silently, on titles that never used the feature.

Runtime resolution converts that into: the host boots normally, one surface
reports `UNAVAILABLE`, and the log says which library was missing.

### What the host can guarantee

`coreinit` is imported by every Wii U title, so an `import.coreinit.<Name>`
shim resolves wherever WiiXLaunch runs. `src/cemu/cemu_fs.asm`,
`cemu_mem.asm`, `cemu_time.asm`, `cemu_logging.asm` are all coreinit.

Everything else is a guess about a particular game. BotW v208's import
table has 445 entries and not one is a socket call, checked against the
RPX. A static `import.nsysnet.socket` in base would bet on every title
WiiXLaunch is ever pointed at.

A game module may import what its game demonstrably imports:
`gx2_imports.asm` lives in the BotW module because GX2 is a claim about
that game, and `scripts/deploy.py` skips a module's shim table when it's
not compiled in.

### Runtime resolution

`src/cemu/cemu_dynload.asm` shims `OSDynLoad_Acquire`,
`OSDynLoad_FindExport`, `OSDynLoad_Release`. Every non-guaranteed library is
looked up at first use:

```
Acquire the RPL by name  ->  null means "this process has no such library"
FindExport each symbol   ->  null means "the library is there, that entry is not"
```

Both failures are distinct and logged. Resolution runs once and caches
including failure. All or nothing: every export must resolve or none are
used, since a partially resolved table would open sockets it couldn't
close.

### Applying it

1. Do not add an `import.<lib>.*` shim table to `src/cemu/`.
2. Resolve through `cemu/cemu_dynload.hpp` at first use, caching the
   outcome.
3. Give the surface an `Available()` and a distinct refusal value for
   "reachable platform, unreachable library."
4. Register the surface anyway where the platform could support it, so
   "unsupported platform" (surface absent) stays distinct from "unavailable
   here" (surface present, call refused at runtime).

Wii U hardware is unaffected: the Aroma plugin has its own import table.

## Every socket is non-blocking

One thread. Mods run inside a tick on the game's own draw thread, so a
blocking call is a frozen game, not a slow one.

`d_net` (`examples/net_mod`) set `SO_NONBLOCK` on its listener but not on
sockets `accept` handed back; accepted sockets don't inherit it. The first
`recv` on one froze the game the moment anything connected.

So the host sets it, on every socket it hands out, in both `Open` and
`Accept`. A socket that won't go non-blocking is closed rather than
returned. `SetNonBlocking` is a no-op requesting what's already true; there
is no way to request a blocking socket.

### The flag is read back

`setsockopt` returning 0 is the report; the socket still being blocking is
the damage. The host reads the option back, best-effort: only a readback
that succeeds and says "blocking" is treated as failure.

## Closing a connection without losing the reply

A server that accepts, sends, and closes in one call looks correct and
isn't:

1. The client has usually not sent anything yet when `accept` returns.
   Closing immediately meets the request with a closed socket.
2. Closing a socket with unread bytes sends an RST, not a FIN, telling the
   client to discard whatever it hasn't read, including the reply already
   on the wire.

Minimum sequence:

```
accept                    -> keep the connection, return
recv until CRLF CRLF     -> bytes must be taken, even if unused
send the reply            -> across as many ticks as it takes
Shutdown(handle, 1)       -> FIN, not RST
Close(handle)
```

`Shutdown` is v1.1 (0 read, 1 write, 2 both). `LastError` is v1.2: the
platform's own error number behind the most recent call, since `Accept`
answers `PLATFORM-ERROR` for the ordinary case of nothing pending. Print
the number; don't branch on it.

A connection needs a deadline; a client that connects and says nothing
would otherwise hold a slot for the session.

## Testing it

`tools/net_test` drives the ownership layer against a fake transport that
recycles file descriptors, lowest free first. It records which descriptor
bytes landed on, so the assertion is that nothing was written to the
recycled descriptor, paired with a positive control that the descriptor's
real owner can still write to it.

The `d_net` sample closes a handle and reuses it on purpose, so a boot log
shows the refusal happening.
