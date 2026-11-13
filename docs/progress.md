# Progress

## Pokedex crash: narrowed to an unregistered handler, not fixed (2026-11-12)

- Sharper reproduction from the maintainer, which changes the picture from the entry below: it is **not** about
  Tauros and **not** about opening an entry. Opening one works, A (details) works, + (habitat) works — **paging to
  the next entry with left/right crashes, on every entry**. It happens with and without any mod.
- The abort is always `CallImpl (invoker.h line 137)` from `main+0xb5d92c`: invoking a delegate that was never
  bound. `obs_abort_impl` now also prints the message's own arguments through the existing name filter, and they
  are two 64-bit hashes (`C3B091777E3EC94A5`, `SE94F3E13286232CF`) plus an object pointer — this game keys things
  by hashed name everywhere, so the game is asking for a handler under a name nothing registered.
- Ruled out along the way: it is not a load that never finishes (the probe's "slot 2 still loading" lines all
  belong to opening the page, and **no file access happens at all** in the frames before the abort, so paging
  loads nothing); it is not KarPur's own Lua extension (`TOTK_LUA_MOD=0` still crashes); it is not the spawn mod.
- Where it lives: the crashing function is `f_7000b5d450` (`main+0xb5d450`..`0xb5d960`), a delegate invoker that
  takes four arguments. `lua_pcallk` (`main+0xb5ddd0`, known from the `lua_mod` patch) is its neighbour, so this is
  the scripting/binding layer of the game, not game logic itself.
- **Root cause located.** The call the invoker asserts on is `main+0xb5ddd0`, which the `lua_mod` notes already
  name as `lua_pcallk`: paging runs a Lua function and it **raises an error**, and the abort is only the C++ side
  refusing a failed protected call. `obs_abort_impl` now also reads the error object off that state's stack (the
  state is in x22; the reader is guarded and cuts at the first non-printable character, and a script engine's own
  error is a diagnostic, not text of the game's). It says:
  `?:-1: attempt to index a nil value (field 'integer index')`.
  So a Lua table indexed by an integer gives nil and the script indexes that nil. The current entry displays and
  only paging fails, which fits a list that holds the entry being shown but not its neighbours. `?:-1` is Lua
  saying the chunk carries no name or line, so the scripts ship stripped and the error names no script.
- **What it is really about, found with that Lua hook.** The hook wrapped the member named by the abort and
  reported its calls: it runs constantly (over 200000 calls in a short session) and only raises while paging.
  Swallowing the error instead of re-raising it keeps the game alive, and then paging shows **the same species
  once per language** before moving on — so the member is the check that picks the one language entry to show.
  Scarlet and Violet record a Pokedex entry per language, unlocked by trading, and the slots a player has not
  unlocked are empty; the check walks into an empty slot and the index of it is nil. Filling every entry in every
  language made paging correct again.
- **Most likely not a bug of this port at all, and not a bug of the game's either.** The maintainer then
  remembered how that save got its Pokedex: a save editor had marked every species **seen** in every language.
  A foreign-language entry cannot be "seen" in the real game — it only exists for a species the player actually
  owns, through a trade — so the save held a combination the game never produces, and the script has no reason to
  expect it. That explains every symptom: the impossible rows are the empty slots the check walks into, and
  filling them in properly (owned, not merely seen) made it consistent again. **Unverified**: the clean check is
  a save the editor never touched (a new game), opening an entry and paging. Until that is done, treat the
  earlier reading of this entry — that it would hit every player — as withdrawn.
- The Lua hook lives in `build/game/dexprobe.lua` (ignored, never shipped) and is only a measuring tool: it is
  read by `TOTK_LUA_MOD_FILE`, which also replaces the built-in `menu_mod.lua`, so it must not be set for normal
  play. Nothing about it is needed for the fix.
- Next step from here: the project can already run its own Lua inside the game's state (`TOTK_LUA_MOD_FILE`,
  `runtime/lua_mod.c`, which sees every class the game's script defined), so the table that is short can be found
  and printed from a script instead of by more static reading. Older plan, now less likely to be the answer: find
  the registration counterpart that fills the container this invoker looks in, then
  probe whether that container is **empty** (an init step never ran — the same shape as the Wwise registry bug in
  the archive, where a find-or-create returned null because its root was never built) or merely missing this one
  hash. That decides between "a whole registration pass does not happen in this port" and "one specific handler
  does not".

## Pokedex crash on Tauros: first diagnosis, superseded by the entry above (2026-11-12)

- Reported by the maintainer, reproducible 100% of the time, with and without any mod: opening the Pokedex entry
  for Tauros aborts the game. Not a memory fault — the log shows the game's own assert firing
  (`guest abort in Abort (assert_api.cpp line 14), called from main+0x293d9bc`, then SVC 0x26 Break).
- Read back from there (lifted C, for investigation only, nothing copied here): `main+0x293d9bc` is the generic
  assert helper; the game code that asserts is `f_7000841140` (`main+0x841140`), and the condition is the return
  of `f_7000bc4dc0(object, 1)`. That function indexes a 16-byte-stride array out of the object, reads the state
  byte at `+8` of entry 1 and returns `(state != 1) && (state & 0xf) != 0` — the shape of a "has this resource
  finished loading" test, where 1 reads as still loading and 0 as never started. So the Pokedex asserts that
  resource slot 1 is ready and it is not.
- Nothing is missing on the file side: every `fs: GetEntryType` around the crash, the Pokedex archives included,
  resolves normally. So it is the slot's state, not a file that cannot be found.
- Plausible cause, untested: Tauros is the species with three Paldean forms, so its entry needs more loaded than
  others, and either one load never completes or the menu reaches the check before an asynchronous load does —
  a timing difference against the console is exactly the kind of thing this port changes.
- Probe built and run (`TOTK_DEX_PROBE=1`, `runtime/ov_system.c`, read-only, logs only the calls about to answer
  no): **slot 2, state 1, "still loading", over and over**. So the resource is asked for and never arrives, rather
  than never being requested. With the species caught (its entry registered), the run gets further and aborts
  somewhere else instead: `CallImpl (invoker.h line 137)` from `main+0xb5d92c`, the shape of invoking an unbound
  delegate, in the same menu subsystem (`main+0xb5d074` is in both backtraces). Right before it the game looks
  something up with index -1 and aborts on the result, so a lookup that finds nothing is the likely trigger.
  `obs_abort_impl` now also logs the abort message's numeric arguments (numbers only, never dereferenced) to name
  which item that is.
- Older next step, now superseded by the probe above: a read-only hook logging that state byte (0 vs 1 vs other)
  separates "never started" from "still loading" from "failed", which decides whether this is a race or a load
  that silently fails. The `FN_ENTER` hook point in `cpu.h` is how to reach it; reading is safe there, writing
  through a cached guest pointer is what went wrong on 2026-11-12 with the shadow work.

## Issue #45: a build that stops after 75 minutes (2026-11-13)

