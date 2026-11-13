<div align="center">

# KarPur

**Pokémon Scarlet & Violet, recompiled into a native PC game.**

*Real Windows x86-64 code with a Vulkan renderer, built on your own PC from your own copy of the game. Not an emulator.*

[Download the launcher](https://github.com/Grunttanamo/KarPur/releases) · [Requirements](#requirements) · [Getting started](#getting-started) · [Troubleshooting](#troubleshooting) · [Roadmap](docs/roadmap.md)

[![Get it on RecompLauncher](https://recomplauncher.com/assets/press/launcher/badge-dark.svg)](https://recomplauncher.com/game/karpur) [![Verified developer on RecompStore](https://recompstore.com/assets/press/verified-dark.svg)](https://recompstore.com/dev/grunttanamo)

</div>

> [!IMPORTANT]
> **This repository contains no game files and never will.** You need your own legally obtained copy of the game and the keys of your own console. Instructions for obtaining them will not be provided.
>
> Everything is built from your files, on your PC, for your own use. Do not share it.

> [!IMPORTANT]
> **AMD GPU owners and Vulkan people wanted.** The remaining AMD-specific bugs (a crash on some RDNA 3 cards during
> character creation, flickering trees in compatibility mode) are hard to track down: the maintainer has no AMD card
> and knows nobody who does. If you own one and know Vulkan or graphics debugging, your help would make a real
> difference — testing, logs, a pull request, or even just pointing out what to look at. Contributions put together
> with the help of an AI assistant are very welcome too. Please open an issue or pull request; see
> [Troubleshooting](#troubleshooting) and the open issues for what is known so far.

## What is KarPur?

KarPur is a fan project. It takes the ARM64 code of *Pokémon Scarlet & Violet* (Nintendo Switch, 2022) and translates it into C code, which it compiles into a Windows program. The Switch system and its graphics API are replaced by a runtime of our own, and the game draws its pictures through Vulkan.

In practice that means:
- a steady **60 FPS**
- **DLSS, DLAA and Frame Generation**
- **keyboard and mouse**
- **more wild Pokémon**

The **KarPur Launcher** does all the building for you.

## Requirements

### Your game

| | What you need | Status |
|---|---|---|
| 🟢 | ***Pokémon Violet*, base game as `.xci`** (a cartridge dump) | Tested by the maintainer |
| 🟢 | **Update 4.0.0 as `.nsp`** | Tested; the only supported update |
| 🟡 | ***Pokémon Scarlet*, base game as `.xci`** | Works according to community reports; not tested by the maintainer |
| 🟢 | **DLC as `.nsp`** (optional) | Confirmed working, including the story areas |
| 🟡 | **Base game as `.nsp`** (an eShop copy, or an `.xci` converted from one) | Merges with the update since launcher 0.3.5 (#40). It also needs a **firmware folder**: the system fonts come from the cartridge image, so an eShop copy takes them from your console's system files (the launcher's *Firmware folder*) |
| 🔴 | Other updates than 4.0.0 | Not tested so not supported |

You also need **`prod.keys`** from the same console.

### Your PC

| | |
|---|---|
| **System** | Windows 10 or 11, 64-bit |
| **Graphics card** | Vulkan 1.3 with a current driver. DLSS, DLAA and Frame Generation need an NVIDIA RTX card |
| **Processor** | 64-bit x86 with AVX2 (most CPUs from 2015 on). Older CPUs get a build of their own automatically; how well the game runs on them is still being tested: reports are very welcome |
| **Memory** | 16 GB RAM recommended |
| **Disk** | About **30 GB free** while building; about 13 GB stay in use afterwards |
| **Internet** | For the first build (about 250 MB of tools, all checked against fixed checksums) |

### Platforms

| | Platform | Status |
|---|---|---|
| 🟢 | Windows 10 / 11 | Tested (Ryzen 7 9800X3D, RTX 5080) |
| ⚪ | Linux, Steam Deck, Proton / Wine | Not tested. The runtime has a Linux port, but the launcher is a Windows program. Reports are welcome |

## Status

**Milestone M6 of 6: PC extras.** The game is playable: from the title screen into the open world, with battles, picnics, fast travel, saving and loading. Now the work is on PC features and polish.

| | Milestone | |
|---|---|---|
| ✅ | M0-M3 | Foundation, tools, the code translator, first sign of life |
| ✅ | M4 | First picture through Vulkan |
| ✅ | M5 | Playable |
| 🔄 | **M6** | **PC extras**: 60 FPS ✅, DLSS / DLAA / Frame Generation ✅, launcher ✅, settings ✅, shader pre-build ✅, DLC ✅, a real AMD/weaker-GPU option (FSR 1 + Handheld mode) ✅, shadow quality ✅, draw distance |

Details: [docs/roadmap.md](docs/roadmap.md) · daily log: [docs/progress.md](docs/progress.md)

## Features

### 🎮 Smooth play
- **60 FPS**, with the game's speed kept right, smooth effects and smooth walking indoors. 90, 120 and 144 FPS are available as experimental limits.
- **Stutter reduction** (Settings tab, off by default, experimental): two contributed changes (#49, #53) that let a
  window image be acquired again at its display time and let threads run without a virtual core until they ask for
  their core number. Measured as an improvement in play, but new, so they stay off until more people have tried them.
- **All shaders prepared during the build:** about 36,000 shaders from your own game files are translated and handed to the graphics driver ahead of time. New areas, Pokémon and attacks appear without the stutter of first-time shader builds.

### ✨ Picture
- **NVIDIA DLAA**: smooth edges in the 3D world, at about 1 ms per frame.
- **NVIDIA DLSS**: the 3D world is rendered at your window's size, or at 4K and then scaled down.
- **DLSS Frame Generation**: 2× on RTX 40, up to 4× on RTX 50 cards. It needs Windows' hardware-accelerated GPU scheduling.
- NVIDIA's files are not part of KarPur: you add your own copies (from any game that ships DLSS) in the launcher.
- **AMD FSR 1** or bilinear scaling, with sharpness, when DLSS is off. The console's own **Handheld mode** (1280×720,
  a real speed gain) is selectable too, next to the usual Docked (1920×1080) — a genuine performance option for AMD
  and other non-NVIDIA cards, no reverse engineering required.
- A **Graphics card** setting (Settings tab) picks which GPU runs the game when more than one is installed; NVIDIA-only
  features lock themselves off automatically once you pick a non-NVIDIA card.
- **Shadow quality** (Settings tab): the game's own sun shadow map is 1024×1024. x2, x3 and x4 take it up to
  4096×4096, which removes most of the jagged edges on shadows, and **Disabled** skips the shadow pass completely.
  It costs the game no memory: the map is reported to the game as the original size, so nothing is taken from the
  streaming it does for NPCs and wild Pokémon.
- The HUD always stays as the game draws it.

### 🌿 A livelier Paldea
**More wild Pokémon**, in four levels. The setting takes effect after a restart.

| Level | Pokémon at once | Good for |
|---|---|---|
| Switch (default) | 15 | the game as it ships |
| Switch 2 | 40 | playing |
| Enhanced | 80 | playing |
| Extreme | 120 | screenshots; can drop to about 35 FPS |

Switch 2 uses the spawn values of the fan mod [Pokémon Compass](https://compass.seri.studio) by Aria Seri and Kas Seri. The higher levels only raise the count. KarPur contains these numbers, none of the mod's files.

**No version exclusives** (off by default): the wild Pokémon that are exclusive to the other edition spawn in yours
as well — 66 spawn entries across Paldea, Kitakami and the Blueberry Academy, including the version-split paradox
Pokémon and Tauros' two breeds. Your own edition's Pokémon are unaffected.

**No trade evolutions** (off by default): the 28 evolutions that can only happen through a trade work without one.
16 of them on reaching a level, 12 by holding the item they needed and gaining a level. Both settings change values
in the data your own copy of the game already contains; KarPur adds none.

### 🕹️ Controls
- **Mouse and keyboard or controller**; the mouse turns the camera.
- **A controls page in the style of the game's own menus**, opened with **F1** or **Esc**. It has:
  - remapping of every key and button
  - button layout and motion controls
  - vibration
  - stick dead zone and mouse sensitivity
  - the performance overlay (Off / FPS / Full)
  - **Quit game**, with a confirmation, so fullscreen no longer needs Alt-F4 (#25)

  Its texts are in English, German and Russian.
- **The launcher itself works with a controller** too (#30): the sidebar's tabs and Play follow the stick or D-pad, A
  opens a page or presses a button, B goes back.

### 🧰 The KarPur Launcher
- **One file**: no installation and no administrator rights.
- **Home**: a start page in your edition's colours, with Play, the state of your build, your last save and **your current team** (name and level, read when the game saves) at a glance.
- **Setup**: builds the game from your dumps, shaders included. A stopped build goes on where it stopped.
- **Settings**: everything from fullscreen and your graphics card to Frame Generation, shadow quality, how many wild Pokémon appear and the two Pokémon settings above, before you start the game.
- **Controls**: keyboard, mouse and controller, with swapped sticks and an inverted camera.
- **Saves**: export your save as a `.zip`, import one, open its folder, or restore one of the last saves.
- **Mods** (experimental, only a few mods tried so far): add your own mods (a folder or a `.zip`, for example from NexusMods) and switch each on or off. Nothing of your game is replaced: KarPur gives the game a mod's file as it loads the original. Mods that only change files work; files a mod adds and a mod's code (`exefs`) are not supported yet, and the tab says so per mod.
- **Repair** and **Update**: only what changed is rebuilt. Your saves and settings stay.
- **Feedback**: a ready-made report about your system for a GitHub issue.

## Getting started

1. **Download** `KarPur-Launcher.exe` from the [releases page](https://github.com/Grunttanamo/KarPur/releases). Windows may warn that the file is unknown, because it is not signed: choose *More info* → *Run anyway*.
2. **Choose your files** in the *Setup* tab:
   - the base game (`.xci`, or an eShop `.nsp` together with a *Firmware folder*)
   - the update and DLC (`.nsp`)
   - `prod.keys`
   - a **target folder without spaces**
3. **Press Build.** The first build takes about 30-60 minutes and keeps your PC busy:
   - the launcher downloads Python and the compiler from their official sources
   - it translates the game's code
   - it compiles `karpur.exe`
   - finally, it prepares all shaders
4. **Play**: press *Play*, or use the shortcut **Pokémon Violet (KarPur)** that the launcher puts into your target folder.

Your dumps, your keys and the built game stay on your PC. A guide in German is in [docs/launcher.md](docs/launcher.md). If you want to do everything by hand, see [docs/setup-windows.en.md](docs/setup-windows.en.md) ([Deutsch](docs/setup-windows.md)).

## Troubleshooting

<details>
<summary><b>The build stops with an error about the firmware or the "shared fonts" folder</b></summary>

Your base game is an `.nsp`, or an `.xci` converted from one. The game's system fonts come from the cartridge image, which an eShop copy does not have. In the launcher's *Setup*, choose your console's system files as **Firmware folder**: the folder with the `.nca` files of your console's system, for example an emulator's `nand\system\Contents\registered`. Then press **Build** again. A cartridge dump (`.xci`) needs no firmware folder.
</details>

<details>
<summary><b>The game crashes right after the character creation</b></summary>

In the launcher, open **Settings** and turn on **Compatibility mode**, then **Save settings**. This sends the game's graphics work to the GPU at once, instead of slightly delayed. Far trees may flicker a little more, and the frame rate can drop a little.

Without the launcher, start the game from PowerShell in the folder of `karpur.exe` like this:

```powershell
$env:TOTK_SUBMIT_DEFER = "0"
.\karpur.exe
```

Please also send us a report (see below), so we can find the cause.
</details>

<details>
<summary><b>The build stops at the linking step with "Permission denied"</b></summary>

The game is still running from that folder and holds `karpur.exe` open, so the new one cannot be written (#48).
Close the game and press **Repair** or **Update** again. The message names the cause in its last line, but it
arrives as a compiler error, which hides it.
</details>

<details>
<summary><b>The build stops at "Self-test"</b></summary>

Exit code `3221225501` means that the processor lacks an instruction set the build used (AVX2). Current builds detect this and build for your processor. Support for CPUs without AVX2 is **a test run**: the game builds and starts, and launcher 0.3.2 makes its math much faster there, but it has hardly been tried. Please tell us in a report how it runs (your CPU and the FPS). Press **Repair** in the launcher after updating it. If the self-test still fails, please send us the launcher log.
</details>

<details>
<summary><b>The game crashes or freezes somewhere else</b></summary>

Use the launcher's **Feedback** tab with **Attach log**, or open an [issue](https://github.com/Grunttanamo/KarPur/issues) and attach the whole `build\game\logs\karpur.log` from your target folder. Please send the whole file: the last lines alone rarely show the cause. Name your graphics card and its driver version too.
</details>

## Feedback

KarPur is mostly tested on the maintainer's PC, so reports from other systems help a lot. The launcher's **Feedback** tab fills in:
- your launcher version, game, update and DLC
- your GPU, CPU, RAM and Windows version

You add a few words, and with *Attach log* the end of the game's log. **Open on GitHub** opens it as a filled-in [issue](https://github.com/Grunttanamo/KarPur/issues). You need a free GitHub account for that, and reports are public. Without an account, *Copy report* copies the report for anywhere else.

## Known limits

- The game renders at 1920 × 1080; DLSS gives a sharper picture at higher window sizes. A higher native resolution is not done yet.
- Online features (Union Circle, trading, Tera Raid matchmaking) are not available. The evolutions that would need
  a trade have an offline route instead (**No trade evolutions** in Settings); four of them take a level rather
  than their own item, because the game cannot name those items in an evolution condition.
- DLC is confirmed to work, including the story expansions.
- Draw distance settings for trees, grass, Pokémon and NPCs are not done yet.
- No temporal AMD upscaler (FSR 2) or frame generation yet; FSR 1 plus Handheld mode is the real performance option for now.

## How it works

1. **Lifting**: the game's ARM64 executables are translated into C code automatically.
2. **Runtime**: our own code stands in for the Switch operating system, including threads, files, input and audio.
3. **Graphics**: the Switch graphics API (NVN) is mapped to Vulkan. The game's shaders are translated with the Ryujinx shader translator.
4. **Build**: Clang compiles everything into an `.exe`, on the player's own PC.
5. **Run-time patches**: the game's menus and data are extended as the game loads them. Your game files on disk stay untouched.

KarPur builds on the MIT-licensed [TotK Recomp](https://github.com/valentderah/totk-pc-recomp) and [LAS Recomp](https://github.com/peiikai/LASRecomp). Code taken from other projects is listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Built with AI

This project would not exist without AI. An AI coding agent ([Claude Code](https://claude.com/claude-code) by Anthropic) writes most of the code and documentation. It also plans the work, explains every step, and analyses the reports and crash logs.

The maintainer owns the game and the console, runs and tests every build on real hardware, and decides what goes in. The rules the agent works under are in [CLAUDE.md](CLAUDE.md).

## Contributing

Read [CLAUDE.md](CLAUDE.md) first for the project rules. Enable the protective hook once per clone:

```
git config core.hooksPath .githooks
```

Before every commit, run:

```
python tools/check_content.py --staged
```

## License

The project's own code is under the [MIT License](LICENSE); third-party code keeps its own license. The license grants nothing for the game.

KarPur is an independent, non-commercial fan project. It is not affiliated with or endorsed by Nintendo, The Pokémon Company, Game Freak or Creatures Inc. *Pokémon*, *Pokémon Scarlet*, *Pokémon Violet* and *Nintendo Switch* are trademarks of their respective owners.
