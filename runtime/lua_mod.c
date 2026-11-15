// KarPur: the runtime's own Lua in the game's Lua state (patch set lua_mod, TOTK_LUA_MOD=1; tools/fps_patches.py).
//
// Violet's UI logic is Haxe compiled to Lua 5.4 (script/lua/bin/release/main/main.blua). main+0xe37dac opens the
// standard libraries, loads that script with luaL_loadbufferx and runs it with lua_pcallk; with the set on, that one
// call (main+0xe37f94) comes here (patch page +0xfc0). The game's call runs first, unchanged; then, once, the runtime
// loads its own Lua (runtime/lua/menu_mod.lua, built in; TOTK_LUA_MOD_FILE names a file to load instead, for trying
// changes without a rebuild) into the same state with the game's luaL_loadbufferx and runs it with the game's
// lua_pcallk, so it sees every class the game's script defined. Its return value or error message goes to the log,
// and the stack top is put back as it was, so the game's code after the call finds its stack unchanged.
// Purpose: the "PC settings" entry in the game's main menu (docs/progress.md 2026-11-03).
//
// The way back: Lua's print reaches the runtime as the debug string of svc OutputDebugString (svc.c), which hands it
// to lua_mod_command. A line "KARPUR_CMD <command>" asks the runtime for something; all serve the PC settings page in
// the game's options (runtime/lua/menu_mod.lua): pc_page_ready locks the F1 menu it replaces, set changes a setting,
// sync writes the current ones into a Lua table, open_controls shows the Controls page.
//
// Reading a string result: Lua 5.4's lua_State keeps the stack top at +16 (after the GC header, status, allowhook and
// nci), then l_G, ci, stack_last at +40 and the stack's base at +48; a stack slot is a 16-byte TValue (value, then
// the type tag); a string's characters start at +24 of its TString, the length of a short one at +11, of a long one
// at +16. Lua moves the whole stack when the runtime's Lua needs more of it (luaD_reallocstack), so the top is put
// back as a distance from the base: the first version wrote the old pointer back, and after a script that grew the
// stack the game aborted from its Lua code a few seconds later (2026-11-03, find_menu.lua and inspect_xmenu.lua).
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cfg.h"
#include "host.h"
#include "platform/platform.h"
#include "ui/menu.h"

#define PAGE_HOST 0xfc0           // host function entry on the patch page (recomp/patches.py layout)
#define LUA_PCALLK 0xb5ddd0       // main offsets in 4.0.0 (tools/fps_patches.py checks the call sites)
#define LUA_LOADBUFFERX 0xe382a0
#define LUA_TOP 16                // offset of the stack top in lua_State
#define LUA_STACK_LAST 40         // offset of the stack's end
#define LUA_STACK 48              // offset of the stack's base
#define TAG_SHORT_STR 0x44        // LUA_VSHRSTR with the collectable bit
#define TAG_LONG_STR 0x54         // LUA_VLNGSTR with the collectable bit

#if defined(__linux__)
static const char builtin[] = {
#include "menu_mod.inc"
};
#else
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"
static const char builtin[] = {
#embed "lua/menu_mod.lua"
};
#pragma clang diagnostic pop
#endif

#define MAX_STATES 16

static uint64_t base;
static PlatMutex lock = PLAT_MUTEX_INIT;
static uint64_t states[MAX_STATES];   // the Lua states the runtime's Lua already ran in
static int nstates, runs;

// The string at stack index -1 of L into out (empty when it is not a string).
static void top_string(uint64_t L, char *out, size_t cap) {
    out[0] = 0;
    uint64_t top = *(const uint64_t *)(uintptr_t)(L + LUA_TOP);
    if (!top || top >= RT_ADDRESS_LIMIT) return;
    uint64_t gc = *(const uint64_t *)(uintptr_t)(top - 16);
    uint8_t tag = *(const uint8_t *)(uintptr_t)(top - 8);
    if ((tag != TAG_SHORT_STR && tag != TAG_LONG_STR) || !gc || gc >= RT_ADDRESS_LIMIT) return;
    uint64_t n = tag == TAG_SHORT_STR ? *(const uint8_t *)(uintptr_t)(gc + 11) : *(const uint64_t *)(uintptr_t)(gc + 16);
    if (n >= cap) n = cap - 1;
    memcpy(out, (const void *)(uintptr_t)(gc + 24), n);
    out[n] = 0;
}

