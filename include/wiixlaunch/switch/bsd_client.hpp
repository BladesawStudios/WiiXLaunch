#pragma once

// WiiXLaunch::Net::Bsd - a minimal client for the Switch's bsd:u socket service, spoken over raw IPC.
//
// Why raw IPC: the host's other nn:: uses are vetted by exact mangled name against the nnSdk the game ships
// (scripts/test_switch_module.py), and nothing proves a given title exports nn::socket. The service itself is
// the same on every title and on Ryujinx, so this talks to it directly: sm: -> bsd:u, then CMIF requests built
// in the thread's TLS message buffer. Only the calls wiixl.net needs exist: socket, bind, listen, accept,
// recv, send, shutdown, close, fcntl and setsockopt.
//
// Sockets are used non-blocking. A call returns the service's own ret (negative on failure) and keeps the
// errno from the last call in LastErrno(). Nothing here blocks except the IPC round trip itself.

#include "../platform.hpp"

#if WIIXL_SWITCH

#include <lib.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace WiiXLaunch::Net::Bsd {

// bsd:u command ids (switchbrew "Sockets services").
enum Cmd : uint32_t {
    kRegisterClient = 0, kStartMonitoring = 1, kSocket = 2, kRecv = 8, kSend = 10, kAccept = 12, kBind = 13,
    kListen = 18, kFcntl = 20, kSetSockOpt = 21, kShutdown = 22, kClose = 26,
};

constexpr int32_t kAfInet = 2, kSockStream = 1, kSolSocket = 0xFFFF, kSoReuseAddr = 4;
constexpr int32_t kFSetFl = 4, kONonBlock = 0x800;
constexpr uint32_t kTransferMemSize = 0x40000;

namespace impl {

// Page-aligned memory handed to the service as its socket buffers; lives for the process.
alignas(0x1000) inline uint8_t g_TransferMemory[kTransferMemSize];
inline Handle g_Sm = 0;
inline Handle g_Bsd = 0;
inline bool g_Tried = false;
inline int32_t g_Errno = 0;

struct Buffer {
    const void* data = nullptr;
    uint64_t size = 0;
};

// One request: raw in-data, up to one input (A) and one output (B) buffer, optional pid and copy handle.
struct Request {
    uint32_t command = 0;
    const void* in = nullptr;
    uint32_t inSize = 0;
    bool sendPid = false;
    Handle copyHandle = 0;
    Buffer send;
    Buffer recv;
    void* out = nullptr;
    uint32_t outSize = 0;
    Handle* moveOut = nullptr;
};

inline uint32_t BufferDescriptorWord(uint64_t address, uint64_t size, uint32_t* packed) {
    *packed = static_cast<uint32_t>(((address >> 36) & 0x3FFFFF) << 2 | ((size >> 32) & 0xF) << 24 |
                                    ((address >> 32) & 0xF) << 28);
    return static_cast<uint32_t>(address);
}

inline uint32_t* PutDescriptor(uint32_t* p, const Buffer& b) {
    uint32_t packed = 0;
    const uint32_t low = BufferDescriptorWord(reinterpret_cast<uint64_t>(b.data), b.size, &packed);
    p[0] = static_cast<uint32_t>(b.size);
    p[1] = low;
    p[2] = packed;
    return p + 3;
}

// Writes the request into the TLS buffer and sends it. Returns the service's result code.
inline Result Call(Handle session, const Request& r) {
    auto* msg = reinterpret_cast<uint32_t*>(armGetTls());
    const bool special = r.sendPid || r.copyHandle;
    const uint32_t sendCount = r.send.data ? 1 : 0, recvCount = r.recv.data ? 1 : 0;

    uint32_t* p = msg + 2;
    if (special) {
        *p++ = (r.sendPid ? 1u : 0u) | (r.copyHandle ? 1u << 1 : 0u);
        if (r.sendPid) { *p++ = 0; *p++ = 0; }
        if (r.copyHandle) *p++ = r.copyHandle;
    }
    if (sendCount) p = PutDescriptor(p, r.send);
    if (recvCount) p = PutDescriptor(p, r.recv);

    const uint32_t headerBytes = static_cast<uint32_t>(reinterpret_cast<uint8_t*>(p) - reinterpret_cast<uint8_t*>(msg));
    const uint32_t pad = ((headerBytes + 15) & ~15u) - headerBytes;
    p = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(p) + pad);
    const uint32_t rawBytes = 16 + r.inSize;

