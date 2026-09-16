![logo](docs/res/PillBanna.png)

# WiiXLaunch

WiiXLaunch is a cross-platform modding SDK and framework, originally built
to make modding *The Legend of Zelda: Breath of the Wild* easier. It has
since grown into a general toolset for executable modding across Nintendo
Switch and the Wii U/Cemu platforms: one C++ codebase, three targets.

It is two things:

* A **framework** that hooks and patches a running game, on Switch
  (AArch64 via ExLaunch), Wii U (PowerPC via WUPS/libfunctionpatcher), and
  Cemu (PC graphic-pack code caves).
* An **SDK** (`sdk/`) that builds standalone, distributable mods (`.wxlm`)
  against an already-built host, with no framework checkout required.

Game-specific knowledge lives in separate game modules
(the first is [wiixlaunch-botw](https://github.com/TKVSC-Team/wiixlaunch-botw)
for Breath of the Wild) rather than in the framework itself, so the same
core supports other games.

Full documentation, including setup and usage for both mod authors and
framework contributors, starts at [docs/overview.md](docs/overview.md).

## License

GPL-3.0. See [LICENSE](LICENSE).

## Credits

**The5thTear**: creator. Conceived the project and built its first Cemu
compiler support from scratch.

**Mindstormmann**: co-developer and led the rewrite into a proper SDK, with dependency
tracking and a custom mod-loading format of its own.

Built on:

* [ExLaunch](https://github.com/shadowninja108/exlaunch): AArch64 inline
  hooking, NSO loading, and memory patching for Nintendo Switch.
* [WiiUPluginSystem (WUPS)](https://github.com/wiiu-env/WiiUPluginSystem):
  plugin architecture and Aroma integration for Nintendo Wii U.
* [libfunctionpatcher](https://github.com/wiiu-env/libfunctionpatcher):
  PowerPC function patching and memory page permission handling for Wii U.
* [libnotifications](https://github.com/wiiu-env/libnotifications):
  on-screen notifications the Wii U host uses for `WIIXL_LOG`.
* [wut](https://github.com/devkitPro/wut): C/C++ headers and OS bindings
  for Nintendo Wii U homebrew.