- A reporter's build lifted and compiled for 75 minutes and then ended with `ModuleNotFoundError: No module named
  'romfs_index'` at `tools/build_game.py` line 36. The module is tracked and ships in the launcher's payload, so
  it was there; what was missing is the search path. Python puts the script's own folder on `sys.path` only when
  it is started the usual way - with `-P` or `-I`, or with `PYTHONSAFEPATH` set in the environment, it does not,
  and then a tool importing a tool beside it fails. Reproduced exactly: `python -P -c "import romfs_index"` in
  `tools/` gives that very message.
- `build_game.py` and `romfs_index.py` now say where their siblings are
  (`sys.path.insert(0, Path(__file__).resolve().parent)`), which 7 other tools in the tree already did; those two
  were the only ones that imported a sibling without it. Checked with `-P`, `-I` and a normal start.
- Not reproducible here otherwise, so what sets it on that machine is unknown (`PYTHONSAFEPATH` is the likely
  one). The fix does not depend on knowing.

## Evolutions: the species table's format, and what trading gates (2026-11-13)

- For a "no trade evolutions" setting (offline, so trading cannot happen). The species table is
  `avalon/data/personal_array.bin`, pack 799, Oodle-kraken, 122439 -> 412572 bytes; it has no schema in the
  archive (`avalon/data/personal_array.bfbs` does not exist), so the layout below was read from the bytes.
- Root: one field, a vector of 1424 entries. In an entry, field 0 is the species number, field 1 the form, and
  **field 23 is a vector of 16-byte evolution structs**: `u16 level, u16 method, u16 argument, u16 x3, u16 target
  species, u16`. Pack 21985 holds `world/data/battle/plib_item_conversion/plib_item_conversion_array.bin`
  (865 -> 1604 bytes) with its own .bfbs, and `battle_item_conversion_array.bin` beside it.
- The methods, read off the data rather than from a list: **4** level reached (the level is in the struct's first
  field), **5 trade**, **6 trade holding an item** (argument a national item number), **8** use an item (argument
  a small index of its own, not the national number: Pikachu is 4), **19 / 20** level up holding an item by day /
  by night (small index too: Sneasel 49, Gligar 50, Happiny 9), **21** level up knowing a move (argument a move
  number: Lickitung 205, Tangela 246, Aipom 458).
- **Violet 4.0.0 has 12 plain trade evolutions** (Kadabra, Machoke, Graveler in two forms, Haunter, Boldore,
  Gurdurr, Phantump, Pumpkaboo in four sizes) **and 16 trade-holding-an-item ones** (Poliwhirl, Slowpoke, Onix,
  Rhydon, Seadra, Scyther, Electabuzz, Magmar, Porygon, Porygon2, Feebas, Dusclops, Clamperl twice, Spritzee,
  Swirlix).
- What the mod does (three files: the species table, the item conversion table, and `arc/data.trpfd`, which it
  only needs because it replaces files inside the archive): it **adds** 45 evolution structs rather than changing
  any. Haunter gets level 36; each trade-holding-an-item case gets a pair of methods 19 and 20 (day and night)
  with the item's small index, and the five small indices that had no item number yet are filled in in the
  conversion table. Diffing it field by field against the maintainer's own table is what showed this.
- So the two groups need different work. Method 5 -> 4 with a level is a change **inside** the existing struct:
  two bytes for the method, two for the level, no growth, a rule with nothing looked up. Method 6 needs the
  item's small index, which Violet's own data does not hold for these items (that is exactly what the mod had to
  fill in), so it cannot be derived from the game and is a decision rather than a transformation.
- Built for the first group: `TOTK_NO_TRADE_EVO=1` (launcher: "No trade evolutions", off by default).
  `data_patch.c evo_fix` walks the table after the decode and rewrites every method-5 struct, with the levels the
  maintainer chose, each above the level the Pokemon itself appears at: Kadabra 40, Machoke 39, Graveler 36,
  Haunter 42, Boldore 35, Gurdurr 40, Phantump 30, Pumpkaboo 30. A species with a trade evolution and no level
  here is logged and left alone, so a new one in a later game version shows up instead of being guessed at.
  Checked by replaying it on the maintainer's own table: all 12 structs rewritten (Graveler has two forms,
  Pumpkaboo four sizes), no method 5 left, nothing outside the list touched. Not tested in play yet.
- The second group, half of it: the small item space is `world/data/battle/plib_item_conversion` (pack 21985),
  **`ItemTableArray { itemID, plibID }`** - 106 rows mapping a national item number to the plibID that every
  method working without a trade uses. The game's own data proves it: plibID 1..6 are the national 80..85 (the
  five stones), 7 and 8 the Shiny and Dusk Stone, 52 the Ice Stone, and Pikachu's method-8 argument is 4, the
  Thunder Stone. So method 6 -> 8 with the argument translated is again a change inside the struct.
  **8 of the 16 are done** this way (Rhydon, Seadra, Electabuzz, Magmar, Porygon, Porygon2, Feebas, Dusclops).
- Method 8 was the first choice and was wrong: it means "the item is **used** on it", and none of these items can
  be used at all, only held, so the evolution stayed as unreachable as trading had left it (the maintainer found
  this in play). Only stones use method 8 in Violet's own data, which should have been the hint. It is method 19
  now, "levels up by day holding the item" - the game's own way of evolving something that holds an item, proved
  by its arguments resolving to the national 326 (Razor Claw, Sneasel) and 110 (Oval Stone, Happiny). There is no
  variant without a time of day; 20 is the same by night, and one struct holds one method, so it is by day. The
  mod writes both, which it can because it adds structs instead of changing them.
- Dusclops is the maintainer's exception (`evo_item_own`): Gligar's Razor Fang (national 327, plibID 50) by
  night, instead of the item its own row names. The seven others level up by day holding their own item.
- The other 8 name an item with **no plibID** (the King's Rock, the Metal Coat, both Deep Sea items, the Sachet
  and the Whipped Dream): the table has 32 rows that hold a plibID and no item at all, and giving one an item
  means writing a FlatBuffer field that is absent, which needs the file rewritten - what the mod does, filling
  five such rows. So those eight keep the condition but get **another item**, which the maintainer chose from the
  73 the game can name (`evo_item_own`, keyed on species and target, since Clamperl has two of these and they
  must stay apart). All 16 are covered now and nothing is left alone.
- A substitute item did not always work. The Metal Alloy (plibID 119), chosen for Onix and Scyther, did nothing
  in play. One cause was found and removed: 119 sits in a row with that item **and** in one of the 33 item-less
  rows, the only plibID of the thirteen used here that was not unique, so which row a lookup finds is luck.
  Moving the item-less row's plibID out of the way (a four-byte write; the field is there, and nothing can look
  that row up by item since it has none) still did not evolve it, so there is more to it - a plibID above the 118
  the game itself uses, or an item it will not accept in a condition, both untested.
- So Onix, Scyther, Spritzee and Swirlix take a level instead and no item at all (`evo_item_level`, the
  maintainer's levels: 40, 30, 20, 20), and the duplicate-row fix came out again rather than write to game data
  for nothing. Poliwhirl, Slowpoke and Clamperl keep a substitute item, which is untested.
- The item names came from the game's own text: `message/dat/German/common/itemname` (pack 12614, the AHTB labels
  are `ITEMNAME_<national number>`, so the label itself carries the number) decoded with `tools/msgpatch.py`'s
  reader - base the section start, key the line index, which a known item confirmed (number 83 decodes to
  Donnerstein, the Thunder Stone, whose plibID is 4, Pikachu's argument). Only numbers are written down here.
- Checked by replaying both halves on the maintainer's table: 12 on a level, 8 on an item, 8 left, and no method
  5 or usable method 6 remaining. Not tested in play yet.

## Sun shadows off, and where version exclusives are not (2026-11-13)

- `TOTK_SHADOW_RES=off` (launcher: Shadow quality "Disabled"): `vk_exec_draw.c shadow_pass_off` drops every draw
  whose only attachment is the square depth target of the shadow map's side (`res_shadow_map_side`), so the map
  keeps the value it was cleared to, the depth test finds nothing in front of the sun and nothing is shadowed. No
  patch in the game is needed, and the whole shadow pass becomes work the GPU never does. **Tested: works.**
- Looked for where version exclusives are decided, for a "no version exclusives" setting. The spawn data is
  `arc/worlddataencountsettingsettingdata.bin` (pack 21993, 44 files; `trpfs.py --watch "encount|wild_pokemon"`,
  then `--report` with the pack's own .bfbs). The species tables are `EncountPokeData[_lc|_su1|_su2]Array` ->
  `values [n x EncountPokeData_*]` with `devid`, `minlevel`, `maxlevel`, `biome1/2`, `lotvalue1/2`, `locationName`,
  `enabletable`, `timetable`, `flagName`, `outbreakLotvalue` and **`versiontable { A: bool, B: bool }`** — which
  looks exactly like the switch we want.
- It is not. Across every encounter table in Violet's archive (1021 entries, 526 distinct `devid`, up to 1024)
  **no `A` or `B` is false**, and `flagName` only ever names story progress (`FSYS_SCENARIO_GAME_CLEAR` and the
  like). The data is the same for both editions: Armarouge (#936, Scarlet's exclusive) has an entry in Violet with
  no flag of any kind, as do both version-split paradox Pokemon (#1009 and #1010).
- **It is the versiontable after all.** An exclusive does not store `false` for the other edition: the field is
  **absent**, and FlatBuffers then answers with the default. Larvitar (#246, Scarlet) has `A = 1` and no `B`;
  Bagon (#371, Violet) has `B = 1` and no `A`; everything else has both. Counting values present per field hid
  this twice over: absent fields are skipped, and there are exactly as many Scarlet as Violet exclusives, so the
  totals matched (426 and 426 in the 449-entry table). Checking the two halves of a known pair one by one is what
  showed it. A probe over the personal table's fields (`TOTK_EXCL_PROBE`, `shader_census.c`) found nothing there,
  as expected in hindsight: four fields "split every pair", but each had 32-40 distinct values (sizes, base stats),
  not the two or three an edition flag would have.
- Entry fields, for later: `devid, sex, formno, minlevel, maxlevel, lotvalue, biome1..4 + lotvalue1..4, area,
  locationName, minheight, maxheight, enabletable, timetable, flagName, bandrate, bandtype, bandpoke, bandSex,
  bandFormno, outbreakLotvalue, pokeVoiceClassification, versiontable, bringItem`; `VersionTable { A, B }` in
  slots 0 and 1. The species tables are the three `pokedata*_array.bin` of that pack (449, 252 and 254 entries);
  Pokemon Compass replaces them with 588, 258 and 335 entries and both flags written out on every one.
- **The fix needs no file rewrite.** A FlatBuffer table reaches its fields through a vtable, and an absent field
  has a 0 there. Writing the present flag's vtable entry into the absent one's makes both flags read the same
  byte, which is the stored `1`: two bytes per exclusive entry, exactly what `data_patch.c` can already do
  (`oodle <packed> <unpacked> <fnv> <offset> u16 mod=<value>`). Vtables are shared between tables of the same
  shape, which here is what we want, but a tool must check that nothing other than a `versiontable` uses the one
  it patches.
- First attempt, wrong and quietly harmful: pointing each absent flag's own vtable slot at the stored flag. It
  applied (the log showed all six bytes written) and changed nothing in play. Simulating the patch on the dumped
  table showed why, and that one check is what should have come before shipping it: the exclusives use **two**
  vtables, and one of them is **six bytes long** - it has no slot for the other edition's flag at all. Writing
  "its" slot wrote two bytes past the vtable, into whatever follows. Nothing crashed, but it was corruption.
- What works: in both vtables the stored flag sits at the same offset in the entry (+7). So the vtable that has
  room for both slots gets its absent slot pointed at that offset, and the entries whose vtable is too short get
  their vtable reference pointed at that one instead. `runtime/data_patch.c excl_fix` walks the table and does
  this itself rather than carrying written-down offsets, so another game version needs no new numbers, and it
  leaves the file alone unless it looks exactly as described. The three tables are named by unpacked size and the
  fingerprint of their packed bytes. `TOTK_NO_EXCLUSIVES=1` (launcher: "No version exclusives", off by default);
  `TOTK_NO_EXCLUSIVES=<path>` takes a list `tools/exclusives.py` wrote, which now follows the same steps.
- Checked by replaying the fix on all three dumped tables: 252, 254 and 449 entries, **every one** of them
  readable as available in both editions afterwards, 66 freed, nothing left over. **Tested in play: works** (a
  Pupitar, Scarlet's, found in Violet). The log names the counts per table, so a file that does not match is
  visible at once: "46 of 449 entries freed (23 pointed at the other vtable), flag at +7".
- A mod that does only this (three replaced `pokedata*_array.bin`) turned out to make the same change - both flags
  written out on all 449 entries - and additionally raises 231 `lotvalue` weights, which is a rarity boost and no
  part of the edition logic. Comparing every field of it against the original is what confirmed that nothing else
  is involved.
- Still open: the Pokedex shows no habitat for a freed Pokemon (a "?"). That list is its own data, not these
  tables, and the mod has the same gap.
- The `TOTK_EXCL_PROBE` probe is removed again: the personal table is ruled out as the place, and this records it.
- The species the tool found are the published exclusive list, which is the check that it found the right thing:
  Paldea 128 (Tauros' fire and water breeds are version-bound too), 200, 246, 247, 316, 317, 371, 372, 425, 426,
  429, 434, 435, 633, 634, 690-693, 765, 766, 874, 875, 885, 886 and the paradox Pokemon 978-993; Kitakami 190,
  207, 313, 314, 845, 877; the Indigo Disk 27, 37, 311, 312, 408, 410.
- Not tested in play yet.

## Shadow sizes, the actual fix: the map costs the game nothing (2026-11-13)

- The two entries below are **superseded**. Growing the game's GPU memory pool to pay for a bigger shadow map was
  the wrong track from the start, and every symptom chased since came from it.
- What it looked like: with any size above the game's own, NPCs in towns vanished a step or two away and came back
  when walked up to, and wild Pokemon did the same in the open world. More pool made it worse, less made it better,
  and at 4096 no value satisfied towns and the open world at once.
- First hypothesis, wrong: the game carves fixed heaps from a `HeapDef` table and never looks at the application
  pool, so the pool should grow with it. Violet has no such table (the path is TotK's; the log says `HeapDef …
  missing`), and the change was a no-op. Second hypothesis, also wrong: the pools are carved at fixed offsets of one
  parent, so a bigger one overlaps its neighbour. A probe of every pool above 32 MiB
  (`TOTK_SHADOW_PROBE`, `nvn_api_core.c`) showed them laid end to end with the neighbour simply moving up — no
  overlap, the room really is added.
- What settled it was measuring the draw distance in one spot with each switch on its own. The game's own
  behaviour is about **30 m**; normal shadows with a 448 MiB pool gave **2-3 m**; a bigger shadow map with the pool
  untouched gave 30 m but crashed on load, because the map does not fit. So the pool was the cause, not the map.
  The sizes in the table at main+0x146e6xx add up to roughly 3040 MiB of the 3285 MiB application pool: the game
  budgets what is left for streaming, and the 180 MiB we took at 4096 came off exactly that.
- **The fix: tell the game the truth it needs and no more.** `nvnTextureBuilderGetStorageSize`
  (`runtime/nvn_api_obj.c`, `storage_size_for_game`) reports a patched shadow map as the 1024 map it replaces — 4
  MiB instead of 128 at 4096. Its storage in the game's pool is bookkeeping for a depth target the CPU never reads
  and the back end holds as its own image, so nothing in the game depends on the number being real. The game's
  pool, its heaps and its streaming budget stay exactly as shipped, and the map costs it nothing.
- Removed with it: `TOTK_VRAM_POOL_MIB`, `res_vram_pool_add` / `res_vram_pool_init`, the `vrampool` patch site in
  `tools/fps_patches.py`, and the shadow extra in `res_heap_extra_bytes`. `res_heap_extra_kib` and the `HeapDef`
  rewrite are back to serving the resolution settings alone.
- **Tested through the launcher: 2048, 3072 and 4096 all run, the shadows are sharp, and NPCs in towns and wild
  Pokemon in the open world behave as the game's own build does.** Not committed.
- Lesson, the second time this bit: a diagnostic switch must be read with `rt_knob`, not `cfg_int`. `cfg_int`
  answers from the environment first and only then needs a row in the settings table, so a probe read with it runs
  fine under `$env:TOTK_…=1` and kills the game at start for anyone who does not set the variable — which is every
  launcher start. Found after the maintainer could not start the game from the launcher at all; every `cfg_int` and
  `cfg_value` key in `runtime/` was then checked against the table (all have rows; the lenient `cfg_get` needs
  none). Test the launcher path, not only the environment-variable path: the launcher is how the game is played.

## Shadow sizes, the real fix: measure the cost, add the room, do not take it (2026-11-12)

- The entry below declared the four sizes working after the `vrampool` patch. They were not. Playing properly
  found two things that only a boot test misses: **wild Pokemon stopped spawning** (gone when looked at, popping
  in elsewhere) at every size above the game's own, and **changing area (DLC <-> base map) crashed every time**,
  with the same null-pointer read in the same registry function as before but reached from the area-loading path.
- Narrowed it with one decisive test: `TOTK_SHADOW_RES=1024` — the patch switched **on** but handing the game the
  same number it uses itself. That ran clean, so the patch mechanism is sound and the size is what matters.
- Then measured instead of assuming. The probe now logs the finished texture's own byte size: the shadow map is a
  **2-layer DEPTH32F array, 131072 KiB at 4096**, so it costs (side^2 - 1024^2) * 4 * 2 over the game's own, which
  is 24 / 64 / 120 MiB at 2048 / 3072 / 4096. The first formula had guessed 8 layers from 2048 upward, so it gave
  0 extra at 2048 (hence the spawn failures and the crash there) and 480 MiB at 4096 — four times too much.
- Too much turned out to be its own failure mode, and the more interesting half of this: the pool the patch grows
  is carved from the game's own fixed budget, so 800 MiB of it starved everything else and the wild Pokemon stopped
  spawning again, this time without a crash. The fix is to stop redistributing: `res_shadow_extra_bytes`
  (`runtime/resolution.c`) derives the measured amount from the setting alone, `res_heap_extra_bytes` adds it to
  the **application pool** in `mem_init` (the mechanism the resolution work already used), and the `vrampool`
  patch hands the GPU pool the game's own 268 MiB plus the same number. Room added, not taken; 50% margin on top.
- **Confirmed in actual play at 2048 and 4096:** shadows sharper, wild Pokemon spawning normally, area changes
  holding. The memory census line in the log is the direct check that it worked — the one
  `CPU_NO_ACCESS|GPU_CACHED|COMPRESSIBLE` pool reads 268.0 MiB untouched, and exactly the patched size with a
  shadow setting on. All four sizes are back in the launcher's "Shadow quality". Not committed yet.
- Note for anything that enlarges a render target later (render scale, output resolution): call
  `res_vram_pool_add` **and** make sure `res_heap_extra_bytes` covers the same amount, or it will take the room
  from the game instead of adding it, and the failure shows up as missing wild Pokemon rather than as a crash.
- `TOTK_POPULATION` above the game's own 15 (the launcher's "Wild Pokémon" row) is a pure data patch that reserves
  nothing, so it takes from the same unchanged budget and was the obvious thing to break next. **Tested: "extreme"
  (120) together with shadows at 4096 runs fine** — plenty of Pokemon on screen, shadows clean, no spawn trouble.
  No extra headroom needed for it, which also says the shadow side really is neutral now.

## GPU memory pool patch: first attempt, superseded by the entry above (2026-11-12)

- Chased the 3072/4096 crash down to its exact cause, with the same technique as the shadow site (counting
  instructions from a known label in the lifted C, maintainer's permission to read it for this, nothing of it
  copied into any file here): the crash is a null-pointer read in the game's own object registry, one step after a
  second lookup silently returns nothing — a classic "ran out of a fixed-size pool" shape, not checked for. Traced
  that pool back to `main+0x146e770`, a function that downgrades a GPU memory pool size from 320 MiB (what the
  shipped data says) to 268 MiB — the exact same instruction word as the shadow site, independently derived and
  confirmed correct by `tools/fps_patches.py`'s own byte check again on the first try.
- Added a second, generic patch set, `vrampool` (`TOTK_VRAM_POOL_MIB` is not a real knob — it's driven by
  `res_vram_pool_add`/`res_vram_pool_init`, `runtime/resolution.c`): any setting that enlarges a render target adds
  how many extra bytes it needs before boot finishes, and this reads the total from the patch page instead of
  downgrading to 268 MiB. `res_shadow_init` now calls it for `TOTK_SHADOW_RES` above 2048, with a generous, rough,
  untested-beyond-today amount scaled on the extra pixel area. Thought to be confirmed at the time because all four
  sizes booted and switched cleanly — **wrong**: nothing here had been played properly yet, and both the amount and
  where it was taken from were off. See the entry above.
- This is written to be reusable: the render-scale and output-resolution work this project keeps running into the
  same missing-patch-table wall on (see the 2026-11-11 and earlier 2026-11-12 entries below) will likely need extra
  room in the same pool once it exists, and can just call `res_vram_pool_add` too instead of finding this crash
  again from scratch.
- Launcher: restored "x3 (3072×3072)" and "x4 (4096×4096)" to "Shadow quality" (`launcher/MainForm.cs`) now that
  they are confirmed safe. Both the `shadow` and `vrampool` sites are in `tools/fps_patches.py`'s own table, so
  they survive its next regeneration. Not committed yet.

## Shadow map size: working, real patch, launcher setting (2026-11-12)

- Found the actual instruction, independently, by counting from a known label inside the lifted C of the one
  function that builds the shadow render target: `main+0x16dc200`, a 4-byte sign-extending load of the field at
  `[x19, #0x3d4]` into x13 (`ldrsw x13, [x19, #0x3d4]`, instruction word `0xb983d66d`) — the exact same read the two
  earlier, reverted attempts below kept finding recomputed and reset before they could use it.
- Added a real instruction patch using the project's own existing, previously unused mechanism (`recomp/patches.py`,
  `gen.py --patches`, documented in `recomp/patches.py`'s own docstring and already working for Violet's frame-rate
  and LOD patches — resolution.c's `output`/`scene`/`aspect`/`fov` are the ones still missing their site files, not
  a missing mechanism): a new `shadow` patch set, `site shadow main 16dc200 b983d66d @page:x13 <ldr from the patch
  page>`, replacing that one load with one that reads `TOTK_SHADOW_RES` from the patch page instead of the game's
  own field. `tools/lift.py` checked the original instruction word against the real binary and accepted it on the
  first try. New runtime function `runtime/resolution.c res_shadow_init`, called once at boot, writes the value and
  switches the set on.
