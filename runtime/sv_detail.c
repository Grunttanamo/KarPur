// Violet 4.0.0: keep the detailed people and Pokemon meshes farther from the camera.
// Based on PokeV-Recomp PR #16 (MIT, bf92855) and PR #15 (MIT, a00907a).
// Model LOD thresholds and the number of simultaneously modeled town people are separate.
// No guest-visible memory capacity or GPU pool size is changed.
#include <stdint.h>
#include <string.h>

#include "cfg.h"
#include "host.h"

#define LOD_GLOBAL 0x46d4e00ull
#define LOD_MGR_VTABLE 0x44a00b8ull
#define NPC_MGR_GLOBAL 0x473af30ull
#define NPC_MGR_VTABLE 0x4468fd8ull
#define GLOBAL_HEAP 0x4763918ull
#define GLOBAL_HEAP_VTABLE 0x44a6500ull

typedef struct {
    const char *name;
    const char *label;
    float baseline[3];  // Violet's unmodded full / middle / far thresholds
    uint64_t node;
    float original[3], written[3];
    int logged;
} Detail;

static Detail people = {.name = "chara_model", .label = "people", .baseline = {8, 15, 30}, .logged = -1};
static Detail pokemon = {.name = "pokemon", .label = "Pokemon", .baseline = {10, 20, 30}, .logged = -1};

// Check the guest map before following a pointer from the model manager. A stale node from
// a field reload must never become a host access violation.
static int accessible(uint64_t addr, uint64_t size, int write) {
    RtMemoryInfo info;
    if (!addr || addr >= RT_ADDRESS_LIMIT || size > RT_ADDRESS_LIMIT - addr || mem_query(addr, &info)) return 0;
    return addr >= info.base && size <= info.size && addr - info.base <= info.size - size &&
           (info.perm & (write ? 3u : 1u)) == (write ? 3u : 1u);
}

static uint64_t read_ptr(uint64_t addr) {
    uint64_t value = 0;
    if (accessible(addr, sizeof value, 0)) memcpy(&value, (const void *)(uintptr_t)addr, sizeof value);
    return value;
}

static int level(const char *key, int maximum) {
    int value = cfg_int(key);
    if (value < 2 || (value & 1)) return 1;
    return value < maximum ? value : maximum;
}

// The game's GlobalHeap can underflow its unsigned used counter when it subtracts the
// free sizes of two allocators. Only this wrapped-negative case is corrected; a real
// used value above total is left alone. The last valid quality-slot count is restored.
static void fix_heap_counter(const RtLoaded *main, uint64_t manager, uint32_t limit, uint64_t *good_slots) {
    uint64_t heap = main->base + GLOBAL_HEAP;
    uint64_t slots_at = manager + 0xc58;
    if (GLOBAL_HEAP + 0x40 > main->size || read_ptr(heap) != main->base + GLOBAL_HEAP_VTABLE ||
        !accessible(heap + 0x28, 0x18, 1) || !accessible(slots_at, 8, 1)) return;
    uint64_t total = read_ptr(heap + 0x28), used = read_ptr(heap + 0x38), slots = read_ptr(slots_at);
    if (total < (128ull << 20) || total > (8ull << 30) || slots > 1024) return;
    if (used <= total) {
        if (slots) *good_slots = slots;
    } else if (used >= (1ull << 63)) {
        uint64_t zero = 0;
        mem_thaw(heap + 0x38, sizeof zero);
        memcpy((void *)(uintptr_t)(heap + 0x38), &zero, sizeof zero);
        uint64_t restored = *good_slots > slots ? *good_slots : slots;
        if (restored != slots) {
            mem_thaw(slots_at, sizeof restored);
            memcpy((void *)(uintptr_t)slots_at, &restored, sizeof restored);
        }
        static unsigned logged;
        if (logged++ < 8)
            rt_log("people shown: corrected wrapped GlobalHeap used counter; quality slots %llu -> %llu",
                   (unsigned long long)slots, (unsigned long long)restored);
        used = 0, slots = restored;
    }
    static uint64_t samples;
    if (limit > 12 && ++samples % 600 == 0)
        rt_log("people shown: limit %u; GlobalHeap used %.0f / %.0f MiB; quality slots %llu%s",
               limit, used / 1048576.0, total / 1048576.0, (unsigned long long)slots,
               used > total ? " (over total; no capacity change)" : "");
}