    msg[0] = 4u | sendCount << 20 | recvCount << 24;
    msg[1] = ((pad + rawBytes + 3) / 4) | (special ? 1u << 31 : 0u);
    p[0] = 0x49434653;  // 'SFCI'
    p[1] = 0;
    p[2] = r.command;
    p[3] = 0;
    if (r.inSize) std::memcpy(p + 4, r.in, r.inSize);

    const Result rc = svcSendSyncRequest(session);
    if (rc != 0) return rc;

    // Response: header, optional handle block, descriptors we did not ask for, then 'SFCO'.
    const uint32_t h0 = msg[0], h1 = msg[1];
    uint32_t* q = msg + 2;
    uint32_t copyHandles = 0, moveHandles = 0;
    if (h1 >> 31) {
        const uint32_t sp = *q++;
        if (sp & 1) q += 2;
        copyHandles = (sp >> 1) & 0xF;
        moveHandles = (sp >> 5) & 0xF;
        if (moveHandles && r.moveOut) *r.moveOut = q[copyHandles];
        q += copyHandles + moveHandles;
    }
    q += ((h0 >> 16) & 0xF) * 2 + ((h0 >> 20) & 0xF) * 3 + ((h0 >> 24) & 0xF) * 3 + ((h0 >> 28) & 0xF) * 3;
    const uint32_t offset = static_cast<uint32_t>(reinterpret_cast<uint8_t*>(q) - reinterpret_cast<uint8_t*>(msg));
    q = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(msg) + ((offset + 15) & ~15u));
    if (q[0] != 0x4F434653) return 0xFFFF;
    if (q[2] == 0 && r.out && r.outSize) std::memcpy(r.out, q + 4, r.outSize);
    return q[2];
}

// {ret, errno} reply shared by most bsd commands.
struct Reply {
    int32_t ret;
    int32_t err;
};

inline int32_t Simple(uint32_t command, const void* in, uint32_t inSize, Buffer send = {}, Buffer recv = {}) {
    Reply reply = {-1, 0};
    Request r;
    r.command = command; r.in = in; r.inSize = inSize; r.send = send; r.recv = recv;
    r.out = &reply; r.outSize = sizeof(reply);
    if (Call(g_Bsd, r) != 0) { g_Errno = -1; return -1; }
    g_Errno = reply.err;
    return reply.ret;
}

inline bool ConnectService() {
    if (g_Tried) return g_Bsd != 0;
    g_Tried = true;

    if (svcConnectToNamedPort(&g_Sm, "sm:") != 0) return false;
    const uint64_t zero = 0;
    Request init;
    init.command = 0; init.in = &zero; init.inSize = 8; init.sendPid = true;
    if (Call(g_Sm, init) != 0) return false;

    const char name[8] = {'b', 's', 'd', ':', 'u', 0, 0, 0};
    Request get;
    get.command = 1; get.in = name; get.inSize = 8; get.moveOut = &g_Bsd;
    if (Call(g_Sm, get) != 0 || g_Bsd == 0) return false;

    Handle tmem = 0;
    if (svcCreateTransferMemory(&tmem, g_TransferMemory, kTransferMemSize, 0) != 0) return false;
    struct Register {
        uint32_t version, tcpTx, tcpRx, tcpTxMax, tcpRxMax, udpTx, udpRx, sbEfficiency;
        uint64_t pid, size;
    } reg = {1, 0x4000, 0x4000, 0x4000, 0x4000, 0x2400, 0xA500, 1, 0, kTransferMemSize};
    Request client;
    client.command = kRegisterClient; client.in = &reg; client.inSize = sizeof(reg);
    client.sendPid = true; client.copyHandle = tmem;
    if (Call(g_Bsd, client) != 0) return false;

    const uint64_t pid = 0;
    Request monitor;
    monitor.command = kStartMonitoring; monitor.in = &pid; monitor.inSize = 8; monitor.sendPid = true;
    return Call(g_Bsd, monitor) == 0;
}

}  // namespace impl

