// KarPur: view and spawn distances of Violet, changed in the data files as the game reads them.
//
// Violet keeps its fade-out distance for models (world/data/gfx/model_dither_fade: ModelDitherFadeDefaultParam,
// FarBeginDistance / FarEndDistance, 75 / 90 in 4.0.0) and the create and destroy distances of field objects and
// Pokémon (world/data/system/object_generation_range: ObjectGenerationRange) in small FlatBuffers files that are stored
// in arc/data.trpfs, some stored, some Oodle-packed (tools/trpfs.py --report, docs/progress.md 2026-11-02).
// tools/trpfs.py --view writes game/data_patch.txt, one line per field:
//   romfs <offset in romfs.bin> <type> <group> <name>            a field of a stored file: changed in every RomFS read
//                                                                that covers it (romfs.c romfs_read)
//   oodle <packed> <unpacked> <fnv1a64 of the first 4 KiB packed> <offset in the file> <type> <group> <name>
//                                                                a field of a packed file: changed right after the
//                                                                game's Oodle decode (data_watch.c, which turns the
//                                                                patch set data_watch on for it)
// type is f32, i32, u32, i16 or u16; the field is multiplied by its group's factor:
//
//   TOTK_VIEW_DIST=f   group "fade": far fade distances (objects and trees stay visible f times as far); 1 = off
//   TOTK_SPAWN_DIST=f  group "spawn": create/destroy distances of objects and Pokémon; 1 = off (more CPU load)
//   TOTK_SPAWN_COUNT=f group "count": maxGenerationNum, how many of a kind exist at once (15 for wild Pokémon in
//                      4.0.0; a known spawn mod raises it to 20..48 together with the distances); 1 = off
//   TOTK_DATA_PATCH_LIST=path  the list (default game/data_patch.txt)
//   TOTK_MOD_VALUES=1  set the values a community mod has in these fields (game/mod_patch.txt, written by
//                      tools/mod_report.py --apply: group "mod=<value>"), as if the mod were installed
//   TOTK_DATA_FIND=1   find every double 1/30 (0x3fa1111111111111) in what the game reads from RomFS and unpacks with
//                      Oodle, and append a line for each to game/data_find.txt with the value 1/60 (group
//                      "mod=0.016666666666666666"); TOTK_MOD_VALUES=game/data_find.txt then applies them. Violet's
//                      indoor walking steps by a double 1/30 that two Lua-like interpreter opcodes read (main+0x4135c,
//                      main+0x422e4; docs/progress.md 2026-11-03), so the constant should sit in a script file.
//   TOTK_SCRIPT_STEP=1 (default; 0 turns it off) replaces every double 1/30 in what the game reads and unpacks with one
//                      frame at the frame limit (1/60 at 60 fps, 1/120 at 120) as it loads, so the scripts step their
//                      fixed 1/30 s updates (indoor walking) once per frame: with the 13 found in 4.0.0 set to 1/60
//                      walking indoors ran smooth at 60 fps and nothing else changed (maintainer's test,
//                      docs/progress.md 2026-11-03). Their accumulators take the frame time, so the speed stays right
//                      at any frame rate. The value is taken once, at the first file: scripts keep it in their own
//                      constants, so a new frame limit reaches them only after a restart.
//   TOTK_MOD_FILES=1   replace whole files of a mod, also ones of another size (game/mod_files.txt, written by
//                      tools/mod_files.py; a path names another list). A romfs line raises the file's unpacked size in
//                      its pack's table of contents, so the game reserves room; a line
//                      modfile <packed> <unpacked> <fnv1a64> <path>  gives the game the file at path instead of
//                      decoding those packed bytes (data_watch.c host_oodle, data_patch_replace).
//   TOTK_POPULATION    how many wild Pokémon at once, read at start: switch (default, the game's 15), switch2 (40),
//                      enhanced (80), extreme (120); the values below in world/data/system/object_generation_range.
//                      switch2 has the values of the fan mod Pokemon Compass by Aria Seri and Kas Seri (v2.1.1.1), taken
//                      alone; with them about 35 Pokémon were in sight, houses whole and the frame rate stable
//                      (maintainer's test, docs/progress.md 2026-11-04). The higher levels change only the count. Earlier
//                      tests of such values lost houses, before the memory model changed.
// type f64 is a 64-bit double, u8 a byte.
//   msgtext <packed> <unpacked> <fnv1a64> <line> <tag> <text>
//                      a line of a message file (.dat, tools/msgpatch.py --relocate) gets a new text of any length:
//                      the runtime writes it, encrypted like the game's (UTF-16, each unit XOR a key that starts at
//                      0x7C89 + 0x2983 * line and turns left by 3 bits), to spare memory above the heap
//                      (memory.c mem_spare_near: in the alias region, where Violet keeps its memory) and points the
//                      line's offset and length there; the file's section length grows to cover it, since the game
//                      reads no line beyond it. The game reads the offset as signed, so the text lies less than
//                      2 GiB after the file (maintainer's tests, docs/progress.md 2026-11-04). The text is UTF-8;
//                      "\n" is a line break. "@<line> " before the text puts it in the space of that other (unused)
//                      line of the same file instead, cut to fit. For the PC settings page in the game's options (docs/progress.md
//                      2026-11-04), whose texts the game fetches by label only.
//   TOTK_UI_PATCH      the list of bytes and texts of the game's UI files that make the PC settings entry and page
//                      (game/ui_patch.txt, written by tools/pc_menu.py; group "mod=<value>" and msgtext lines). Used when
//                      the file is there, looked for from the working directory and from build/game; 0 turns it off,
//                      a path names another list.
//
// The original values never leave the player's PC; the log names fields and offsets only.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cfg.h"
#include "host.h"
#include "platform/platform.h"

double gfx_window_refresh_hz(void);   // ui/menu.h, which is for the menu's C++ side too

#define MAX_FIELDS 16384   // a large mod (Pokemon Compass: 2032 files) raises one size field per file

typedef struct {
    uint64_t at;   // byte offset in romfs.bin, or in the unpacked file (decoded)
    uint64_t packed, unpacked, print;   // decoded: which file
    char type;     // 'f' f32, 'i' i32, 'u' u32, 'h' i16, 'H' u16, 'd' f64, 'b' u8
    float factor;
    int done;          // decoded: set at least once
    int absolute;      // set value instead of multiplying (a mod's value, group "mod=<value>")
    double value;
    char name[64];
} Field;

static Field fields[MAX_FIELDS];   // romfs fields, sorted by offset
static int nfields;
static Field decoded[MAX_FIELDS];  // fields of Oodle-packed files
static int ndecoded;

