# Violet memory hierarchy in KarPur (2026-11-16)

This is an ownership and accounting map for the local Violet 4.0.0 build. It combines static data flow with the saved startup traces in `build/memory-budget-startup.log` and `build/memory-chain-probe.log`. A matching size or address containment does not by itself prove a C++ parent-child relationship.

## The two chains

```text
KarPur guest-memory runtime (runtime/memory.c)
  application ceiling: 3285 MiB by default, plus optional resolution allowance
  reported through GetInfo 21/22; enforced on newly committed guest pages
    |
    +--> Nintendo SDK StandardAllocator / CentralHeap
    |      startup physical map: 3165.5 MiB guest address range
    |      separate earlier map: 27.2 MiB
    |      later mappings lie inside the 3165.5 MiB range
    |        |
    |        +--> game allocation wrapper main+0xb40eb0
    |               global SDK allocator instances at main+0x4740ee0
    |               350 / 268 / 50 MiB backing requests
    |                 --> matching NVN pools in main+0x1587a5c
    |
    +--> Nintendo SDK QueryMemoryInfo
           game function main+0x2b7da90: queried field (~91.082 MiB)
           + fixed 2920 MiB = GlobalHeap total (~3011.082 MiB)
           GlobalHeap object main+0x4763918
           immediate stored parent: main+0x47639c8

Violet trinity_config (game policy; main+0x146e5f0)
  +--> named TrinityHeapType allocations via main+0x11e1a60
  |      JobScheduler 90, Gfx2 900, Vfx2 68, Physics 140,
  |      Ui2d 32, Fs 800, Lua 104, Sound 64, EventSystem 8,
  |      Object 256 MiB
  +--> nested graphics sizes 350 / 268 / 50 MiB
         --> the matching requests and NVN pools above
```

The top runtime ceiling is an application-memory accounting limit, not 3285 MiB of host VRAM. The 64 GiB alias reservation is virtual address space, not committed RAM. Host Vulkan allocations have their own VMA and driver budget. All these numbers describe different layers and must not be added together.

## Proven links and their evidence

| Link | Evidence | Scope |
| --- | --- | --- |
| Runtime ceiling to SDK report | `runtime/memory.c` defines `DEFAULT_POOL=0xCD500000`, reports `L.pool` for GetInfo 21 and used pages for 22, and checks new alias commits against that pool. | Code, default path. `TOTK_APP_POOL` and resolution allowance can change the value. |
| SDK mapping to nested maps | Physical map #2 is `0x57a280000..0x640000000` (3165.5 MiB). Each logged later map #3-#20 of at least 8 MiB lies within this address interval. Map #1, 27.2 MiB, is separate. | Observed startup address ranges; no claim about every unlogged small map. |
| Graphics sizes to NVN pools | `main+0x146e5f0` changes 384/320/48 to 350/268/50 MiB. The wrapper requests precisely those sizes with 4096-byte alignment; `main+0x1587a5c` then initializes NVN pools of those sizes. | Static table flow plus live startup trace. |
| NVN backing to SDK allocator | `main+0xb40eb0` selects one of two allocator objects at `main+0x4740ee0` with a 72-byte stride and imports `nn::mem::StandardAllocator::Allocate`. The SDK call chain continues through `CentralHeap`, `TlsHeapCentral` and `VammManager` to physical page mapping. | Observed wrapper calls and imported target; this is the backing-storage path. |
| SDK memory report to GlobalHeap | `main+0x2b7da90` calls `nn::os::QueryMemoryInfo`, reads its `+0x20` field, adds `0xB6800000` (2920 MiB), and passes the result to `main+0x13eca54` for the static GlobalHeap object. In one capture the result was 3011.082 MiB. | Code and one live initialization sample. |
| Trinity names to their sizes | `main+0x11e1a60` passes the config's first group and original `TrinityHeapType::*` strings to `main+0x11e3664`; that function allocates each named heap through the supplied allocator interface. | Static game-code flow. Ten requested sizes total 2462 MiB, but are not an additional kernel map. |

## Links still unproven

1. **Trinity Gfx2 versus the 350/268/50 MiB NVN backing.** The three pool sizes are in the nested Trinity configuration, but their observed backing-storage calls go through the SDK `StandardAllocator` path. The pool-setup helper also has an object allocator; one observed object had a 256 MiB field and a zero-byte request. This cannot be the direct 350 MiB backing allocator. A size-table relationship is proven; an object ownership relationship to the named 900 MiB `Gfx2` heap is not.
2. **GlobalHeap versus the named Trinity heaps.** The GlobalHeap's stored immediate parent is a static allocator object at `main+0x47639c8`. The named heap constructor instead receives its allocator through a Trinity manager interface. We have not observed a pointer joining these objects, so the named sizes must not be presented as subdivisions of the 3011 MiB GlobalHeap.
3. **All SDK internal bookkeeping.** Address containment shows later mappings reuse the 3165.5 MiB guest range. It does not expose the SDK's complete page ownership or all smaller allocations.

## What this says about changing MiB values