static void run_mod(Cpu *c, uint64_t L) {
    uint64_t size = 0;
    char *own = NULL;
    const char *file = cfg_get("TOTK_LUA_MOD_FILE");
    if (file && *file) {
        own = (char *)plat_file_read(file, &size);
        if (!own) rt_log("lua mod: cannot read %s; running the built-in script", file);
    }
    const char *src = own ? own : builtin;
    if (!own) size = sizeof builtin;
    uint64_t top0 = *(const uint64_t *)(uintptr_t)(L + LUA_TOP), base0 = *(const uint64_t *)(uintptr_t)(L + LUA_STACK);
    uint64_t last0 = *(const uint64_t *)(uintptr_t)(L + LUA_STACK_LAST);
    if (!(base0 && base0 <= top0 && top0 <= last0 && !((top0 - base0) & 15))) {
        rt_log("lua mod: the Lua state at %#llx does not look like Lua 5.4's (stack %#llx..%#llx, top %#llx): not run",
               (unsigned long long)L, (unsigned long long)base0, (unsigned long long)last0, (unsigned long long)top0);
        free(own);
        return;
    }
    uint64_t depth = top0 - base0;
    static char msg[65536];   // one Lua runs at a time: the game builds its states one after the other
    uint64_t t0 = plat_now_ns();
    uint64_t st = rt_call(c, base + LUA_LOADBUFFERX, 5, L, (uint64_t)(uintptr_t)src, size,
                          (uint64_t)(uintptr_t)"=karpur", 0);
    if (st) {
        top_string(L, msg, sizeof msg);
        rt_log("lua mod: loading %s failed (%llu): %s", own ? file : "the built-in script", (unsigned long long)st, msg);
    } else {
        st = rt_call(c, base + LUA_PCALLK, 6, L, 0, 1, 0, 0, 0);   // no arguments, one result, no handler
        top_string(L, msg, sizeof msg);
        // one log line per result line, so a longer report stays readable
        const char *p = msg[0] ? msg : "(no string result)";
        rt_log("lua mod: %s %s in %.2f ms:", own ? file : "built-in script", st ? "failed" : "ran",
               (plat_now_ns() - t0) / 1e6);
        while (*p) {
            const char *nl = strchr(p, '\n');
            int n = nl ? (int)(nl - p) : (int)strlen(p);
            rt_log("lua mod:   %.*s", n, p);
            p += n + (nl != NULL);
        }
    }
    uint64_t base1 = *(const uint64_t *)(uintptr_t)(L + LUA_STACK);
    if (base1 != base0) rt_log("lua mod: Lua moved its stack while the runtime's Lua ran (%#llx -> %#llx)",
                               (unsigned long long)base0, (unsigned long long)base1);
    *(uint64_t *)(uintptr_t)(L + LUA_TOP) = base1 + depth;   // the game's stack as it was, wherever it is now
    free(own);
}

