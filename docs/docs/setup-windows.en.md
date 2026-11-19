# Setup on Windows

Deutsche Fassung: [setup-windows.md](setup-windows.md).

This guide takes you from an empty PC to the running game, with no prior knowledge needed. You build everything yourself from your own game.

**What you need:**
- *Pokémon Violet* with update **4.0.0**, dumped from your own console, and `prod.keys` from the same console. How to get these is not covered here. Scarlet and other versions are not tested.
- Windows and a graphics card with a current Vulkan driver.
- About **30 GB of free space** during setup. About 12 GB stay in use afterwards.
- About 1 to 2 hours, most of it waiting.

**Important:** dumps, keys and the `game\` folder stay with you. Never share or upload them.

## 1. Open a terminal

Start menu → type **"Terminal"** → open *Terminal*. The tab at the top should say "PowerShell".

Copy all the commands below into it and confirm each with Enter. Paste with **Ctrl+V** or a right click.

## 2. Install the programs

Git, Python, MSYS2 (which has the C compiler) and .NET 10 (for the shader translator). Answer any prompts with `Y`.

```
winget install --id Git.Git -e
```

```
winget install --id Python.Python.3.14 -e
```

```
winget install --id MSYS2.MSYS2 -e
```

```
winget install --id Microsoft.DotNet.SDK.10 -e
```

The Visual Studio Build Tools provide the linker for the shader translator. **This downloads about 3–5 GB and takes 10–20 minutes.** An installer window shows the progress and closes by itself at the end:

```
winget install --id Microsoft.VisualStudio.BuildTools -e --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

Update MSYS2. **Run this command twice**, because the first run only renews the base packages and then exits:

```
C:\msys64\usr\bin\bash.exe -lc "pacman -Syuu --noconfirm"
```

Install the compiler and libraries (about 1–2 GB):

```
C:\msys64\usr\bin\bash.exe -lc "pacman -S --needed --noconfirm make mingw-w64-ucrt-x86_64-clang mingw-w64-ucrt-x86_64-lld mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-sdl3 mingw-w64-ucrt-x86_64-opus mingw-w64-ucrt-x86_64-libvpx mingw-w64-ucrt-x86_64-zstd mingw-w64-ucrt-x86_64-vulkan-devel mingw-w64-ucrt-x86_64-vulkan-memory-allocator"
```

**Now close the terminal and open it again**, so Windows finds the new programs. Then check:

```
python --version
```

Expected output: `Python 3.14` or newer. Older versions do not work.

```
dotnet --version
```

Expected output: a number starting with `10.`

```
C:\msys64\ucrt64\bin\clang.exe --version
```

Expected output: among others, `Target: x86_64-w64-windows-gnu`.

## 3. Get the project

Everything goes under `C:\KarPur`. Paths without spaces save trouble later.

```
mkdir C:\KarPur
```

```
cd C:\KarPur
```

```
git clone https://github.com/Grunttanamo/KarPur.git repo
```

```
cd C:\KarPur\repo
```

Set up the Python environment:

```
python -m venv .venv
```

```
.venv\Scripts\python -m pip install -r tools\requirements.txt
```

Expected output at the end: `Successfully installed ...`

From now on, always start the tools with `.venv\Scripts\python`.

## 4. Enter your game files