The game's config, `GlobalHeap` calculation, SDK allocator, and runtime ceiling are separate decisions. Increasing one number does not automatically expand its children or give them independent space. In the earlier 4K experiment, enlarging the 268 MiB pool coincided with lost NPCs and Pokemon; this analysis does not identify a safe replacement size or prove a single cause for despawning. The live startup `0x10801` came from a 512 KiB SDK page request when only about 0.164 MiB remained under the runtime ceiling; it happened before world streaming and does not identify a specific scene heap.

The opt-in 4096-shadow experiment recorded in `docs/progress.md` later raised the runtime report, the SDK's initial map, Violet's GlobalHeap and its compressible NVN pool by the same 180 MiB. The SDK map grew automatically from its memory query; no SDK binary patch was needed. GlobalHeap required its own game patch. Despite all four increases, the early `0x10801` request still had about 0.164 MiB of headroom in both modes. This startup result does not establish whether the larger budgets preserve world objects during play.

## Retired +512 MiB stress test (2026-11-16)

The removed `Test-Memory-Plus512.cmd` launched the preview launcher with `TOTK_MEMORY_PLUS512_TEST=1` for one process. It raised the runtime application ceiling 3285 -> 3797 MiB, the fixed term in GlobalHeap 2920 -> 3432 MiB, all ten verified Trinity named heap values by 512 MiB each, and four verified nested graphics values (18/350/268/50 -> 530/862/780/562 MiB). The SDK's initial physical map grew from its memory query; there was no separate SDK binary patch. The test was deliberately overcommitted if these heap requests drew from one parent: the ten named sizes alone grew from 2462 to 7582 MiB. It did not alter unrelated dynamic allocations, unverified config fields, host VRAM, or the 64 GiB virtual reservation.
Violet has no `HeapDef` table to increase; that older path is for TotK (see `docs/progress.md`).

An automated `--boot` run with this flag reached the first NVN call successfully (925 SVCs, 11 threads, 624 host calls). Its trace confirmed all fourteen table edits. It did not reach a loaded world or prove that those heaps can all be committed. The early SDK `0x10801` still occurred with ~0.164 MiB free. The separate +180 MiB shadow-budget mode also reached `--boot` after the GlobalHeap patch became shared.

The full launcher run at 20:31 failed during startup (`build/memory-plus512-tests/20261116-203125-plus512-failed.log`). The runtime ceiling was 3797 MiB; the SDK mapped 3677.5 MiB at startup. The GlobalHeap reported 3523.082 MiB. The three initial NVN pools really requested 862, 780 and 562 MiB. Later `MapPhysicalMemory` failed for a new 369.5 MiB request with only 45.414 MiB free; then 13.25 MiB failed with 8.414 MiB free and 0.5 MiB failed with 0.164 MiB free. Violet logged `OnAllocationFailed` and the SDK aborted. The final `svc #0x26 unknown` is the unimplemented Break used by that abort, not the initial failure. This demonstrates that raising each confirmed game budget by 512 MiB while raising the application ceiling by only 512 MiB is insufficient for startup. It does not identify a safe alternative budget layout.

### Split-report follow-up

The removed split-report variant raised the runtime's guest-page commit allowance while `GetInfo` still reported 3797 MiB to the SDK, so the SDK's initial map remained 3677.5 MiB. `Test-Memory-Split.cmd` used 4096 MiB extra for its child launcher. It created an inconsistent total-versus-used report and was strictly a diagnostic experiment.

Automated, hidden, time-limited starts with identical +512 game heap values showed: +1024 MiB commit-only allowance exhausted and aborted with `OnAllocationFailed` (`split1024-timed.stderr.txt`); +2048 MiB ran for 35 seconds beyond frame 1200 with no `0x10801` (`split2048-timed.stderr.txt`); +4096 MiB ran for 45 seconds beyond frame 1800 with no `0x10801` (`split4096-timed.stderr.txt`). The latter two were stopped by the test timeout while in the menu; neither demonstrates stable play in a loaded world. The comparison supports the diagnosis that extra runtime commit headroom, separate from the SDK's reported starting size, resolves this particular startup failure.

The maintainer's full `Test-Memory-Split.cmd` playtest (`build/memory-plus512-tests/20261116-204758-plus512-split.log`) reached the world and exited on window close after more than 10,000 frames, with no `0x10801`, `OnAllocationFailed` or fatal error. NPCs and Pokemon nevertheless despawned. The game-visible GlobalHeap total was only 3523 MiB, while the sampled used counter remained 4934-5158 MiB for most of the run; the town-person manager reported one quality slot despite a `People shown` limit of 48. Later the used counter wrapped, the existing narrow correction reset it, and quality slots eventually rose to 10. In the earlier positive +180 MiB shadow run, the comparable samples were 2632-2823 / 3191 MiB and 10 quality slots. The split test therefore removed the kernel commit failure but did not give Violet consistent internal accounting or prove that its streaming and visibility policies had more usable capacity. The quality-slot collapse is a strong correlate of the reported missing NPCs; the log cannot prove that every missing NPC or Pokemon had that same cause. Do not treat this split-report configuration as a gameplay fix.

After the playtest, the +512 and split-report patch sets, runtime allowance, and test launchers were removed. The targeted 180 MiB shadow-budget experiment remains available. The captured logs stay under ignored `build/` for analysis. Nothing was committed or pushed.