// The game builds several Lua states (one per script host; six in a run to the field), each running the main script
// once: the runtime's Lua runs once in each, numbered in the log.
static void host_run_main(Cpu *c) {
    uint64_t a[6];
    memcpy(a, c->x, sizeof a);
    uint64_t r = rt_call(c, base + LUA_PCALLK, 6, a[0], a[1], a[2], a[3], a[4], a[5]);
    plat_mutex_lock(&lock);
    int seen = 0, k = ++runs;
    for (int i = 0; i < nstates; i++) seen |= states[i] == a[0];
    if (!seen && nstates < MAX_STATES) states[nstates++] = a[0];
    int idx = nstates;
    plat_mutex_unlock(&lock);
    rt_log("lua mod: the game's main script ran (run %d, state %d%s, lua_pcallk %llu)", k, idx,
           seen ? ", seen before" : "", (unsigned long long)r);
    // The built-in script needs the texts and the button of the patch list (tools/pc_menu.py): without it the game
    // stays as it ships, the downloadable-content button included.
    const char *file = cfg_get("TOTK_LUA_MOD_FILE");
    if (!r && !seen && ((file && *file) || data_patch_ui_loaded())) {
        static PlatMutex run_lock = PLAT_MUTEX_INIT;   // the message buffer is shared
        plat_mutex_lock(&run_lock);
        run_mod(c, a[0]);
        plat_mutex_unlock(&run_lock);
    }
    c->x[0] = r;
}

// The settings the PC settings page (runtime/lua/menu_mod.lua) may change: live ones of the F1 menu, and POPULATION,
// which takes effect at the next start.
static const char *const pc_keys[] = {"FULLSCREEN", "WINDOW_SIZE", "FPS_LIMIT", "VK_PRESENT", "UPSCALER", "SHARPNESS",
                                      "PERF_OVERLAY", "POPULATION"};

static int pc_key(const char *k, size_t n) {
    for (size_t i = 0; i < sizeof pc_keys / sizeof *pc_keys; i++)
        if (strlen(pc_keys[i]) == n && !memcmp(pc_keys[i], k, n)) return (int)i;
    return -1;
}

// "set KEY VALUE": the setting changes at once (cfg_set, as an F1 menu row does) and totk.ini is written.
static void cmd_set(const char *s, size_t len) {
    const char *sp = memchr(s, ' ', len);
    int k = sp ? pc_key(s, (size_t)(sp - s)) : -1;
    size_t vn = sp ? len - (size_t)(sp + 1 - s) : 0;
    if (k < 0 || !vn || vn > 31) {
        rt_log("lua mod: set: not a PC setting: %.*s", (int)(len < 64 ? len : 64), s);
        return;
    }
    char name[48], value[32];
    snprintf(name, sizeof name, "TOTK_%s", pc_keys[k]);
    memcpy(value, sp + 1, vn), value[vn] = 0;
    cfg_set(name, value);
    int saved = cfg_save();
    rt_log("lua mod: PC settings page: %s=%s%s", pc_keys[k], value, saved ? "" : " (totk.ini not written)");
}