static void update_people_shown(const RtLoaded *main) {
    static int wanted;
    static uint64_t manager, good_slots;
    static uint32_t original, written;
    static int logged = -1;
    if (!wanted) {
        wanted = cfg_int("TOTK_PEOPLE_SHOWN");
        if (wanted < 12 || wanted % 12) wanted = 12;
        if (wanted > 48) wanted = 48;
    }
    if (NPC_MGR_GLOBAL + 8 > main->size || NPC_MGR_VTABLE >= main->size) return;
    uint64_t current_manager = read_ptr(main->base + NPC_MGR_GLOBAL);
    if (!current_manager || read_ptr(current_manager) != main->base + NPC_MGR_VTABLE ||
        !accessible(current_manager + 0xe8, 4, 1) || !accessible(current_manager + 0xc58, 8, 1)) {
        manager = 0;
        return;
    }
    uint32_t current;
    memcpy(&current, (const void *)(uintptr_t)(current_manager + 0xe8), sizeof current);
    if (current_manager != manager || current != written) {
        if (current == 0 || current > 1024) { manager = 0; return; }
        manager = current_manager;
        original = written = current;
    }
    uint32_t target = (uint32_t)wanted > original ? (uint32_t)wanted : original;
    if (current != target) {
        mem_thaw(manager + 0xe8, sizeof target);
        memcpy((void *)(uintptr_t)(manager + 0xe8), &target, sizeof target);
        written = target;
    }
    if (logged != (int)target) {
        logged = (int)target;
        rt_log("people shown: %u town people at once (game %u); this setting does not resize memory pools", target, original);
    }
    fix_heap_counter(main, manager, target, &good_slots);
}

static void update_entry(uint64_t map, Detail *entry, int multiplier) {
    uint64_t node = read_ptr(map + 0x38);
    const size_t name_size = strlen(entry->name);
    for (int i = 0; i < 64 && node; ++i) {
        if (!accessible(node, 0x64, 0)) return;
        uint64_t chars = read_ptr(node + 0x18);
        uint64_t length = read_ptr(node + 0x20);
        if (length == name_size && accessible(chars, length, 0) &&
            !memcmp((const void *)(uintptr_t)chars, entry->name, name_size)) {
            uint64_t at = node + 0x58;
            if (!accessible(at, sizeof entry->written, 1)) return;
            float current[3];
            memcpy(current, (const void *)(uintptr_t)at, sizeof current);
            if (node != entry->node || memcmp(current, entry->written, sizeof current)) {
                if (!(current[0] > 0 && current[0] < current[1] && current[1] < current[2] && current[2] < 10000)) {
                    entry->node = 0;
                    return;
                }
                if (node != entry->node || memcmp(current, entry->original, sizeof current)) entry->logged = -1;
                entry->node = node;
                memcpy(entry->original, current, sizeof current);
                memcpy(entry->written, current, sizeof current);
            }
            float scaled[3];
            // The UI lists distances based on the unmodded game. A mod may already raise
            // individual thresholds (e.g. 24 / 9000 / 9999 m). Keep those as a floor,
            // rather than multiplying them into enormous distances on every LOD tier.
            for (int j = 0; j < 3; ++j) {
                float requested = entry->baseline[j] * (float)multiplier;
                scaled[j] = entry->original[j] > requested ? entry->original[j] : requested;
            }
            if (memcmp(current, scaled, sizeof scaled)) {
                mem_thaw(at, sizeof scaled);
                memcpy((void *)(uintptr_t)at, scaled, sizeof scaled);
                memcpy(entry->written, scaled, sizeof scaled);
            }
            if (entry->logged != multiplier) {
                entry->logged = multiplier;
                rt_log("detail: %s full model within %.0f m (requested at least %.0f m, loaded %.0f m); thresholds %.0f / %.0f / %.0f m",
                       entry->label, scaled[0], entry->baseline[0] * multiplier, entry->original[0],
                       scaled[0], scaled[1], scaled[2]);
            }
            return;
        }
        uint64_t next = read_ptr(node);
        if (next == node) return;
        node = next;
    }
}

void sv_detail_present(void) {
    static int people_level, pokemon_level;
    if (!people_level) {
        people_level = level("TOTK_PEOPLE_DETAIL", 10);
        pokemon_level = level("TOTK_POKEMON_DETAIL", 8);
        if (people_level > 1 || pokemon_level > 1)
            rt_log("detail: people at least %.0f m, Pokemon at least %.0f m; original GPU and application pools unchanged",
                   people.baseline[0] * people_level, pokemon.baseline[0] * pokemon_level);
    }
    const RtLoaded *main = ldr_module(0);
    if (!main) return;
    update_people_shown(main);
    if (people_level == 1 && pokemon_level == 1) return;
    if (LOD_GLOBAL + 8 > main->size || LOD_MGR_VTABLE >= main->size) return;
    uint64_t p = read_ptr(main->base + LOD_GLOBAL);
    p = read_ptr(p);
    p = read_ptr(p + 0x38);
    p = read_ptr(p + 0x50);
    if (!p || read_ptr(p) != main->base + LOD_MGR_VTABLE) return;
    uint64_t map = read_ptr(p + 0x30);
    if (!map) return;
    if (people_level > 1) update_entry(map, &people, people_level);
    if (pokemon_level > 1) update_entry(map, &pokemon, pokemon_level);
}
