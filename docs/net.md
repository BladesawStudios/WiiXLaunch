# wiixl.net — sockets that belong to somebody

`wiixl.net` is the first surface where a mod holds a resource with a
**lifetime**. Hooks and patches are install-once and the host owns the result
forever. A socket is not like that: a mod that opens one and never closes it
holds a system handle for the whole session, and *"the game runs out of sockets
after an hour"* is a report with no owner attached — the exact failure class the
rest of this framework exists to eliminate.

So the host tracks them, and hands out **its own handles** rather than raw
descriptors.

## Why not raw descriptors

Not tidiness. A raw descriptor turns use-after-close into silent cross-mod
corruption:

1. mod A closes fd 5
2. mod B opens a socket and the OS hands it fd 5 — descriptors get reused, that
   is what they do
3. mod A, still holding a stale `5`, sends its response down **mod B's
   connection**

Nothing crashes. B's client receives A's data, intermittently, and the bug reads
like a host bug. A `Handle` carries a **generation** alongside the slot index,
the generation moves every time the slot is reused, and A's stale handle comes
back `STALE-HANDLE` instead of reaching B's socket.

```
bits  0..15   slot index + 1     (so 0 is never a valid handle)
bits 16..31   generation         (moves on every acquisition)
```

The generation is 16 bits, so a slot would have to be recycled 65536 times
before a stale handle could alias a live one. That is a **bound**, not an
impossibility.

## Why per-module quotas — this is fault isolation

Without per-module attribution the only cap that can exist is a global one. A
global cap means the mod that **leaks** exhausts the pool while the mods that get
**refused** are whichever ones happened to ask next: the failure lands on
innocent modules and names none of them.

With a per-module cap (`kMaxPerModule = 8`, out of `kMaxSockets = 24`) the leak
is contained to its owner. The leaking mod hits its own ceiling, is refused by
name, and every other mod keeps working. `tools/net_test` asserts exactly that —
that a second module is *unaffected* — because "the cap works" would be equally
true of a global cap that punishes everyone.

An **accepted** connection goes through the quota too. A server that accepts
every frame and forgets to close is the real leak vector, not `Open`. And when
an accept is refused, the host **closes** the descriptor the platform already
handed over rather than leaving it untracked — the one thing worse than a
tracked leak is an untracked one.

## What tracking does not buy

Tracking buys attribution and containment. It does **not** buy recovery, and
three cases have to be told apart:

| Case | What happens |
|---|---|
| A leak within a session | **Contained.** The quota stops it at 8 and `LogState` names who holds what. |
| A hang inside a tick | **Attribution only.** Nothing reclaims anything, because nothing runs — the game is frozen. What you get is Tick's in-flight record naming the module and this module's socket count beside it. |
| Shutdown | **The process owns the handles.** There is no module-unload path in this system, so a host-side "close everything on the way out" would be a function with no caller. |

That last row is deliberate. `CloseAllFor` exists and its only caller today is a
module closing its **own** sockets through the surface. When module unload or a
tick watchdog exists, that is what will call it — until then this code does not
pretend the host reclaims anything on its own.

## Two different ways there is no network

These are separate questions answered at separate times, and they must not
collapse into one value:

- **The surface is not registered** — this platform has no socket implementation
  at all. Switch. A mod that imports any `wiixl.net` symbol has `wiixl.net` as a
  required surface, so it is refused **at load, by name**, and never runs.
- **`UNAVAILABLE` from a call** — this platform has sockets and they could not be
  reached here. On Cemu, a title whose process has no network stack.

## Reaching nsysnet on Cemu

Every other shim table in `src/cemu/` resolves `import.coreinit.<Name>`, which
works because coreinit is imported by every title that exists. **nsysnet is
not** — BotW v208's import table has 445 entries and not one is a socket call.

And Cemu fails the **entire graphic pack** when a patch import cannot be
resolved. A static `import.nsysnet.socket` in base would therefore mean that on
any setup where nsysnet is not in the title's process, WiiXLaunch does not load
*at all* — not the network surface, the whole framework, every mod, with the
pack simply not applying and nothing in a log to say why. That is the worst
failure shape available to this project.

So nsysnet is resolved **at runtime** through coreinit's dynamic loader, which
BotW *does* import (`OSDynLoad_Acquire` / `FindExport` / `Release`,
EXTERNAL:12a–12c). The three shims in `src/cemu/cemu_dynload.asm` are coreinit
calls like every other table's, so they always resolve, and "nsysnet is not
there" becomes a value the log names on a host that otherwise works normally.

All ten exports must resolve or none are used: a partially resolved table would
open sockets it could not close.

This mechanism is not specific to sockets. It is how base reaches **any** library
the game does not itself import, and the next surface that needs one should use
it rather than adding a static import and hoping.

## Testing it

`tools/net_test` cannot open a socket, and that is not what needs testing. It
drives the ownership layer against a fake transport that **recycles file
descriptors, lowest free first**, exactly like a real OS — because a
use-after-close is only dangerous once the number has been handed to somebody
else, and no real platform will do that on cue.

The fake records *which descriptor* bytes landed on, so the assertion is not "it
returned an error" but "**and nothing was written to the recycled descriptor**".
An implementation that returned an error and still wrote would pass the first and
fail the second.

Every refusal is paired with the same call succeeding. Without those positive
controls, an `Open` that returned `NO-MODULE` unconditionally would leave half
the assertions green.

The `d_net` sample closes a handle and then uses it **on purpose**, so a boot log
shows the refusal happening rather than a comment claiming it would.