#define MAX_TEXTS 1024
typedef struct {
    uint64_t packed, unpacked, print;   // which file
    uint32_t line;
    int donor;         // >= 0: the text goes in the space of that line of the file (cut to fit), not spare memory
    int done;
    char tag[64];
    uint16_t *units;   // the text in UTF-16 with its terminator, not encrypted
    uint32_t nunits;
} MsgText;
static MsgText texts[MAX_TEXTS];
static int ntexts;
static uint64_t lo = UINT64_MAX, hi;
static uint64_t applied;

static float factor_of(const char *key) {
    const char *e = cfg_get(key);
    float f = e && *e ? strtof(e, NULL) : 1.0f;
    return f > 0.05f && f < 100.0f ? f : 1.0f;
}

static int by_offset(const void *a, const void *b) {
    uint64_t x = ((const Field *)a)->at, y = ((const Field *)b)->at;
    return x < y ? -1 : x > y;
}

// A mod's files (the launcher's Mods tab lists every enabled mod; a large one has thousands): kept as paths and sizes,
// read from disk when the game unpacks the original (the mod stays in its folder, nothing is copied or replaced on
// disk), sorted by the original's packed size for the lookup at each decode.
typedef struct {
    uint64_t packed, unpacked, print;   // the original file: its packed bytes
    char *path;                         // the mod's file
    uint64_t n;
    int used;
} ModFile;
static ModFile *mod_files;
static int nmod_files, cap_mod_files, mod_file_failed, mod_files_sorted;

// object_generation_range_array.bin of Violet 4.0.0: its packed and unpacked size and fingerprint, so another file or
// game version is left alone; then offset, type and value of each field.
// TOTK_NO_EXCLUSIVES: the three species tables of world/data/encount, by unpacked size and the fingerprint of
// their packed bytes (Violet 4.0.0; tools/exclusives.py reports them for another version).
static const struct { uint64_t unpacked, print; } excl_files[] = {
    {34724, 0x888111ef69f71aa3ull}, {36784, 0x8ae3f9b2548a5528ull}, {58984, 0x0a60102c8801df28ull},
};
static int excl_on;

// An entry of those tables carries versiontable { A: bool, B: bool }, and a wild Pokemon exclusive to one edition
// does not store `false` for the other: that field is absent, so FlatBuffers answers with the default, which is no.
// Both editions ship the same data (docs/progress.md 2026-11-13).
// Making both flags read the one stored byte is all it takes, and it needs no rewritten file: a FlatBuffer table
// reaches its fields through a vtable, and several tables share one. Violet 4.0.0 has one vtable for each edition's
// exclusives - one holds both slots with the other edition's absent, the other is two bytes short and has no slot
// for it at all - and in both the stored flag sits at the same offset in the entry. So the vtable that has room
// gets its absent slot pointed at that offset, and the entries of the short one are pointed at that vtable: one
// slot and one vtable reference per entry, all inside the file. Found by walking rather than written down, so the
// next game version needs no new offsets; anything that does not look exactly like this is left alone.
#define EXCL_SLOT 28   // versiontable's slot in an entry (devid 0 .. bringItem 29)
#define EXCL_MAX 512

static uint32_t fb_u32(const uint8_t *b, uint64_t at) {
    uint32_t v;
    memcpy(&v, b + at, 4);
    return v;
}

static uint16_t fb_u16(const uint8_t *b, uint64_t at) {
    uint16_t v;
    memcpy(&v, b + at, 2);
    return v;
}

static int64_t fb_vtable(const uint8_t *b, uint64_t pos) {
    int32_t d;
    memcpy(&d, b + pos, 4);
    return (int64_t)pos - d;
}

// The field's offset from the table's start, or 0 when the table's vtable has no slot for it or leaves it absent.
static uint16_t fb_slot(const uint8_t *b, uint64_t n, uint64_t pos, unsigned slot) {
    int64_t vt = fb_vtable(b, pos);
    if (vt < 0 || (uint64_t)vt + 4 > n) return 0;
    uint16_t size = fb_u16(b, (uint64_t)vt);
    uint64_t at = 4 + 2 * (uint64_t)slot;
    return at + 2 <= size && (uint64_t)vt + at + 2 <= n ? fb_u16(b, (uint64_t)vt + at) : 0;
}

static void excl_fix(uint8_t *b, uint64_t n) {
    if (n < 32) return;
    uint64_t root = fb_u32(b, 0);
    uint16_t vo = root + 8 <= n ? fb_slot(b, n, root, 0) : 0;   // the root's only field is the vector of entries
    if (!vo || root + vo + 4 > n) return;
    uint64_t v = root + vo;
    v += fb_u32(b, v);
    if (v + 4 > n) return;
    uint32_t count = fb_u32(b, v);
    if (!count || count > 100000 || v + 4 + 4 * (uint64_t)count > n) return;
    uint64_t pos[EXCL_MAX];
    int64_t vts[EXCL_MAX];
    int many = 0, both = 0;
    uint16_t off = 0;
    for (uint32_t k = 0; k < count; k++) {
        uint64_t e = v + 4 + 4 * (uint64_t)k;
        e += fb_u32(b, e);
        if (e + 4 > n) return;
        uint16_t so = fb_slot(b, n, e, EXCL_SLOT);
        if (!so || e + so + 4 > n) continue;
        uint64_t vp = e + so;
        vp += fb_u32(b, vp);
        if (vp + 4 > n) return;
        uint16_t a = fb_slot(b, n, vp, 0), bb = fb_slot(b, n, vp, 1);
        if (a && bb) {
            both++;
            continue;
        }
        if (!a && !bb) continue;        // neither flag: the game's own default decides, nothing to do
        if (off && (a ? a : bb) != off) return;   // the stored flag must sit at one offset in all of them
        off = a ? a : bb;
        if (many == EXCL_MAX) return;
        pos[many] = vp;
        vts[many++] = fb_vtable(b, vp);
    }
    if (!many || !both) return;
    int64_t target = -1;   // a vtable with room for both slots: the one whose other edition's slot is only absent
    for (int i = 0; i < many; i++)
        if ((uint64_t)vts[i] + 8 <= n && fb_u16(b, (uint64_t)vts[i]) >= 8) target = vts[i];
    if (target < 0) return;
    uint16_t a = fb_u16(b, (uint64_t)target + 4), bb = fb_u16(b, (uint64_t)target + 6);
    if ((a != 0 && a != off) || (bb != 0 && bb != off) || (a && bb)) return;
    memcpy(b + (uint64_t)target + (a ? 6 : 4), &off, 2);   // both slots now read the stored byte
    int moved = 0;
    for (int i = 0; i < many; i++) {
        if (vts[i] == target) continue;
        int32_t d = (int32_t)((int64_t)pos[i] - target);
        memcpy(b + pos[i], &d, 4);   // this entry's versiontable uses the vtable that has room
        moved++;
    }
    uint64_t k = __atomic_add_fetch(&applied, 1, __ATOMIC_RELAXED);
    if (k <= 64)
        rt_log("data patch: no version exclusives: %d of %u entries freed (%d pointed at the other vtable), flag at "
               "+%u", many, count, moved, off);
}

