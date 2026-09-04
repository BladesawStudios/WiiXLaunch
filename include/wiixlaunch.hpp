#pragma once

#include "wiixlaunch/platform.hpp"
#include "wiixlaunch/offsets.hpp"
#include "wiixlaunch/context.hpp"
#include "wiixlaunch/patch.hpp"
#include "wiixlaunch/hook.hpp"
#include "wiixlaunch/call.hpp"
#include "wiixlaunch/time.hpp"
#include "wiixlaunch/debug_log.hpp"

// Not optional, despite being usable standalone. On Cemu each of these
// carries the `inline uint32_t g_Cemu*ShimTableOffset` global that
// scripts/deploy.py patches with its shim table's offset - and deploy.py
// can only patch a symbol that is actually present in the ELF. A header
// nothing includes emits no symbol, so its table gets spliced into the
// codecave with no way to reach it. That is how the memory shims shipped
// unreachable for a while.
//
// Modules are always external and the loader reads them off the
// filesystem, so a host that does not emit the FS shim table is a broken
// host rather than a configuration. mem.hpp is here for the same reason:
// the host owns the heap, and it allocates through those shims.
#include "wiixlaunch/fs.hpp"
#include "wiixlaunch/mem.hpp"
