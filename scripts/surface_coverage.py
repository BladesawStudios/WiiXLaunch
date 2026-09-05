#!/usr/bin/env python3
"""Reports which of the module's public API has no surface symbol.

WHY THIS EXISTS. "Can a mod do everything a source mod could?" was, until this
script, a question answered by reading nineteen headers and remembering. That is
not an answer anybody can check, and it silently stops being true every time a
function is added to the module and not to a surface.

So it is a NUMBER. The script lists every public entry point in the game module
and base framework, matches it against the symbols the surfaces export, and
prints what is uncovered.

WHAT IT IS NOT. It is not a proof of parity. A name matching a name says the
symbol exists, not that it does the same thing or takes the same arguments -
those the surfaces' own comments and the boot log have to carry. What this
catches is the thing nothing else can: a public function nobody exposed.

INTENTIONAL OMISSIONS ARE LISTED, NOT IGNORED. A function in EXCLUDED is one
somebody decided a mod should not have, with the reason next to it. That is the
difference between "we thought about it" and "we forgot", and the difference is
invisible unless it is written down.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MODULE = os.path.join(ROOT, "vendor", "wiixlaunch-botw", "include", "wiixlaunch", "botw")
BASE = os.path.join(ROOT, "include", "wiixlaunch")

# Headers whose public API a mod is expected to be able to reach.
SCANNED = [
    (os.path.join(MODULE, "game"), "botw"),
    (os.path.join(MODULE, "gui", "gui.hpp"), "botw"),
    # The graphics headers were MISSING from this list until asked whether the
    # port had parity. botw.gfx was written and never measured, so "0 uncovered"
    # was 0 uncovered OF WHAT THIS LOOKED AT - which is the same error the impl
    # skip made, one level up. A scan's blind spot reports as coverage.
    (os.path.join(MODULE, "graphics", "gx2.hpp"), "botw"),
    (os.path.join(MODULE, "graphics", "nvn.hpp"), "botw"),
    (os.path.join(BASE, "mem.hpp"), "base"),
    (os.path.join(BASE, "time.hpp"), "base"),
    (os.path.join(BASE, "call.hpp"), "base"),
]

# Where surface symbols are declared.
SURFACE_DIRS = [
    os.path.join(MODULE, "surfaces"),
    os.path.join(MODULE, "surfaces.hpp"),
    os.path.join(BASE, "loader"),
]

# Public functions deliberately NOT on a surface, and why. Each of these was a
# decision; leaving the reason here is what stops the next person re-deciding it
# by accident.
EXCLUDED = {
    # Host plumbing a mod has no business calling - these install hooks or hand
    # back raw engine pointers that the surfaces wrap properly.
    "GetRaw": "raw pointer; botw.player's escape hatch is the audited way",
    "GetActor": "returns Actor by value; botw.actor hands out handles instead",
    "GetAll": "std::vector; botw.actor's Query/QueryAt replaces it",
    "GetAllActors": "std::vector; see Query",
    "GetDynamicActors": "std::vector; see Query",
    "GetStaticActors": "std::vector; see Query",
    "QueryDynamicActors": "std::vector; see Query",
    "QueryStaticActors": "std::vector; see Query",
    "ForEach": "template callback; see Query",
    "ForEachDynamic": "template callback; see Query",
    "ForEachStatic": "template callback; see Query",
    "ForEachOfType": "template callback; botw.pouch enumerates by index",
    "OnUpdate": "single callback slot; botw.player's RegisterTick is the fanned-out form",
    "OnTick": "single callback slot; see RegisterTick",
    "OnFrame": "single callback slot; botw.gui/botw.gfx fan it out, attributed",
    "OnLoaded": "single callback slot; botw.flyt fans it out",
    "OnKorokGet": "single callback slot; botw.events makes it consumable",
    "OnShrineComplete": "single callback slot; ConsumeShrineComplete instead",
    "OnTowerOpen": "single callback slot; ConsumeTowerOpen instead",
    "OnInitialized": "single callback slot; botw.gfx Init covers the need",
    "OnInputRead": "single callback slot, runs inside the input hook",
    "SetRedirect": "returns a const char* the game reads; botw.flyt owns the strings",
    "GetDevice": "raw graphics device pointer",
    "GetGraphicsNvn": "raw graphics context pointer",
    "GetGraphicsContext": "raw graphics context pointer",
    "GetContextState": "raw graphics context pointer",
    "GetPhysicsObject": "raw Havok pointer; the velocity accessors wrap it",
    "GetHavokMotion": "raw Havok pointer; GetHavokVelocity wraps it",
    "GetControllerVelocityPtr": "raw pointer into the actor; the value form is exposed",
    "GetControllerDirectionPtr": "raw pointer into the actor; the value form is exposed",
    "GetPtclSys": "raw particle-system pointer",
    "IsMappedPtr": "host-side pointer validation",
    "IsPlausibleHeapPtr": "host-side pointer validation; botw.memory has its own",
    "IsReadablePtr": "host-side pointer validation",
    "IsPlausibleProc": "host-side pointer validation",
    "IsPlausible": "host-side pointer validation",
    "PlausiblePointer": "host-side pointer validation",
    "PlausibleCode": "host-side pointer validation",
    "IsFiniteFloat": "host-side numeric validation",
    "IsFiniteMatrix": "host-side numeric validation",
    "EnsureHeapWrapper": "internal allocator plumbing",
    "FindOrLoadResource": "private in the module; Spawn does it",
    "BuildTransformMatrix": "internal to Vfx::Spawn",
    "SetTransform": "internal to Vfx::Update",
    "ResIdFor": "internal resource bookkeeping",
    "InstallLoadHook": "botw.flyt Init installs it",
    "Init": "each surface has its own Init where one is needed",
    "Tick": "botw.events exposes its own Tick",
    "Update": "covered per-surface where it is public API",
    "Get": "private accessor helper",
    "Set": "private accessor helper",
    "Get2": "offset-based pane accessor; the named setters cover the real uses",
    "Set2": "offset-based pane accessor",
    "Get3": "offset-based pane accessor",
    "Set3": "offset-based pane accessor",
    "WallYawCos": "internal geometry table",
    "WallYawSin": "internal geometry table",
    "GetAspectTerms": "exposed as botw.display GetAspectTerms",
    "CoreinitProvider": "allocator plumbing wired at host init",
    "UseCoreinitHeap": "host heap policy, chosen once at init",
    "UseCodeCaveHeap": "host heap policy, chosen once at init",
    "CurrentHeap": "host heap policy",
    "GetMonotonicTicks": "exposed as wiixl.time GetMonotonicTicks",
    "TicksToCalendarTime": "wiixl.time GetCalendarTime does the conversion",
    "GetWallClockTicks": "wiixl.time GetCalendarTime is the useful form",
    "FormatDateTime": "wiixl.time FormatNow is the useful form",
    "ResolveCemuTime": "Cemu shim plumbing",
    "CemuTimeShimTable": "Cemu shim plumbing",
    "ShimAt": "Cemu shim plumbing",
    "WriteDigits": "internal formatting helper",
    "GetTargetFunction": "template; wiixl.call ResolveTarget returns the address",
    "ResolveTarget": "exposed as wiixl.call ResolveTarget",
    "DumpEvents": "exposed as botw.sound DumpEvents",
    "PlayFirstAvailable": "array of C strings; Play in a loop is the mod-side form",
    "Available": "exposed per surface",
    "IsValid": "exposed per surface",
    "Delete": "exposed as botw.actor Delete",
    "Spawn": "exposed per surface",
    "Stop": "exposed as botw.vfx Stop",

    # --- names the surfaces already carry under a clearer spelling ----------
    #
    # These are not gaps. A surface renamed the call where the module's own name
    # was ambiguous once several surfaces existed side by side, and the coverage
    # scan matches by name, so each one has to say so here rather than sit in a
    # list of things that look forgotten.
    "GetCurrentLife": "botw.actor GetLife / botw.player ActorGetLife",
    "SetCurrentLife": "botw.actor SetLife / botw.player ActorSetLife",
    "GetCurrentHearts": "botw.actor GetHearts",
    "SetCurrentHearts": "botw.actor SetHearts",
    "GetById": "botw.actor FindById",
    "GetCount": "botw.actor Count",
    "GetClimate": "botw.world GetWeatherClimate (weather) / GetTemperature (climate)",
    "SetClimate": "botw.world SetTemperature",
    "GetCompletion": "botw.gamedata GetCompletionBreakdown",
    "CountOfType": "botw.pouch SlotCount",
    "GetRootPane": "botw.flyt RootPane",
    "GetMapRegionUnlock": "botw.map GetRegionUnlock",
    "SetMapRegionUnlock": "botw.map SetRegionUnlock",
    "SetMapRegionUnlockAll": "botw.map SetRegionUnlockAll",
    "GetMapRegionScaleLevel": "botw.map GetRegionScaleLevel",
    "SetMapRegionScaleLevel": "botw.map SetRegionScaleLevel",
    "GetMapRegionMarker": "botw.map GetRegionMarker",
    "SetMapRegionMarker": "botw.map SetRegionMarker",
    "SetMapRegionActivated": "botw.map SetRegionActivated",
    "SetModifier": "botw.pouch SetModifierByName",
    "SetModifierAt": "botw.pouch SetItemModifier, addressed by (slot, index)",
    "GetCookData": "botw.pouch ItemCookData, addressed by (slot, index)",
    "GetModifier": "botw.pouch ItemModifier, addressed by (slot, index)",
    "FindItem": "botw.pouch FindItemIndex - an index, not a pointer a mod could hold",
    "FindNewestItem": "FindItemIndex covers it; the pouch is walked in order",
    "Width": "botw.gui CanvasSize fills both",
    "Height": "botw.gui CanvasSize fills both",
    "DeviceWidth": "botw.gui DeviceSize fills both",
    "DeviceHeight": "botw.gui DeviceSize fills both",
    "PixelScaleX": "botw.gui PixelScale fills both",
    "PixelScaleY": "botw.gui PixelScale fills both",
    "ViewportOffsetX": "botw.gui ViewportOffset fills both",
    "ViewportOffsetY": "botw.gui ViewportOffset fills both",
    "SnapX": "botw.gui Snap fills both",
    "SnapY": "botw.gui Snap fills both",
    "NavUp": "botw.gui Nav(0)",
    "NavDown": "botw.gui Nav(1)",
    "NavLeft": "botw.gui Nav(2)",
    "NavRight": "botw.gui Nav(3)",
    "Frame": "botw.gui FrameNumber",
    "GetArmourEffects": "botw.armour GetArmourEffect, one effect at a time",

    # --- host internals a mod has no business reaching ----------------------
    #
    # Manager pointers, offset arithmetic helpers and hook bookkeeping. Every
    # one of these is something the surfaces call ON a mod's behalf; handing
    # them over would be handing over the pointer the surface exists to keep.
    "WorldMgr": "manager pointer",
    "MapMgr": "manager pointer",
    "TimeMgr": "manager pointer",
    "PieceActor": "manager pointer",
    "GetPieceActor": "raw actor pointer; botw.armour reports the effects instead",
    "Byte": "pane offset arithmetic",
    "FieldAt": "pane offset arithmetic",
    "GetFlagsRaw": "pane flag word; IsVisible is the meaning of it",
    "Layout": "constructor",
    "Pane": "constructor",
    "PicturePane": "constructor",
    "Actor": "constructor",
    "Apply": "climate override plumbing, applied by the setters",
    "HeldMode": "climate hold bookkeeping",
    "HeldValue": "climate hold bookkeeping",
    "Arm": "events bookkeeping; Init arms it",
    "State": "events bookkeeping",
    "Install": "hook installation, done by the surfaces' Init",
    "HookInstalled": "hook bookkeeping",
    "OverrideActive": "completion display bookkeeping",
    "OverrideWhole": "completion display bookkeeping",
    "OverrideHundredths": "completion display bookkeeping",
    "OverrideForceVisible": "completion display bookkeeping",
    "CountBeasts": "internal to GetCompletion",
    "FlagMax": "internal flag-maximum lookup",
    "WriteBoolFlag": "internal to the map setters",
    "GetFlagDebug": "flag-store diagnostics for host debugging",
    "ReadName": "pouch offset arithmetic",
    "WalkItems": "template callback; the indexed reads replace it",
    "RequestActorForItem": "internal to EquipItem",
    "TickEquipRefresh": "internal, driven by the module's own tick",
    "ModifierName": "modifier bit naming, host-side",
    "ModifierWantsFloat": "modifier encoding detail",
    "ModifierFloatBits": "modifier encoding detail",
    "Mat3Mul": "vfx matrix helper",
    "RotX": "vfx matrix helper",
    "RotY": "vfx matrix helper",
    "RotZ": "vfx matrix helper",
    "LoadAdditional": "layout archive loading, driven by the game",
    "Corners": "gui geometry helper",
    "FitToBox": "gui text layout helper",
    "FormatFixed": "gui number formatting helper",
    "FrameFromCorner": "gui geometry helper",
    "PlateBounds": "gui geometry helper",
    "Focus": "gui focus bookkeeping; SetFocus/ClaimFocus are the API",
    "FocusableCount": "gui focus bookkeeping",
    "QuadsLastFrame": "gui draw statistics for host debugging",
    "LoaderRunToCompletion": "gui asset loader; LoadNow is the API",
    "TextureSize": "botw.gfx GetTextureSize reports it",
    "SetHostProvider": "arena allocator wiring, set once at host init",
    "GetTargetStart": "image start; wiixl.call ImageBase reports it",
    "TicksToSeconds": "constexpr conversion; wiixl.time TicksPerSecond is the input",
    "TicksToMilliseconds": "constexpr conversion",
    "TicksToMicroseconds": "constexpr conversion",
    "SecondsToTicks": "constexpr conversion",
    "MillisecondsToTicks": "constexpr conversion",

    # --- the module's own additions for the surfaces ------------------------
    #
    # Added to player.hpp so botw.player could fan out a single callback slot.
    # They are the plumbing behind RegisterTick, not something a mod calls.
    "AddTick": "the registry behind botw.player RegisterTick",
    "TickCount": "player tick bookkeeping",
    "TickRegisterName": "player tick refusal naming",
    "LogTickState": "reported at the load point",
    "IsInitialised": "botw.player Init reports whether it installed",
    "ClearPositionHold": "position-hold plumbing behind SetPosition",
    "IsPositionHeld": "position-hold plumbing",
    "NudgePosition": "botw.actor NudgeTo",

    # --- private members the scan cannot see are private ---------------------
    #
    # The regex below matches by indentation and does not track access
    # specifiers, so a private member of Canvas looks like a public one. These
    # four are private and the widgets that use them are exposed instead. Listed
    # rather than silenced, because "the scanner cannot tell" is itself a fact
    # worth having written down.
    "NineSlice": "private in Canvas; the widgets that use it are exposed",
    "DrawOptionRow": "private in Canvas; Button/Toggle/Slider draw with it",
    "DrawValueWithArrows": "private in Canvas; Selector draws with it",
    "ResolveOutputAspect": "private aspect helper; GetEffectiveOutputAspect is the API",
    "RegisterDrawCallback": "GUI installs its own with botw.gfx; RegisterFrame is the mod API",

    # --- state the surfaces drive, not state a mod sets ----------------------
    #
    # Reference-returning accessors (int& CellSize()) and hook bookkeeping. The
    # surfaces expose the VALUE forms - GetCellSize, SetWallGeometry - because a
    # reference a mod could hold is a pointer into host memory by another name.
    "CellSize": "reference accessor; botw.region GetWallGeometry reports it",
    "WallHeight": "reference accessor; see GetWallGeometry",
    "WallThickness": "reference accessor; see GetWallGeometry",
    "BuildRadius": "reference accessor; botw.region GetBuildRadius",
    "WallsEnabled": "reference accessor; botw.region GetWallsEnabled",
    "PushbackEnabled": "reference accessor; botw.region GetPushbackEnabled",
    "WallYawIndex": "reference accessor; botw.region GetWallYaw",
    "WallPitchIndex": "reference accessor; botw.region GetWallPitch",
    "MarkerEnabled": "reference accessor; botw.region GetMarkerEnabled",
    "MarkerScale": "reference accessor; botw.region GetMarkerScale",
    "MarkerOffset": "reference accessor; botw.region GetMarkerOffset",
    "MarkerSpawned": "marker bookkeeping",
    "MarkerMissTicks": "marker bookkeeping",
    "UnlockMask": "reference accessor; botw.region GetUnlockMask",
    "Armed": "region bookkeeping",
    "LastCellX": "wall rebuild bookkeeping",
    "MapTowerEcoMap": "the game's own region table",
    "GetCellSize": "botw.region GetWallGeometry reports all three",
    "GetWallHeight": "botw.region GetWallGeometry reports all three",
    "GetWallThickness": "botw.region GetWallGeometry reports all three",
    "ValidRegion": "private in Region; botw.map RegionFirst/RegionCount give the range",

    "ExtraTable": "reference accessor for the extra-effect table",
    "ExtraAny": "extra-effect bookkeeping",
    "ExtraHookInstalled": "hook bookkeeping",
    "ExtraEffectsActive": "extra-effect bookkeeping",
    "InitExtraEffects": "installed by botw.armour SetExtraEffect on first use",
    "RecomputeExtraAny": "internal to the extra-effect setters",

    "BeastMarkerHookInstalled": "hook bookkeeping",
    "GetMapUnlock": "botw.map GetShrineUnlock",
    "SetMapUnlock": "botw.map SetShrineUnlock",
    "SetMapUnlockAll": "botw.map SetShrineUnlockAll",
    "GetShrineCount": "botw.map ShrineCount",
    "GetTrackedBeastMarker": "botw.map GetBeastMarker",
    "TrackedBeastMarkers": "botw.map BeastMarkerCount",
    "InitBeastMarkers": "botw.map InitBeastMarkers",
    "ForgetBeastMarkers": "botw.map ForgetBeastMarkers",

    "WeatherHold": "reference accessor; botw.world GetWeatherHold",
    "GetWeatherHoldFrames": "botw.world GetWeatherHoldFrames",
    "TickWeatherHold": "botw.world TickWeatherHold",

    "DaysFromCivil": "calendar arithmetic behind GetCalendarTime",
    "ToUnixSeconds": "calendar arithmetic behind GetCalendarTime",
    "OSGetTime": "the coreinit call behind the wall clock",

    # --- the GX2/NVN command layer -------------------------------------------
    #
    # These are one-line wrappers around the graphics driver's own entry points,
    # used to BUILD DrawSprite and DrawMesh. Exposing them would mean handing a
    # mod the command buffer to drive directly, which is a different and much
    # larger contract than "draw this sprite" - every one of them can corrupt the
    # frame or hang the GPU, and none of them can be made safe by this boundary.
    #
    # A mod that genuinely needs the command layer wants a surface designed for
    # it, not these leaked through one at a time.
    "SetContextState": "GX2 command layer",
    "SetAttribBuffer": "GX2 command layer",
    "SetFetchShader": "GX2 command layer",
    "SetVertexShader": "GX2 command layer",
    "SetPixelShader": "GX2 command layer",
    "SetPixelSampler": "GX2 command layer",
    "SetPixelTexture": "GX2 command layer",
    "SetShaderModeEx": "GX2 command layer",
    "SetViewport": "GX2 command layer",
    "SetScissor": "GX2 command layer",
    "SetBlendControl": "GX2 command layer",
    "SetColorControl": "GX2 command layer",
    "SetColorBuffer": "GX2 command layer",
    "SetDepthBuffer": "GX2 command layer",
    "SetDepthOnlyControl": "GX2 command layer",
    "SetCullOnlyControl": "GX2 command layer",
    "SetTargetChannelMasks": "GX2 command layer",
    "InitSampler": "GX2 command layer",
    "InitSamplerClamping": "GX2 command layer",
    "InitTextureRegs": "GX2 command layer",
    "Invalidate": "GX2 cache management, done by the calls that need it",
    "DrawEx": "GX2 command layer",
    "CalcSurfaceSizeAndAlignment": "GX2 surface arithmetic",
    "CalcFetchShaderSizeEx": "GX2 shader arithmetic",
    "InitFetchShaderEx": "GX2 shader setup",
    "EnsureMeshPipeline": "pipeline setup, done on first DrawMesh",
    "EnsureMeshDepthTexture": "pipeline setup",
    "EnsureSpritePipeline": "pipeline setup",
    "AllocTextureSurface": "surface-level allocation; CreateTexture covers the mod case",
    "CreateTextureFromSurface": "surface-level creation; CreateTexture covers the mod case",
    "FinalizeTexture": "host texture teardown; the host owns texture lifetime",
    "FactorFromLyt": "layout blend-factor decoding",
    "FromLyt": "layout blend-mode decoding",
    "NominateSource": "the host names its own frame source",
    "OSLog": "host logging; wiixl.core Log is the mod's",
    "AliasScene": "aliases the colour buffer as a texture; internal to BlurBackdrop",
    "BlurPass": "one pass of BlurBackdrop",
    "BatchFlush": "internal to EndBatch",
    "DrawDone": "GPU fence, issued by the calls that need it",
    "EnsureDepthBuffer": "pipeline setup, done on first DrawMesh",
    "CombineFromLyt": "layout blend-combine decoding",
}

PUBLIC_FN = re.compile(
    r"^\s{0,4}(?:static\s+)?(?:constexpr\s+)?"
    r"(?:const\s+)?[A-Za-z_][A-Za-z0-9_:<>*&\s]*?\b([A-Z][A-Za-z0-9_]*)\s*\(",
)
SYMBOL = re.compile(r'WIIXL_SURFACE_SYMBOL\("([^"]+)"')


def public_functions(path):
    """Public entry points in one header, skipping impl namespaces.

    THE IMPL SKIP TRACKS BRACES, not a comment.

    It used to end on a line equal to "} // namespace impl", which is how those
    namespaces happen to be closed in most of this tree - and in map.hpp they
    are not. The result was that everything after the first impl namespace in
    that file was skipped, its public functions were never counted, and the
    coverage number came out HIGHER than the truth.

    That is the failure this project keeps naming: a check reporting confidence
    it has not earned. Verified by removing a surface symbol and watching the
    count fail to move - the liveness test that caught it was itself a case that
    could not fail, and had to be redone with a symbol known to be counted.

    Depth counting cannot drift that way: the namespace ends where its brace
    ends, whatever the comment says.
    """
    names = set()
    impl_depth = 0        # brace depth at which the current impl namespace opened
    depth = 0
    in_impl = False

    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            stripped = line.strip()
            opens = line.count("{")
            closes = line.count("}")

            starts_impl = (stripped.startswith("namespace impl")
                           or stripped.startswith("namespace detail"))

            if not in_impl and not stripped.startswith("//") and not stripped.startswith("*"):
                m = PUBLIC_FN.match(line)
                if m and not starts_impl:
                    names.add(m.group(1))

            if starts_impl and not in_impl:
                in_impl = True
                impl_depth = depth

            depth += opens - closes

            if in_impl and depth <= impl_depth:
                in_impl = False

    return names


def scan(target):
    if os.path.isfile(target):
        return {os.path.basename(target): public_functions(target)}
    out = {}
    for name in sorted(os.listdir(target)):
        if name.endswith(".hpp"):
            out[name] = public_functions(os.path.join(target, name))
    return out


def surface_symbols():
    names = set()
    for target in SURFACE_DIRS:
        paths = []
        if os.path.isfile(target):
            paths = [target]
        elif os.path.isdir(target):
            paths = [os.path.join(target, n) for n in os.listdir(target) if n.endswith(".hpp")]
        for p in paths:
            with open(p, encoding="utf-8", errors="replace") as f:
                names.update(SYMBOL.findall(f.read()))
    return names


def main():
    exported = surface_symbols()
    if not exported:
        sys.stderr.write("[surface_coverage] found NO surface symbols at all - the\n"
                         "  scan is broken, not the tree. Refusing to report 0%%.\n")
        return 1

    total = 0
    covered = 0
    excluded = 0
    gaps = []

    for target, _ in SCANNED:
        for header, fns in scan(target).items():
            for fn in sorted(fns):
                total += 1
                if fn in exported:
                    covered += 1
                elif fn in EXCLUDED:
                    excluded += 1
                else:
                    gaps.append((header, fn))

    print("[surface_coverage] %d public entry point(s): %d on a surface, "
          "%d deliberately excluded, %d uncovered"
          % (total, covered, excluded, len(gaps)))
    print("[surface_coverage] %d surface symbol(s) exported across all surfaces"
          % len(exported))

    if gaps:
        print("[surface_coverage] uncovered:")
        last = None
        for header, fn in gaps:
            if header != last:
                print("  %s" % header)
                last = header
            print("      %s" % fn)

    # A floor on the export count, so a surface quietly losing its table is not
    # reported as improved coverage.
    if len(exported) < 400:
        sys.stderr.write("\n[surface_coverage] only %d symbols exported, expected at "
                         "least 400 - a surface table has shrunk.\n" % len(exported))
        return 1

    # AND A FLOOR ON WHAT WAS SCANNED. A regex that stops matching, or a header
    # that moves, would otherwise report perfect coverage of nothing - which is
    # the same shape as test_wxlm printing "verified" for a file that did not
    # exist.
    if total < 450:
        sys.stderr.write("\n[surface_coverage] only %d public entry points found, "
                         "expected at least 450 - the scan is broken, not the "
                         "tree.\n" % total)
        return 1

    if gaps:
        sys.stderr.write("\n[surface_coverage] %d public entry point(s) have no "
                         "surface symbol and no entry in EXCLUDED.\n"
                         "  Either expose it, or add it to EXCLUDED WITH THE REASON -\n"
                         "  the difference between a decision and an oversight is\n"
                         "  invisible unless it is written down.\n" % len(gaps))
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