// TOTK_NO_TRADE_EVO: Pokemon that evolve only by trading cannot evolve at all in a port with no trading, so the
// condition becomes a level instead. The species table is avalon/data/personal_array.bin (pack 799, Oodle-kraken,
// 122439 -> 412572 bytes; Violet 4.0.0): a vector of 1424 entries, field 0 the species number and field 23 a
// vector of 16-byte evolution structs { u16 level, u16 method, u16 argument, u16 x3, u16 target species, u16 }.
// Method 5 is "by trading" and method 4 "on reaching a level", with the level in the struct's first field, so the
// change fits inside the struct: two bytes for the method, two for the level, and the file keeps its size. Read
// from the bytes, not from a list: docs/progress.md 2026-11-13 says how, and how the methods were identified.
// The levels are the maintainer's choice, each above the level the Pokemon itself appears at.
// Trade evolutions that also need a held item (method 6) are not these: their item is a national number, while
// every method that works without trading counts items in a small space of its own that Violet's data does not
// map for those items. They stay as they are.
#define EVO_SLOT 23      // the evolution vector's slot in a species entry
#define EVO_STRUCT 16    // bytes per evolution
#define EVO_TRADE 5      // method: by trading
#define EVO_LEVEL 4      // method: on reaching the level in the struct's first field
#define EVO_TRADE_ITEM 6 // method: by trading while holding an item; the argument is the item's national number
#define EVO_HOLD_ITEM 19       // method: it levels up by day holding the item; the argument is the item's plibID
#define EVO_HOLD_ITEM_NIGHT 20 // the same by night
// National item number -> plibID. Every method that works without trading counts items in a small space of its
// own: world/data/battle/plib_item_conversion (pack 21985) maps the two as ItemTableArray { itemID, plibID }, and
// the game's own data confirms it (plibID 1..6 are the national 80..85, the five stones; Pikachu's method-8
// argument is 4, the Thunder Stone; method 19's are the Razor Claw and the Oval Stone).
// Method 19, not 8: these items cannot be used on a Pokemon at all, only held, so "the item is used" leaves the
// evolution just as unreachable as trading did (the maintainer's test, 2026-11-13). 19 is "levels up by day
// holding it", which is the game's own way of evolving a Pokemon that holds something; a variant without a time
// of day does not exist, and the second one, 20, is the same by night. One struct holds one method, so it is day.
// Only the items Violet already gives a plibID are here. The King's Rock, the Metal Coat, the two Deep Sea items,
// the Sachet and the Whipped Dream have none: their rows in that table hold a plibID and no item at all, and
// adding one means writing a field that is absent, so those evolutions stay as they are (docs/progress.md).
// Where the maintainer chose another item than the one the entry names, because that one has no plibID and so
// cannot be named to the game at all. Keyed on the target as well, since Clamperl has two of these and they must
// stay apart. The numbers are national item numbers with the plibID beside them.
static const struct { uint16_t species, target, plib, method; } evo_item_own[] = {
    {61, 186, 87, EVO_HOLD_ITEM},    // Poliwhirl -> Politoed: item 2345 for the King's Rock (221, no plibID)
    {79, 199, 87, EVO_HOLD_ITEM},    // Slowpoke -> Slowking: the same
    {366, 367, 111, EVO_HOLD_ITEM},  // Clamperl -> Huntail: item 537 for the Deep Sea Tooth (226, no plibID)
    {366, 368, 27, EVO_HOLD_ITEM},   // Clamperl -> Gorebyss: item 299, so the two stay apart
};

// Where a trade-with-item evolution takes a level instead, because no item the game can name suited it. The
// maintainer's levels. Tried with a substitute item first (the Metal Alloy for Onix and Scyther, a Sweet for
// Spritzee and Swirlix) and it did not evolve them in play, so the item is out of the condition altogether.
static const struct { uint16_t species, target, level; } evo_item_level[] = {
    {95, 208, 40},    // Onix -> Steelix
    {123, 212, 30},   // Scyther -> Scizor
    {682, 683, 20},   // Spritzee -> Aromatisse
    {684, 685, 20},   // Swirlix -> Slurpuff
};

static const struct { uint16_t item, plib; const char *name; } evo_item[] = {
    {235, 118, "Dragon Scale"},   {252, 113, "Up-Grade"},     {321, 117, "Protector"},
    {322, 115, "Electirizer"},    {323, 116, "Magmarizer"},   {324, 114, "Dubious Disc"},
    {325, 112, "Reaper Cloth"},   {537, 111, "Prism Scale"},
};
static const struct { uint64_t unpacked, print; } evo_file[] = {{412572, 0x015856eb1c2b489aull}};
static const struct { uint16_t species, level; const char *name; } trade_evo[] = {
    {64, 40, "Kadabra"},  {67, 39, "Machoke"},   {75, 36, "Graveler"}, {93, 42, "Haunter"},
    {525, 35, "Boldore"}, {533, 40, "Gurdurr"},  {708, 30, "Phantump"}, {710, 30, "Pumpkaboo"},
};
static int evo_on;

