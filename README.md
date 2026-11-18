<div align="center">

<img width="542" height="176" alt="KarPur_s" src="https://github.com/user-attachments/assets/7f471ad8-26c9-42bb-8381-840466c7921d" />


**Pokémon Scarlet & Violet, recompiled into a native PC game.**

*Native Windows and experimental Linux x86-64 builds with a Vulkan renderer, built on your own PC from your own copy of the game. Not an emulator.*

**Quick start:** Use your own XCI, the 4.0.0 update, and `prod.keys`; build with KarPur's launcher or RecompLauncher. RecompLauncher accepts XCI base games only because it cannot request the firmware folder needed by an NSP base game.

[Download the launcher]([https://github.com/privacymatters42069-del/PokiNative/releases/]) · [0.4.1 release notes](docs/releases/0.4.1.md) · [Requirements](#requirements) · [Getting started](#getting-started) · [Troubleshooting](#troubleshooting) · [Roadmap](docs/roadmap.md)

<a href="https://recomplauncher.com/game/Pokinative"><picture><source media="(prefers-color-scheme: dark)" srcset="https://recomplauncher.com/assets/press/launcher/badge-light.svg"><img alt="Get it on RecompLauncher" src="https://recomplauncher.com/assets/press/launcher/badge-dark.svg" height="48"></picture></a> [![Verified developer on RecompStore](https://recompstore.com/assets/press/verified-dark.svg)](https://recompstore.com/dev/privacymatters42069-del)

Please respect Grunttanamo and their decision to step away from the project, report all issues and complaints here

thank you

</div>

The information below documents the last release and remains available for reference. No further development is planned by the original maintainer.

> [!IMPORTANT]
> **This repository contains no game files and never will.** You need your own legally obtained copy of the game and the keys of your own console. Instructions for obtaining them will not be provided.
>
> Everything is built from your files, on your PC, for your own use. Do not share it.

> [!IMPORTANT]
> **If the game crashes, try the two independent Compatibility switches in Settings.** The original mode changes GPU submission timing; **Keep hot memory active** sets `hot_cool = 0` and can be turned off again. On supported desktop PCs, enabling Resizable BAR may also improve performance; see the [launcher guide](docs/launcher.md#die-tabs) for the general UEFI/BIOS steps. Results vary by system.

> [!IMPORTANT]
> **AMD GPU owners and Vulkan people wanted.** In the 0.4.1 candidate, AMD RDNA 3 GPUs enable the extra transform-feedback
> barriers by default during character creation as an unconfirmed mitigation; set `TOTK_XFB_SYNC=0` to opt out. Flickering trees in compatibility
> mode is also AMD-specific and hard to track down: the maintainer has no AMD card
> and knows nobody who does. If you own one and know Vulkan or graphics debugging, your help would make a real
> difference — testing, logs, a pull request, or even just pointing out what to look at. Contributions put together
> with the help of an AI assistant are very welcome too. Please open an issue or pull request; see
> [Troubleshooting](#troubleshooting) and the open issues for what is known so far.

## What is PokiNative?

PokiNative is a fork of KarPur a fan project. It takes the ARM64 code of *Pokémon Scarlet & Violet* (Nintendo Switch, 2022) and translates it into C code, which it compiles into a native Windows or Linux program. The Switch system and its graphics API are replaced by a runtime of our own, and the game draws its pictures through Vulkan.

In practice that means:
- a steady **60 FPS**
- **DLSS, DLAA and Frame Generation**
- **FSR 2**, adjustable internal render resolution and screen-space lighting
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
| **System** | Windows 10 or 11, 64-bit; experimental x86-64 Linux with glibc 2.38 or newer |
| **Graphics card** | Vulkan 1.3 with a current driver. DLSS, DLAA and Frame Generation need an NVIDIA RTX card |
| **Processor** | 64-bit x86 with AVX2 (most CPUs from 2015 on). Older CPUs get a build of their own automatically; how well the game runs on them is still being tested: reports are very welcome |
| **Memory** | 16 GB RAM recommended |
| **Disk** | About **30 GB free** while building; about 13 GB stay in use afterwards |
| **Internet** | For the first build (about 250 MB of tools, all checked against fixed checksums) |
| **Resizable Bar Activated** | For better performance and less crash |

### Platforms

| | Platform | Status |
|---|---|---|
| 🟢 | Windows 10 / 11 | Tested (Ryzen 7 9800X3D, RTX 5080) |
| 🧪 | Linux, Steam Deck | Experimental native AppImage launcher that builds the game locally. Scenery, text entry and performance still need wider Linux testing; Linux/Vulkan reports and pull requests are welcome. See [Linux launcher guide](docs/launcher-linux.md) |

## Status

**Milestone M6 of 6: PC extras.** The game is playable: from the title screen into the open world, with battles, picnics, fast travel, saving and loading. Now the work is on PC features and polish.

| | Milestone | |
|---|---|---|
| ✅ | M0-M3 | Foundation, tools, the code translator, first sign of life |
| ✅ | M4 | First picture through Vulkan |
| ✅ | M5 | Playable |
| 🔄 | **M6** | **PC extras**: 60 FPS ✅, DLSS / DLAA / Frame Generation ✅, FSR 2 ✅, launcher ✅, settings ✅, shader pre-build ✅, DLC ✅, render scale ✅, shadow quality ✅, detail and population controls ✅; further Linux and graphics polish remains |

Details: [docs/roadmap.md](docs/roadmap.md) · daily log: [docs/progress.md](docs/progress.md)

## Features

### 🎮 Smooth play
- **60 FPS**, with the game's speed kept right, smooth effects and smooth walking indoors. Stay at 60 FPS for normal play: limits above 60 can break battle transitions and visual effects ([#85](https://github.com/Grunttanamo/KarPur/issues/85)).
- **Stutter reduction** (Settings tab, off by default, experimental): two contributed changes (#49, #53) that let a
  window image be acquired again at its display time and let threads run without a virtual core until they ask for
  their core number. Measured as an improvement in play, but new, so they stay off until more people have tried them.
- **All shaders prepared during the build:** about 36,000 shaders from your own game files are translated and handed to the graphics driver ahead of time. New areas, Pokémon and attacks appear without the stutter of first-time shader builds.

### ✨ Picture
- **NVIDIA DLAA**: smooth edges in the 3D world, at about 1 ms per frame.
- **NVIDIA DLSS**: the 3D world is rendered at your window's size, or at 4K and then scaled down.
- **DLSS Frame Generation**: 2× on RTX 40, up to 4× on RTX 50 cards. It needs Windows' hardware-accelerated GPU scheduling.
- NVIDIA's files are not bundled. The launcher can download `nvngx_dlss.dll` from NVIDIA on request, or you can provide your own copy. Frame Generation needs the separate supported Streamline files.
- **AMD FSR 2**: temporal anti-aliasing or window-sized upscaling on Vulkan, included with KarPur. Moving characters and effects can leave trails until their own motion is tracked.
- **Render scale**: internal image sizes from 800×450 to 3840×2160, independent of the window size. Wide, tall and portrait formats are experimental; their UI, visibility and character framing still need work. Fullscreen uses the monitor size.
- **Ambient occlusion** and **Indirect light**: separate Off/Low/Medium/High settings for contact shading and screen-space colour bounce.
- **AMD FSR 1** or bilinear scaling, with sharpness, when DLSS is off. The console's own **Handheld mode** (1280×720,
  a real speed gain) is selectable too, next to the usual Docked (1920×1080) — a genuine performance option for AMD
  and other non-NVIDIA cards, no reverse engineering required.
- A **Graphics card** setting (Settings tab) picks which GPU runs the game when more than one is installed; NVIDIA-only
  features lock themselves off automatically once you pick a non-NVIDIA card.
- **Shadow quality** (Settings tab): the game's own sun shadow map is 1024×1024. x2, x3 and x4 take it up to
  4096×4096, which removes most of the jagged edges on shadows, and **Disabled** skips the shadow pass completely.
  It costs the game no memory: the map is reported to the game as the original size, so nothing is taken from the
  streaming it does for NPCs and wild Pokémon.
- At non-16:9 render scales, the HUD stays in a centred 16:9 area; some screens still need layout work.

### 🌿 A livelier Paldea
**More wild Pokémon**, in four levels. The setting takes effect after a restart.

| Level | Pokémon at once | Good for |
|---|---|---|
| Switch (default) | 15 | the game as it ships |
| Switch 2 | 40 | playing |
| Enhanced | 80 | playing |
| Extreme | 120 | screenshots; can drop to about 35 FPS |

Switch 2 uses the spawn values of the fan mod [Pokémon Compass](https://compass.seri.studio) by Aria Seri and Kas Seri. The higher levels only raise the count. KarPur contains these numbers, none of the mod's files.

**People detail** and **Pokémon detail** can keep models at full detail farther away, up to 80 m. **Town people shown** offers the game's 12 or 24, 36 and 48 at once. Higher choices can increase memory pressure, so lower them if buildings or characters disappear.

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
- **Settings**: fullscreen, window size, Render scale, GPU choice, DLSS/FSR 2, lighting, shadows, population, detail distance and two independent compatibility switches.
- **Controls**: keyboard, mouse and controller, with swapped sticks and an inverted camera.
- **Saves**: export your save as a `.zip`, import one, open its folder, or restore one of the last saves.
- **Mods** (experimental, only a few mods tried so far): add your own mods (a folder or a `.zip`, for example from NexusMods) and switch each on or off. Nothing of your game is replaced: KarPur gives the game a mod's file as it loads the original. The tab follows `mod_files` in the game's settings and offers optional size and BNTX filters for large texture packs. Files a mod adds and a mod's code (`exefs`) are not supported yet.
- **Repair** and **Update**: only what changed is rebuilt. Your saves and settings stay.
- **Feedback**: a ready-made report about your system for a GitHub issue.

## Getting started

1. **Download** `KarPur-Launcher.exe` for Windows or `KarPur-Linux-Launcher-0.4.1.AppImage` for Linux from the [releases page](https://github.com/privacymatters42069-del/PokiNative/releases). Windows may warn that the file is unknown, because it is not signed: choose *More info* → *Run anyway*. Linux instructions are in the [AppImage guide](docs/launcher-linux.md).
2. **Choose your files** in the *Setup* tab:
   - the base game (`.xci`, or an eShop `.nsp` together with a *Firmware folder*)
   - the update and DLC (`.nsp`)
   - `prod.keys`
   - a target folder with at least 30 GB free; spaces in its path are supported
3. **Press Build.** The first build takes about 30-60 minutes and keeps your PC busy:
   - the launcher downloads Python and the compiler from their official sources
   - it translates the game's code
   - it compiles `karpur.exe`
   - finally, it prepares all shaders
4. **Play**: press *Play*, or use the shortcut **Pokémon Violet (KarPur)** that the launcher puts into your target folder.

Your dumps, your keys and the built game stay on your PC. A guide in German is in [docs/launcher.md](docs/launcher.md). If you want to do everything by hand, see [docs/setup-windows.en.md](docs/setup-windows.en.md) ([Deutsch](docs/setup-windows.md)).

### Use RecompLauncher

Install PokiNative/KarPur from its page in RecompLauncher, then provide your own `.xci`, the 4.0.0 update, and `prod.keys` from your console; DLC is optional. RecompLauncher runs the normal build in the background and starts the game when it finishes. Its setup supports XCI base games only because RecompLauncher cannot request the firmware folder needed by an NSP base game. Use the apps own Setup tab for NSP.

> [!IMPORTANT]
> **For a new game, keep Render scale at 1920×1080 until character creation is complete.** The character preview and ID photo can be misplaced or missing at other internal resolutions. After finishing character creation, change Render scale to your preferred size and restart the game. This is a temporary workaround while the capture and layout are fixed.

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

Use the launcher's **Feedback** tab with **Attach log**, or open an [issue]([https://github.com/privacymatters42069-del/PokiNative/issues]) and attach the whole `build\game\logs\karpur.log` from your target folder. Please send the whole file: the last lines alone rarely show the cause. Name your graphics card and its driver version too.
</details>

## Feedback

PokiNative is mostly tested on the maintainer's PC, so reports from other systems help a lot. The launcher's **Feedback** tab fills in:
- your launcher version, game, update and DLC
- your GPU, CPU, RAM and Windows version

You add a few words, and with *Attach log* the end of the game's log. **Open on GitHub** opens it as a filled-in [issue]([https://github.com/privacymatters42069-del/PokiNative/issues]). You need a free GitHub account for that, and reports are public. Without an account, *Copy report* copies the report for anywhere else.

## Known limits

- Render scale changes the game's internal output resolution. The character creation preview and ID photo are currently reliable at 1920×1080; switch scales after creating your character. Wide, tall and portrait layouts remain experimental.
- Online features (Union Circle, trading, Tera Raid matchmaking) are not available. The evolutions that would need
  a trade have an offline route instead (**No trade evolutions** in Settings); four of them take a level rather
  than their own item, because the game cannot name those items in an evolution condition.
- DLC is confirmed to work, including the story expansions.
- People and Pokémon detail distance and the number of town people are configurable. Tree and grass visibility still follows the game's own streaming and culling decisions.
- FSR 2 is available; there is no general AMD frame-generation option yet. Linux rendering remains experimental, with open texture and text-entry reports.

## How it works

1. **Lifting**: the game's ARM64 executables are translated into C code automatically.
2. **Runtime**: our own code stands in for the Switch operating system, including threads, files, input and audio.
3. **Graphics**: the Switch graphics API (NVN) is mapped to Vulkan. The game's shaders are translated with the Ryujinx shader translator.
4. **Build**: Clang compiles everything into an `.exe`, on the player's own PC.
5. **Run-time patches**: the game's menus and data are extended as the game loads them. Your game files on disk stay untouched.

PokiNative builds KarPur (https://github.com/Grunttanamo/KarPur) and on the MIT-licensed [TotK Recomp](https://github.com/valentderah/totk-pc-recomp) and [LAS Recomp](https://github.com/peiikai/LASRecomp). Code taken from other projects is listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

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
