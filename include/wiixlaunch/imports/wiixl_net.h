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
//
// The comments are the SURFACE's own, carried across - they say why a
// symbol behaves as it does, which is the half a signature cannot.
#pragma once

#include <cstdint>

extern "C" {

// Can a socket be opened right now? 0 means no, and Open's result says why.
extern uint32_t wiixl_import__wiixl_net__Available(void);

// The handle comes back through the out-parameter and the RESULT comes back as
// the return value, rather than "0 means it failed somehow".
//
// A mod that cannot tell ModuleQuota from Unavailable cannot behave differently
// about them, and it should: one means "you are leaking", the other means "this
// host has no network". Returning only a handle would have flattened both into
// a zero.
extern uint32_t wiixl_import__wiixl_net__Open(uint32_t* outHandle);
extern uint32_t wiixl_import__wiixl_net__SetNonBlocking(uint32_t handle);
extern uint32_t wiixl_import__wiixl_net__SetReuseAddr(uint32_t handle);
extern uint32_t wiixl_import__wiixl_net__Bind(uint32_t handle, uint32_t port);
extern uint32_t wiixl_import__wiixl_net__Listen(uint32_t handle, uint32_t backlog);
extern uint32_t wiixl_import__wiixl_net__Accept(uint32_t listener, uint32_t* outHandle);
extern int32_t wiixl_import__wiixl_net__Recv(uint32_t handle, void* buffer, uint32_t maxSize);
extern int32_t wiixl_import__wiixl_net__Send(uint32_t handle, const void* buffer, uint32_t size);
extern uint32_t wiixl_import__wiixl_net__Close(uint32_t handle);

// Everything this module holds, released. Returns how many were closed.
//
// The owner is the calling module, taken from the host - a mod cannot close
// another mod's sockets by naming them, for the same reason it cannot register a
// hook under another mod's name.
extern uint32_t wiixl_import__wiixl_net__CloseAll(void);
extern uint32_t wiixl_import__wiixl_net__LocalIp(uint32_t handle);

// How many sockets this module currently holds, and how many it may hold.
//
// Both, because a mod that only knows the cap cannot tell how close it is, and
// one that only knows its count cannot tell whether that is a lot.
extern uint32_t wiixl_import__wiixl_net__Held(void);
extern uint32_t wiixl_import__wiixl_net__Quota(void);

// The host's own name for a result code, so a mod can log a refusal without
// keeping its own copy of this enum - a copy that would silently stop matching
// the day a value is added.
extern const char* wiixl_import__wiixl_net__ResultName(uint32_t result);

// how: 0 read, 1 write, 2 both.
//
// A server MUST NOT send a reply and then Close while the client's request is
// still unread - TCP answers that with an RST and the client loses the reply.
// Drain what the peer sent, then Shutdown(1), then Close.
extern uint32_t wiixl_import__wiixl_net__Shutdown(uint32_t handle, uint32_t how);

// The platform's own error number for the most recent transport call.
//
// PLATFORM-ERROR is where every reason the host cannot name ends up, and until
// now the number behind it went only to the host log. Accept in particular
// returns it for the ORDINARY case - a non-blocking listener with nothing
// pending - so a mod polling once a frame sees a failure name on almost every
// frame and cannot tell it from a listener that has genuinely broken.
//
// This is the platform's number, not the host's: it means whatever nsysnet or
// the Wii U socket library means by it, and a mod should print it rather than
// branch on it.
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
// address - the module fails to relocate. See docs/framework/modules.md.
#define WXL_USE_wiixl_net(sym) \
    inline decltype(&wiixl_import__wiixl_net__##sym) volatile sym = \
        &wiixl_import__wiixl_net__##sym