static void evo_fix(uint8_t *b, uint64_t n) {
    if (n < 32) return;
    uint64_t root = fb_u32(b, 0);
    uint16_t vo = root + 8 <= n ? fb_slot(b, n, root, 0) : 0;
    if (!vo || root + vo + 4 > n) return;
    uint64_t v = root + vo;
    v += fb_u32(b, v);
    if (v + 4 > n) return;
    uint32_t count = fb_u32(b, v);
    if (!count || count > 100000 || v + 4 + 4 * (uint64_t)count > n) return;
    int done = 0, items = 0, left = 0;
    for (uint32_t k = 0; k < count; k++) {
        uint64_t e = v + 4 + 4 * (uint64_t)k;
        e += fb_u32(b, e);
        if (e + 4 > n) return;
        uint16_t so = fb_slot(b, n, e, 0), eo = fb_slot(b, n, e, EVO_SLOT);
        if (!so || !eo || e + so + 2 > n || e + eo + 4 > n) continue;
        uint16_t species = fb_u16(b, e + so);
        uint64_t ev = e + eo;
        ev += fb_u32(b, ev);
        if (ev + 4 > n) return;
        uint32_t m = fb_u32(b, ev);
        if (m > 64 || ev + 4 + (uint64_t)m * EVO_STRUCT > n) continue;
        for (uint32_t j = 0; j < m; j++) {
            uint64_t at = ev + 4 + (uint64_t)j * EVO_STRUCT;
            uint16_t method = fb_u16(b, at + 2), target = fb_u16(b, at + 12);
            if (method == EVO_TRADE) {
                uint16_t level = 0;
                for (size_t i = 0; i < sizeof trade_evo / sizeof *trade_evo; i++)
                    if (trade_evo[i].species == species) level = trade_evo[i].level;
                if (!level) {   // a trade evolution this build has no level for: left alone, and said so
                    left++;
                    rt_log("data patch: species %u evolves by trading into %u and has no level here; left alone",
                           species, target);
                    continue;
                }
                uint16_t to = EVO_LEVEL;
                memcpy(b + at, &level, 2);
                memcpy(b + at + 2, &to, 2);
                done++;
            } else if (method == EVO_TRADE_ITEM) {
                uint16_t item = fb_u16(b, at + 4), plib = 0, to = EVO_HOLD_ITEM, level = 0;
                for (size_t i = 0; i < sizeof evo_item_level / sizeof *evo_item_level; i++)
                    if (evo_item_level[i].species == species && evo_item_level[i].target == target)
                        level = evo_item_level[i].level;
                if (level) {   // no item in the condition at all: a level, as the plain trade evolutions have
                    uint16_t lv = EVO_LEVEL;
                    memcpy(b + at, &level, 2);
                    memcpy(b + at + 2, &lv, 2);
                    memcpy(b + at + 4, &plib, 2);   // the item number has no meaning for method 4
                    done++;
                    continue;
                }
                for (size_t i = 0; i < sizeof evo_item_own / sizeof *evo_item_own; i++)
                    if (evo_item_own[i].species == species && evo_item_own[i].target == target)
                        plib = evo_item_own[i].plib, to = evo_item_own[i].method;
                if (!plib)
                    for (size_t i = 0; i < sizeof evo_item / sizeof *evo_item; i++)
                        if (evo_item[i].item == item) plib = evo_item[i].plib;
                if (!plib) {   // an item with no plibID: it cannot be named to the game, so this one stays
                    left++;
                    rt_log("data patch: species %u evolves by trading into %u while holding item %u, which has no "
                           "plibID here; left alone", species, target, item);
                    continue;
                }
                memcpy(b + at + 2, &to, 2);
                memcpy(b + at + 4, &plib, 2);
                items++;
            }
        }
    }
    if (!done && !items && !left) return;
    uint64_t c = __atomic_add_fetch(&applied, 1, __ATOMIC_RELAXED);
    if (c <= 64)
        rt_log("data patch: no trade evolutions: of %u species entries, %d now evolve on a level and %d on a level "
               "while holding an item, %d left alone", count, done, items, left);
}

static const struct { uint32_t at; char type; double value; const char *name; } population[] = {
    {0xd4, 'f', 20, "wild Pokemon: nearest they appear"},
    {0xd8, 'f', 60, "wild Pokemon: farthest they appear"},
    {0xdc, 'f', 80, "wild Pokemon: nearest they go"},
    {0xe0, 'f', 80, "wild Pokemon: farthest they go"},
    {0xe4, 'i', 0, "wild Pokemon: how many at once"},   // the level's count
    {0x3c, 'i', 5, "values[8]: how many at once"},
};

static int ui_loaded;    // the UI list is in use
int data_patch_ui_loaded(void) {
    data_patch_init();
    return ui_loaded;
}

static int find_on;      // TOTK_DATA_FIND
static int step_on = -1; // TOTK_SCRIPT_STEP
static volatile int64_t steps_set;

static int step_wanted(void) {
    if (step_on < 0) step_on = rt_knob("SCRIPT_STEP", "1");
    return step_on;
}

int data_patch_wants_decodes(void) { return ndecoded > 0 || ntexts > 0 || nmod_files > 0 || find_on || step_wanted(); }

// Fields of packed files not set yet (data_watch.c turns its decoder redirect off at 0).
int data_patch_open(void) {
    if (find_on || step_wanted() || nmod_files) return 1;   // the search, the step and mod files want every decode
    int n = 0;
    for (int i = 0; i < ndecoded; i++) n += !__atomic_load_n(&decoded[i].done, __ATOMIC_RELAXED);
    for (int i = 0; i < ntexts; i++) n += !__atomic_load_n(&texts[i].done, __ATOMIC_RELAXED);
    return n;
}

// UTF-8 text (with "\n" for a line break) to UTF-16 units and a terminator; characters beyond the BMP become '?'.
static uint32_t to_utf16(const char *s, uint16_t *out, uint32_t cap) {
    uint32_t n = 0;
    const uint8_t *p = (const uint8_t *)s;
    while (*p && n + 1 < cap) {
        uint32_t c = *p++;
        if (c == '\\' && *p == 'n') { p++; c = '\n'; }
        else if (c >= 0xF0) { c = '?'; while ((*p & 0xC0) == 0x80) p++; }
        else if (c >= 0xE0 && (p[0] & 0xC0) == 0x80 && (p[1] & 0xC0) == 0x80) {
            c = (c & 0x0F) << 12 | (p[0] & 0x3F) << 6 | (p[1] & 0x3F);
            p += 2;
        } else if (c >= 0xC0 && (p[0] & 0xC0) == 0x80) {
            c = (c & 0x1F) << 6 | (p[0] & 0x3F);
            p++;
        } else if (c >= 0x80) c = '?';
        out[n++] = (uint16_t)c;
    }
    out[n++] = 0;
    return n;
}

// "modfile <packed> <unpacked> <fnv1a64> <path>"; 1 taken, 0 not such a line or no such file.
static int load_mod_file(const char *line) {
    unsigned long long packed, unpacked, print;
    int at = 0;
    if (sscanf(line, "modfile %llu %llu %llx %n", &packed, &unpacked, &print, &at) != 3 || !at) return 0;
    int64_t size = plat_file_size(line + at);
    if (size <= 0) {
        rt_log("data patch: mod file not used: %s", line + at);
        mod_file_failed = 1;
        return 1;
    }
    if (nmod_files == cap_mod_files) {
        int cap = cap_mod_files ? cap_mod_files * 2 : 256;
        ModFile *n = realloc(mod_files, (size_t)cap * sizeof *n);
        if (!n) { mod_file_failed = 1; return 1; }
        mod_files = n, cap_mod_files = cap;
    }
    char *path = malloc(strlen(line + at) + 1);
    if (!path) { mod_file_failed = 1; return 1; }
    strcpy(path, line + at);
    mod_files[nmod_files++] = (ModFile){packed, unpacked, print, path, (uint64_t)size, 0};
    mod_files_sorted = 0;
    return 1;
}

