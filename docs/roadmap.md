# Roadmap

Every milestone has a clear goal that can be checked. A milestone counts as done only once it has been checked on the maintainer's PC with the real game.

| | Goal | Done when … |
|---|---|---|
| ✅ **M0** | Repository foundation | Rules, licences, protection against game files and CI are in place |
| ✅ **M1** | Tools & extraction | The toolchain runs on Windows, and a script unpacks the player's own dump (base + update) into `game/` and prints a report: game version, hashes, modules, SDK version |
| ✅ **M2** | Lifter works | The C code generated from `main`, `sdk` and `subsdk*` compiles completely; a report lists unsupported instructions (goal: 0) |
| ✅ **M3** | First sign of life | `karpur.exe` starts the game code, reads files from the RomFS and writes logs, still without a picture |
| ✅ **M4** | First picture | The title screen is rendered through Vulkan |
| ✅ **M5** | Playable | Walking around, battles, saving and loading work |
| 🔄 **M6** | PC extras | Higher frame rate, a better picture, settings, an installer, smooth play on other PCs |

## M6 in detail

| | Part | State |
|---|---|---|
| ✅ | 60 FPS (90 / 120 / 144 experimental) | Done |
| ✅ | KarPur Launcher (build, repair, update, settings, controls, feedback) | Done, 0.2.0 released |
| ✅ | Settings and controls in the game (F1 / Esc) | Done, with a Quit game button (#25) and the performance overlay choice |
| ✅ | More wild Pokémon (four levels) | Done |
| ✅ | NVIDIA DLAA, DLSS, DLSS Frame Generation | Done |
| ✅ | All shaders prepared during the build | Done (about 36,000; crash-safe against driver faults) |
| 🔄 | Stability on other PCs | Older CPUs without AVX2: builds and starts, faster math since 0.3.2, a test run that needs reports; a crash after the character creation on some PCs (compatibility mode as a workaround) |
| 🔄 | AMD alternatives | AMD FSR 1 plus Handheld mode is confirmed. The maintainer confirmed the Vulkan FSR 2.2 picture in Windows gameplay in the playground; its launcher controls no longer carry an experimental label. It does not change the game's GPU or shadow pools. Linux gameplay, moving-object motion/reactivity and AMD frame generation remain to be checked or developed. |
| ✅ | DLC | Maintainer confirmed the DLC unlock works and that the DLC areas play correctly on a save that had already visited them. Reaches players with the next release (`dlc` build step). |
| 🔄 | Draw distance | The maintainer confirmed the first four model-detail choices for people and Pokémon. The playground now caps both model-detail ranges at 80 m and static town-person choices at 48, without changing pool capacities. Wild Pokémon spawn range and terrain/foliage distance remain separate work. |
| ✅ | Launcher extras | A look per edition, the current team with icons from the player's own game, a Saves tab, a Mods tab (experimental; files a mod adds and code mods not yet), the launcher itself playable with a controller (#30), and a Graphics card / Docked-Handheld Mode setting |
| 🔄 | Higher internal resolution and other shapes | The maintainer confirmed five 16:9 render-scale choices (480p through 2160p) in play without growing the game's GPU pool. 450p (800×450) is added for the 0.4.0 candidate and awaits a playtest. Experimental 21:9/32:9 and 4:3/3:2/16:10/9:16 choices have local checks; wider views still need LOD/culling and speech-bubble work, especially at 32:9. |
| 🔄 | Linux and Steam Deck | The experimental native Linux runtime and Avalonia AppImage launcher build and run on Steam Deck. A Linux memory-sync change improved missing scenery in the maintainer's test. Render-scale, FSR 2, lighting and detail controls are in the 0.4.0 candidate, but the complete AppImage build and Linux gameplay still need testing; reports #71, #78 and #81 remain open. |

## How we work

- Only the maintainer sees game files. Tools run on their PC and produce **reports** (numbers, addresses, function names, error messages) that can be shared.
- No bytes from the game, no keys and no generated code belong in reports, issues, commits or the chat.