- **Confirmed working, repeatedly, in actual play:** shadows sit where they belong and look visibly sharper at
  `TOTK_SHADOW_RES=2048`. `3072` and `4096` crash the game on load every single time, even with a freshly emptied
  pipeline cache (ruled out as the cause) — most likely the game's own GPU memory budget for render targets is not
  sized for a shadow map that much bigger; not investigated further today. Added
  `invalidate_pipeline_cache_if_needed` (`resolution.c`) regardless: a marker file next to `pipeline-cache.bin`
  records the shadow size it was last built for, and drops the cache on a mismatch, since a stale cached pipeline
  for a different size is still a real way for this setting to misbehave even where the size itself is safe.
- Added "Shadow quality" to the launcher's Settings page (`launcher/MainForm.cs`): Original (1024×1024) and x2
  (2048×2048) only, since x3/x4 are not safe yet. Writes `shadow_res` under `[display]` in `karpur.ini`, same
  pattern as every other dropdown there. Built and the maintainer confirmed both options save, load and run.
- Moved the `shadow` site into `tools/fps_patches.py`'s own `SITES` table (new `ldr_w` helper, `PAGE_SHADOW_RES`),
  next to the other test patches it already carries (`lod_bias`, `dynres_full`, ...) — it had only been hand-
  appended to the generated `build/fps_patches.txt` while testing, which `tools/fps_patches.py` would have
  overwritten on its next run. Re-ran it: still validates against the real binary, same bytes. Not committed yet.

## Shadow map size: second attempt, also reverted, caused a crash (2026-11-12)

- Continuing straight on from the attempt below: writing the field once, at the entry of the one function that
  builds the shadow render target, correctly fixed shadow *placement* (fence/tree shadows sat where they belonged
  again) but did not change the actual size — traced with a second diagnostic (`TOTK_SHADOW_TRACE`, also removed)
  down to one specific point: something inside `main+0x5380480`, called from within that same one-time setup,
  reads the field back down from our value to 1024 before the real consumer ever sees it. This confirmed the field
  is not a stable "setting" another part of the game can just be told a new value for — something recomputes it
  fresh each time from whatever the game's actual settings say (1024, no exposed way to change it yet).
- Tried writing the field a second time, right after the point that resets it, to beat the reset. **Reverted: this
  crashed the game outright (not every time; silent, no window, process gone, no clean exit in the log).** The
  write re-used a cached object pointer across an unbounded number of later, unrelated calls to the same reset
  function (which runs for other reasons too, elsewhere, with a different object each time) — writing through a
  stale pointer there, into memory that may no longer belong to that object, is exactly the kind of corruption that
  crashes without a clean error. Removed the whole mechanism (the field-write hook, its hand-written hook point in
  `cpu.h`'s `FN_ENTER`, its boot-time arming call, both cfg rows) back to the same safe state as below:
  `TOTK_SHADOW_PROBE` only, no behaviour change. Rebuilt and confirmed the game boots and plays normally again.
  Not committed.
- **Conclusion for this whole thread:** the shadow map's size is a real, dynamically-read value, not a hardcoded
  instruction — but reading it is as far as a host-side hook can safely go. Changing it for good needs the game's
  own patch-table mechanism, which does not exist yet for Violet (`output`/`scene`/`fov`/`aspect` all already
  depend on it and are currently inert — see the 2026-11-11 render-scale entries). Picking this up again means
  building that mechanism first, not another hook.

## Shadow map size: TOTK_SHADOW_RES tried and reverted, breaks shadow placement (2026-11-12)

- Followed up on the call chain below: the size is not a fixed instruction to patch, but two 32-bit fields read from
  a renderer object right before the texture is built (confirmed by hand, not by a probe this maintainer ran — see
  the game-content-boundary note below). The patch-table mechanism that would be the "proper" way to override it
  (like `output`/`scene`/`fov`/`aspect`) does not exist for Violet yet, same blocker as the render-scale thread.
- Tried the simpler route instead: `TOTK_SHADOW_RES=<pixels>` (`runtime/nvn_api_obj.c`, `nvnTextureInitialize`)
  resized just the one texture `TOTK_SHADOW_PROBE` identified (square, `NVN_FORMAT_DEPTH32F`) before the host built
  it, without touching the game's own two fields.
- **Reverted: broke shadow placement.** At `TOTK_SHADOW_RES=2048` the log showed no crash or validation error and
  the shadows looked sharper at first glance, but the maintainer then found every shadow (fence, trees, everything)
  stuck to the player's own position instead of the object casting it — moves with the player regardless of what
  is actually there. The texture's own memory/layout was fine; what broke is the mapping between world space and
  the (now bigger) texture, most likely because the game's own two size fields (used for the shadow projection the
  CPU sets up) never changed, only the texture the host built from them. Removed the override entirely
  (`TOTK_SHADOW_RES` and its `cfg.c` entry); `TOTK_SHADOW_PROBE` (the diagnostic, no behaviour change) stays.
  **This path needs the real patch-table mechanism (overriding the CPU-side fields themselves, consistently),
  not a host-side texture resize.** Not committed.
- One slip this session, corrected: briefly read `generated/main/*.c` (the lifted game code) directly while tracing
  the call chain, which this project's own rule says agents must never do. The maintainer explicitly allowed it for
  this investigation, on the condition that nothing from it is written into any file that reaches GitHub — the
  notes above describe behavior and addresses only, never the lifted code itself.

## Shadow map size: found the call chain, not yet a patch (2026-11-12)

- Continuing the resolution/render-scale thread from 2026-11-11 (output res and render scale are a bigger job: no
  "output"/"scene" patch set exists for Violet, and the main pass renders directly at the output size rather than
  through one scaled scene buffer). Picking this up separately, since it looked more tractable: the per-frame
  pass-size log from that earlier probe already showed a `1024x1024+depth` pass with many draws every frame,
  alongside the main `1920x1080+depth` one — a shadow-map candidate.
- Added `TOTK_SHADOW_PROBE=1` (diagnostic, off by default, `runtime/nvn_api_obj.c`): logs the first call from each
  distinct (call chain, size) that initializes a depth-format texture (`nvnTextureInitialize`), walking the frame-
  pointer chain up 6 levels (the same technique as `value_scan.c`'s `watch_write`, which only needed 2).
- **Found:** the immediate two callers (`main+0x107f450`, `main+0x107ef30`) and the next two above them
  (`main+0x107e4d0`, `main+0x107e288`) are shared by every render target the game builds (1920x1080, 960x540,
  480x270, all `fmt=0x35` depth+stencil, and the 1024x1024 one). The chain only splits at the 5th level: the main
  scene's render targets all come from `main+0x2ac446c` / `main+0x2ac4484`, while the **1024x1024, `fmt=0x34`
  (depth only, no stencil) texture comes from its own chain, `main+0x1321468` -> `main+0x16dc2c4`** — a separate,
  shadow-specific setup path. This confirms it is the shadow map and not another use of that size.
- **Not done yet:** this only says where the render target is *built*, not where its size (1024) is chosen as a
  literal — that needs reading the disassembly around `main+0x16dc2c4` / `main+0x1321468` (or a register-value
  probe at that call) to find the instruction to patch, the same kind of work as the `output`/`scene` patch sets
  this would need to follow. Bigger step than the probe; not started.

## F1/Esc: performance overlay choice; launcher: removed Render scale (2026-11-11)

- The Performance overlay choice (`tab_screen`, Off/FPS/Full) was only reachable with `TOTK_MENU_LOCK=0` (the old
  full tabbed menu); F1/Esc opens the simplified, locked Controls-only page by default, which had no way to change it
  in-game at all (launcher only). Added the same `opt_choice("TOTK_PERF_OVERLAY", ...)` row to `tab_controls`, so it
  now shows on both the locked and the full menu (mutually exclusive per frame, no ID clash). Syntax-checked with
  clang; not yet built into the real game.
- Removed the launcher's "Render scale" row (`MainForm.cs`: the `renderScale` control, `RenderScales` array, its
  note, and the `render_scale` ini write) since it does not do anything yet (see above) and was confusing; the
  underlying `TOTK_RENDER_SCALE` cfg/runtime support and the `TOTK_RES_PROBE` diagnostic stay for whenever that gets
  picked up again. Builds (0 warnings/errors), copied into `launcher/bin/Preview2`. No commit/push.

## Overlay: dropped the "3D scene" line (2026-11-11)

- Maintainer's ask: the Full overlay's "3D scene" row always read the same as "Game image" (always 100%, since there is
  no output/scene patch set for Violet/Scarlet) and was pure noise. Removed it from `perf_picture`
  (`runtime/ui/menu.cpp`) and the now-unused `UI_MENU_OV_SCENE` / `UI_MENU_OV_FMT_SCENE` strings (en, ru) from
  `ui_text.h`/`ui_text.c`. Also noticed, while touching this file, two other dead in-game-menu strings
  (`UI_MENU_RENDER_SCALE*`, `UI_MENU_OUTPUT_RES*`) that were never wired into any menu row — left them alone for now,
  not part of this ask. Syntax-checked with clang; not yet built into a full game exe or run. No commit/push.

## Handheld-mode setting, real performance win (2026-11-11)

- The deep trace below for render-scale bottomed out at a persistent display-config object (reached through two
  pointer hops, a field copied from `x23+0x8`), not a single literal to patch; before going further the maintainer
  tried the console's own existing docked/handheld switch (`TOTK_OPERATION_MODE`, already in `cfg.c`, read by
  `ov_system.c`) as a quick side test. **Confirmed: handheld renders at 1280x720** (the Switch's own real handheld
  resolution), a genuine speed gain with zero reverse engineering, since the game already supports it natively.
- Added "Mode" (Docked 1920x1080 / Handheld 1280x720) to the launcher's Settings page (`MainForm.cs`), next to the
  render scale/GPU rows: `ini.Set("display", "operation_mode", ...)`, loaded/saved like the other dropdowns. Builds
  (0 warnings/errors), copied into `launcher/bin/Preview2`. This is the actually useful, no-reverse-engineering AMD/
  weak-GPU performance option the maintainer originally wanted from the render-scale idea; it stays in the Settings
  page as its own thing. No commit/push.

## Render scale: finding out why it does nothing (2026-11-11)

- The maintainer set `render_scale = 50` and forced the iGPU via the new GPU switch; the game ran on it (confirmed:
  overlay named "AMD Radeon(TM) Graphics"), but the "3D scene" overlay line stayed "1920x1080, 100%" and no FPS
  changed. The log explains why: `resolution: the generated C has no output and scene patch sets; the game keeps its
  own resolution`. `resolution.c`'s comments cite `recomp/patches-v121.txt` / `patches-v143.txt` as where the real
  hook into the game's own render-target sizes would live, but neither file, nor any patch set named "output" or
  "scene", exists anywhere in the repo (`recomp/patches.py` only has the page-layout comment; the only real patch
  table produced anywhere is `build/fps_patches.txt` for the frame-rate/LOD/data-watch fixes). So `TOTK_RENDER_SCALE`
  (and `TOTK_OUTPUT_RES`, the aspect-ratio and FOV patches with it) have never actually been wired into Violet: this is
  a pre-existing gap, not something this session's launcher change broke. Corrected course with the maintainer instead
  of quietly shipping a setting that does nothing.