// "msgtext <packed> <unpacked> <fnv1a64> <line> <tag> <text>"; 1 taken, 0 not such a line.
static int load_text(const char *line) {
    unsigned long long packed, unpacked, print;
    unsigned idx;
    char tag[64];
    int at = 0;
    if (sscanf(line, "msgtext %llu %llu %llx %u %63s %n", &packed, &unpacked, &print, &idx, tag, &at) != 5 || !at)
        return 0;
    if (ntexts >= MAX_TEXTS) return 0;
    int donor = -1, skip = 0;
    if (line[at] == '@' && sscanf(line + at, "@%d %n", &donor, &skip) == 1) at += skip;
    uint16_t buf[1024];
    uint32_t n = to_utf16(line + at, buf, 1024);
    MsgText *t = &texts[ntexts];
    t->donor = donor;
    t->units = malloc(n * sizeof buf[0]);
    if (!t->units) return 0;
    memcpy(t->units, buf, n * sizeof buf[0]);
    t->nunits = n, t->packed = packed, t->unpacked = unpacked, t->print = print, t->line = idx;
    snprintf(t->tag, sizeof t->tag, "%s", tag);
    ntexts++;
    return 1;
}

// A message file just unpacked to out (n bytes): header u16 languages (1), u16 lines, u32 section length, u32 key (0),
// u32 section offset; at the section, u32 length, then per line u32 offset (from the section), u16 units, u16 flags.
static void apply_text(MsgText *t, uint8_t *out, uint64_t n) {
    uint16_t langs, lines;
    uint32_t key0, sec;
    if (n < 16) return;
    memcpy(&langs, out, 2), memcpy(&lines, out + 2, 2), memcpy(&key0, out + 8, 4), memcpy(&sec, out + 12, 4);
    if (langs != 1 || key0 || t->line >= lines || (uint64_t)sec + 4 + 8ull * lines > n) {
        rt_log("data patch: %s: not a message file with line %u (%u languages, %u lines)", t->tag, t->line, langs, lines);
        return;
    }
    uint64_t base = (uintptr_t)out + sec;
    uint16_t *dst;
    if (t->donor >= 0) {
        if ((uint32_t)t->donor >= lines) return;
        uint32_t doff;
        uint16_t dlen;
        memcpy(&doff, out + sec + 4 + 8ull * t->donor, 4), memcpy(&dlen, out + sec + 4 + 8ull * t->donor + 4, 2);
        if ((uint64_t)sec + doff + 2ull * dlen > n || !dlen) return;
        if (t->nunits > dlen) t->units[dlen - 1] = 0, t->nunits = dlen;   // cut to the donor's space
        dst = (uint16_t *)(out + sec + doff);
    } else {
        dst = mem_spare_near(base, t->nunits * 2ull);
    }
    uint64_t dist = (uintptr_t)dst - base;
    if (!dst || (uintptr_t)dst < base || dist > 0x7FFFFFFFull || t->nunits > 0xFFFF) {
        rt_log("data patch: %s: no spare memory within 2 GiB after the file at %p", t->tag, (void *)out);
        return;
    }
    uint16_t key = (uint16_t)(0x7C89 + 0x2983 * t->line);
    for (uint32_t i = 0; i < t->nunits; i++) {
        dst[i] = t->units[i] ^ key;
        key = (uint16_t)(key << 3 | key >> 13);
    }
    uint32_t off = (uint32_t)dist;
    uint16_t len = (uint16_t)t->nunits;
    uint8_t *entry = out + sec + 4 + 8ull * t->line;
    memcpy(entry, &off, 4), memcpy(entry + 4, &len, 2);
    if (t->donor < 0) {
        // The game reads a line only when it lies within the section length (in the header at +4 and at the
        // section's start): a text in spare memory read back empty until that length was raised to cover it.
        uint32_t need = off + 2u * len, have;
        memcpy(&have, out + sec, 4);
        if (need > have) memcpy(out + sec, &need, 4), memcpy(out + 4, &need, 4);
    }
    rt_log("data patch: %s: line %u now %u units at %s (+%#x from its section)", t->tag, t->line, t->nunits - 1,
           t->donor >= 0 ? "the space of another line" : "spare memory", off);
}

static uint64_t fnv1a64(const uint8_t *p, uint64_t n) {
    uint64_t h = 0xCBF29CE484222645ull;
    for (uint64_t i = 0; i < n; i++) h = (h ^ p[i]) * 0x100000001B3ull;
    return h;
}

// One list file; returns the fields taken, adds the left-alone ones to *skipped; -1 when the file is missing.
static int load_list(const char *path, float view, float spawn, float count, int mods, int *skipped) {
    uint64_t size = 0;
    char *text = (char *)plat_file_read(path, &size);
    if (!text) return -1;
    int taken = 0;
    char *end = text + size, *line = text;
    while (line < end) {
        char *nl = memchr(line, '\n', (size_t)(end - line));
        if (!nl) nl = end;
        *nl = 0;
        if (nl > line && nl[-1] == 13) nl[-1] = 0;   // Python on Windows writes these lists with CR LF
        unsigned long long at, packed, unpacked, print;
        char type[8], group[40];
        Field f = {0};
        if (line[0] == 'm' && load_mod_file(line)) {
            taken++;
            line = nl + 1;
            continue;
        }
        if (line[0] == 'm' && load_text(line)) {
            taken++;
            line = nl + 1;
            continue;
        }
        int romfs = line[0] != '#' && sscanf(line, "romfs %llx %7s %39s %63s", &at, type, group, f.name) == 4;
        int oodle = !romfs && line[0] != '#' && sscanf(line, "oodle %llu %llu %llx %llx %7s %39s %63s", &packed,
                                                       &unpacked, &print, &at, type, group, f.name) == 7;
        if (romfs || oodle) {
            f.at = at;
            if (oodle) f.packed = packed, f.unpacked = unpacked, f.print = print;
            f.type = !strcmp(type, "f32") ? 'f' : !strcmp(type, "i32") ? 'i' : !strcmp(type, "u32") ? 'u'
                   : !strcmp(type, "i16") ? 'h' : !strcmp(type, "u16") ? 'H' : !strcmp(type, "f64") ? 'd'
                   : !strcmp(type, "u8") ? 'b' : 0;
            if (!strncmp(group, "mod=", 4)) {
                f.absolute = mods, f.value = strtod(group + 4, NULL), f.factor = mods ? 0.0f : 1.0f;
            } else {
                f.factor = !strcmp(group, "fade") ? view : !strcmp(group, "spawn") ? spawn
                         : !strcmp(group, "count") ? count : 1.0f;
            }
            if (!f.type || (!f.absolute && f.factor == 1.0f)) (*skipped)++;
            else if (romfs && nfields < MAX_FIELDS) fields[nfields++] = f, taken++;
            else if (oodle && ndecoded < MAX_FIELDS) decoded[ndecoded++] = f, taken++;
        }
        line = nl + 1;
    }
    free(text);
    return taken;
}

