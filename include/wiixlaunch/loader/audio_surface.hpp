#pragma once

// wiixl.audio v1 - a mod's own sounds.
//
// The game's sound events (botw.sound) can only play what the game already has. This plays a mod's own: raw PCM read from
// the mod's directory and sent straight to the hardware mixer on an AX voice, alongside whatever the game is playing. No
// game audio is replaced.
//
// The file is what a mod's build turns its audio into, since nothing here decodes MP3 or anything else:
//
//     0x00  'WXAU'
//     0x04  sample rate, Hz          (big-endian uint32, like everything after it)
//     0x08  sample count
//     0x0C  channels                 (1; mono is all this plays)
//     0x10  int16 samples, big-endian
//
// AX is sndcore2.rpl, which the game uses but does not import by name the way the code cave would need, so on Cemu it is
// reached through the dynamic loader exactly as wiixl.net reaches nsysnet (cemu_dynload.hpp). Cemu only, for now: the
// surface is not registered elsewhere, and a mod requiring it is refused by name at load.
//
// Every entry obeys the ABI rules in surface.hpp: no structs by value, opaque handles and primitives only.

#include <wiixlaunch/platform.hpp>
#include <wiixlaunch/loader/surface.hpp>
#include <wiixlaunch/debug_log.hpp>
#include <wiixlaunch/mod_context.hpp>
#include <wiixlaunch/mod_fs.hpp>
#include <wiixlaunch/fs.hpp>
#include <wiixlaunch/mem.hpp>

#if WIIXL_CEMU
#include <wiixlaunch/cemu/cemu_dynload.hpp>
#endif

#include <cstdint>
#include <cstring>