- **First step towards a real fix:** `runtime/nvn_api_obj.c nvnTextureBuilderSetSize2D` now calls `res_probe_note`
  (`TOTK_RES_PROBE=1`, diagnostic, off by default): logs the first call from each distinct caller (by its own return
  address, `ldr_describe`) that builds a 2D texture sized 1920, 1080, 1600 or 900 on either side, to find which
  function(s) in the game's own code set its fixed output/scene render target sizes. Also added `GPU_NAME` to
  `cfg.c`'s section table (the GPU launcher setting from earlier today) and `RES_PROBE` to its known-keys list.
  Syntax-checked with clang (`-fsyntax-only`, one pre-existing unrelated warning); not yet run against the real game.
- **First run (test RA):** one hit, `SetSize2D 1920x1080 from main+0x107f154`, right before the game's own present-
  interval setup at boot: a strong candidate for the output/swapchain surface. No 1600x900 scene hit at all, so the
  scene size is probably not built through one `SetSize2D` call. Extended the probe to `nvnTextureBuilderSetWidth` /
  `SetHeight` too (each logged on its own, deduped by caller + which API), since the scene may set its two dimensions
  separately. Not yet re-run.
- **Second run (test RB, on the iGPU this time):** three hits, all **1920x1080**, none at 1600x900: `SetSize2D
  1920x1080 from main+0x107f154` (the output surface, as before), and a pair right next to each other,
  `SetWidth 1920x0 from main+0x7f1788` / `SetHeight 0x1080 from main+0x7f17a8` (likely one more 1920x1080 surface built
  through the other API shape, not the scene). The frame's own pass-size report in the same log
  (`vk_exec: pass sizes per frame ... 1920x1080+depth 38.5 passes 425 draws, 960x540+depth 31.4 passes 179 draws,
  1024x1024+depth 19.4 passes 163 draws, ...`) shows the main pass already drawing directly at 1920x1080, with the
  other sizes looking like separate fixed-size passes (shadow maps, a half-res post effect), not one shared "scene"
  surface scaled up to the output afterwards. So this port may not render through a single smaller internal buffer the
  way `resolution.c`'s comments assumed (modelled on the original Switch docked 1600x900 -> 1080p upscale); if true,
  there is no one constant to patch for a cheap win, and a real render-scale would mean changing the main pass's own
  viewport/attachment sizes throughout, a bigger and less certain job than first estimated. Told the maintainer plainly
  instead of continuing to probe without a clear next hypothesis. No commit/push.

## AMD render scale setting (2026-11-11)