void data_patch_init(void) {
    static int done;
    if (done) return;
    done = 1;
    float view = factor_of("TOTK_VIEW_DIST"), spawn = factor_of("TOTK_SPAWN_DIST"), count = factor_of("TOTK_SPAWN_COUNT");
    // TOTK_MOD_VALUES=1 (or a list path): the values tools/mod_report.py --apply took from a mod (game/mod_patch.txt)
    const char *mv = cfg_get("TOTK_MOD_VALUES");
    int mods = mv && *mv && strcmp(mv, "0");
    find_on = rt_knob("DATA_FIND", "0");
    if (find_on) {
        FILE *fp = fopen("game/data_find.txt", "w");   // a fresh list each run
        if (fp) fputs("# double 1/30 found by TOTK_DATA_FIND (karpur); apply with TOTK_MOD_VALUES=game/data_find.txt\n", fp), fclose(fp);
        rt_log("data find: looking for double 1/30 in RomFS reads and Oodle decodes; lines go to game/data_find.txt");
    }
    // The PC settings page in the game's main menu (in the downloadable-content button's place) only with TOTK_UI_PATCH=1
    // or a list's path: off by default since 2026-11-08 - the maintainer wanted the game's DLC entry back, and the
    // launcher has the settings now (F1 / Esc open the Controls page, ui/menu.cpp)
    const char *ui = cfg_get("TOTK_UI_PATCH");
    int uis = ui && *ui && strcmp(ui, "0");
    const char *level = cfg_value("TOTK_POPULATION");
    int wild = !strcmp(level, "switch2") ? 40 : !strcmp(level, "enhanced") ? 80 : !strcmp(level, "extreme") ? 120 : 0;
    if (wild) {
        for (size_t i = 0; i < sizeof population / sizeof *population; i++) {
            Field f = {.at = population[i].at, .packed = 157, .unpacked = 312, .print = 0x8a61deb07c34472bull,
                       .type = population[i].type, .absolute = 1, .value = population[i].value};
            if (!f.value) f.value = wild;
            snprintf(f.name, sizeof f.name, "%s", population[i].name);
            decoded[ndecoded++] = f;
        }
        rt_log("data patch: population %s, up to %d wild Pokemon at once", level, wild);
    }
    const char *mod_list = cfg_get("TOTK_MOD_FILES");
    int mfs = mod_list && *mod_list && strcmp(mod_list, "0");
    // TOTK_NO_EXCLUSIVES=1 (or a list's path): the vtable bytes that let the other edition's exclusive wild Pokemon
    // spawn here too (game/exclusives.txt, written by tools/exclusives.py; off by default - the game's own
    // behaviour). 66 entries of the three species tables are exclusive, and 6 bytes cover them all, since the
    // exclusives of one edition share one vtable.
    const char *excl = cfg_get("TOTK_NO_EXCLUSIVES");
    int excls = excl && *excl && strcmp(excl, "0");
    const char *tev = cfg_get("TOTK_NO_TRADE_EVO");
    int tevs = tev && *tev && strcmp(tev, "0");
    if (view == 1.0f && spawn == 1.0f && count == 1.0f && !mods && !uis && !mfs && !excls && !tevs) return;
    int skipped = 0;
    if (view != 1.0f || spawn != 1.0f || count != 1.0f) {
        const char *path = cfg_get("TOTK_DATA_PATCH_LIST");
        if (!path || !*path) path = "game/data_patch.txt";
        int n = load_list(path, view, spawn, count, 0, &skipped);
        if (n < 0) rt_log("data patch: VIEW_DIST/SPAWN_DIST set, but no list %s (tools/trpfs.py --view writes it)", path);
    }
    if (mods) {
        const char *path = strcmp(mv, "1") ? mv : "game/mod_patch.txt";
        int n = load_list(path, 1, 1, 1, 1, &skipped);
        if (n < 0) rt_log("data patch: MOD_VALUES set, but no list %s (tools/mod_report.py --apply writes it)", path);
        else rt_log("data patch: %d values of a mod from %s", n, path);
    }
    if (excls) {
        if (strcmp(excl, "1")) {   // a list of its own, for another game version (tools/exclusives.py --out)
            int n = load_list(excl, 1, 1, 1, 1, &skipped);
            if (n < 0) rt_log("data patch: NO_EXCLUSIVES=%s is not a list (tools/exclusives.py writes one); the "
                              "editions keep their exclusive Pokemon", excl);
            else rt_log("data patch: %d version-exclusive bytes from %s: both editions' wild Pokemon spawn", n, excl);
        } else {
            excl_on = 1;
            rt_log("data patch: no version exclusives: both editions' exclusive wild Pokemon spawn");
        }
    }
    if (tevs) {
        evo_on = 1;
        rt_log("data patch: no trade evolutions: the %zu Pokemon that only evolve by trading evolve on a level",
               sizeof trade_evo / sizeof *trade_evo);
    }
    if (uis) {
        const char *path = ui && *ui && strcmp(ui, "1") ? ui : "game/ui_patch.txt";
        int n = load_list(path, 1, 1, 1, 1, &skipped);
        if (n < 0 && path != ui) n = load_list(path = "../../game/ui_patch.txt", 1, 1, 1, 1, &skipped);
        if (n < 0) rt_log("data patch: no UI list (tools/pc_menu.py writes game/ui_patch.txt): no PC settings page");
        else rt_log("data patch: %d UI bytes and texts from %s", n, path);
        ui_loaded = n > 0;
    }
    const char *mf = cfg_get("TOTK_MOD_FILES");
    if (mf && *mf && strcmp(mf, "0")) {
        const char *path = strcmp(mf, "1") ? mf : "game/mod_files.txt";
        int n = load_list(path, 1, 1, 1, 1, &skipped);
        if (n < 0) rt_log("data patch: MOD_FILES set, but no list %s (tools/mod_files.py writes it)", path);
        else rt_log("data patch: %d mod files to replace, from %s", nmod_files, path);
        // A raised size without its file makes the decoder fail and the game wait for ever: all of the list or nothing.
        if (mod_file_failed) {
            int kept = 0;
            for (int i = 0; i < nfields; i++)
                if (strncmp(fields[i].name, "modsize:", 8)) fields[kept++] = fields[i];
            nfields = kept;
            for (int i = 0; i < nmod_files; i++) free(mod_files[i].path);
            nmod_files = 0;
            rt_log("data patch: a mod file could not be read: no mod file is replaced");
        }
    }
    qsort(fields, (size_t)nfields, sizeof fields[0], by_offset);
    if (nfields) lo = fields[0].at, hi = fields[nfields - 1].at + 4;
    rt_log("data patch: %d fields in stored files and %d in packed files (%d left alone); view distance x%g, "
           "spawn distance x%g, spawn count x%g", nfields, ndecoded, skipped, view, spawn, count);
    if (!nfields && !ndecoded) rt_log("data patch: the lists name no field: check tools/trpfs.py --view / mod_report.py");
}

