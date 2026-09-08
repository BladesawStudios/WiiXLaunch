// GENERATED FILE - do not edit.
// Regenerate with: python scripts/gen_imports.py
//
// wiixl.net v1.2, 17 symbol(s), from the surface's own table.
//
// Declaring a symbol here costs nothing. BINDING one is what makes it an
// import, and that is opt-in:
//
//     namespace S { WXL_USE_wiixl_net(Available); }
//     S::Available(...);
//
// so a mod that uses two symbols imports two, not all 17.
#pragma once

#include <cstdint>

extern "C" {
extern uint32_t wiixl_import__wiixl_net__Available(void);
extern uint32_t wiixl_import__wiixl_net__Open(uint32_t* outHandle);
extern uint32_t wiixl_import__wiixl_net__SetNonBlocking(uint32_t handle);
extern uint32_t wiixl_import__wiixl_net__SetReuseAddr(uint32_t handle);
extern uint32_t wiixl_import__wiixl_net__Bind(uint32_t handle, uint32_t port);
extern uint32_t wiixl_import__wiixl_net__Listen(uint32_t handle, uint32_t backlog);
extern uint32_t wiixl_import__wiixl_net__Accept(uint32_t listener, uint32_t* outHandle);
extern int32_t wiixl_import__wiixl_net__Recv(uint32_t handle, void* buffer, uint32_t maxSize);
extern int32_t wiixl_import__wiixl_net__Send(uint32_t handle, const void* buffer, uint32_t size);
extern uint32_t wiixl_import__wiixl_net__Close(uint32_t handle);
extern uint32_t wiixl_import__wiixl_net__CloseAll(void);
extern uint32_t wiixl_import__wiixl_net__LocalIp(uint32_t handle);
extern uint32_t wiixl_import__wiixl_net__Held(void);
extern uint32_t wiixl_import__wiixl_net__Quota(void);
extern const char* wiixl_import__wiixl_net__ResultName(uint32_t result);
extern uint32_t wiixl_import__wiixl_net__Shutdown(uint32_t handle, uint32_t how);
extern int32_t wiixl_import__wiixl_net__LastError(void);
}

// The version this header was generated from. A mod that needs a symbol
// added in a later minor should pass --require wiixl.net@1.2 when packing,
// so an older host refuses it by name instead of resolving short.
namespace wiixl_surface_wiixl_net {
inline constexpr unsigned kVersionMajor = 1;
inline constexpr unsigned kVersionMinor = 2;
}

// VOLATILE is not style. Without it the compiler folds the indirect call
// into a direct branch and emits a relocation kind that cannot reach a host
// address - the module fails to relocate. See docs/modules.md.
#define WXL_USE_wiixl_net(sym) \
    inline decltype(&wiixl_import__wiixl_net__##sym) volatile sym = \
        &wiixl_import__wiixl_net__##sym