// "sync ADDR SLOT KEY:a,b,c" from the PC settings page: ADDR is a Lua table of integers (tostring of {0, 0, ...});
// the runtime writes into its slot SLOT (0-based) the position of the setting's value in the list (the nearest for a
// number between two steps, -1 when it is none of them). "sync ADDR SLOT MONITOR_W" writes the monitor's width in
// pixels (0 unknown). One setting per line, as the game's print may cut a long one. Lua 5.4's Table keeps its type at
// +8 (0x05: the object header has no collectable bit, only a TValue pointing to it has, 0x45), the size of its array
// part at +12 and the array at +16; an integer TValue is the value and the tag 0x03. Only a slot that holds an integer
// is written, so the table needs no write barrier.
static void cmd_sync(const char *s, size_t len) {
    char buf[256];
    unsigned long long t = 0;
    unsigned slot = 0;
    int used = 0;
    if (len >= sizeof buf) {
        rt_log("lua mod: sync: line of %llu bytes is too long", (unsigned long long)len);
        return;
    }
    memcpy(buf, s, len), buf[len] = 0;
    if (sscanf(buf, "%llx %u %n", &t, &slot, &used) != 2 || !used) {
        rt_log("lua mod: sync: cannot read %s", buf);
        return;
    }
    uint8_t tt = t && t < RT_ADDRESS_LIMIT ? *(const uint8_t *)(uintptr_t)(t + 8) : 0xFF;
    if (tt != 0x05) {
        rt_log("lua mod: sync: %llx is not a Lua table (type %#x)", t, tt);
        return;
    }
    uint32_t size = *(const uint32_t *)(uintptr_t)(t + 12);
    uint64_t arr = *(const uint64_t *)(uintptr_t)(t + 16);
    if (slot >= size || !arr || arr >= RT_ADDRESS_LIMIT) return;
    char *tok = buf + used, *colon = strchr(tok, ':');
    int64_t pos = -1;
    if (!strcmp(tok, "MONITOR_W")) {
        int w = 0, h = 0;
        pos = gfx_window_monitor_size(&w, &h) ? w : 0;
    } else {
        int k = colon ? pc_key(tok, (size_t)(colon - tok)) : -1;
        if (k < 0) return;
        char name[48];
        snprintf(name, sizeof name, "TOTK_%s", pc_keys[k]);
        const char *cur = cfg_value(name);
        int64_t near = -1, i = 0;
        char *end = NULL;
        double want = strtod(cur, &end), best = 0;
        int numeric = end != cur;
        for (char *v = colon + 1; *v; i++) {
            char *comma = strchr(v, ',');
            size_t vn = comma ? (size_t)(comma - v) : strlen(v);
            if (strlen(cur) == vn && !memcmp(cur, v, vn)) pos = i;
            double got = strtod(v, &end), diff = got > want ? got - want : want - got;
            if (numeric && end != v && (near < 0 || diff < best)) near = i, best = diff;
            v += vn + (comma != NULL);
        }
        if (pos < 0) pos = near;   // a value between two steps (the sharpness 85, set by hand): the nearest step
    }
    uint8_t *tv = (uint8_t *)(uintptr_t)(arr + 16ull * slot);
    if (tv[8] == 0x03) memcpy(tv, &pos, 8);
}

void lua_mod_command(const char *text, uint64_t n) {
    static const char tag[] = "KARPUR_CMD ";
    if (n < sizeof tag - 1 || memcmp(text, tag, sizeof tag - 1)) return;
    const char *cmd = text + sizeof tag - 1;
    size_t len = (size_t)(n - (sizeof tag - 1));
    while (len && (cmd[len - 1] == '\n' || cmd[len - 1] == '\r' || cmd[len - 1] == ' ' || !cmd[len - 1])) len--;
    if (len == 13 && !memcmp(cmd, "pc_page_ready", 13)) {
        // The F1 menu is locked only once the page that replaces it is in place: the page's script runs and its texts
        // and button come from the patch list. Any other start keeps F1, so the settings stay reachable.
        int lock = data_patch_ui_loaded() && rt_knob("MENU_LOCK", "1");
        if (lock) menu_lock();
        rt_log("lua mod: PC settings page ready; F1 menu %s", lock ? "locked (TOTK_MENU_LOCK=0 keeps it)" : "stays");
    } else if (len == 13 && !memcmp(cmd, "open_controls", 13)) {
        rt_log("lua mod: PC settings page opens the Controls tab");
        menu_open_controls();
    } else if (len > 4 && !memcmp(cmd, "set ", 4)) {
        cmd_set(cmd + 4, len - 4);
    } else if (len > 5 && !memcmp(cmd, "sync ", 5)) {
        cmd_sync(cmd + 5, len - 5);
    } else {
        rt_log("lua mod: unknown command %.*s", (int)(len < 64 ? len : 64), cmd);
    }
}

void lua_mod_init(void) {
    int set = rt_patch_set("lua_mod");
    if (set < 0 || !rt_patch_page()) return;
    // The entry exists whenever the set does: with the set off the site keeps its original call and never comes here.
    rt_register_host((uint64_t)(uintptr_t)rt_patch_page() + PAGE_HOST, host_run_main);
    base = ldr_module(0)->base;
    if (rt_patch_enabled(set))
        rt_log("lua mod: on; the runtime's Lua runs after the game's main script (main+%#x)", LUA_PCALLK);
}