static void scale(Field *f, uint8_t *p) {
    if (f->absolute) {
        switch (f->type) {
        case 'f': { float v = (float)f->value; memcpy(p, &v, 4); break; }
        case 'i': { int32_t v = (int32_t)f->value; memcpy(p, &v, 4); break; }
        case 'u': { uint32_t v = (uint32_t)f->value; memcpy(p, &v, 4); break; }
        case 'h': { int16_t v = (int16_t)f->value; memcpy(p, &v, 2); break; }
        case 'H': { uint16_t v = (uint16_t)f->value; memcpy(p, &v, 2); break; }
        case 'd': { double v = f->value; memcpy(p, &v, 8); break; }
        case 'b': *p = (uint8_t)f->value; break;
        }
        return;
    }
    switch (f->type) {
    case 'f': { float v; memcpy(&v, p, 4); v *= f->factor; memcpy(p, &v, 4); break; }
    case 'i': { int32_t v; memcpy(&v, p, 4); v = (int32_t)(v * f->factor + (v < 0 ? -0.5f : 0.5f)); memcpy(p, &v, 4); break; }
    case 'u': { uint32_t v; memcpy(&v, p, 4); v = (uint32_t)(v * f->factor + 0.5f); memcpy(p, &v, 4); break; }
    case 'h': { int16_t v; memcpy(&v, p, 2); v = (int16_t)(v * f->factor + (v < 0 ? -0.5f : 0.5f)); memcpy(p, &v, 2); break; }
    case 'H': { uint16_t v; memcpy(&v, p, 2); v = (uint16_t)(v * f->factor + 0.5f); memcpy(p, &v, 2); break; }
    case 'd': { double v; memcpy(&v, p, 8); v *= f->factor; memcpy(p, &v, 8); break; }
    }
}

static uint64_t width(const Field *f) {
    return f->type == 'b' ? 1 : f->type == 'h' || f->type == 'H' ? 2 : f->type == 'd' ? 8 : 4;
}

// ---- TOTK_DATA_FIND: double 1/30 in the data the game reads
#define FIND_MAX 256
static const uint64_t D130 = 0x3FA1111111111111ull;
static struct { uint64_t a, b, c, d; } found[FIND_MAX];   // romfs: (off, 0, 0, 0); oodle: (unpacked, print, off, packed)
static int nfound;
static volatile int find_lock;

static void find_note(int romfs, uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
    while (__atomic_exchange_n(&find_lock, 1, __ATOMIC_ACQUIRE)) {}
    int i = 0;
    while (i < nfound && !(found[i].a == a && found[i].b == b && found[i].c == c)) i++;
    int fresh = i == nfound && nfound < FIND_MAX;
    if (fresh) found[nfound++] = (typeof(found[0])){a, b, c, d};
    int k = nfound;
    __atomic_store_n(&find_lock, 0, __ATOMIC_RELEASE);
    if (!fresh) return;
    char line[160];
    if (romfs) snprintf(line, sizeof line, "romfs %llx f64 mod=0.016666666666666666 d130_%d\n", (unsigned long long)a, k);
    else snprintf(line, sizeof line, "oodle %llu %llu %llx %llx f64 mod=0.016666666666666666 d130_%d\n",
                  (unsigned long long)d, (unsigned long long)a, (unsigned long long)b, (unsigned long long)c, k);
    FILE *fp = fopen("game/data_find.txt", "a");
    if (fp) fputs(line, fp), fclose(fp);
    if (k <= 64) rt_log("data find: double 1/30 %s (#%d)", romfs ? "in a stored RomFS file" : "in an Oodle-packed file", k);
}

// One frame at the frame limit, in seconds, for this whole run; the limit in Hz goes to *hz.
static double script_step(int *hz) {
    static int rate;
    if (!rate) {
        int r = frame_limit_hz();
        // the monitor's rate (TOTK_FPS_LIMIT=display) when the window has measured it, else the usual limit
        if (!r) r = (int)(gfx_window_refresh_hz() + 0.5);
        __atomic_store_n(&rate, r < 30 ? 60 : r > 360 ? 360 : r, __ATOMIC_RELAXED);
    }
    *hz = rate;
    return 1.0 / rate;
}

static void find_in(int romfs, uint8_t *p, uint64_t n, uint64_t base, uint64_t unpacked, uint64_t print,
                    uint64_t packed) {
    for (uint64_t i = 0; i + 8 <= n; i++) {
        if (p[i + 7] != 0x3F || p[i + 6] != 0xA1) continue;
        uint64_t v;
        memcpy(&v, p + i, 8);
        if (v != D130) continue;
        if (find_on) {
            if (romfs) find_note(1, base + i, 0, 0, 0);
            else find_note(0, unpacked, print, i, packed);
        }
        if (step_on > 0) {
            int hz;
            double step = script_step(&hz);
            memcpy(p + i, &step, 8);
            int64_t k = plat_inc64(&steps_set);
            if (k <= 16 || !(k & (k - 1)))
                rt_log("script step: double 1/30 #%lld in %s set to 1/%d (TOTK_SCRIPT_STEP=0 keeps it)", (long long)k,
                       romfs ? "a stored RomFS file" : "an Oodle-packed file", hz);
        }
    }
}