- Exposed `TOTK_RENDER_SCALE` (already read by `resolution.c`, unrelated to this session's reverted Compass work) as a
  "Render scale" dropdown on the launcher's Settings page (`launcher/MainForm.cs`): 50/67/75/game's own (default,
  5/6)/100/125/150/200%. It sits above Upscaling, with a note that pairing a lower scale with AMD FSR 1 is a real speed
  gain on non-NVIDIA cards, no new runtime code needed (the scene renders smaller, the game's own FSR 1 EASU scales it to
  `TOTK_OUTPUT_RES`, then the present's FSR 1 / bilinear scales that to the window). Written to `karpur.ini`
  `[display] render_scale`, read back the same way as the other Settings fields.
- Not yet built or tested with the real game; the maintainer will try it on the RDNA 2 iGPU next. No commit/push.
- **Graphics card setting (2026-11-11):** added "Graphics card" to Settings (`MainForm.cs DetectGpus`): runs the built
  `karpur.exe --probe-vkdevice` once per exe (cached), which only enumerates Vulkan devices (no game files touched),
  and reads the "vk: gpu N: name (type T, ...)" lines `vk_device.c pick_device` already logs; "Automatic" plus one entry
  per found card (name, "(integrated)"/"(discrete)"), written to `karpur.ini` `[display] gpu_name` (already read by
  `vk_device.c`, matched as a case-insensitive substring; added `GPU_NAME` to `cfg.c`'s section table for consistency).
  Maintainer's ask: picking a card that is not NVIDIA should lock the NVIDIA-only options instead of silently failing.
  `UpdateGpuLock` disables Anti-aliasing, DLSS model, Frame generation, "Add nvngx_dlss.dll" and "Add Streamline files",
  switches them off/away if they were on, and removes the two DLSS entries from Upscaling, with a note saying why;
  "Automatic" leaves everything open (any installed card could end up running the game). Builds (0 warnings/errors).
  - **Found and fixed:** the maintainer had a stale `gpu_index = 0` in `karpur.ini` from before this dropdown existed
    (an earlier, manual workaround this chat suggested). `vk_device.c pick_device` ANDs `TOTK_GPU_INDEX` and
    `TOTK_GPU_NAME` when both are set, so index 0 (the RTX 5080) plus name "AMD Radeon..." matched no device at all:
    `FATAL: vk: no usable Vulkan device`, the game exiting in well under a second regardless of which card the
    dropdown had selected. `SaveGameSettings` now also clears `gpu_index` whenever the dropdown saves, since the
    launcher's own GPU setting replaces that manual step. Fixed the maintainer's existing `karpur.ini` by hand too.
    No commit/push.

## Compass instruction compatibility probe (2026-11-11)

- Added an explicit experimental --plugin-compat generator profile, used only by tools/compass_workspace.py probe. It models TPIDR2 as per-thread storage in Cpu's reserved padding, disabled-GCS reads as zero, and SMSTOP disable forms in the fixed non-streaming/no-ZA model. Pending TPIDR2 state faults explicitly; SMSTART, SVCR and guarded-stack writes remain rejected. This is limited compatibility behavior, not complete SME/GCS emulation. Cpu's size/vector offsets stay unchanged; kernel fiber switches propagate the new thread-local slot.
- Reserved all-ones encodings stay undefined-instruction traps instead of unsupported-FP false positives. Independently checked 1024 synthetic encodings with LLVM for Cortex-A57 and generic/extended targets. No plugin bytes copied into source.
- Probe 20261111-095204: 326 functions, seven compatibility sites, 2/2 C files compiled, zero compiler errors. The raw unsupported count is still 1: an unallocated slot at subsdk9+0xa8. It stays a trap and is unreachable from its emitted +0x38 entry under the current static CFG. The probe records that separately; it does not hide the raw count, remove the fault, prove runtime hook reachability or claim a runnable Compass build.
- Tests: seven Python decoding/control-flow cases, native synthetic C execution for register round-trips/context independence/XZR/GCS-off/SMSTOP preservation/Cpu ABI, four guarded exit cases, four EH regressions, strict-system output comparison with the backup for 131072 synthetic encodings, and kernel.c syntax check. Full Compass initialization/execution remains untested.
- Existing source/docs backed up to C:/KarPur/backup/compass-instructions-20261111. Normal game, preview2, real saves and mod switches remain unchanged. No commit/push. See docs/compass-compatibility.md for limitations and the next integration stages.


## Isolated Compass workspace and first plugin compilation (2026-11-11)

- Maintainer provided C:/KarPur/KarPurCompassTest. Added tools/compass_workspace.py: prepare verifies independent baseline/native DLL/ExeFS copies, creates fresh baseline/profile and compass-profile, and provides Run-Baseline.cmd with explicit profile/ExeFS/RomFS paths. It strips inherited TOTK variables and sets a separate shader cache. No real saves are copied. Baseline self-test: 0 failures. Compass stays inactive.
- Plugin generation exposed a missing EH table represented by equal, nonzero start/end offsets. Fixed recomp/module.py to skip that absent table and recomp/gen.py inner_mask to handle zero FDEs. Four synthetic tests pass; original main/subsdk0/sdk EH starts/ranges match the backed-up parser (177528/9831/30320 entries).
- Plugin-only probe in KarPurCompassTest/probes/20261111-093314: 326 functions, 32210 instruction slots (includes padding/traps), two C files, two compiled successfully, zero compiler errors. Eight unsupported instruction sites remain (system-register/system and FP/SIMD classes). This is not an executable Compass build; the hook bridge, linker compatibility and missing RomFS paths remain unimplemented. No partly supported mod was enabled.
- Backups before source/docs edits: C:/KarPur/backup/compass-workspace-20261111. No changes to original ExeFS, normal game/preview2 binaries, mod toggles or real saves; no commit/push. Details in compass-compatibility.md.


## Compass / TotK ExeFS investigation (2026-11-11)

- Inspected TotK Recomp at 7a37fc3c346cd6f2b3c80637effb27b880ce817b. It supports compiled IPS/IPSwitch patch sets, but explicitly rejects whole mods containing subsdk plugins or replacement main modules. This does not supply a ready-made loader for Compass.
- Added `tools/mod_exefs_report.py`, a read-only metadata diagnostic. Both local Compass packages contain main.npdm + subsdk9, not IPS patches. Violet: 93,829-byte NSO, 487 relocations, 15 imports (13 weak), 27 exports and five constructor-array entries. One strong import has no definition in the original linked game modules: nn::ro::detail::g_LookupGlobalManualFunctionPointer. Symbol presence alone does not verify runtime compatibility.
- The plugin exports exl_inline_hook_impl. exlaunch's public implementation creates ARM trampolines at runtime; KarPur must bridge hooks to lifted code, including direct calls and interior function sites. Merely adding the module or making text writable is insufficient. The missing RomFS additions/uncompressed replacements remain separate requirements.
- Full evidence, pinned upstream links and implementation stages: [compass-compatibility.md](compass-compatibility.md). Metadata-only local reports: build/compass-research-20261111/{violet,scarlet}-exefs.json. No mod toggles, launcher/game executables, save files or original ExeFS changed; no commit/push. Backed up this document under C:/KarPur/backup/compass-research-20261111 before editing.


## Launcher banner alignment and input hints (2026-11-11)

- Home metadata now shares the bottom edge of Play/Open game folder at both normal and minimum window sizes. Compact labels and action widths keep all fields on one row in narrow windows.
- Violet's logo is right-aligned; only its background crop moves downward to show Miraidon's head. Scarlet retains its previous crop and left logo. Source image files are unchanged.
- A/B badges have a shaded face, upper highlight and drop shadow. Their glyphs use visible-ink bounds for centering; Back sits closer to B.
- An application message filter observes clicks and mouse-wheel scrolling in the launcher, hides pad hints without consuming mouse input, and lets subsequent pad input restore the contextual hint. Hover alone does not interrupt controller navigation. The filter is removed when the form closes.
- Backups preserve source/docs and replaced preview2 outputs under `C:\KarPur\backup\launcher-polish-20261111`. No native game rebuild, commit or push.


## Launcher edition artwork and contextual controller hints (2026-11-11)

- Home now uses an edge-to-edge, square banner with the maintainer's supplied Scarlet/Violet backgrounds and transparent logos, following the automatic/manual launcher palette. Sidebar edition icons follow the same choice after a build; the neutral supplied image is also the executable icon.
- Removed the three redundant Home statistic cards and all Home shader counts. Banner chips show game version, DLC and the most recent matching launcher-start.log record, falling back to an explicitly labelled save timestamp. Team/profile/icon loading is unchanged.
- Sidebar A appears only on the controller-selected tab. Entering with A replaces it with B Back above Play; returning or switching tabs hides B. Removed persistent controller instruction text.
- Vertical pad movement prioritizes the nearest visual row, preventing wide checkboxes from being skipped in favour of distant narrow controls. Setup remains editable and Play still follows its selected target.
- Source/assets and changed preview2 publish outputs are backed up under `C:\KarPur\backup\launcher-art-20261111-0917`, preserving repository-relative paths. Native game executable is not rebuilt. No commit or push.


The current state, the decisions that shaped it, and what comes next. Newest log entries on top.

The detailed German log up to 2026-11-08 (every test, A to BT, with its numbers) is in [progress-archive-de.md](progress-archive-de.md). Code comments that cite `docs/progress.md` with a date up to 2026-11-08 refer to that archive.

## Current milestone: M6, PC extras (M5 complete)

*Pokémon Violet* 4.0.0 runs from the title screen into the open world, with battles, picnics, fast travel, saving and loading. On the maintainer's PC (Ryzen 7 9800X3D, RTX 5080) it holds 56–60 FPS almost everywhere, towns included, with no tree flicker (test BT, 2026-11-08). The game's main menu has a PC Settings page; the older full settings menu on F1 is locked (`TOTK_MENU_LOCK=0` opens it). The KarPur Launcher 0.3.2 builds the game from a player's own dumps without any setup ([launcher.md](launcher.md), German).

Scarlet and other game versions are untested.

## Defaults that matter

Runtime switches are environment variables `TOTK_<NAME>` or lines in `karpur.ini`; `tools/run_with.py NAME=VALUE` sets them for one run. These defaults are the result of the frame rate and flicker work up to test BT:

| Switch | Default | What it does |
|---|---|---|
| `SUBMIT_SYNC` | 0 | Submits do not wait for the GPU (CPU and GPU overlap). |
| `SUBMIT_DEFER`, `SUBMIT_DEFER_US`, `DEFER_PRESENT` | 1, 6000, 1 | A recorded submission reaches the GPU when the next one begins, before a present/upload/wait, or after 6 ms: late CPU writes reach the VRAM mirrors first. |
| `HOT_REFRESH` | 0 | Hot chunks (memory the CPU writes late) are bound from imported guest memory, so the GPU reads them when it runs, as on the Switch. |
| `HOT_LATE_SUBS`, `RACE_SUBS` | 1, 1 | A chunk turns hot after one late write, or one write while the GPU ran a submission that read it from the mirror. Needed for split submissions without flicker. |
| `HOT_BIND_MAX`, `HOT_EXACT`, `HOT_COOL` | 512 KiB, 1, 300 | Only bindings up to 512 KiB read hot chunks from guest memory; index/indirect bindings use their real size; hot chunks cool after 300 quiet submissions. |
| `COMPUTE_VRAM` | 1 | A dispatch's storage blocks in guest memory are copied into a VRAM ring around the dispatch. |
| `VB_VRAM` | 4096 | For draws with at least 4096 vertex invocations, vertex streams up to 4 KiB in guest memory are copied into a VRAM ring right before the draw. |
| `XFB_MIRROR` | 1 | Transform feedback writes into the mirror (written back at the submission's end). |
| `SPLIT_DRAWS`, `SPLIT_US` | 64, 1000 | A submission goes to the GPU in parts, so the GPU starts while the replay still records. |
| `MIRROR_DEMOTE` | 1 | Pools the CPU writes while submissions are recorded give up their mirror. |

## What we learned (summary of the archive)

**Memory model.** The Switch has one memory for CPU and GPU; a discrete PC GPU has its own. CPU-visible pools are imported host memory with a VRAM mirror. The game writes some data late, even while the GPU already runs the work that reads it. Reading everything from imported memory is correct but slow (PCIe); reading stale mirrors makes trees flicker. The working combination: mirrors synced before each submit ("late sync"), deferred submits, and "hot" chunks that are read from imported memory once the game was seen writing them late or while the GPU ran.

**Frame rate: small data read very often over PCIe.** The cost of imported memory is not its bytes but how often the GPU reads it:
- *Snow and sandstorm:* the particle compute shader `1821327f` read and wrote six storage blocks in guest memory: 11 ms per frame, plus system-wide audio dropouts. In VRAM it takes 0.02 ms. Fixed by `COMPUTE_VRAM`.
- *Ground grass:* fragment shader `e963d67a`; every vertex of ~150,000 per frame read four stride-0 vertex streams of 16–192 bytes from guest memory: about 10 ms. Most of the cost was in the vertex stage, not the pixels (`SKIP_MODE=raster`). Fixed by `VB_VRAM`, which helped well beyond grass.

**Frame rate: towns.** In towns the GPU needed ~15 ms per frame but frames took ~24 ms. The game waits once per frame at `main+0xcd0eb0` (`nvnSyncWait`) for the previous frame, as on the Switch; here the GPU only started after the replay had recorded the whole submission. `SPLIT_DRAWS=64` starts the GPU earlier (towns went from 41–47 to 52–60 FPS); its old tree flicker disappears with `HOT_LATE_SUBS=1 RACE_SUBS=1`.

**What did not help.** `HOT_PASS` (copying the hot chunks a pass used last frame: the data moves every frame), `VS_VRAM` (vertex-stage uniform and storage buffers are not in guest memory), `MIRROR_DEMOTE=0` (fixed snow, but every other place lost frames), removing the submit deferral (no gain, short flicker), `HOT_REFRESH=1/2` (copies made too early: flicker).

**View distance.** The LOD table (`trinity_ModelLodSetting`, `MOD_FILES`) sets terrain and rock detail, not when objects appear. Trees and fences, Pokémon (the spawn table and the LOD table both limit them) and grass have limits of their own. Raising spawn distances made objects flicker or vanish. The Oodle data hook stays active for the whole session because the UI list waits for files that never load; it is a suspect for flicker after loading.

**PC Settings menu.** `game/ui_patch.txt` holds only sizes, hashes, offsets and KarPur's own values and texts, the same for every Violet 4.0.0, so it ships as `tools/data/ui_patch_violet_4.0.0.txt`.

## Diagnostics

All off by default except the abort line.

| Tool | Shows |
|---|---|
| `TRACE=perf` | Frame times, render thread waits, mirror and hot chunk statistics, PCIe bindings by kind, and every 10 s the `nvnSyncWait` callers that blocked |
| `PASS_TIME=1/2/3` | GPU time by pass, fragment shader and compute shader; 2 adds a full barrier after each pass, 3 times every draw alone |
| `SKIP_FS=<id>` + F9 | Switches one shader's draws or dispatches off and on; describes its first draws (textures, buffers, where each lies, pipeline state) |
| `SKIP_MODE=raster` | With `SKIP_FS`: F9 keeps the vertex work and drops only the rasterization |
| `PCIE_RANK=1` | Every 600 frames, the shader pairs that read guest memory most often (vertex invocations × bindings) |
| `AUDIO_QUEUE_MAX_MS` | Caps the audio queue (diagnostic) |
| `guest abort in …` (always) | The SDK function, file and line before the game aborts |

## Launcher and build pipeline

- `launcher/` (C# WinForms, self-contained, one exe) carries the repository's files, the shader translator and hactool; `tools/make_launcher.py` builds it.
- `tools/setup_build.py` runs the steps of `docs/setup-windows.md` with Python's embeddable package and MSYS2's clang unpacked from pinned packages (`tools/toolchain_msys2.py`, `KARPUR_UCRT`). It runs only steps whose outputs are missing or whose inputs changed, which makes it the resume, Repair and Update path.
- Releases: tag `vX.Y.Z` with the asset `KarPur-Launcher.exe`; raise `<Version>` in `launcher/KarPurLauncher.csproj` first. The Feedback tab opens `.github/ISSUE_TEMPLATE/feedback.yml` filled in; issues get the label `feedback`.

## Next steps

1. **Launcher review (2026-11-11).** Controller-sized controls and explicit focus, spatial navigation, atomic page changes and a cached Home banner are implemented locally. Test the revised launcher with a real controller, including tab changes and binding capture. Keep Home's team.json/catalog/icon interface intact.
2. **AMD temporal upscaling, then frame generation.** Reuse the existing tone-mapped scene/depth/camera-motion inputs and HUD separation from vk_taa.c. Start with an optional Vulkan FSR upscaler and an explicit fallback, then implement AMD's Vulkan frame-generation swapchain (separate from Streamline). The DLLs in AMD_NVIDIA_DLLs are DX12, not Vulkan. Reference: AMD's official FSR SDK and GPUOpen integration manuals; refs/PokeZ-ARecomp has FSR 2 source and a Vulkan FSR 3.1 upscaler adapter. Neither new path is implemented in KarPur yet. Test on the maintainer's RDNA 2 iGPU; no performance promise for that small GPU. New paths stay off by default until tested.
3. **Complete mod support in separate stages.** First direct RomFS overlays and uncompressed replacements; then archive-table additions for new packed files (data.trpfd/trpfs). Code mods need version/build-ID checks, patched-code regeneration and/or lifting additional NSO modules with hook redirection. A Switch exefs plugin cannot simply execute as x86-64, and runtime instruction hooks must reach the lifted functions. Use a separate profile for Compass (it requires a new save); do not label the current partial support final.
4. **Stability.** Long sessions (hot chunks/PCIe), map-opening stutters and the roughly 70-second spike; collect complete community crash logs. A rare AbortImpl from main+0xb5d930 after fast travel remains unexplained.
5. **View distance** for grass, bushes and NPCs; **higher internal resolution**; Scarlet and other PCs remain to be tested.
6. Runtime leftovers: transform feedback pause/resume and DrawTransformFeedback, NVN functions as called, codec::HardwareOpusDecoder.

## Open questions

- Same names do not mean same structures: between SDK 13 and 20, struct layouts and revisions can change (for example `NpadFullKeyState`, the `AudioRendererParameter` revision). Every replacement function taken over must be checked against SDK 13.
- Spin-wait patches: TotK routes ~860 wait loops through `rt_spin_gate`. We have no patch file; asynchronous preemption (`TOTK_ASYNC_PREEMPT`, on) should catch hangs but may cost speed. Look here first for hangs at start.
- Internal names and environment variables are still called `TOTK_*`. Rename once the runtime has settled.
- `--seconds S` ends a run through TotK's world clock; with `rt_game_version = 0` it is unclear whether that runs. Timed runs with `--shot` need an end condition of their own (a frame count).
- `--probe-shader` needs a shader census (TotK's `tools/shader_census.py`, not taken over); only worth it once we know where Violet keeps its shader packs (likely in `data.trpfs`).
- Are `nn::movie` functions called at all? The videos are Bink 2 (`.bk2`), decoded by the game's own code, so TotK's `ov_movie.c` (VP9) is probably not needed.

## Log

### 2026-11-11: use preview2 directly, remove the workspace restrictions
- Maintainer request: work in the repo, put all launcher UI/controller/banner changes in `launcher/bin/preview2`, back up only replaced/changed files, keep every Setup path and Build/Repair/Update available; other setup/mod management is manual. The separate preview/workspace approach below is superseded.
- Removed the global workspace override, command/sidecar mode, disabled Setup grid, read-only target, Build/Repair/update guards and suppression of saved launcher preferences. Preview2 finds its source-checkout ancestor only to choose the initial Target folder. Builder.Root follows the editable target (source root directly, regular installation target + KarPur), so all tabs and Play use the selected build. Local source builds do not auto-rebuild on startup; the buttons remain available for manual use.
- Kept spatial controller navigation, binding capture handling, focus/hit areas, banner bitmap/clip fix, atomic page changes, responsive team cards, edition metadata fallback, DLC-folder detection and existing game/runtime changes. No mod lists copied or toggles changed. Validation: isolated Release build (0 warnings/errors) and 37 synthetic UI/path checks passed, including editable Setup fields, Build/Repair/Update availability, selecting source and installed targets, team/save path changes and embedded build payload. Neither game executable is rebuilt or replaced. No commit or push.

### 2026-11-11: preview started the older installation instead of the workspace
- The maintainer confirmed that startup works again, but DLC and Quit in F1 were absent. Their working development launcher is `repo/launcher/bin/preview2/`. File times confirm two different game builds: `repo/build/game/karpur.exe` 2026-11-11 06:37, installed `KarPurTEST/KarPur/build/game/karpur.exe` 2026-11-10 21:39. The workspace's existing log reports one add-on content and mounts aoc1. The preview inherited the installed target from launcher.json, which explained the missing local changes. The Compass/Lua failure observed in the older installation remains a separate diagnostic; it did not prove the preview was launching the intended build.
- Explicit development selection: `--workspace=<root>` or private `karpur-workspace.txt` next to the launcher; a launcher inside a source checkout also finds its repository ancestor. Invalid explicit workspaces fail visibly instead of falling back. All game paths (Play, working directory, settings, team/icons, saves and mods) resolve from that root. Home shows Workspace and the game executable's build time; DLC is detected from the extracted add-on folders, not from a saved Setup input path. Normal installed-launcher behaviour is unchanged outside workspace mode.
- Workspace mode does not read/write installed launcher preferences, auto-rebuild via the release payload, or allow Build/Repair/release installation to overwrite local source. Use repository build tools for local game changes. Play logs its exact executable/working directory next to the launcher in launcher-start.log. The maintainer's original preview2 files and both game executables are left intact. The new preview's sidecar points explicitly to C:/KarPur/repo. Workspace edition detection falls back to the extraction report's application ID when setup_info.json is absent (manual build). Isolated Release build and 37 synthetic UI/path checks passed, including stale-target override, preview2 ancestry, working directory, settings/save roots, DLC detection, invalid-path rejection and payload-write guards. No game launch was used as proof of these path checks. No commit or push.

### 2026-11-11: black screen after Play, partial Compass mod active
- The maintainer reported a persistent black screen after Play with the launcher preview. The installed game's log (`KarPurTEST/KarPur/build/game/logs/karpur.log`, 08:12 run) shows 1469 file replacements; Compass replaces main.blua and main_dynamic.blua, the second main-script call returns Lua runtime error 2, and rendering stays at the startup passes (14 draws per frame, no scene). mods.json enables Compass alongside Improved Dynamic LODs and PokePopulation; Compass still lacks its 552 added files, 16 uncompressed replacements and exefs code. This is strong evidence of the known incomplete Compass path, not a new renderer change (the preview changed only launcher UI/docs; Play's process start is unchanged).
- Recovery: backed up mods.json, mod_files.txt, karpur.ini and the failing log under `C:/KarPur/backup/blackscreen-20261111-081551/` before changing anything. Disabled only PokeCompass_Violet_Standalone and regenerated mod_files.txt from the other two mods: 3 replacement files, no script replacements. Graphics settings and real saves were not changed; mod_files remains enabled for the two supported mods. Restart from Play is needed to apply the list.
- An isolated startup attempt with a separate profile produced only the first four Streamline startup lines and exited before the Lua check; it does not establish whether recovery fixes the maintainer's desktop run. The maintainer must confirm the next launch. Do not claim a verified game-start fix from the configuration check alone. No commit or push.

### 2026-11-11: DLC accepted, launcher/controller review, next feature stages
- The maintainer confirmed that the DLC switch works and asked to make it standard. It already defaults to on (`fs_host.c dlc_on`, `rt_knob("DLC", "1")`); the normal `dlc` build step extracts the player's add-on content, and nn::aoc reports/mounts it. Keep `TOTK_DLC=0` as a diagnostic opt-out. No additional unlock patch is needed; only supplied DLC is reported.
- Launcher visual revision: 1260x840 initial window, responsive sidebar spacing down to the 1000x680 minimum, larger buttons/dropdown rows/checkbox hit areas and switches, explicit cyan focus, subtle card borders and edition gradients. Home distributes up to six team cards across one row. Its team.json fields, national-species/form catalog lookup, icon fallback and save watcher are unchanged. Replaced team/icon/chip controls are disposed on refresh.
- Tab glitch: hide outgoing pages before showing the incoming one, suspend content painting through layout/refresh and redraw all descendants once. Hero caches a bitmap by size/palette; child buttons/chips copy their own part without replacing the graphics clip. Revisiting the selected tab preserves pending edits instead of reloading them on controller A.
- Controller: spatial arrows follow the visible arrangement; up/down change rows; left/right change dropdowns/sliders/switches or move between neighbouring buttons. A enters/activates, B closes a dropdown or returns to the sidebar, LB/RB change tabs, X opens binding context options, Start plays. Disabled/read-only controls are skipped, focus scrolls into view, and a tab remembers its last control. Binding capture owns the pad while waiting (navigation must not consume B/Start/directions); returning from focus loss/reconnect first needs a neutral pad.
- Local checks: isolated .NET Release build and synthetic WinForms review (no game launched or real save modified), team JSON/catalog/icon/shiny handling, six-card widths at 1260/1000, 24 Setup/Home transitions, clip preservation and controller action handlers. Real-controller feel and absence of a transient glitch on the maintainer's desktop still need confirmation. FSR/frame-generation and broader mod formats are follow-ups, not implemented by this launcher revision. No commit, push or release.

### 2026-11-08: launcher 0.2.0
- Released KarPur Launcher 0.2.0: NVIDIA DLAA, DLSS upscaling to the window or 4K, DLSS Frame Generation 2x-4x, the Controls tab (keys, controller buttons, sticks), rounded dropdowns and dark scrollbars, the game's DLC button back with F1 / Esc opening the Controls page. The first release whose Update tab can be tested from an older launcher (0.1.0 → 0.2.0).

### 2026-11-08: the scene camera for a temporal anti-aliasing (tests BU, BV)
- ZA Recomp (Pokémon Legends: Z-A, MIT, github.com/Hexveil404/PokeZ-ARecomp, read in `refs/`) runs NVIDIA DLAA and AMD FSR 2/3 on a recompiled Game Freak game: it jitters the scene passes' viewport, makes camera motion vectors from the depth with this and last frame's view-projection (vertex uniform block c5 of the G-buffer draws), replaces the game's final copy of its tone-mapped image with the resolve, and loads NGX at run time from the driver (`nvngx_dlss.dll` supplied by the player). Its other quality extras: GTAO, higher shadow map resolution, 16x anisotropic filtering, water reflections. Its frame rate comes mainly from a device-local cache of all vertex/index ranges refreshed from `GetWriteWatch` pages at each submission, with GPU-written pages tracked per 64-byte line and cached again after 120 frames.
- `TOTK_CAM_PROBE=1` (diagnostic, off): per draw with depth, the perspective matrices in the vertex shader's uniform blocks, every 600 frames in the log.
- Result (BV): Violet draws a G-buffer too (4 colour targets, 1920×1080). The camera's view-projection is **c5 (NVN uniform slot 2) +0, stored as columns**: 1 distinct per frame, changes in every frame while the camera moves and in none while it stands, fov 46°, aspect 1.778. c5 +0x180 behaves the same (a copy or last frame's: not yet told apart), +0x80 is the projection alone (constant), +0x140 its inverse. c10 (slot 7) holds per-object matrices (260+ per frame), as in Z-A. The shadow passes (1024×1024) use other c5 offsets.
- BW: c5 +0x180 is neither this frame's +0 nor last frame's (in every frame the camera moved): the last frame's camera has to be kept by us. The presented image (1920×1080, `R8G8B8A8_UNORM`) is filled by one draw (fragment shader `77a9f872`) from a 1920×1080 `R8G8B8A8_SRGB` image, into which the HUD is drawn last (a 20×20 BC4 icon, a 2048×2048 R8 font atlas). What fills that image first is the next question (BX: the probe now lists every run into each image, from the first).
- BX, the end of Violet's frame (Vulkan formats: 122 `B10G11R11_UFLOAT`, 130 `D32_SFLOAT_S8_UINT`, 64 `A2B10G10R10_UNORM`, 43 `R8G8B8A8_SRGB`, 37 `R8G8B8A8_UNORM`):
  1. `4042fad2` (one full-screen draw, 5 textures) writes a 1920×1080 HDR image (122) from the scene depth (130), a 1920×1080 HDR image and two 480×270 HDR images: a post pass that reads depth (fog / depth of field / bloom composite); some frames `48ffa881` adds a 960×540 HDR image into it.
  2. `333e928a` (one draw) tone-maps that HDR image with a 32×32 LUT (64) into the 1920×1080 sRGB image: the first run into it.
  3. The HUD is drawn over it (`0caa4920`, `efcb19c8`, `6db4269e`, `7e8532e8`: BC textures up to 700×700).
  4. `77a9f872` copies it into the presented image.
  No separate anti-aliasing pass shows between them, and the projection at c5 +0x80 never changes, so the game has no temporal anti-aliasing of its own.
- Where a DLAA / FSR 2 resolve fits: right after `333e928a` and before the first HUD draw, on the tone-mapped sRGB image (as ZA Recomp resolves Z-A's tone-mapped picture), with the depth `4042fad2` samples and motion vectors from c5 +0 of this frame and the last.
- DLAA step 1: `runtime/vk_dlaa.c` (adapted from ZA Recomp) loads NGX from the driver when `TOTK_DLAA=1` (default off), adds the extensions it asks for at instance and device creation, and shuts it down before `vkDestroyDevice` (without that, the driver faulted in `nvcuda64.dll` at exit). `karpur.exe --probe-vkmem` with `TOTK_DLAA=1` on the RTX 5080 with `nvngx_dlss.dll` 310.9.1 next to the exe: "NGX up; DLSS available", exit 0. Not yet called for frames. Of the files the maintainer collected (`C:\KarPur\AMD_NVIDIA_DLLs`, all validly signed): `nvngx_dlss.dll` is the one needed; `nvngx_dlssg.dll` and `sl.*` (Streamline 2.14: frame generation, Reflex) would need Streamline's interposer; `nvngx_dlssd.dll` (ray reconstruction) needs ray-traced inputs; the AMD files are DirectX 12 builds (FSR on Vulkan needs `amd_fidelityfx_vk.dll` or FSR 2 from source).
- DLAA step 2, **works (test BY)**: `runtime/vk_taa.c` with `TOTK_DLAA=1`. The scene passes (viewports of passes with the 1920×1080 scene depth) are jittered (Halton 2, 3, 8 frames) once a resolve has run; right after the tone mapping draw `333e928a` (`TOTK_DLAA_AT`) the sRGB image is copied to RGBA8, `shaders/taa_mv.comp` makes the camera's motion vectors from the depth (last frame's view-projection × this frame's inverse; NDC +y at row 0), DLSS resolves, and the result is copied back before the HUD is drawn. Violet's depth is reversed (clip depth 0.32 at 1 unit, 0.0003 at 1000; found from c5 +0x80 at run time). The maintainer saw smoother edges (stairs, trees) at 60 FPS; GPU busy per frame 5.3 → 6.4 ms. Switches for checks: `TOTK_DLAA_JX` / `_JY` (jitter sign), `TOTK_DLAA_JITTER_BIAS`, `TOTK_DLAA_DEPTH_INV`, `TOTK_DLAA_PRESET` (default 11, K). Building shaders needs MSYS2's `mingw-w64-ucrt-x86_64-shaderc` (installed 2026-11-08).
- Open for DLAA: moving characters and effects get the camera's motion only (ZA Recomp redraws skinned characters into the motion vectors for that); a setting in the launcher and the PC Settings page; the launcher has to bring or ask for `nvngx_dlss.dll`; AMD and Intel GPUs could get FSR 2 (MIT source) on the same inputs.
- DLAA setting: `dlaa = 0|1` in `[display]` of `karpur.ini` (`runtime/cfg.c`, restart). The launcher's Settings page has "Anti-aliasing" (Game's own / NVIDIA DLAA) and "Add nvngx_dlss.dll", which copies the player's own copy next to `karpur.exe` after checking the certificate name and file description. The in-game PC Settings page has no row for it: no checked free labels are left in the message file `option` (archive 2026-11-04); a row would need labels of another message file and a redirect in `fC8CEF9EF`.
- DLSS upscaling, **works (tests BZ-CB)**: `dlss_out = game|window|<w>x<h>` (`TOTK_DLSS_OUT`, live). DLSS resolves the 1080p scene to that size; the picture is scaled down into the game's image (so the HUD and the rest of the frame go on at 1080p) and kept. At the present, `shaders/taa_compose.comp` builds the shown picture at the output size: DLSS's picture where the game's image still equals the scaled-down copy, the presented image (bilinear) within one game pixel of anything the HUD drew. Found on the way: the last draw (`77a9f872`) reads the game's image sRGB-decoded (shown against it decoded: 0.7/1000 difference, as is: 160-220/1000), so DLSS's picture is decoded too (`TOTK_DLSS_CONVERT`, default 1); comparing against the presented image instead of the game's image marked everything as HUD (BZ). About 3% of the picture is HUD in the field. `TOTK_DLSS_DEBUG=1` tints the HUD part red. The maintainer: 1440p and 4K both "look fantastic"; slight smearing (ghosting) remains. The launcher has "DLSS output".
- Ghosting (test CC): trails behind what moves on its own (idle arms, freckles, leaves in front of the camera), which our camera-only motion vectors do not cover. Jitter flipped on both axes removed the trails but made the character tremble (history barely used: the default sign is right); Y alone: unchanged; preset E (CNN) instead of K (transformer): no trails, "everything looks good". Default preset now E (`dlaa_preset`, launcher "DLSS model"). Per-object motion vectors (ZA Recomp's objmv) would let K be used without trails.
- Frame generation, checked (no code yet): the maintainer's Streamline 2.14 files support it on Vulkan (`sl.interposer.dll` exports 229 `vk*` hooks and `slSetVulkanInfo`; `nvngx_dlssg.dll` has `NVSDK_NGX_VULKAN_*`). DLSS-G needs depth, motion vectors and a HUD-less colour image at present time (all available here), ideally UI colour and alpha, Reflex markers (FG depends on them), and the swapchain going through Streamline (its interposer as the Vulkan loader, or manual hooking of swapchain creation and present plus `slSetVulkanInfo` with the queues it asks for); the swapchain is recreated whenever FG turns on or off. Alternative for every GPU: AMD FSR 3.1 frame generation on Vulkan (`amd_fidelityfx_vk.dll` from the FidelityFX SDK; not among the collected files, which are DirectX 12 builds).
- Frame generation stage 1 (`runtime/vk_slfg.cpp`, `TOTK_FRAMEGEN=1`, Streamline headers in `runtime/third_party/streamline/`): Streamline is loaded at run time in manual hooking mode (no over-the-air downloads); KarPur creates instance and device itself with the extensions and extra queues `slGetFeatureRequirements` lists and calls `slSetVulkanInfo`; surface, swapchain, acquire, present and `vkDeviceWaitIdle` go through Streamline's proxies. Streamline's own `vkCreateInstance`/`vkCreateDevice` proxies crashed in `sl.common.dll` (a call through a null pointer while its plugins initialised, even with no feature loaded); the Windows crash note now lists the stack words that point into modules when the fault address is 0. `sl.common.dll` is mandatory besides `sl.interposer.dll`. On the maintainer's PC (`karpur.exe --probe-vkmem`, RTX 5080, driver 621.12): Reflex supported, DLSS-G **not**: "requires GPU hardware scheduling to be enabled in the OS" (HAGS was off, `HwSchMode = 1`).
- Frame generation stage 1, working (2026-11-08, after the maintainer turned HAGS on and restarted): DLSS-G "supported (ok)", multi-frame up to 5 generated frames. Three faults found with `karpur.exe --probe-window` (no game data needed): (1) the manual hooking path's own instance/device proxies (above); (2) OBS Studio's implicit Vulkan layers (`VK_LAYER_OBS_HOOK`, `VK_LAYER_WEOBS_HOOK`) faulted in the driver under DLSS-G's swapchain: `DISABLE_VULKAN_OBS_CAPTURE=1` is set for KarPur's process when frame generation is on (`TOTK_FRAMEGEN_OBS=1` keeps them); (3) `gfx_present.c` fetched the swapchain images in one call with room for 8: DLSS-G cloned 8 fake buffers for a 6-image swapchain and the driver faulted on the missing two; now count first, then exactly that many. Default integration is now the interposer mode (`TOTK_FRAMEGEN_MODE=interposer`): karpur.exe's 152 `vulkan-1.dll` imports are pointed at `sl.interposer.dll` at start (Streamline's standard integration without relinking); `manual` keeps the manual hooking path (own device with Streamline's extensions, queues incl. the native optical flow queue, `slSetVulkanInfo`). Diagnostics: `TOTK_FRAMEGEN_VERBOSE=1` (Streamline's verbose log), `TOTK_FAULT_STACK=1` (module stack words for every foreign fault). `--probe-window` fails by one step in one pixel with or without Streamline (the maintainer's `karpur.ini` picture settings; not looked into).
- Frame generation stage 2 (test CE, `FRAMEGEN=2` with `DLSS_OUT=window`): DLSS-G on, the first frame tagged (depth and motion vectors 1920×1080, HUD-less picture at the window's 2526×1421, near 0.32, far 6000, fov 46°). Each present tags the depth, the motion vectors and the HUD-less picture (a second output of `taa_compose.comp`) as `eOnlyValidNow` in the present's command buffer, sets the constants (Violet's column-major matrices read as rows are Streamline's row-major ones) and Reflex's PCL markers (all four up to the render submission at the present, the present markers around `vkQueuePresentKHR`). Found: frames without a scene (loading screens, menus) had no markers, and DLSS-G stopped with `eDLSSGStatusFailReflexNotDetectedAtRuntime`; now every present gets its markers and DLSS-G is switched off while frames come without the scene. KarPur's overlay counts the game's own frames, not the generated ones. Open: a warning that no backbuffer extent was tagged (optional); the generated frame count in the overlay (`slDLSSGGetState`).
- **Frame generation works (test CG):** with `FRAMEGEN=4` the log shows 240 frames shown per second for 60 rendered (4.00 each, DLSS-G status 0), 195 for 49 in a dip; 2× and 3× work too; controls unchanged. The maintainer: 40 FPS now look smooth. The overlay's Presentation row shows "FG <n> FPS". Not yet in the launcher or `karpur.ini` as a setting; it needs `dlss_out = window`, V-Sync off, HAGS on, `nvngx_dlssg.dll` and the `sl.*` DLLs (incl. `sl.common.dll`) next to `karpur.exe`.
- Frame generation as a setting: `framegen = 0|1|2|3|4` in `[display]` of `karpur.ini` (restart); with 2..4 DLSS's output follows the window whatever `dlss_out` says. The launcher's Settings page has "Frame generation" (Off/2x/3x/4x; choosing it sets Anti-aliasing to DLAA and DLSS output to the window), "Add Streamline files" (copies `sl.interposer`, `sl.common`, `sl.dlss_g`, `sl.reflex`, `sl.pcl` and `nvngx_dlssg` from a folder the player chooses, NVIDIA's certificate name checked) and a note naming missing files and whether HAGS is on.
- Launcher and menu rework (2026-11-08, the maintainer's list): the launcher's pages scroll; Upscaling offers AMD FSR 1, bilinear, NVIDIA DLSS to the window and DLSS 4K (the "DLSS output" row is gone; `dlss_out` is written from it); Sharpness shows only with FSR, the DLSS model only with DLAA/DLSS; the feedback link stays under 2000 characters (Windows' shell opened nothing for longer ones; the report goes to the clipboard) and a failed open is caught; a new **Controls** tab writes `[input]` (`pad_layout`, `stick_deadzone`, `mouse_sens`, `rumble_strength`, `rumble_triggers`, `motion`, and `bind_<action> = key:<name>, pad:default` for all 24 actions). In the game: the overlay's Scaling row names DLSS and its sizes or adds DLAA; the **PC settings page is off by default** (`TOTK_UI_PATCH` unset: the game's DLC button is back; `UI_PATCH=1` keeps the page), and F1 / Esc open KarPur's Controls page (`TOTK_MENU_LOCK=0`: the whole old menu).
- Shader pre-build (asked by the maintainer): today shaders and pipelines met in play are recorded and built at each start (2120 pipelines in ~6 s); other games build all of them once after installing. That needs Violet's shader packs found in the RomFS (the open question below) and a translator run over all of them on the player's PC.
  - What a translation needs: the program (control section, code, c1) **and the answers** (texture/image types of the slots it reads, VS attribute types, FS output types), known only at draw time; the input id hashes both. The runtime already has the TotK machinery: `shader-programs.bin` / `shader-answers.bin` / `pipeline-keys.bin` recorded in play, `--prepare-shaders` from `seed-programs.bin` + `seed-answers.bin`, the cache pack. Missing: the programs from the player's own files (they cannot ship).
  - `tools/shader_where.py` (report: counts and names only) searched the 3394 recorded programs (14.3 MiB of code; only 1383 distinct code middles: many programs share their code) in the unpacked RomFS: **0 found**. Violet's shader code is inside Oodle-packed files of `data.trpfs`. The pack types hint little: one `.bnsh` pack (1.3 MiB) and one `.trsha` pack (3.7 MiB).
  - A free Oodle decoder (ooz, in ZA Recomp) is GPLv3: not usable here. The way left: the game's own `OodleLZ_Decompress` (`main+0xcc8750`, already hooked by `data_watch.c`), called by the runtime for every Oodle-packed file in a census mode, to find which packed files hold shader programs and then their container format.
  - **Census (test CH, `runtime/shader_census.c`, `SHADER_CENSUS=1`):** all 23589 packs of `data.trpfs`, 378977 Oodle decodes (31.8 GiB unpacked) with the game's decoder, none failed, 58 s. **3379 of 3394 recorded programs found** (stage 0: 1689/1691, 4: 1645/1658, 5: 45/45) in 769 packed files, all Oodle-packed. Of the 200 holders listed, 186 begin with `VFXB` (effect binaries, `.ptcl` / `.trsot` packs, up to 209 programs each) and 14 with `SARC` (UI layouts, `applidata*`). The report lists only the first 200 holders, so the containers of the other 569 (likely the model / material shaders, `.bnsh` / `.trsha`) are not yet known.
  - **Container probe (test CI):** every one of the 769 holders has its code inside a **BNSH**: 71 standalone BNSH files (`.trsha` packs 70 files / 361 programs, `system_resource/gfx2/shader/NX64/env_titan/material/standard.bnsh` 168 programs in 12 MiB), 684 VFXB effect files (`.ptcl` 301, `.trsot` 339, `.trskl` 21, `.tracp` 21, …; 2775 programs; a BNSH embedded after the texture block BNTX) and 14 UI SARCs (entry `bgsh/__ArchiveShader.bnsh`, 75 programs). Not found: 15 small programs (13 fragment, 2 vertex, 256..1792 bytes). So one extractor, a BNSH reader applied to standalone files, SARC entries and BNSHs inside VFXB, covers all of it.
  - **BNSH layout (test CJ):** BNSH starts with the NintendoWare file header ("BNSH", 0, version 0x2010c, BOM 0xFEFF with alignment 2^12, name offset, first block 0x60, relocation table offset, file size at +0x1C). A shader info holds **u64 control offset, u64 code offset, u32 code length** (offsets from the BNSH's start; the length equals control+0x704); code 0x100-aligned, controls 8-aligned; several infos can share one code with different controls.
  - **Extractor, dry run (test CK, same command):** the census now walks every BNSH in every unpacked file for such triples, takes the stage from the Maxwell SPH after the code's first 0x30 bytes (else compute), and writes each distinct program (code hash as `gfx_shader_code_hash`, exported from `gfx_shader.c`) to `bnsh-programs.bin` next to `karpur.exe` in `shader-programs.bin` form; the report checks the result against the recorded programs (exact reproductions and stage mismatches).
  - **Extraction works (test CK):** 10472 BNSH, 112127 shader infos, **36043 distinct programs** (vertex 17853, fragment 17949, compute 241; no tessellation or geometry), 243.5 MiB; **3376 of the 3394 recorded programs reproduced exactly**, the stage guess never wrong (vertex 1684, fragment 1647, compute 45). 37 s of extra work in the 1-minute census.
  - **Answers:** the use masks of all 3401 recorded variants (`.use` files of `build/game/shader-cache`) have **no texture or image slots**: Violet's shaders never ask for texture types. So default answers (float attributes, float outputs, compute local size from control+0x748) reproduce 3385 of the 3401 play variants (7 vertex shaders read integer attributes, 2 fragment shaders write integer targets, 7 compute records carry a shared memory size).
  - **Seed (test CL):** the census now writes `seed-programs.bin` and `seed-answers.bin` (default answers; priority 0 for programs play recorded, 2 for the rest) next to `karpur.exe`; `karpur.exe --prepare-shaders` translates them and builds the stage libraries into the driver's cache (vk_pipeline.c lb_warm_prepared) and now also logs how many play-recorded variants the prepared ones cover. Next: measure the preparation's time and the stutter in play; then the launcher's "prepare shaders" step after the build (census + prepare).
  - **Preparation run (test CL):** `--prepare-shaders` translated 32530 programs in 271 s (the rest cached); 146 failed, all of them candidates the stage guess called compute (241 found, 95 good): probably no shaders at all, to be filtered out. 35897 variants prepared; they cover **3367 of the 3401 variants play recorded** (the 18 programs not extracted plus the 16 with other answers). The driver-cache step then ended without a message at about 32000 of 35897; `--warm-shaders` (6 s with a warm cache) ends the same way with exit code 0xc0000374 (heap corruption).
  - **Cause found:** variant 33238 of `shader-prepared.bin` alone (fragment shader, input id 7134bd07…, priority 2: never met in play) crashes inside `lib_create` (the NVIDIA 621.12 driver building its fragment library); its SPIR-V passes `spirv-val`; its neighbours, also with storage image writes and depth replacing, build fine. Crash hunt knobs: `TOTK_LIB_WARM_TRACE=1` (one thread, each variant and library build named on stderr), `TOTK_LIB_WARM_FROM` / `TOTK_LIB_WARM_COUNT` (a slice of the list).
  - **Next:** a crash guard for the preparation: each worker writes the variant it builds into `shader-warm-inflight.bin`; after a crash the process restarts, a single variant left there goes to `shader-warm-skip.bin` (builds at its first draw if ever needed), several suspects are built first one at a time. A draft is in the session scratchpad (`warm_resilient.py`, not applied; its list-compaction step must be rewritten). Also: filter the false compute candidates in the census, then the launcher step (census + prepare after the build), then measure stutter in play.
  - **Crash guard (done, 2026-11-09):** each preparation worker writes the variant it builds into `shader-warm-inflight.bin`; `--prepare-shaders` / `--warm-shaders` run their work in a child process (`plat_spawn_self`, at most 20 restarts) and start it again after a crash: one variant left in flight goes to `shader-warm-skip.bin`, several are built first one at a time. Measured: crash with 8 in flight, crash on the single suspect, third run sets 7134bd07… aside and finishes all 35896 others in 9.6 s.
  - The census leaves out header-less candidates without a sane compute local size (236; the 146 failed translations of test CL had sizes like 37054 x 0 x 36): 35897 programs, compute 95. It also runs without `shader-programs.bin` (a fresh install: extraction only).
  - **Build step `shaders`** (tools/setup_build.py, after the self-test; the launcher shows "Preparing all shaders"): the census (`TOTK_SHADER_CENSUS=1`), then `karpur.exe --prepare-shaders`; runs again after every rebuild of karpur.exe and when `shader-prepared.bin` is missing; a failure is logged and the build goes on. Measured on the maintainer's PC with warm caches: 72 s; a first run about 6 minutes (census 1, translation 4.5, driver cache 1).
  - **Community reports (2026-11-09):**
    - Self-test exit code 0xc000001d (illegal instruction) on a player's PC: the build used `-march=x86-64-v3` (AVX2, FMA, BMI2) and the CPU lacks it. `tools/cpu_march.py` now asks clang for the host CPU (`-march=native` macros) and builds for `x86-64-v2` when AVX2/FMA/BMI2 are missing (FMADD then goes through the C library's software `fma`); `KARPUR_MARCH` forces a level; release builds (`--sysroot`) keep v3; the self-test checks the level the build used. Not yet tested on such a CPU.
    - A crash right after the character creation, which players work around with `TOTK_SUBMIT_DEFER=0`: no log yet. The launcher's Settings get "Compatibility mode" (`submit_defer = 0` in `[compatibility]`). Needed: a whole `karpur.log` of such a crash.
    - A crash after the language selection: only the log's last lines (transform feedback, compute storage copied to VRAM, a VRAM headroom block), no fault note. Needed: the whole log, the GPU and its driver.
    - A base game as `.nsp` fails in the fonts step (no shared fonts in an eShop image): the README says so in its requirements and troubleshooting.
  - Launcher: the dropdown's closed face is painted in WM_PAINT alone, buffered (the native square face showed through on hover).
  - README rewritten (requirements with tested / community / unsupported, platforms, status M6, features, troubleshooting); roadmap: M5 done, M6 in progress with its parts.
  - **Launcher redesign (2026-11-09, not released):** edition palettes (`Theme.Palette`: Violet night indigo with violet and neon cyan, Scarlet wine with scarlet and amber, neutral before a build; `Theme.SetPalette` maps every control's colours); a **Home** page (a scene painted from shapes and gradients per edition, no game art: Violet a ringed moon, circuit lines and a neon horizon grid, Scarlet a low sun with rays and mountain ridges; chips for version/DLC, prepared shaders and the last save; Play and Open game folder; three stat cards); a **Saves** page (`launcher/SavesPage.cs`: export the committed save `build/profile/save/<uid>` as a .zip, import a .zip with `main` at its top, restore one of `save-backup/*`; import and restore first move the current save and any finished commit `save-tx/ready-*` into `save-backup/<time>-launcher`; both refuse while `karpur` runs); nav glyphs for every page; pages open at their top. Hidden switches for design checks: `--shot=<png>` (the window drawn into a picture), `--colour=violet|scarlet`.
  - **The current team on Home:** the save file is encrypted with a static pad from the game (SwishCrypto), which cannot ship. Instead `runtime/team.c` reads the party from the game's memory when the game commits its save (`team_on_save` from `fs_host.c save_commit`): a background thread scans the heap and alias region for PK9 records (the published format: LCRNG seeded by the EC, blocks shuffled by (EC >> 13) % 24, a u16 checksum over 0x08..0x148, no key needed), takes the longest run of at most six with party stats (level 1-100, current HP <= max) and writes `<profile>/team.json` (species as stored, form, level, shiny, nickname); later saves read the remembered place first. `TOTK_TEAM=0` turns it off, `TOTK_TEAM_PROBE=1` logs every run found. The launcher's Home shows the team as cards (a monogram in the edition's gradient, name, level, a star when shiny).
    - Test CM: the scan never started (`plat_cas32` takes (p, new, expected) and returns the old value; the check was inverted). Test CN, fixed: 2.55 GiB of committed heap scanned in 0.4 s (`plat_mem_next_readable` walks committed regions only; a one-word prefilter decrypts block A's species alone), **68 checksummed records** found: runs at stride 0x150 without party stats (stored copies, boxes) and four single records with party stats at scattered addresses (levels 55, 100, 14, 20). The party is no array of records; next probe (test CO): arrays of pointers to those records, and each party-like record's species and level, to match against the maintainer's team. Test CP (the maintainer's team: Azumarill 100, Lechonk 14, Jigglypuff 15, Hoppip 16, Meowscarada 100, Bounsweet 16, with their current HP): Lechonk, Hoppip and Meowscarada matched by species (national numbers: 915, 187, 908), current HP (0x8A) and name length, in one block at stride 0x150 (slots 2, 4, 5 of the party; no party stats: stored records). The three others did not decrypt: the game holds some records decrypted for the moment, so the scan now also accepts records whose checksum holds as they are (test CQ). The level is not in a stored record; it follows from the EXP (0x10) and the species' growth curve (checked: 3333 EXP medium-fast = 14, 2947 medium-slow = 16, 1059860 medium-slow = 100), so the growth curves are needed (from the player's own personal table, not shipped). Tests CR-CT: the three other slots hold valid encrypted records too; the block order index is `((EC >> 13) & 31) % 24` (the code had `(EC >> 13) % 24`, right only by chance for the first three). The party is now taken as a run of up to six records at stride 0x150 (`SLOT`) that look like a player's Pokémon (species 1-1025, EXP up to 1,640,000, HP below 1000, a name); the records the game holds plain were noise and are no longer accepted. Levels: the shader census looks for the personal table among the unpacked files (a FlatBuffer vector of 900-4000 tables; a u16 field the species, a u8 field grouping 16 well-known species by their curves) and writes `growth.bin` (one curve per species) next to `karpur.exe`; `team.c` computes the level from the EXP with the six published curves. team.json gains `exp` and `hp`; the launcher hides the level when it is 0. **Test CU: works.** The census found the personal table (1424 entries, species field 0, curve field 10; 1026 species) and the scan the party (6 at stride 0x150, 0.4 s): Azumarill 100, Lechonk 14, Jigglypuff 15, Hoppip 16, Meowscarada 100, Bounsweet 16, all as in the game. Released with launcher 0.3.0. After the release the maintainer's Home page showed six wild Pokémon (an Arrokuda and five Hoppip, lv. 6-8, an outbreak) instead of the team: the game keeps the wild Pokémon around the player in the same form and stride. A team member now also needs an original trainer's name (0xF8, UTF-16); the probe logs its length and the TID. Test CV: works. Of the records logged, 53 had no original trainer (wild ones and others), the team's six all one (6 characters, one TID), and the scan took the team: Azumarill 100, Lechonk 14, Jigglypuff 15, Hoppip 16, Meowscarada 100, Bounsweet 16.
  - Launcher: the Home banners for Violet and Scarlet are pictures the maintainer drew in Paint Tool SAI in the editions' colours (`launcher/assets/banner-*.jpg`, embedded; no game art); the painted scenes stay as the fallback and for the neutral palette. The sidebar mark is drawn (`LogoMark`): a rounded square in the chosen edition's accent only, with a white K.
  - **GitHub issues (2026-11-10):** #12 black shadows on AMD RDNA 2 (RX 6750 XT, RX 6900 XT; driver 26.10.1 "shader compiler" branch); #17/#18 device lost on AMD during the character creation (#18: RX 7900 XTX, RDNA 3, LLPC; GPL was on there, `TOTK_GPL=0` ran 4-18 times longer; both runs end with "occlusion query slot … not available after its fence" and the breadcrumb in the submission right after a transform-feedback layout 2, the character's GPU skinning); #13/#22/#14 crashes around the character creation on NVIDIA (no full log yet); #20 the toolchain download fails with CERTIFICATE_VERIFY_FAILED behind HTTPS-scanning antivirus; #15 single-digit FPS on a CPU without AVX2 (the x86-64-v2 build works); #16 stutters.
    - GPL is now off by default on every AMD driver (was: RDNA 1/2 only).
    - Black shadows: the maintainer's Ryzen 9800X3D has an RDNA 2 iGPU (`TOTK_GPU_INDEX=1`) to reproduce it; first suspect the gather coordinate bias that only non-NVIDIA GPUs get (`TOTK_GATHER_BIAS=0` turns it off), as shadow filtering gathers depth (test CW). Test CW: reproduced on the iGPU (every surface facing away from the sun black), `GATHER_BIAS=0` changed nothing. The cause: the 32x32 ambient light cube (a square 2D array of 6 layers sampled through a cube view) needs CUBE_COMPATIBLE, which `vk_texture.c` left out on all AMD (for the rainbow terrain arrays of Savage Wind Recomp #59), so the replay bound a black stand-in. Now AMD gets the flag on square 6n-layer arrays up to 128x128 (the light probes) (test CX). **Test CX: fixed** on the iGPU (shaded sides lit as on NVIDIA, no rainbow blocks in the terrain).
    - #20: `tools/toolchain_msys2.py download` now tries Python's TLS, then Python with VERIFY_X509_STRICT off, then Windows' own TLS (`curl.exe` of Windows 10/11, Schannel), each ending at the pinned SHA-256; a certificate failure that remains names the antivirus cause. Checked here: both ways fetch a pinned package with the right hash.
    - #18 (setup note): `setup_build.py --firmware DIR` passes a folder of the console's system NCAs to `extract_fonts.py`; with it a base game as NSP is accepted (`extract.py` opens NSP and XCI). The launcher's Setup has "Firmware folder (optional)"; the README's requirements and troubleshooting say so.
    - #14 (GTX 1060, 16 GB RAM): the run that ended with a message ran out of commit (the system at 22676 of 22680 MB, KarPur 4951 MB): `rt_fatal` now adds a plain-language line when the system is at 97% of its commit limit (close programs, or let Windows manage the page file).
    - #13 / #22 (RTX 4070 Ti / 4070, crash around the character creation): only the logs' last lines, no fault note; need the whole logs. A fresh game can be tested without touching the maintainer's save through `TOTK_PROFILE_DIR` (a profile folder of its own). Test CY (a fresh game through the character creation on the maintainer's RTX 5080): no crash; the full logs of #13/#22 are needed.
    - The maintainer's launcher folder still showed wild Pokémon after 0.3.1: its `KarPur-Launcher.exe` was 0.3.1, but the game had been built by 0.3.0 and never rebuilt (the launcher had been replaced without the Update tab's `--updated` start), and its team.json was a day old. The launcher now writes `build/launcher_version.txt` after every finished build and rebuilds at start when it differs from its own version (also after a launcher replaced by hand); `--shot` design checks skip that.
  - **Team icons (2026-11-10):** the icons are packs `pokemon/data/pmNNNN/pmNNNN_FF_GG/icon/pmNNNN_FF_GG_00.bntx` (979; the trpfd names them without slashes), each with two BNTX textures, 100x100 and 256x256, format 0x1b01 (BC2), block-linear with block height 4 / 8 GOBs, data after BRTD (test CZ). Paldea's Pokémon have folders of their own numbers; the game's `pokemon/catalog/poke_resource_table.trpmcatalog` maps species and form to the model path `pmNNNN/pmNNNN_FF_GG/...` (Azumarill 184 -> pm0184, Meowscarada 908 -> pm1012, Lechonk 915 -> pm1019; test DA). The shader census now decodes every 100x100 icon (BC1/2/3, Tegra block-linear deswizzle) to `build/game/icons/<folder>.png` and writes `icons/catalog.txt` ("species form folder"); the launcher's team cards draw the icon (species and form, else form 0) over a glow in the edition's colours, else the monogram. **Test DB: works**: 966 icons written, a catalog of 1012 species and forms (Bounsweet 761 -> pm0812); the Azumarill icon decoded cleanly, and the Home page shows the whole team with icons (drawn at 96 px, as the icons carry a margin).
  - Launcher: Home refreshes when team.json changes (a FileSystemWatcher on the profile) and when the game it started exits; `Theme.Rounded` and the hero survive a size of 0 (a minimised window threw "Parameter is not valid" in GDI+).
  - **Mods tab (2026-11-10):** `launcher/ModsPage.cs`: mods (folders, or .zip unpacked into `<root>\mods`) in `<root>\mods.json` with a switch each; switching runs `tools/mod_files.py` for the mods switched on, in their order (a later mod wins a shared file), into `game\mod_files.txt`, and sets `mod_files` in karpur.ini (0 when none is on). Nothing on disk is replaced. `mod_files.py` gained `--out` and a last line `SUMMARY {json}` per mod (files it replaces, adds, changes unpacked, code). `runtime/data_patch.c`: mod files unlimited (was 64), read from disk when the game unpacks the original (was: all loaded at start), looked up by packed size; `MAX_FIELDS` 16384 (a size field per file). Pokémon Compass 2.1.1.1 (Violet): 1467 files replaced, 552 added files (new wild Pokémon groups, Pokédex scenes, message files) and 16 files the game stores unpacked are not supported yet, and its exefs code cannot run; Improved Dynamic LODs (2 files) and PokePopulation (1 file) are fully usable. Test DC (repo build, Improved Dynamic LODs + PokePopulation): 3 mod files listed, `common_resident_1.trscn` (25444 bytes for 24828) and `object_generation_range_array.bin` used in a short visit; KarPur's own population setting (switch2) then writes its values into the same file. Next: files a mod adds (they need entries in the archive's tables).
  - **Quit, pad, DLC (2026-11-11):** (1) the F1/Esc page has "Y  Quit game" in the lower bar (asks first; `UI_MENU_QUIT_CONFIRM`; issue #25). (2) The launcher works with a controller (`launcher/PadNav.cs`, XInput): stick / D-pad choose a tab or Play in the sidebar (the page follows), A enters a page, stick / D-pad move between controls, left/right change a dropdown, slider or switch, A presses, B goes back (or closes a dropdown), Start presses Play; focus rings show while a pad is used (issue #30; the auto-start option is still to do). (3) DLC: Violet's DLC NSP holds one PublicData NCA (index 1, 0.1 MiB) whose RomFS is only `keyfile.dat`, the proof of ownership; the expansions' data is in the update. `tools/extract_dlc.py` (build step `dlc`) unpacks it with hactool into `game/dlc/<index>/`; `fs_host.c fs_aoc_indices` lists those indices to `nn::aoc` (ListAddOnContent, CountAddOnContent) and `MountAddOnContent` mounts the folder read-only (`TOTK_DLC=0` hides it). Test DD: load a save that visited the DLC areas. The costumes may be a separate item (the game's own shop data), to be seen in the test.
  - **Test DD: works.** The build's log shows the add-on content reported and mounted at start (`aoc: 1 add-on content`, `fs mount aoc1: -> .../dlc/1`), and the maintainer confirmed the DLC areas play correctly on a save that had already visited them. No crash, no missing-content message. DLC counts as done for M6; it only needs to reach players through the next release's `dlc` build step (already in `tools/setup_build.py`). No commit/push yet.
  - **CPUs without AVX2 (issue #15, GTX 1660, 8 threads: 15.8 FPS, at times 1.5):** the frame time went to the lifted code, not to rendering. Without FMA, `__builtin_elementwise_fma` (FMADD, FMLA, FRECPS, FRSQRTS: 2943 scalar fused ops in main alone, plus the vector ones) compiled to calls of the C library's software fma, one per lane: main_0045.c had 115 such calls in an x86-64-v2 build. `runtime/simd.h KFMA` now computes a float FMA as the product and sum in double rounded once (the product is exact; 0 of 20,000,000 random cases differed from fmaf), vectors through 4 x double, and doubles unfused; with FMA it is the same instruction as before (118 vfmadd in main_0045.c). `tools/cpu_march.py` builds for `-march=native` on a CPU without AVX2/FMA/BMI2 (its own AVX, F16C, …; x86-64-v2 when even SSE4.2/POPCNT are missing), and the self-test checks each extension the build was compiled with. Not tested on such a CPU (the reporter of #15 can, after a release).
  - **Test in play (2026-11-09):** over half an hour with every shader prepared; the maintainer: it never ran as well as now. Next: a launcher release (0.3.0) with the `shaders` build step.
  - (Test CJ's command was the same:) the census measures the BNSH layout for two programs per file: where the control section lies, which u32 fields hold the code's and control's offsets, and the length / size / 0x880 fields beside them, plus the first BNSH header words. Then the extractor walks every BNSH and writes all programs in `shader-programs.bin` form.
- Next for DLAA: find the game's final copy of the tone-mapped image and its own anti-aliasing, then the jitter, the motion vector pass and NGX.

### 2026-11-08: progress log in English
- The log is in English from now on, so contributors who do not read German can follow it. The German log up to this point moved to [progress-archive-de.md](progress-archive-de.md), unchanged.
- Released KarPur Launcher 0.1.0 (commit `c3bab43`, README `61ac70f`): https://github.com/Grunttanamo/KarPur/releases/tag/v0.1.0.