inline bool Available() { return impl::ConnectService(); }
inline int32_t LastErrno() { return impl::g_Errno; }

inline int32_t Socket(int32_t domain, int32_t type, int32_t protocol) {
    const int32_t in[3] = {domain, type, protocol};
    return impl::Simple(Cmd::kSocket, in, sizeof(in));
}

inline int32_t Bind(int32_t fd, uint16_t port) {
    const uint8_t addr[16] = {16, kAfInet, uint8_t(port >> 8), uint8_t(port), 0};  // sockaddr_in, any address
    return impl::Simple(Cmd::kBind, &fd, sizeof(fd), {addr, sizeof(addr)});
}

inline int32_t Listen(int32_t fd, int32_t backlog) {
    const int32_t in[2] = {fd, backlog};
    return impl::Simple(Cmd::kListen, in, sizeof(in));
}

inline int32_t Accept(int32_t fd) {
    uint8_t peer[16] = {};
    struct AcceptReply { int32_t ret, err; uint32_t addrLen; } reply = {-1, 0, 0};
    impl::Request r;
    r.command = Cmd::kAccept; r.in = &fd; r.inSize = sizeof(fd); r.recv = {peer, sizeof(peer)};
    r.out = &reply; r.outSize = sizeof(reply);
    if (impl::Call(impl::g_Bsd, r) != 0) { impl::g_Errno = -1; return -1; }
    impl::g_Errno = reply.err;
    return reply.ret;
}

// IPC buffers must sit in memory the kernel will map for a request. The caller's buffer may not (the loader's arena is code-type
// memory, which Ryujinx refuses with InvalidCurrentMemory), so data is bounced through the stack, one chunk per call.
constexpr uint32_t kBounce = 4096;

inline int32_t Recv(int32_t fd, void* buffer, uint32_t length) {
    uint8_t bounce[kBounce];
    const uint32_t chunk = length < kBounce ? length : kBounce;
    const int32_t in[2] = {fd, 0};
    const int32_t got = impl::Simple(Cmd::kRecv, in, sizeof(in), {}, {bounce, chunk});
    if (got > 0) std::memcpy(buffer, bounce, static_cast<uint32_t>(got));
    return got;
}

inline int32_t Send(int32_t fd, const void* buffer, uint32_t length) {
    uint8_t bounce[kBounce];
    const uint32_t chunk = length < kBounce ? length : kBounce;
    std::memcpy(bounce, buffer, chunk);
    const int32_t in[2] = {fd, 0};
    return impl::Simple(Cmd::kSend, in, sizeof(in), {bounce, chunk});
}

inline int32_t Shutdown(int32_t fd, int32_t how) {
    const int32_t in[2] = {fd, how};
    return impl::Simple(Cmd::kShutdown, in, sizeof(in));
}

inline int32_t Close(int32_t fd) { return impl::Simple(Cmd::kClose, &fd, sizeof(fd)); }

inline int32_t SetNonBlocking(int32_t fd) {
    const int32_t in[3] = {fd, kFSetFl, kONonBlock};
    return impl::Simple(Cmd::kFcntl, in, sizeof(in));
}

inline int32_t SetOptInt(int32_t fd, int32_t level, int32_t option, int32_t value) {
    const int32_t in[3] = {fd, level, option};
    return impl::Simple(Cmd::kSetSockOpt, in, sizeof(in), {&value, sizeof(value)});
}

}  // namespace WiiXLaunch::Net::Bsd

#endif  // WIIXL_SWITCH