Keep the dumps and `prod.keys` **outside** the repo, for example in `C:\KarPur\dumps\`.

```
copy config.example.toml config.local.toml
```

```
notepad config.local.toml
```

Enter the **paths** to the base game, the update and `prod.keys`, and save. If you have no DLC, write `dlc = []`. Git ignores this file, so it stays with you.

## 5. Unpack the game files

Unpack the program files (about a minute):

```
.venv\Scripts\python tools\extract.py
```

Expected output: a report between two lines of `=====`. A line with `error:` usually means a path from step 4 is wrong.

Get the system fonts (a few seconds):

```
.venv\Scripts\python tools\extract_fonts.py
```

Expected output: among others, six lines from `font 0 standard: ... bytes` to `font 5 nintendo-extension: ... bytes`.

Build the hactool tool, which merges the base game and the update (under a minute):

```
.venv\Scripts\python tools\build_hactool.py
```

Merge the game data. **This takes about 5–20 minutes** and temporarily needs the roughly 30 GB of space:

```
.venv\Scripts\python tools\extract_romfs.py
```

Expected output: again a report between `=====` lines.

## 6. Build the shader translator

It turns the game's shaders into the language Vulkan understands. The first time, .NET downloads packages, so **expect 3–10 minutes**:

```
.venv\Scripts\python tools\build_xlate.py
```

Expected output at the end: `built ...\build\xlate\totk_xlate.exe (... MB) in ... s`. The name comes from the project the translator was taken from.

## 7. Translate and build the game

First prepare the frame rate patches. **Don't skip this step, or the game will only run at 30 FPS:**

```
.venv\Scripts\python tools\fps_patches.py
```

Expected output at the end: `patch table: ... written to ...\build\fps_patches.txt`. If the tool reports `sites differ`, this is not Violet 4.0.0.

Translate the game code to C and compile it. **This takes 10–40 minutes**, and your PC will be fully loaded and may get loud. In between you will see lines like `125/400 files, 300s`:

```
.venv\Scripts\python tools\lift.py
```

Expected output: a report containing `Compiled: 404 of 404 files (0 errors)` or similar, with 0 errors.

Build `karpur.exe` (about 5 minutes):

```
.venv\Scripts\python tools\build_game.py
```

Expected output at the end: `build\game\karpur.exe: file ... MiB`.

Self-test (a few seconds, needs no game data):

```
build\game\karpur.exe --selftest
```

Expected output at the end: `selftest: passed (0 failures)`

## 8. Start the game

```
.venv\Scripts\python tools\run_with.py
```

**Good to know for the first start:**
- The window opens behind your other windows, and its taskbar icon flashes. Click it.
- The Vulkan shaders are built bit by bit as you first play, so it can stutter a little at the start.
- **F11** toggles fullscreen, **F10** the performance overlay, and **Esc** releases the mouse. A click in the window captures it again.

Close the game with the **X** on the window.

## 9. Set up PC Settings in the main menu

**For Violet 4.0.0 one command is enough.** It copies the list that comes with KarPur into place:

```
copy tools\data\ui_patch_violet_4.0.0.txt game\ui_patch.txt
```

At the next start **PC Settings** is in the main menu under Options, and you can skip the rest of this step. For another game version:

For the "PC Settings" entry to appear in the game's main menu (X), you need a list that matches your game files. It stays under `game\`. Until then the game runs normally, with the settings in the menu on **F1**.

```
.venv\Scripts\python tools\pc_menu.py
```

The first time, the tool reports `game/dump lacks the unpacked menu files`. This is normal. Start the game once like this:

```
.venv\Scripts\python tools\run_with.py DATA_WATCH=1
```

Play until you can open the main menu (X), open it and then **Options**, and close the game. Then run the tool again:

```
.venv\Scripts\python tools\pc_menu.py
```

Expected last line: `done: ...\game\ui_patch.txt`

At the next normal start (step 8), **PC Settings** is in the main menu under Options:

- **Picture:** fullscreen, window size, frame rate limit, frame sync, upscaling, sharpness, performance overlay.
- **Pokémon population**, meaning how many wild Pokémon roam at once: Switch (15, the default), Switch 2 (40), Enhanced (80) or Extreme (120, can lower the frame rate a lot). Takes effect after a restart.
- **Controls:** left or right opens the page for key mapping, controller, rumble and mouse.

Frame rate limits above 60 are experimental: effects may stutter. Restart after changing the limit so walking indoors stays smooth.

With this list, **F1** stays closed. To get the whole old menu for one run, for troubleshooting for example:

```
.venv\Scripts\python tools\run_with.py MENU_LOCK=0
```

## Updating

Get what's new and rebuild. This is usually much faster than the first time:

```
cd C:\KarPur\repo
```

```
git pull
```

```
.venv\Scripts\python -m pip install -r tools\requirements.txt
```

```
.venv\Scripts\python tools\fps_patches.py
```

```
.venv\Scripts\python tools\lift.py
```

```
.venv\Scripts\python tools\build_game.py
```

Close the game first: while it runs, `karpur.exe` cannot be overwritten.

## If something goes wrong

- **Save data:** it is in `build\profile\save\`. Back up that folder before trying anything big.
- **Log of the last run:** `build\game\logs\karpur.log`. Show the last 80 lines like this:

  ```
  Get-Content build\game\logs\karpur.log -Tail 80
  ```

- **Reporting a problem:** copy the exact error message or these log lines into an [issue on GitHub](https://github.com/Grunttanamo/KarPur/issues). Reports and the log contain names, numbers and addresses, but also paths from your PC, so have a quick look first. Dumps, keys and files from `game\` never belong there.

The project is still in development. Crashes and graphics errors can happen; what is open is listed in [progress.md](progress.md).

## Working on the project yourself

Once, turn on the protection that blocks commits containing game files:

```
git config core.hooksPath .githooks
```

Before every commit:

```
python tools/check_content.py --staged
```

Expected output: `check_content: OK`
