#pragma once

#include "wiixlaunch/platform.hpp"
#include "wiixlaunch/offsets.hpp"
#include "wiixlaunch/context.hpp"
#include "wiixlaunch/patch.hpp"
#include "wiixlaunch/hook.hpp"
#include "wiixlaunch/call.hpp"
#include "wiixlaunch/time.hpp"
#include "wiixlaunch/debug_log.hpp"

// Required, not optional: deploy.py patches each header's Cemu shim-table
// offset global, and can only patch a symbol actually present in the ELF.
// Not including one of these emits no symbol, so its shim table cannot be
// reached from the codecave.
#include "wiixlaunch/fs.hpp"
#include "wiixlaunch/mem.hpp"