namespace WiiXLaunch::AudioSurface {

constexpr const char* kName = "wiixl.audio";
constexpr uint16_t kVersionMajor = 1;
constexpr uint16_t kVersionMinor = 0;

#if WIIXL_CEMU
constexpr bool kSupported = true;
#else
constexpr bool kSupported = false;
#endif

namespace impl {

constexpr uint32_t kMagic = 0x57584155u;          // 'WXAU'
constexpr uint32_t kHeaderBytes = 16;
constexpr uint32_t kMaxSounds = 32;
constexpr uint32_t kMaxPlaying = 8;               // voices this host holds at once; the oldest is cut for a ninth
constexpr uint32_t kMaxFileBytes = 4u << 20;
// Above anything the game asks for, so the game's own voices never take one of these mid-sound. When every voice is
// busy AX gives this one a voice of lower priority instead - one game sound cut short, for a moment.
constexpr uint32_t kVoicePriority = 31;

// sndcore2's own types, as wut's <sndcore2/voice.h> lays them out.
struct AxVoiceOffsets {
    uint16_t dataType;        // 10 = LPCM16
    uint16_t loopingEnabled;
    uint32_t loopOffset;
    uint32_t endOffset;       // the last sample, inclusive
    uint32_t currentOffset;
    const void* data;
};
// sndcore2 is the Wii U's, so its layout only holds where pointers are 32-bit.
// Elsewhere the surface is unsupported and this struct is never handed to AX.
#if WIIXL_CEMU || WIIXL_WIIU
static_assert(sizeof(AxVoiceOffsets) == 0x14, "AXVoiceOffsets");
#endif

struct AxMix {                // AXVoiceDeviceMixData: one per output channel, four buses each
    struct { uint16_t volume; int16_t delta; } bus[4];
};
static_assert(sizeof(AxMix) == 0x10, "AXVoiceDeviceMixData");

struct AxVe {
    uint16_t volume;          // 0x8000 = unity
    int16_t delta;
};

constexpr uint16_t kFormatLpcm16 = 10;
constexpr uint32_t kSrcLinear = 1;
constexpr uint32_t kStatePlaying = 1;
constexpr uint32_t kDeviceTv = 0;
constexpr uint32_t kDeviceDrc = 1;
constexpr uint32_t kMixChannels = 6;              // the TV's; the GamePad reads the first four

using AxVoice = void*;
using FnAcquireVoice = AxVoice (*)(uint32_t priority, void (*callback)(void*), void* userContext);
using FnFreeVoice = void (*)(AxVoice);
using FnVoiceBegin = int32_t (*)(AxVoice);
using FnVoiceEnd = int32_t (*)(AxVoice);
using FnSetVoiceType = void (*)(AxVoice, uint32_t);
using FnSetVoiceVe = void (*)(AxVoice, AxVe*);
using FnSetVoiceDeviceMix = int32_t (*)(AxVoice, uint32_t type, uint32_t id, AxMix* mix);
using FnSetVoiceSrcType = void (*)(AxVoice, uint32_t);
using FnSetVoiceSrcRatio = int32_t (*)(AxVoice, float);
using FnSetVoiceOffsets = void (*)(AxVoice, AxVoiceOffsets*);
using FnSetVoiceState = void (*)(AxVoice, uint32_t);
using FnIsVoiceRunning = int32_t (*)(AxVoice);
using FnGetInputSamplesPerSec = uint32_t (*)();
using FnIsInit = int32_t (*)();

struct AxTable {
    FnAcquireVoice acquireVoice;
    FnFreeVoice freeVoice;
    FnVoiceBegin voiceBegin;
    FnVoiceEnd voiceEnd;
    FnSetVoiceType setVoiceType;
    FnSetVoiceVe setVoiceVe;
    FnSetVoiceDeviceMix setVoiceDeviceMix;
    FnSetVoiceSrcType setVoiceSrcType;
    FnSetVoiceSrcRatio setVoiceSrcRatio;
    FnSetVoiceOffsets setVoiceOffsets;
    FnSetVoiceState setVoiceState;
    FnIsVoiceRunning isVoiceRunning;
    FnGetInputSamplesPerSec getInputSamplesPerSec;
    FnIsInit isInit;
};

inline AxTable g_Ax = {};
inline bool g_AxResolved = false;
inline bool g_AxComplete = false;

struct Sound {
    const int16_t* samples = nullptr;
    uint32_t count = 0;
    uint32_t rate = 0;
    char owner[32] = {};
};

struct Playing {
    AxVoice voice = nullptr;
    uint32_t started = 0;     // play order, for cutting the oldest
};

inline Sound g_Sounds[kMaxSounds];
inline uint32_t g_SoundCount = 0;
inline Playing g_Playing[kMaxPlaying];
inline uint32_t g_PlayCounter = 0;

#if WIIXL_CEMU
// sndcore2, resolved once. Every export must resolve: a voice acquired with no way to free it would leak for good.
inline const AxTable* Ax() {
    if (g_AxResolved) return g_AxComplete ? &g_Ax : nullptr;
    g_AxResolved = true;

    if (!Backend::CemuDynLoadAvailable()) {
        WIIXL_LOG("Audio: the coreinit dynamic-loader shims are not in this payload, so sndcore2 cannot be reached");
        return nullptr;
    }
    Backend::OSDynLoadModule mod = Backend::CemuAcquireRpl("sndcore2.rpl");
    if (!mod) {
        WIIXL_LOG("Audio: OSDynLoad_Acquire(sndcore2.rpl) failed - no sound");
        return nullptr;
    }

    void** slots[] = {
        reinterpret_cast<void**>(&g_Ax.acquireVoice),      reinterpret_cast<void**>(&g_Ax.freeVoice),
        reinterpret_cast<void**>(&g_Ax.voiceBegin),        reinterpret_cast<void**>(&g_Ax.voiceEnd),
        reinterpret_cast<void**>(&g_Ax.setVoiceType),      reinterpret_cast<void**>(&g_Ax.setVoiceVe),
        reinterpret_cast<void**>(&g_Ax.setVoiceDeviceMix), reinterpret_cast<void**>(&g_Ax.setVoiceSrcType),
        reinterpret_cast<void**>(&g_Ax.setVoiceSrcRatio),  reinterpret_cast<void**>(&g_Ax.setVoiceOffsets),
        reinterpret_cast<void**>(&g_Ax.setVoiceState),     reinterpret_cast<void**>(&g_Ax.isVoiceRunning),
        reinterpret_cast<void**>(&g_Ax.getInputSamplesPerSec), reinterpret_cast<void**>(&g_Ax.isInit),
    };
    const char* names[] = {
        "AXAcquireVoice", "AXFreeVoice", "AXVoiceBegin", "AXVoiceEnd", "AXSetVoiceType", "AXSetVoiceVe",
        "AXSetVoiceDeviceMix", "AXSetVoiceSrcType", "AXSetVoiceSrcRatio", "AXSetVoiceOffsets", "AXSetVoiceState",
        "AXIsVoiceRunning", "AXGetInputSamplesPerSec", "AXIsInit",
    };
    constexpr uint32_t kCount = sizeof(names) / sizeof(names[0]);
    for (uint32_t i = 0; i < kCount; ++i) {
        void* addr = Backend::CemuFindRplExport(mod, names[i]);
        if (!addr) {
            WIIXL_LOG("Audio: sndcore2.rpl has no export '%s' - no sound", names[i]);
            return nullptr;
        }
        *slots[i] = addr;
    }
    g_AxComplete = true;
    WIIXL_LOG("Audio: sndcore2.rpl resolved through the dynamic loader, %u exports", kCount);
    return &g_Ax;
}
#else
inline const AxTable* Ax() { return nullptr; }
#endif

// AX calls this when it takes a voice back. The slot forgets it, so it is never freed twice.
extern "C" inline void AudioVoiceDropped(void* voice) {
    for (auto& p : g_Playing) {
        if (p.voice == voice) p.voice = nullptr;
    }
}

// Frees every voice that has played to its end, and returns a slot to play in: a free one, or the oldest, cut.
inline Playing* TakeSlot(const AxTable* ax) {
    Playing* free = nullptr;
    Playing* oldest = nullptr;
    for (auto& p : g_Playing) {
        if (p.voice && !ax->isVoiceRunning(p.voice)) {
            ax->freeVoice(p.voice);
            p.voice = nullptr;
        }
        if (!p.voice) {
            if (!free) free = &p;
        } else if (!oldest || p.started < oldest->started) {
            oldest = &p;
        }
    }
    if (free) return free;
    ax->voiceBegin(oldest->voice);
    ax->setVoiceState(oldest->voice, 0);
    ax->voiceEnd(oldest->voice);
    ax->freeVoice(oldest->voice);
    oldest->voice = nullptr;
    return oldest;
}

inline uint32_t ReadBe32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

// 1 when sounds can play here right now: sndcore2 reached and AX initialised by the game.
extern "C" inline uint32_t AudioAvailable() {
    const AxTable* ax = Ax();
    return ax && ax->isInit() ? 1u : 0u;
}

// Loads a WXAU file from the calling mod's own directory (scoped like wiixl.core's ModReadFile) into host memory, once,
// and returns a sound handle, or 0 when it cannot be read or is not WXAU. Load at entry: the samples come from the game's
// own heap and are never freed.
extern "C" inline uint32_t AudioLoadSound(const char* path) {
    const char* owner = ModContext::Current();
    char full[ModFS::kMaxScopedPath];
    const ModFS::PathResult r = ModFS::Resolve(path, full);
    if (r != ModFS::PathResult::Ok) {
        WIIXL_LOG("Audio: %s refused '%s' - %s", owner ? owner : "<host>", path ? path : "(null)",
                  ModFS::PathResultName(r));
        return 0;
    }
    if (g_SoundCount >= kMaxSounds) {
        WIIXL_LOG("Audio: %s refused '%s' - all %u sound slots are held", owner, path, kMaxSounds);
        return 0;
    }

    FS::File f;
    if (!f.Open(full)) {
        WIIXL_LOG("Audio: %s: '%s' not found", owner, full);
        return 0;
    }
    // FS reads want a 64-byte-aligned buffer and a 64-byte multiple (or the tail): the header comes in as one block.
    const uint32_t size = f.Size();
    alignas(64) uint8_t header[64];
    if (size < 64 || size > kMaxFileBytes || f.ReadAt(0, header, 64) != 64 || ReadBe32(header) != kMagic) {
        WIIXL_LOG("Audio: %s: '%s' is not a WXAU file (%u bytes)", owner, path, size);
        f.Close();
        return 0;
    }
    const uint32_t rate = ReadBe32(header + 4);
    const uint32_t count = ReadBe32(header + 8);
    const uint32_t channels = ReadBe32(header + 12);
    const uint32_t bytes = count * 2u;
    if (channels != 1 || rate < 4000 || rate > 96000 || count == 0 || kHeaderBytes + bytes > size) {
        WIIXL_LOG("Audio: %s: '%s' has a bad header (%u Hz, %u samples, %u channels)", owner, path, rate, count, channels);
        f.Close();
        return 0;
    }

    void* heap = Mem::GetBaseHeapHandle(Mem::BaseHeap::MEM2);
    auto* samples = static_cast<int16_t*>(Mem::AllocFromExpHeap(heap, bytes, 64));
    if (!samples) {
        WIIXL_LOG("Audio: %s: no memory for '%s' (%u bytes)", owner, path, bytes);
        f.Close();
        return 0;
    }
    // The samples run to the end of the file, so this is the tail read FS allows at any length.
    const uint32_t got = f.ReadAt(kHeaderBytes, samples, bytes);
    f.Close();
    if (got != bytes) {
        Mem::FreeToExpHeap(heap, samples);
        WIIXL_LOG("Audio: %s: '%s' read short", owner, path);
        return 0;
    }

    Sound& s = g_Sounds[g_SoundCount];
    s.samples = samples;
    s.count = count;
    s.rate = rate;
    std::strncpy(s.owner, owner ? owner : "", sizeof(s.owner) - 1);
    WIIXL_LOG("Audio: %s loaded '%s' (%u Hz, %u ms)", s.owner, path, rate, count * 1000u / rate);
    return ++g_SoundCount;
}

// Plays a loaded sound once, at `volume` (1 = as recorded), on the TV and the GamePad. Returns 1 if it started.
extern "C" inline uint32_t AudioPlaySound(uint32_t sound, float volume) {
    if (sound == 0 || sound > g_SoundCount) return 0;
    const Sound& s = g_Sounds[sound - 1];
    const AxTable* ax = Ax();
    if (!ax || !ax->isInit()) return 0;

    Playing* slot = TakeSlot(ax);
    AxVoice voice = ax->acquireVoice(kVoicePriority, &AudioVoiceDropped, nullptr);
    if (!voice) {
        WIIXL_LOG("Audio: no AX voice free for sound %u", sound);
        return 0;
    }

    if (!(volume >= 0.0f)) volume = 0.0f;
    if (volume > 1.99f) volume = 1.99f;
    AxVe ve{static_cast<uint16_t>(volume * 32768.0f), 0};

    AxMix mix[kMixChannels];
    std::memset(mix, 0, sizeof(mix));
    mix[0].bus[0].volume = 0x8000;   // left
    mix[1].bus[0].volume = 0x8000;   // right

    AxVoiceOffsets offsets{};
    offsets.dataType = kFormatLpcm16;
    offsets.loopingEnabled = 0;
    offsets.loopOffset = 0;
    offsets.endOffset = s.count - 1;
    offsets.currentOffset = 0;
    offsets.data = s.samples;

    const uint32_t mixRate = ax->getInputSamplesPerSec();
    ax->voiceBegin(voice);
    ax->setVoiceType(voice, 0);
    ax->setVoiceVe(voice, &ve);
    ax->setVoiceDeviceMix(voice, kDeviceTv, 0, mix);
    ax->setVoiceDeviceMix(voice, kDeviceDrc, 0, mix);
    ax->setVoiceSrcType(voice, kSrcLinear);
    ax->setVoiceSrcRatio(voice, mixRate ? static_cast<float>(s.rate) / static_cast<float>(mixRate) : 1.0f);
    ax->setVoiceOffsets(voice, &offsets);
    ax->setVoiceState(voice, kStatePlaying);
    ax->voiceEnd(voice);

    slot->voice = voice;
    slot->started = ++g_PlayCounter;
    return 1;
}

inline const Surface::Symbol kSymbols[] = {
    WIIXL_SURFACE_SYMBOL("Available", &AudioAvailable),
    WIIXL_SURFACE_SYMBOL("LoadSound", &AudioLoadSound),
    WIIXL_SURFACE_SYMBOL("PlaySound", &AudioPlaySound),
};

} // namespace impl

// Registers wiixl.audio, or deliberately does not, like wiixl.net: a mod requiring it on a platform with no backing is
// refused by name at load rather than finding out at its first sound.
inline bool Register() {
    if constexpr (!kSupported) {
        WIIXL_LOG("Surface: wiixl.audio NOT registered - no AX path on this platform yet. A mod requiring it will be "
                  "refused by name.");
        return false;
    }
    Surface::Registration reg{};
    reg.name = kName;
    reg.versionMajor = kVersionMajor;
    reg.versionMinor = kVersionMinor;
    reg.symbols = impl::kSymbols;
    reg.symbolCount = static_cast<uint32_t>(sizeof(impl::kSymbols) / sizeof(impl::kSymbols[0]));
    return Surface::Register(reg);
}

} // namespace WiiXLaunch::AudioSurface