// A read of n bytes at byte offset off of romfs.bin landed in buf.
void data_patch_read(uint64_t off, void *buf, uint64_t n) {
    if (find_on || step_wanted()) find_in(1, buf, n, off, 0, 0, 0);
    if (!nfields || off >= hi || off + n <= lo) return;
    int i = 0, j = nfields;
    while (i < j) {   // first field at or after off
        int m = (i + j) / 2;
        if (fields[m].at < off) i = m + 1;
        else j = m;
    }
    for (; i < nfields; i++) {
        Field *f = &fields[i];
        uint64_t w = width(f);
        if (f->at + w > off + n) break;
        scale(f, (uint8_t *)buf + (f->at - off));
        uint64_t k = __atomic_add_fetch(&applied, 1, __ATOMIC_RELAXED);
        if (k <= 64) rt_log("data patch: %s at romfs %#llx %s%g", f->name, (unsigned long long)f->at,
                            f->absolute ? "= " : "x", f->absolute ? f->value : f->factor);
    }
}

// The game asks its Oodle decoder for out_n bytes at out from packed_n bytes at packed (data_watch.c host_oodle): when
// those bytes are a mod's file, writes the mod's file there and returns its size; else 0, and the decoder runs.
static int mod_cmp(const void *a, const void *b) {
    uint64_t x = ((const ModFile *)a)->packed, y = ((const ModFile *)b)->packed;
    return x < y ? -1 : x > y;
}

uint64_t data_patch_replace(const uint8_t *packed, uint64_t packed_n, uint8_t *out, uint64_t out_n) {
    if (!nmod_files) return 0;
    if (!__atomic_load_n(&mod_files_sorted, __ATOMIC_ACQUIRE)) {   // the list is complete before the first decode
        static PlatMutex sort_lock = PLAT_MUTEX_INIT;
        plat_mutex_lock(&sort_lock);
        if (!mod_files_sorted) qsort(mod_files, (size_t)nmod_files, sizeof *mod_files, mod_cmp), __atomic_store_n(&mod_files_sorted, 1, __ATOMIC_RELEASE);
        plat_mutex_unlock(&sort_lock);
    }
    // the entries for files of this packed size (the game decodes a file from all of its packed bytes)
    int lo = 0, hi = nmod_files;
    while (lo < hi) {
        int m = (lo + hi) / 2;
        if (mod_files[m].packed < packed_n) lo = m + 1;
        else hi = m;
    }
    uint64_t print = 0;
    for (int i = lo; i < nmod_files && mod_files[i].packed == packed_n; i++) {
        ModFile *m = &mod_files[i];
        uint64_t head = m->packed < 4096 ? m->packed : 4096;
        if (out_n != m->n && out_n != m->unpacked) continue;
        if (!print) print = fnv1a64(packed, head);
        if (print != m->print) continue;
        int first = !__atomic_exchange_n(&m->used, 1, __ATOMIC_RELAXED);
        if (out_n < m->n) {   // the size in the pack's table was not read from there: the file would not fit
            if (first) rt_log("data patch: the game reserves %llu bytes for a mod file of %llu: original kept",
                              (unsigned long long)out_n, (unsigned long long)m->n);
            return 0;
        }
        uint64_t got = 0;
        uint8_t *data = (uint8_t *)plat_file_read(m->path, &got);
        if (!data || got != m->n) {
            if (first) rt_log("data patch: mod file changed or gone since the start: %s: original kept", m->path);
            free(data);
            return 0;
        }
        memcpy(out, data, m->n);
        free(data);
        if (first) rt_log("data patch: mod file used: %llu bytes instead of %llu", (unsigned long long)m->n,
                          (unsigned long long)m->unpacked);
        return m->n;
    }
    return 0;
}

// The game's Oodle decoder unpacked n bytes to out from packed_n bytes at packed (data_watch.c host_oodle).
// TOTK_MOD_DRY=1 finds the fields but leaves them (A/B test of the redirect alone).
void data_patch_decoded(const uint8_t *packed, uint64_t packed_n, uint8_t *out, uint64_t n) {
    static int dry = -1;
    if (dry < 0) {
        const char *e = cfg_get("TOTK_MOD_DRY");
        dry = e && *e && strcmp(e, "0");
    }
    uint64_t print = 0, print_head = 0;
    if (find_on || step_wanted()) {
        uint64_t head = packed_n < 4096 ? packed_n : 4096;
        find_in(0, out, n, 0, n, fnv1a64(packed, head), packed_n);
    }
    for (int i = 0; i < ndecoded; i++) {
        Field *f = &decoded[i];
        uint64_t head = f->packed < 4096 ? f->packed : 4096, w = width(f);
        if (f->unpacked != n || packed_n < head || f->at + w > n) continue;
        if (print_head != head) print = fnv1a64(packed, head), print_head = head;
        if (print != f->print) continue;
        if (!dry) scale(f, out + f->at);
        __atomic_store_n(&f->done, 1, __ATOMIC_RELAXED);
        uint64_t k = __atomic_add_fetch(&applied, 1, __ATOMIC_RELAXED);
        if (k <= 64) rt_log("data patch: %s in a packed file (+%#llx) %s%g", f->name, (unsigned long long)f->at,
                            f->absolute ? "= " : "x", f->absolute ? f->value : f->factor);
    }
    if (evo_on && !dry) {
        uint64_t head = packed_n < 4096 ? packed_n : 4096;
        for (size_t i = 0; i < sizeof evo_file / sizeof *evo_file; i++) {
            if (evo_file[i].unpacked != n) continue;
            if (print_head != head) print = fnv1a64(packed, head), print_head = head;
            if (print == evo_file[i].print) evo_fix(out, n);
        }
    }
    if (excl_on && !dry)
        for (size_t i = 0; i < sizeof excl_files / sizeof *excl_files; i++) {
            if (excl_files[i].unpacked != n) continue;
            uint64_t head = packed_n < 4096 ? packed_n : 4096;
            if (print_head != head) print = fnv1a64(packed, head), print_head = head;
            if (print == excl_files[i].print) excl_fix(out, n);
        }
    for (int i = 0; i < ntexts; i++) {
        MsgText *t = &texts[i];
        uint64_t head = t->packed < 4096 ? t->packed : 4096;
        if (t->unpacked != n || packed_n < head) continue;
        if (print_head != head) print = fnv1a64(packed, head), print_head = head;
        if (print != t->print) continue;
        if (!dry) apply_text(t, out, n);
        __atomic_store_n(&t->done, 1, __ATOMIC_RELAXED);
    }
}
