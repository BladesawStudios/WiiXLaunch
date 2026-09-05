#pragma once

// wiixl.net v1 - TCP, owned.
//
// A SEPARATE SURFACE FROM wiixl.core, on purpose. Not everything WiiXLaunch runs
// on has sockets: Switch has no implementation here at all. If these entries
// lived in wiixl.core, a mod that needs networking would load happily on Switch
// and then discover at its first send that nothing works - which is the "does
// nothing quietly" failure, in the place where it is hardest to report.
//
// As its own surface it is simply not registered on a host that cannot back it,
// and a mod declaring `wiixl.net` required is refused AT LOAD, BY NAME, with a
// line saying which surface was missing. The mod author learns this from a boot
// log instead of from a bug report.
//
// Note the second distinction, which the registry cannot express and the results
// can: NOT REGISTERED means "this platform has no sockets" (Switch), while
// Unavailable from a call means "this platform has sockets and they could not be
// reached here" - on Cemu, a title whose process has no nsysnet. Those are
// different problems and they are answered at different times.
//
// Every entry obeys the ABI rules in surface.hpp: no structs by value, no
// varargs, opaque handles and primitives only. A Handle is an opaque uint32 the
// host issues; a mod must never treat it as a descriptor, and cannot usefully
// try, because it is not one.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/loader/surface.hpp>
#include <wiixlaunch/net.hpp>
#include <wiixlaunch/mod_context.hpp>

#include <cstdint>

namespace WiiXLaunch::NetSurface {

constexpr const char* kSurfaceName = "wiixl.net";
constexpr uint16_t kVersionMajor = 1;
constexpr uint16_t kVersionMinor = 0;

namespace impl {

// Can a socket be opened right now? 0 means no, and Open's result says why.
extern "C" inline uint32_t NetAvailable() {
    if constexpr (!Net::Transport::Supported) return 0;
    return Net::Transport::Available() ? 1u : 0u;
}

// The handle comes back through the out-parameter and the RESULT comes back as
// the return value, rather than "0 means it failed somehow".
//
// A mod that cannot tell ModuleQuota from Unavailable cannot behave differently
// about them, and it should: one means "you are leaking", the other means "this
// host has no network". Returning only a handle would have flattened both into
// a zero.
extern "C" inline uint32_t NetOpen(uint32_t* outHandle) {
    Net::Handle h = Net::kInvalidHandle;
    const Net::Result r = Net::Open(&h);
    if (outHandle) *outHandle = h;
    return static_cast<uint32_t>(r);
}

extern "C" inline uint32_t NetSetNonBlocking(uint32_t handle) {
    return static_cast<uint32_t>(Net::SetNonBlocking(handle));
}

extern "C" inline uint32_t NetSetReuseAddr(uint32_t handle) {
    return static_cast<uint32_t>(Net::SetReuseAddr(handle));
}

extern "C" inline uint32_t NetBind(uint32_t handle, uint32_t port) {
    if (port > 0xFFFFu) return static_cast<uint32_t>(Net::Result::BadArgument);
    return static_cast<uint32_t>(Net::Bind(handle, static_cast<uint16_t>(port)));
}

extern "C" inline uint32_t NetListen(uint32_t handle, uint32_t backlog) {
    return static_cast<uint32_t>(Net::Listen(handle, backlog));
}

extern "C" inline uint32_t NetAccept(uint32_t listener, uint32_t* outHandle) {
    Net::Handle h = Net::kInvalidHandle;
    const Net::Result r = Net::Accept(listener, &h);
    if (outHandle) *outHandle = h;
    return static_cast<uint32_t>(r);
}

extern "C" inline int32_t NetRecv(uint32_t handle, void* buffer, uint32_t maxSize) {
    return Net::Recv(handle, buffer, maxSize);
}

extern "C" inline int32_t NetSend(uint32_t handle, const void* buffer, uint32_t size) {
    return Net::Send(handle, buffer, size);
}

extern "C" inline uint32_t NetClose(uint32_t handle) {
    return static_cast<uint32_t>(Net::Close(handle));
}

// Everything this module holds, released. Returns how many were closed.
//
// The owner is the calling module, taken from the host - a mod cannot close
// another mod's sockets by naming them, for the same reason it cannot register a
// hook under another mod's name.
extern "C" inline uint32_t NetCloseAll() {
    const char* owner = ModContext::Current();
    if (!owner || owner[0] == '\0') return 0;
    return Net::CloseAllFor(owner);
}

extern "C" inline uint32_t NetLocalIp(uint32_t handle) {
    return Net::LocalIp(handle);
}

// How many sockets this module currently holds, and how many it may hold.
//
// Both, because a mod that only knows the cap cannot tell how close it is, and
// one that only knows its count cannot tell whether that is a lot.
extern "C" inline uint32_t NetHeld() {
    const char* owner = ModContext::Current();
    return owner ? Net::CountFor(owner) : 0u;
}

extern "C" inline uint32_t NetQuota() {
    return Net::kMaxPerModule;
}

// The host's own name for a result code, so a mod can log a refusal without
// keeping its own copy of this enum - a copy that would silently stop matching
// the day a value is added.
extern "C" inline const char* NetResultName(uint32_t result) {
    if (result > static_cast<uint32_t>(Net::Result::PlatformError)) return "?";
    return Net::ResultName(static_cast<Net::Result>(result));
}

// --- the table -------------------------------------------------------------
//
// APPEND ONLY, same rule as wiixl.core: adding an entry bumps the minor,
// changing or removing one bumps the major.
inline const WiiXLaunch::Surface::Symbol kSymbols[] = {
    WIIXL_SURFACE_SYMBOL("Available",      &NetAvailable),
    WIIXL_SURFACE_SYMBOL("Open",           &NetOpen),
    WIIXL_SURFACE_SYMBOL("SetNonBlocking", &NetSetNonBlocking),
    WIIXL_SURFACE_SYMBOL("SetReuseAddr",   &NetSetReuseAddr),
    WIIXL_SURFACE_SYMBOL("Bind",           &NetBind),
    WIIXL_SURFACE_SYMBOL("Listen",         &NetListen),
    WIIXL_SURFACE_SYMBOL("Accept",         &NetAccept),
    WIIXL_SURFACE_SYMBOL("Recv",           &NetRecv),
    WIIXL_SURFACE_SYMBOL("Send",           &NetSend),
    WIIXL_SURFACE_SYMBOL("Close",          &NetClose),
    WIIXL_SURFACE_SYMBOL("CloseAll",       &NetCloseAll),
    WIIXL_SURFACE_SYMBOL("LocalIp",        &NetLocalIp),
    WIIXL_SURFACE_SYMBOL("Held",           &NetHeld),
    WIIXL_SURFACE_SYMBOL("Quota",          &NetQuota),
    WIIXL_SURFACE_SYMBOL("ResultName",     &NetResultName),
};

} // namespace impl

// Registers wiixl.net, or deliberately does not.
//
// Returns false on a platform with no socket implementation, and says so once.
// That silence in the registry is the mechanism: Surface::Resolve refuses the
// mod by name, at load, before its entry runs.
inline bool Register() {
    if constexpr (!Net::Transport::Supported) {
        WIIXL_LOG("Surface: wiixl.net NOT registered - this platform has no socket "
                  "implementation. A mod requiring it will be refused by name.");
        return false;
    }

    WiiXLaunch::Surface::Registration reg{};
    reg.name = kSurfaceName;
    reg.versionMajor = kVersionMajor;
    reg.versionMinor = kVersionMinor;
    reg.symbols = impl::kSymbols;
    reg.symbolCount = static_cast<uint32_t>(sizeof(impl::kSymbols) / sizeof(impl::kSymbols[0]));
    return WiiXLaunch::Surface::Register(reg);
}

} // namespace WiiXLaunch::NetSurface
