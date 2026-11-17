// The op-stream replay core of the Vulkan back end: walks the command handles of
// one submission into its command buffer, keeps render targets and fixed-function state, opens and closes dynamic
// rendering passes and records clears and barriers. Draws, binds and dispatches go to vk_exec_op_draw
// (vk_exec_draw.c); copies, counters, events and conditional rendering to vk_exec_op_xfer (vk_exec_xfer.c). Images
// stay in VK_IMAGE_LAYOUT_GENERAL (vk_texture.c); full memory barriers order passes, clears and whatever those two
// record between them.
//
// The op loop (END / BEGIN / JUMP / CALL, which ops are pure state, the state defaults, render targets kept until they
// change, clears skipped while rendering is disabled) follows MacNativeGames
// games/totk/third_party/d2r_gfx/gfx/nvn/nvn_mtl.m:10588-10700, 7409-7460, 5449-5457
// (https://github.com/ly05010419/MacNativeGames @ b75be8cfa704df3701e86130c8ced268e79d6b11, MIT, Copyright (c) 2026
// jhaizhou). Pass begin and end (GENERAL-layout attachments, LOAD/STORE, render area from the smallest attachment, a
// full barrier before each pass), the scissor clip and attachment clears follow LAS Recomp
// runtime/nvn/vulkan/exec.c:855-1149, 3553-3778 (https://github.com/peiikai/LASRecomp @
// da35b83a9611d046a9a681aae668d7085954d37f, MIT, Copyright (c) 2026 Peiikai).
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cfg.h"
#include "host.h"
#include "nvn_api.h"
#include "nvn_tex.h"
#include "platform/platform.h"
#include "tex.h"
#include "vk.h"
#include "vk_exec.h"

#define WEAK __attribute__((weak))
#define CALL_DEPTH 8

typedef struct {
    VkImage image;
    VkFormat format;
    VkImageViewType type;
    VkImageAspectFlags aspect;
    uint32_t level, base, layers;
    VkImageView view;
} ViewEnt;

static VkExec X;
static int x_ready;
static _Thread_local int x_here;   // this thread is inside vk_exec_record (vk_exec_inline_begin)

// ---- TOTK_TRACE=gfx frame counters

VkExecFrame vk_exec_frame;
static struct { const uint8_t *mem; uint32_t w, h, fmt, n; } fw[64];   // textures written this frame
static int nfw;

uint64_t vk_exec_recording_fence(void) { return x_here ? X.fence : 0; }

void vk_exec_frame_written(const tex_t *t) {
    if (!(rt_trace & RT_TRACE_GFX) || !t) return;
    for (int i = 0; i < nfw; i++)
        if (fw[i].mem == t->mem) { fw[i].n++; return; }
    if (nfw == 64) return;
    fw[nfw].mem = t->mem, fw[nfw].w = t->d.width, fw[nfw].h = t->d.height, fw[nfw].fmt = (uint32_t)t->d.format;
    fw[nfw++].n = 1;
}

// Render target sizes: passes and the draws in them per size (with or without depth), to tell the 3D scene's size
// (the size with depth and most draws) from the output and the smaller effect targets.
static struct { uint32_t w, h, depth; uint64_t passes, draws; } psz[32];
static int npsz, cur_psz = -1;
static uint64_t psz_draws0;

// ---- TOTK_PASS_TIME=1 (a diagnostic, off by default): where the GPU time of a frame goes. Timestamps at the start of
// a pass (before its barrier), inside it after it began, at its end and after each op outside a pass split the GPU's
// time into barriers before passes, bodies of passes by target size, dispatches, copies, clears, inline updates, game
// barriers and the start and end of a submission (mirror uploads and write-backs). The slowest dispatch and pass of the
// period are named by their group counts and by their target size and draws.
// TOTK_PASS_TIME=2 also takes a full memory barrier right after each pass and dispatch, before its stamp: a stamp alone
// does not wait for the work before it, so a dispatch's time turned up in the game barrier after it. Only this mode
// charges a pass or a dispatch with its own work. The costliest shaders are listed by id, the one TOTK_SKIP_FS takes
// (a pass by its last draw's fragment shader). The added barriers cost GPU time of their own.
// Results are read when the submission's fence completes. A stamp waits for the work before it at the bottom of the
// pipe, so the sum can run a little above the time without the switch.
#define PT_SLOTS 8
#define PT_STAMPS 16384
#define PT_LABELS 40
enum { PT_OTHER, PT_BARRIER, PT_DISPATCH, PT_COPY, PT_CLEAR, PT_UB, PT_GAME, PT_START, PT_END, PT_SIZE0 };
static VkQueryPool pt_pool;
static int pt_state;   // 0 not decided, 1 on, -1 off
static float pt_period;
static uint64_t pt_mask;
static struct { uint32_t w, h, depth; } pt_size[PT_LABELS - PT_SIZE0];
static int pt_nsize;
static volatile int64_t pt_ns[PT_LABELS], pt_lost;
static uint8_t pt_label[PT_SLOTS][PT_STAMPS];   // pt_label[s][i]: what the time between stamps i - 1 and i was spent on
static uint32_t pt_aux[PT_SLOTS][PT_STAMPS];    // a dispatch's group counts (10 bits each) or a pass's draws
static uint32_t pt_aux_next;                    // replay thread: pt_aux of the next stamp
static double pt_max_disp, pt_max_pass, pt_max_game;   // completion thread: slowest of the period (ms)
static uint32_t pt_max_disp_aux, pt_max_pass_aux, pt_max_game_aux;
static uint8_t pt_max_pass_label;
static uint64_t pt_game_slow;                   // completion thread: game barriers over 1 ms in the period
static uint32_t pt_prev_pass;                   // replay thread: the last pass, label << 24 | draws (a game barrier's pt_aux)
uint32_t vk_exec_last_fs, vk_exec_last_cs;
static uint32_t pt_prev_fs;                     // replay thread: vk_exec_last_fs at the end of the last pass
static uint32_t pt_fs[PT_SLOTS][PT_STAMPS];     // a game barrier's pt_prev_fs
static uint32_t pt_max_game_fs;
static int pt_strict;                           // TOTK_PASS_TIME=2 (and 3)
static int pt_split;                            // TOTK_PASS_TIME=3: every draw is a pass of its own (timed alone)
#define PT_FS_N 128
typedef struct { uint32_t id, n; double ms; } PtById;
static PtById pt_by_fs[PT_FS_N], pt_by_cs[PT_FS_N];   // completion thread: pass bodies and dispatches by shader
static uint32_t pt_id_next;                     // replay thread: the shader of the next stamp, 0: pt_prev_fs
static uint32_t pt_max_disp_id;

static void pt_tab_add(PtById *tab, uint32_t id, double ms) {
    for (uint32_t k = 0; k < PT_FS_N; k++) {
        PtById *e = &tab[(id + k) % PT_FS_N];
        if (!e->n || e->id == id) {
            e->id = id, e->n++, e->ms += ms;
            return;
        }
    }
}

// The costliest entries of tab as text, most time first; empties tab.
static void pt_tab_line(PtById *tab, double frames, char *out, size_t cap) {
    size_t o = 0;
    out[0] = 0;
    for (int k = 0; k < 6 && o < cap; k++) {
        int best = -1;
        for (int i = 0; i < PT_FS_N; i++)
            if (tab[i].n && (best < 0 || tab[i].ms > tab[best].ms)) best = i;
        if (best < 0) break;
        o += (size_t)snprintf(out + o, cap - o, "%s%08x %.2f ms in %.1f", k ? ", " : "", tab[best].id, tab[best].ms / frames,
                              tab[best].n / frames);
        tab[best].n = 0;
    }
    memset(tab, 0, sizeof(PtById) * PT_FS_N);
}
static volatile uint8_t pt_busy[PT_SLOTS];
static int pt_cur = -1, pt_next, pt_skip;       // replay thread: slot of this submission; no free slot for it
static uint32_t pt_n;

typedef struct { int slot; uint32_t n; } PtRead;

static void pt_setup(void) {
    pt_state = -1;
    if (!rt_knob("PASS_TIME", "0")) return;
    VkCtx *c = vk_ctx();
    VkPhysicalDeviceVulkan12Features f12 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceFeatures2 f2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &f12};
    vkGetPhysicalDeviceFeatures2(c->phys, &f2);
    uint32_t nq = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(c->phys, &nq, NULL);
    VkQueueFamilyProperties *qp = calloc(nq ? nq : 1, sizeof *qp);
    if (!qp) return;
    vkGetPhysicalDeviceQueueFamilyProperties(c->phys, &nq, qp);
    uint32_t bits = c->gfx_family < nq ? qp[c->gfx_family].timestampValidBits : 0;
    free(qp);
    VkQueryPoolCreateInfo qi = {.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, .queryType = VK_QUERY_TYPE_TIMESTAMP,
                                .queryCount = PT_SLOTS * PT_STAMPS};
    if (!f12.hostQueryReset || !bits || vkCreateQueryPool(c->device, &qi, NULL, &pt_pool) != VK_SUCCESS) {
        rt_log("vk_exec: TOTK_PASS_TIME: no hostQueryReset, timestamps or query pool: passes not timed");
        return;
    }
    vkResetQueryPool(c->device, pt_pool, 0, PT_SLOTS * PT_STAMPS);
    pt_period = c->props.limits.timestampPeriod;
    pt_mask = bits >= 64 ? ~0ull : (1ull << bits) - 1;
    pt_state = 1;
    const char *mode = cfg_get("TOTK_PASS_TIME");
    pt_split = mode && !strcmp(mode, "3");
    pt_strict = pt_split || (mode && !strcmp(mode, "2"));
    rt_log("vk_exec: GPU time by pass measured (TOTK_PASS_TIME)%s%s", pt_strict ? ", with a full barrier after each pass" : "",
           pt_split ? "; each draw ends its pass, so the fragment shader table times single draws" : "");
}

// Marks the end of what the GPU just did as label (replay thread).
static void pt_stamp(VkExec *ex, int label) {
    if (!pt_state) pt_setup();
    if (pt_state < 0 || pt_skip) return;
    if (pt_cur < 0) {
        if (__atomic_load_n(&pt_busy[pt_next], __ATOMIC_ACQUIRE)) {   // readbacks are behind: skip this submission
            pt_skip = 1;
            plat_inc64(&pt_lost);
            return;
        }
        pt_cur = pt_next;
        pt_next = (pt_next + 1) % PT_SLOTS;
        pt_busy[pt_cur] = 1;
        pt_n = 0;
    }
    if (pt_n == PT_STAMPS) return;
    pt_label[pt_cur][pt_n] = (uint8_t)label;
    pt_aux[pt_cur][pt_n] = pt_aux_next;
    pt_fs[pt_cur][pt_n] = pt_id_next ? pt_id_next : pt_prev_fs;
    pt_id_next = 0;
    pt_aux_next = 0;
    vkCmdWriteTimestamp(ex->cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pt_pool, (uint32_t)pt_cur * PT_STAMPS + pt_n);
    pt_n++;
}

// TOTK_PASS_TIME=2: the work so far finishes before the next stamp. No image changes layout and no tracking state
// changes: the replay goes on as before.
static void pt_strict_barrier(VkExec *ex) {
    if (!pt_strict) return;
    VkMemoryBarrier2 mb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                           .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                           .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
                           .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                           .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT};
    VkDependencyInfo di = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .memoryBarrierCount = 1, .pMemoryBarriers = &mb};
    vkCmdPipelineBarrier2(ex->cb, &di);
}

static int pt_pass_label(const VkExec *ex) {
    uint32_t dep = ex->pass_depth ? 1 : 0;
    for (int i = 0; i < pt_nsize; i++)
        if (pt_size[i].w == ex->pass_w && pt_size[i].h == ex->pass_h && pt_size[i].depth == dep) return PT_SIZE0 + i;
    if (pt_nsize == PT_LABELS - PT_SIZE0) return PT_OTHER;
    __atomic_store_n(&pt_size[pt_nsize].w, ex->pass_w, __ATOMIC_RELAXED);
    __atomic_store_n(&pt_size[pt_nsize].h, ex->pass_h, __ATOMIC_RELAXED);
    __atomic_store_n(&pt_size[pt_nsize].depth, dep, __ATOMIC_RELAXED);
    __atomic_store_n(&pt_nsize, pt_nsize + 1, __ATOMIC_RELEASE);
    return PT_SIZE0 + pt_nsize - 1;
}

static void pt_read(void *arg) {   // completion thread, one at a time
    static uint64_t r[2 * PT_STAMPS];
    PtRead *p = arg;
    VkCtx *c = vk_ctx();
    uint32_t first = (uint32_t)p->slot * PT_STAMPS;
    // A stamp in a part that was never submitted stays unavailable: it is left out, the others still count.
    VkResult res = vkGetQueryPoolResults(c->device, pt_pool, first, p->n, 16 * (size_t)p->n, r, 16,
                                         VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    if (res == VK_SUCCESS || res == VK_NOT_READY) {
        uint64_t prev = 0;
        int have = 0;
        for (uint32_t i = 0; i < p->n; i++) {
            if (!r[2 * i + 1]) { have = 0; continue; }
            if (have) {
                int label = pt_label[p->slot][i];
                double ns = (double)((r[2 * i] - prev) & pt_mask) * pt_period;
                plat_add64(&pt_ns[label], (int64_t)ns);
                if (label == PT_DISPATCH && ns / 1e6 > pt_max_disp)
                    pt_max_disp = ns / 1e6, pt_max_disp_aux = pt_aux[p->slot][i], pt_max_disp_id = pt_fs[p->slot][i];
                if (label == PT_GAME && ns > 1e6) pt_game_slow++;
                if (label == PT_GAME && ns / 1e6 > pt_max_game)
                    pt_max_game = ns / 1e6, pt_max_game_aux = pt_aux[p->slot][i], pt_max_game_fs = pt_fs[p->slot][i];
                if (label >= PT_SIZE0) pt_tab_add(pt_by_fs, pt_fs[p->slot][i], ns / 1e6);
                if (label == PT_DISPATCH) pt_tab_add(pt_by_cs, pt_fs[p->slot][i], ns / 1e6);
                if (label >= PT_SIZE0 && ns / 1e6 > pt_max_pass)
                    pt_max_pass = ns / 1e6, pt_max_pass_aux = pt_aux[p->slot][i], pt_max_pass_label = (uint8_t)label;
            }
            prev = r[2 * i];
            have = 1;
        }
    }
    vkResetQueryPool(c->device, pt_pool, first, p->n);
    __atomic_store_n(&pt_busy[p->slot], 0, __ATOMIC_RELEASE);
    free(p);
}

// At the end of a submission's recording; last_part: the last part holds commands, so a stamp in it runs.
static void pt_finish(VkExec *ex, int last_part) {
    pt_skip = 0;
    if (pt_cur < 0) return;
    if (last_part) pt_stamp(ex, PT_END);
    PtRead *p = malloc(sizeof *p);
    if (!p) rt_fatal("vk: out of memory");
    p->slot = pt_cur, p->n = pt_n;
    vk_completion_add(ex->fence, pt_read, p);
    pt_cur = -1;
}

static void pt_summary(double frames) {
    if (pt_state <= 0) return;
    int n = __atomic_load_n(&pt_nsize, __ATOMIC_ACQUIRE);
    double ms[PT_LABELS], total = 0;
    for (int i = 0; i < PT_LABELS; i++) total += ms[i] = (double)plat_xchg64(&pt_ns[i], 0) / 1e6 / frames;
    int order[PT_LABELS - PT_SIZE0];
    for (int i = 0; i < n; i++) order[i] = i;
    for (int i = 1; i < n; i++)   // most time first
        for (int j = i; j > 0 && ms[PT_SIZE0 + order[j]] > ms[PT_SIZE0 + order[j - 1]]; j--) {
            int t = order[j]; order[j] = order[j - 1]; order[j - 1] = t;
        }
    char line[600];
    size_t o = 0;
    line[0] = 0;
    for (int k = 0; k < n && k < 10 && o < sizeof line - 60; k++) {
        int i = order[k];
        o += (size_t)snprintf(line + o, sizeof line - o, ", %ux%u%s %.2f", pt_size[i].w, pt_size[i].h,
                              pt_size[i].depth ? "+depth" : "", ms[PT_SIZE0 + i]);
    }
    rt_log("vk_exec: GPU ms per frame: total %.2f, dispatches %.2f, copies %.2f, clears %.2f, inline updates %.2f, game "
           "barriers %.2f, submission start %.2f, end %.2f, other %.2f, barrier before a pass %.2f; pass bodies%s; "
           "submissions not timed %lld", total, ms[PT_DISPATCH], ms[PT_COPY], ms[PT_CLEAR], ms[PT_UB], ms[PT_GAME],
           ms[PT_START], ms[PT_END], ms[PT_OTHER], ms[PT_BARRIER], line, (long long)plat_xchg64(&pt_lost, 0));
    int mp = pt_max_pass_label >= PT_SIZE0 ? pt_max_pass_label - PT_SIZE0 : 0;
    rt_log("vk_exec: slowest dispatch %.2f ms (shader %08x, groups %ux%ux%u, 0 when indirect), slowest pass %.2f ms (%ux%u%s, %u draws)",
           pt_max_disp, pt_max_disp_id, pt_max_disp_aux >> 20, pt_max_disp_aux >> 10 & 1023, pt_max_disp_aux & 1023, pt_max_pass,
           pt_size[mp].w, pt_size[mp].h, pt_size[mp].depth ? "+depth" : "", pt_max_pass_aux);
    int gl = (int)(pt_max_game_aux >> 24) - PT_SIZE0;
    if (gl < 0 || gl >= n) gl = 0;
    rt_log("vk_exec: slowest game barrier %.2f ms, right after a pass %ux%u%s with %u draws, last fragment shader %08x; "
           "game barriers over 1 ms per frame %.2f", pt_max_game, pt_size[gl].w, pt_size[gl].h,
           pt_size[gl].depth ? "+depth" : "", pt_max_game_aux & 0xffffff, pt_max_game_fs, (double)pt_game_slow / frames);
    char top[300], topc[300];
    pt_tab_line(pt_by_fs, frames, top, sizeof top);
    pt_tab_line(pt_by_cs, frames, topc, sizeof topc);
    rt_log("vk_exec: pass bodies per frame by last fragment shader (ms in passes): %s", top);
    rt_log("vk_exec: dispatches per frame by compute shader (ms in dispatches): %s", topc);
    pt_max_disp = pt_max_pass = pt_max_game = 0;
    pt_game_slow = 0;
}

static void size_begin(const VkExec *ex) {
    uint32_t dep = ex->pass_depth ? 1 : 0;
    int i = 0;
    while (i < npsz && !(psz[i].w == ex->pass_w && psz[i].h == ex->pass_h && psz[i].depth == dep)) i++;
    if (i == npsz) {
        if (npsz == 32) { cur_psz = -1; return; }
        psz[npsz++] = (typeof(psz[0])){ex->pass_w, ex->pass_h, dep, 0, 0};
    }
    psz[i].passes++;
    cur_psz = i;
    psz_draws0 = vk_exec_frame.draws;
}

static void size_end(void) {
    if (cur_psz >= 0) psz[cur_psz].draws += vk_exec_frame.draws - psz_draws0;
    cur_psz = -1;
}

// The scene size the overlay shows (res_sizes without resolution patch sets): the largest pass with depth that holds
// at least a quarter of the draws of the busiest size.
static unsigned scene_wh[2];
void vk_exec_scene_size(unsigned out[2]) {
    out[0] = __atomic_load_n(&scene_wh[0], __ATOMIC_RELAXED);
    out[1] = __atomic_load_n(&scene_wh[1], __ATOMIC_RELAXED);
}

static void size_summary(double frames) {
    int order[32];
    for (int i = 0; i < npsz; i++) order[i] = i;
    for (int i = 1; i < npsz; i++)   // most draws first
        for (int j = i; j > 0 && psz[order[j]].draws > psz[order[j - 1]].draws; j--) {
            int t = order[j]; order[j] = order[j - 1]; order[j - 1] = t;
        }
    char line[700];
    size_t o = 0;
    line[0] = 0;
    for (int k = 0; k < npsz && k < 10 && o < sizeof line - 80; k++) {
        int i = order[k];
        o += (size_t)snprintf(line + o, sizeof line - o, "%s%ux%u%s %.1f passes %.0f draws", k ? ", " : "", psz[i].w,
                              psz[i].h, psz[i].depth ? "+depth" : "", psz[i].passes / frames, psz[i].draws / frames);
    }
    rt_log("vk_exec: pass sizes per frame, most draws first: %s", line);
    uint64_t top = 0, best = 0;
    for (int i = 0; i < npsz; i++)
        if (psz[i].depth && psz[i].draws > top) top = psz[i].draws;
    for (int i = 0; i < npsz; i++)
        if (psz[i].depth && psz[i].draws * 4 >= top && top && (uint64_t)psz[i].w * psz[i].h > best) {
            best = (uint64_t)psz[i].w * psz[i].h;
            __atomic_store_n(&scene_wh[0], psz[i].w, __ATOMIC_RELAXED);
            __atomic_store_n(&scene_wh[1], psz[i].h, __ATOMIC_RELAXED);
        }
    npsz = 0;
    cur_psz = -1;
}

// KarPur: vk_exec_frame summed over its resets, so coverage.c can tell how much each F5 phase replayed.
static VkExecFrame exec_total;
// KarPur: time of vk_exec_record on the replay thread: walking the records into Vulkan commands (pipelines, views and
// barriers made on the way included), and the mirror and uniform snapshot work around it (vk_mem_mirror_*, vk_usnap_*).
static uint64_t replay_walk_ns, replay_sync_ns;

static void add_total(const VkExecFrame *f) {
    _Static_assert(sizeof(VkExecFrame) % sizeof(uint64_t) == 0, "VkExecFrame holds uint64_t counters only");
    const uint64_t *s = (const uint64_t *)f;
    uint64_t *d = (uint64_t *)&exec_total;
    for (size_t i = 0; i < sizeof *f / sizeof *s; i++) d[i] += s[i];
}

// coverage.c (TOTK_COVERAGE, TOTK_HOSTCALLS): the replay totals so far, as named counters. The replay runs on the
// render thread and these are read at a present, so a phase boundary may move a frame's counts to its neighbour.
int coverage_gpu_counters(uint64_t *v, const char **names) {
    static const char *const n[] = {"exec:submits", "exec:passes", "exec:draws seen", "exec:draws", "exec:dispatches",
                                    "exec:clears", "exec:copies", "exec:calls", "exec:full barriers",
                                    "shader:keys", "shader:translated", "time:replay walk", "time:replay sync",
                                    "time:inline copies", "bytes:inline copies"};
    enum { N = sizeof n / sizeof *n };
    if (names)
        for (int i = 0; i < N; i++) names[i] = n[i];
    if (v) {
        const VkExecFrame *f = &vk_exec_frame, *t = &exec_total;
        uint64_t keys = 0, xlated = 0, ins = 0, inb = 0;
        gfx_shader_counts(&keys, &xlated);
        nvn_inline_stats(&ins, &inb);
        const uint64_t x[N] = {t->submits + f->submits, t->passes + f->passes, t->draws_seen + f->draws_seen,
                               t->draws + f->draws, t->dispatches + f->dispatches, t->clears + f->clears,
                               t->copies + f->copies, t->calls + f->calls, t->barriers + f->barriers, keys, xlated,
                               __atomic_load_n(&replay_walk_ns, __ATOMIC_RELAXED),
                               __atomic_load_n(&replay_sync_ns, __ATOMIC_RELAXED), ins, inb};
        memcpy(v, x, sizeof x);
    }
    return N;
}

// Without TOTK_TRACE=gfx: the means per frame over the last 600 presents, for finding what makes a frame slow on the
// GPU (each full barrier lets the GPU run nothing else until all earlier work is done).
static void frame_summary(uint64_t frame) {
    static uint64_t since;
    VkExecFrame *f = &vk_exec_frame;
    if (!since) since = frame;
    uint64_t n = frame - since;
    if (n < 600) return;
    double d = (double)n;
    rt_log("vk_exec: frames %llu-%llu, per frame: submits %.1f, passes %.1f, draws %.1f (of %.1f), clears %.1f, copies "
           "%.1f, dispatches %.1f, full barriers %.1f, attachment barriers %.1f, without full barrier %.1f, restarts for a "
           "sampled write %.1f; feedback draws %.1f, in-pass barriers %.1f, pass restarts %.1f",
           (unsigned long long)since, (unsigned long long)frame, f->submits / d, f->passes / d, f->draws / d,
           f->draws_seen / d, f->clears / d, f->copies / d, f->dispatches / d, f->barriers / d, f->light_barriers / d,
           f->deferred / d, f->hz_restarts / d, f->fb_draws / d,
           f->fb_barriers / d, f->fb_restarts / d);
    rt_log("vk_exec:   full barrier before a pass or clear because of: untracked writes %.1f (ops %.1f, game barriers "
           "%.1f, storage writes of draws %.1f, transform feedback %.1f, after a pass %.1f, buffer start %.1f, list full "
           "%.1f, restarts %.1f), images not named %.1f, feedback layouts %.1f, first in buffer %.1f, sampled before %.1f; "
           "game barriers left to the tracking %.1f",
           f->full_why[VK_HZ_WHY_OTHER] / d, f->other_src[VK_HZ_OP] / d, f->other_src[VK_HZ_GAME] / d,
           f->other_src[VK_HZ_DRAW_WRITES] / d, f->other_src[VK_HZ_XFB] / d, f->other_src[VK_HZ_AFTER_PASS] / d,
           f->other_src[VK_HZ_CB_START] / d, f->other_src[VK_HZ_OVERFLOW] / d, f->other_src[VK_HZ_RESTART] / d,
           f->full_why[VK_HZ_WHY_UNKNOWN] / d, f->full_why[VK_HZ_WHY_FBL] / d, f->full_why[VK_HZ_WHY_FIRST] / d,
           f->full_why[VK_HZ_WHY_SAMPLED] / d, f->game_barriers_tracked / d);
    size_summary(d);
    pt_summary(d);
    add_total(f);
    memset(f, 0, sizeof *f);
    since = frame;
}

void vk_exec_frame_log(const tex_t *p, uint64_t frame) {
    if (!(rt_trace & RT_TRACE_GFX)) { frame_summary(frame); return; }
    VkExecFrame *f = &vk_exec_frame;
    int hit = 0;
    char list[512] = "";
    size_t o = 0;
    for (int i = 0; i < nfw; i++) {
        if (p && fw[i].mem == p->mem) hit = 1;
        if (o < sizeof list - 48)
            o += (size_t)snprintf(list + o, sizeof list - o, " %p:%ux%u/%#x*%u", (const void *)fw[i].mem, fw[i].w,
                                  fw[i].h, fw[i].fmt, fw[i].n);
    }
    rt_log("frame %llu: submits %llu passes %llu draws %llu/%llu clears %llu copies %llu dispatches %llu; present %p "
           "%ux%u/%#x written %d; feedback draws %llu barriers %llu restarts %llu layouts %llu; targets%s",
           (unsigned long long)frame, (unsigned long long)f->submits,
           (unsigned long long)f->passes, (unsigned long long)f->draws, (unsigned long long)f->draws_seen,
           (unsigned long long)f->clears, (unsigned long long)f->copies, (unsigned long long)f->dispatches,
           p ? (const void *)p->mem : NULL, p ? p->d.width : 0, p ? p->d.height : 0, p ? (uint32_t)p->d.format : 0,
           hit, (unsigned long long)f->fb_draws, (unsigned long long)f->fb_barriers, (unsigned long long)f->fb_restarts,
           (unsigned long long)f->fb_layouts, list);
    add_total(f);
    memset(f, 0, sizeof *f);
    nfw = 0;
}

WEAK void vk_exec_op_draw(VkExec *ex, uint32_t op, const void *payload) {
    (void)ex, (void)payload;
    rt_fatal("vk: op %s is not replayed yet", nvn_op_name(op));
}

WEAK void vk_exec_op_xfer(VkExec *ex, uint32_t op, const void *payload) {
    (void)ex, (void)payload;
    rt_fatal("vk: op %s is not replayed yet", nvn_op_name(op));
}

enum { W_UNRENDERABLE, W_LEVEL, W_LAYER, W_VIEW_FORMAT, W_BLOCK_VIEW, W_BLOCK_LAYERS, W_CLEAR_TARGET, W_CLEAR_MASK, W_STENCIL_MASK, W_COUNT };

static void warn_once(int key, const char *fmt, ...) {
    static volatile int32_t seen[W_COUNT];
    if (__atomic_exchange_n(&seen[key], 1, __ATOMIC_RELAXED)) return;
    char msg[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    rt_log("vk: %s (logged once)", msg);
}

// NVN defaults of the replay state (nvn_mtl.m).
static void state_reset(VkExec *ex) {
    memset(ex, 0, sizeof *ex);
    ex->st_vmask[0] = ex->st_vmask[1] = ex->st_wmask[0] = ex->st_wmask[1] = 0xff;
    ex->depth_range[1] = 1.0f;
    vk_pipe_state_defaults(&ex->pipe);
    ex->barrier_at = -1;
}

// ---- render targets

static uint32_t level_extent(uint32_t v, uint32_t level) {
    v >>= level;
    return v ? v : 1;
}

// A view format other than the texture's is used when both map directly and have the same texel size (the image is
// created MUTABLE_FORMAT for colour formats). blocks: t is a BC texture with block_views, and the view an uncompressed
// format whose texel is one of its blocks.
static int view_format(const tex_t *t, const view_t *v, int blocks, VkFormat *out) {
    TexVkFormat a, b;
    int bw, bh, vbw, vbh;
    if (!tex_vk_format(t->d.format, &a) || a.vk != a.ideal || !tex_vk_format(v->format, &b) || b.vk != b.ideal ||
        b.depth || b.stencil)
        return 0;
    int bpb = nvn_fmt_bpb(t->d.format, &bw, &bh), vbpb = nvn_fmt_bpb(v->format, &vbw, &vbh);
    if (!bpb || bpb != vbpb || (!blocks && (bw != 1 || bh != 1)) || vbw != 1 || vbh != 1) return 0;
    VkFormatProperties fp;
    vkGetPhysicalDeviceFormatProperties(vk_ctx()->phys, b.vk, &fp);
    if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)) return 0;
    *out = b.vk;
    return 1;
}

// ---- 3D BC render targets
// A 3D BC texture has no block views (vk_texture.c create_tex, TOTK_BLOCK_VIEWS_3D): on RDNA2 cards such an image
// made BLOCK_TEXEL_VIEW_COMPATIBLE and 2D_ARRAY_COMPATIBLE took GPU writes past the end of its memory (issue #5). Draws
// into it through an uncompressed view of its blocks (agl's BC4 encoder fills 32x32x32 and 64x64x64 volumes and their
// levels at the logo) go to a stand-in: a 2D array of the blocks of one level in the view's format, one layer per
// depth plane. It starts with the level's content, so a pass that writes part of a plane keeps the rest, and each pass
// end copies the planes it drew into the level (a copy between a compressed and a size-compatible uncompressed format).
typedef struct { VkImage bc, img; VmaAllocation alloc; VkFormat fmt; uint32_t level, w, h, d; } BcStandIn;
#define BC_STANDINS 64   // one per level drawn: 6 volumes of up to 7 levels at the logo
static BcStandIn bcs[BC_STANDINS];
static int nbcs;
static PlatMutex bcs_lock = PLAT_MUTEX_INIT;   // the replay adds and reads; destroy_tex (any thread) removes

static void image_barrier(VkCommandBuffer cb, VkImage img, VkImageLayout from, uint32_t layers) {
    VkImageMemoryBarrier2 ib = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
                                .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                                .oldLayout = from, .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                .image = img, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layers}};
    VkDependencyInfo di = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &ib};
    vkCmdPipelineBarrier2(cb, &di);
}

static void memory_barrier(VkCommandBuffer cb, VkPipelineStageFlags2 src, VkAccessFlags2 sa, VkPipelineStageFlags2 dst,
                           VkAccessFlags2 da) {
    VkMemoryBarrier2 mb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2, .srcStageMask = src, .srcAccessMask = sa,
                           .dstStageMask = dst, .dstAccessMask = da};
    VkDependencyInfo di = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .memoryBarrierCount = 1, .pMemoryBarriers = &mb};
    vkCmdPipelineBarrier2(cb, &di);
}

// The stand-in of level level of a 3D BC image for draws through view format fmt, made on first use; 0 when none can
// be made (logged). Replay thread.
static int bc_standin(const VkTexImage *img, uint32_t level, VkFormat fmt, BcStandIn *out) {
    static int warned;
    plat_mutex_lock(&bcs_lock);
    int full = nbcs == BC_STANDINS;
    for (int i = 0; i < nbcs; i++)
        if (bcs[i].bc == img->image && bcs[i].level == level && bcs[i].fmt == fmt) {
            *out = bcs[i];
            plat_mutex_unlock(&bcs_lock);
            return 1;
        }
    plat_mutex_unlock(&bcs_lock);
    if (full) {
        if (!warned++) rt_log("vk: more than %d 3D BC render targets: the others are left out", BC_STANDINS);
        return 0;
    }
    uint32_t lw = level_extent(img->width, level), lh = level_extent(img->height, level);
    BcStandIn s = {img->image, VK_NULL_HANDLE, NULL, fmt, level, (lw + 3) / 4, (lh + 3) / 4,
                   level_extent(img->depth, level)};
    VkImageCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D, .format = fmt,
                            .extent = {s.w, s.h, 1}, .mipLevels = 1, .arrayLayers = s.d, .samples = VK_SAMPLE_COUNT_1_BIT,
                            .tiling = VK_IMAGE_TILING_OPTIMAL,
                            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                     VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
    VmaAllocationCreateInfo ai = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
    if (vmaCreateImage(vk_ctx()->vma, &ci, &ai, &s.img, &s.alloc, NULL) != VK_SUCCESS) {
        if (!warned++) rt_log("vk: stand-in for a 3D BC render target could not be created: left out");
        return 0;
    }
    // In the upload batch, which goes to the queue before this part: GENERAL, then the texture's blocks.
    VkCommandBuffer cb = vk_upload_begin();
    image_barrier(cb, s.img, VK_IMAGE_LAYOUT_UNDEFINED, s.d);
    memory_barrier(cb, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
                   VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkImageCopy rg = {.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1},
                      .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, s.d},
                      .extent = {lw, lh, s.d}};   // texels of the compressed source
    vkCmdCopyImage(cb, img->image, VK_IMAGE_LAYOUT_GENERAL, s.img, VK_IMAGE_LAYOUT_GENERAL, 1, &rg);
    memory_barrier(cb, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                   VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    vk_upload_end();
    plat_mutex_lock(&bcs_lock);
    bcs[nbcs++] = s;
    plat_mutex_unlock(&bcs_lock);
    rt_log("vk: 3D BC render target %ux%ux%u level %u drawn through a %ux%u x %u stand-in (format %d)", img->width,
           img->height, img->depth, level, s.w, s.h, s.d, (int)fmt);
    *out = s;
    return 1;
}

// vk_exec_draw.c vk_exec_image_gone: the stand-in of a BC image that goes, freed once the GPU is done with it.
void vk_exec_bc_gone(VkImage image) {
    plat_mutex_lock(&bcs_lock);
    for (int i = 0; i < nbcs; i++)
        if (bcs[i].bc == image) {
            vk_defer_image(bcs[i].img, bcs[i].alloc);
            bcs[i--] = bcs[--nbcs];
        }
    plat_mutex_unlock(&bcs_lock);
}

// At a pass end: the planes the pass drew into a stand-in go to its texture.
static void bc_copy_back(VkExec *ex, const VkExecTarget *t) {
    plat_mutex_lock(&bcs_lock);
    for (int i = 0; i < nbcs; i++) {
        if (bcs[i].img != t->image) continue;
        memory_barrier(ex->cb, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
                       VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkImageCopy rg = {.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, t->base_layer, t->layers},
                          .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, bcs[i].level, 0, 1},
                          .dstOffset = {0, 0, (int32_t)t->base_layer},
                          .extent = {bcs[i].w, bcs[i].h, t->layers}};   // texels of the uncompressed source
        vkCmdCopyImage(ex->cb, t->image, VK_IMAGE_LAYOUT_GENERAL, bcs[i].bc, VK_IMAGE_LAYOUT_GENERAL, 1, &rg);
        memory_barrier(ex->cb, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                       VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
        ex->recorded++;
        break;
    }
    plat_mutex_unlock(&bcs_lock);
}

// A draw into a 3D BC image without block views through an uncompressed view of its blocks: the target is the
// image's stand-in (bc_standin). 1 when o is filled (target or left out), 0 for any other target.
static int bc_target(const tex_t *t, const view_t *v, int has_v, const VkTexImage *img, VkExecTarget *o) {
    int bw, bh;
    if (img->type != VK_IMAGE_TYPE_3D || img->block_views || !nvn_fmt_bpb(t->d.format, &bw, &bh) || bw != 4 || bh != 4)
        return 0;
    VkFormat fmt;
    BcStandIn s;
    uint32_t level = has_v && v->set_levels ? v->base_level : 0;
    if (level >= img->levels) {
        warn_once(W_LEVEL, "render target level %u of a %u-level texture: left out", level, img->levels);
        return 1;
    }
    if (!has_v || !v->set_format || !view_format(t, v, 1, &fmt) || !bc_standin(img, level, fmt, &s)) {
        warn_once(W_BLOCK_VIEW, "render target format %#x (%ux%u) through view format %#x: no block view, left out",
                  t->d.format, t->d.width, t->d.height, has_v && v->set_format ? v->format : 0);
        return 1;
    }
    uint32_t base = 0, layers = s.d;
    if (v->set_layers) {
        base = v->min_layer;
        layers = v->nlayers ? v->nlayers : (base < s.d ? s.d - base : 0);
    }
    if (base >= s.d || !layers) return 1;
    if (base + layers > s.d) layers = s.d - base;
    o->image = s.img;   // not img->image: view_of makes this command buffer's view of the stand-in
    o->img = img;
    o->format = fmt;
    o->view_type = layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
    o->aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    o->level = 0, o->base_layer = base, o->layers = layers;
    o->width = s.w, o->height = s.h, o->samples = 1;
    o->nvn_format = v->format;
    return 1;
}

static void resolve_target(const tex_t *t, const view_t *v, int has_v, int depth_slot, VkExecTarget *o) {
    memset(o, 0, sizeof *o);
    o->tex = t;
    if (!t) return;
    const VkTexImage *img = vk_texture_get(t);
    if (img && !depth_slot && bc_target(t, v, has_v, img, o)) return;   // a 3D BC image: its stand-in
    VkImageUsageFlags need = depth_slot ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    int is3d = img && img->type == VK_IMAGE_TYPE_3D && !depth_slot;   // slices drawn through 2D views (2D_ARRAY_COMPATIBLE)
    if (!img || !(img->usage & need) || (img->type != VK_IMAGE_TYPE_2D && !is3d)) {
        warn_once(W_UNRENDERABLE, "render target format %#x target %d (%ux%u) is not renderable here: left out",
                  t->d.format, t->d.target, t->d.width, t->d.height);
        return;
    }
    uint32_t level = has_v && v->set_levels ? v->base_level : 0;
    if (level >= img->levels) {
        warn_once(W_LEVEL, "render target level %u of a %u-level texture: left out", level, img->levels);
        return;
    }
    // A 3D level's depth planes take the place of layers (Vulkan views them as the array layers of a 2D array view).
    uint32_t nlay = is3d ? level_extent(img->depth, level) : img->layers;
    uint32_t base = 0, layers = nlay;
    if (has_v && v->set_layers) {
        base = v->min_layer;
        layers = v->nlayers ? v->nlayers : (base < nlay ? nlay - base : 0);
    }
    if (base >= nlay || !layers) {
        warn_once(W_LAYER, "render target layer %u of a %u-layer texture: left out", base, nlay);
        return;
    }
    if (base + layers > nlay) layers = nlay - base;
    VkFormat fmt = img->format;
    int32_t nvn_fmt = t->d.format;
    int blocks = !depth_slot && img->block_views;
    if (blocks) {   // a BC texture takes draws only through an uncompressed view of its blocks
        if (!has_v || !v->set_format || !view_format(t, v, 1, &fmt)) {
            warn_once(W_BLOCK_VIEW, "render target format %#x (%ux%u) through view format %#x: no block view, left out",
                      t->d.format, t->d.width, t->d.height, has_v && v->set_format ? v->format : 0);
            return;
        }
        nvn_fmt = v->format;
        if (vk_dump_armed) vk_dump_block_target(t->d.width, t->d.height);
        if (layers > 1) {   // an uncompressed view of a BC image has one layer (no maintenance6 here)
            warn_once(W_BLOCK_LAYERS, "render target format %#x: block view of %u layers, first layer only", t->d.format,
                      layers);
            layers = 1;
        }
    } else if (!depth_slot && has_v && v->set_format && v->format != t->d.format) {
        if (view_format(t, v, 0, &fmt)) nvn_fmt = v->format;
        else warn_once(W_VIEW_FORMAT, "render target view format %#x of a format %#x texture: texture format used",
                       v->format, t->d.format);
    }
    o->image = img->image;
    o->img = img;
    o->format = fmt;
    o->view_type = layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
    o->aspect = depth_slot ? img->aspect & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
                           : VK_IMAGE_ASPECT_COLOR_BIT;
    o->level = level, o->base_layer = base, o->layers = layers;
    o->is3d = (uint8_t)is3d;
    o->blocks = (uint8_t)blocks;
    o->width = level_extent(img->width, level), o->height = level_extent(img->height, level);
    if (blocks) o->width = (o->width + 3) / 4, o->height = (o->height + 3) / 4;   // the view's extent: 4x4 blocks
    o->samples = img->samples ? img->samples : 1;
    o->nvn_format = nvn_fmt;
    if (!o->aspect) o->image = VK_NULL_HANDLE;
}

/* The size of a pass without attachments: the far corner of viewport 0, else of the scissor, 1..16384, as
   MacNativeGames and nxnative size it. TotK draws a fragment shader that only writes a storage buffer into a 1x1
   viewport; a pass sized by the viewport's width alone puts an offset viewport outside the scissor, and the cave rock
   loses its ambient light. */
void vk_exec_noatt_dims(const VkExec *ex, uint32_t *w, uint32_t *h) {
    double x1 = 0, y1 = 0;
    if (ex->has_vp) {
        x1 = ceil(fmax(ex->vp[0][0], ex->vp[0][0] + ex->vp[0][2]));
        y1 = ceil(fmax(ex->vp[0][1], ex->vp[0][1] + ex->vp[0][3]));
    }
    if ((x1 < 1 || y1 < 1) && ex->has_scissor) x1 = (double)ex->scissor.x + ex->scissor.w, y1 = (double)ex->scissor.y + ex->scissor.h;
    *w = x1 < 1 ? 1 : x1 > 16384 ? 16384 : (uint32_t)x1;
    *h = y1 < 1 ? 1 : y1 > 16384 ? 16384 : (uint32_t)y1;
}

// Resolves ex->rt to this command buffer's images and derives the pass parameters and the pipeline's target formats.
static void resolve_rt(VkExec *ex) {
    if (ex->rt_resolved) return;
    ex->rt_resolved = 1;
    const p_set_rt *r = &ex->rt;
    int n = r->n < 0 ? 0 : r->n > 8 ? 8 : r->n;
    uint32_t w = UINT32_MAX, h = UINT32_MAX, layers = UINT32_MAX, samples = 0;
    for (int i = 0; i < 8; i++) {
        VkExecTarget *t = &ex->color[i];
        resolve_target(i < n ? r->color[i] : NULL, &r->cview[i], i < n && r->has_cview[i], 0, t);
        ex->pass_color[i] = t->image ? t->format : VK_FORMAT_UNDEFINED;
        ex->pipe.color_fmt[i] = t->image ? t->nvn_format : 0;
        if (!t->image) continue;
        if (t->width < w) w = t->width;
        if (t->height < h) h = t->height;
        if (t->layers < layers) layers = t->layers;
        if (!samples) samples = t->samples;
    }
    VkExecTarget *d = &ex->depth;
    resolve_target(r->depth, &r->dview, r->has_dview, 1, d);
    ex->pass_depth = d->image && (d->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) ? d->format : VK_FORMAT_UNDEFINED;
    ex->pass_stencil = d->image && (d->aspect & VK_IMAGE_ASPECT_STENCIL_BIT) ? d->format : VK_FORMAT_UNDEFINED;
    ex->pipe.depth_fmt = d->image ? d->nvn_format : 0;
    if (d->image) {
        if (d->width < w) w = d->width;
        if (d->height < h) h = d->height;
        if (d->layers < layers) layers = d->layers;
        if (!samples) samples = d->samples;
    }
    ex->pass_noatt = w == UINT32_MAX;
    if (ex->pass_noatt) {
        vk_exec_noatt_dims(ex, &w, &h);
        layers = 1;
        static int logged;
        if (logged < 8 && ex->has_vp && (ex->vp[0][0] != 0 || ex->vp[0][1] != 0 || ex->vp[0][2] != w || ex->vp[0][3] != h))
            logged++, rt_log("vk_exec: pass without attachments %ux%u for viewport %g,%g %gx%g", w, h, ex->vp[0][0],
                             ex->vp[0][1], ex->vp[0][2], ex->vp[0][3]);
    }
    ex->pass_w = w, ex->pass_h = h, ex->pass_layers = layers;
    ex->pass_ncolor = (uint32_t)n;
    ex->pass_samples = samples ? samples : 1;
    ex->pipe.samples = (uint8_t)ex->pass_samples;
}

// The attachment view of a target. A registered texture's view lives as long as its image (vk_exec_target_view); any
// other image's view is made for this command buffer and freed at its fence.
static VkImageView view_of(VkExec *ex, const VkExecTarget *t) {
    if (t->img && t->img->image == t->image)
        return vk_exec_target_view(t->img, t->format, t->view_type, t->aspect, t->level, t->base_layer, t->layers, t->blocks);
    ViewEnt *v = ex->views;
    for (int i = 0; i < ex->nviews; i++)
        if (v[i].image == t->image && v[i].format == t->format && v[i].type == t->view_type && v[i].aspect == t->aspect &&
            v[i].level == t->level && v[i].base == t->base_layer && v[i].layers == t->layers)
            return v[i].view;
    VkImageViewCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = t->image,
                                .viewType = t->view_type, .format = t->format,
                                .subresourceRange = {t->aspect, t->level, 1, t->base_layer, t->layers}};
    VkImageViewUsageCreateInfo use = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO,
                                      .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT};
    if (t->blocks) ci.pNext = &use;   // not the BC image's sampling and transfer usages
    VkImageView view;
    if (vkCreateImageView(vk_ctx()->device, &ci, NULL, &view) != VK_SUCCESS)
        rt_fatal("vk: render target view (format %d, %ux%u) could not be created", (int)t->format, t->width, t->height);
    if (ex->nviews == ex->cap_views) {
        ex->cap_views = ex->cap_views ? ex->cap_views * 2 : 16;
        ex->views = realloc(ex->views, sizeof(ViewEnt) * (size_t)ex->cap_views);
        if (!ex->views) rt_fatal("vk: out of memory");
        v = ex->views;
    }
    v[ex->nviews++] = (ViewEnt){t->image, t->format, t->view_type, t->aspect, t->level, t->base_layer, t->layers, view};
    return view;
}

typedef struct { int n; VkImageView v[]; } ViewList;

static void free_views(void *arg) {   // completion thread, once the submission that used them is done
    ViewList *l = arg;
    VkDevice dev = vk_ctx()->device;
    for (int i = 0; i < l->n; i++) vkDestroyImageView(dev, l->v[i], NULL);
    free(l);
}

// ---- target history
// The presented frame (gfx_counters.frames + 1; 0: never) each target subresource was last rendered into, and the one
// before. prepare_draw lets a draw into a target the previous frame did not render (a one-shot draw: a volume or LUT
// generated once) wait for its shaders and pipeline instead of skipping it. Per subresource, not per image: a volume
// may be generated a few slices per frame. Direct-mapped: a slot another subresource took reads as not rendered, so a
// collision makes a draw wait rather than skip.
typedef struct { VkImage image; uint32_t level, layer; uint64_t last, prev; } TargetFrames;
static TargetFrames tfr[1024];

static TargetFrames *tfr_slot(const VkExecTarget *t) {
    uint64_t h = ((uint64_t)(uintptr_t)t->image ^ (uint64_t)t->level << 56 ^ (uint64_t)t->base_layer << 40) *
                 0x9E3779B97F4A7C15ull;
    return &tfr[h >> 54];
}

static int tfr_match(const TargetFrames *e, const VkExecTarget *t) {
    return e->image == t->image && e->level == t->level && e->layer == t->base_layer;
}

static void tfr_note(const VkExecTarget *t, uint64_t stamp) {
    TargetFrames *e = tfr_slot(t);
    if (!tfr_match(e, t)) *e = (TargetFrames){t->image, t->level, t->base_layer, 0, 0};
    if (e->last != stamp) e->prev = e->last, e->last = stamp;
}

static int tfr_recent(const VkExecTarget *t, uint64_t stamp) {
    const TargetFrames *e = tfr_slot(t);
    return tfr_match(e, t) && (e->last == stamp ? e->prev : e->last) == stamp - 1;
}

int vk_exec_targets_recent(VkExec *ex, int any) {
    resolve_rt(ex);
    uint64_t stamp = __atomic_load_n(&gfx_counters.frames, __ATOMIC_RELAXED) + 1;
    int n = 0, recent = 0;
    for (int i = 0; i < 9; i++) {
        const VkExecTarget *t = i < 8 ? &ex->color[i] : &ex->depth;
        if (!t->image) continue;
        n++;
        if (tfr_recent(t, stamp)) recent++;
        else if (!any) return 0;
    }
    return any ? recent > 0 : n > 0;
}

// One line naming each target of the current pass: format, extent, layer and the two frames it was last rendered in
// (prepare_draw's pipeline wait log).
void vk_exec_targets_describe(VkExec *ex, char *buf, size_t cap) {
    resolve_rt(ex);
    size_t n = 0;
    buf[0] = 0;
    for (int i = 0; i < 9 && n < cap; i++) {
        const VkExecTarget *t = i < 8 ? &ex->color[i] : &ex->depth;
        if (!t->image) continue;
        const TargetFrames *e = tfr_slot(t);
        int m = tfr_match(e, t);
        int w = snprintf(buf + n, cap - n, "%s%c%d fmt %d %ux%u L%u seen %llu/%llu", n ? "; " : "", i < 8 ? 'c' : 'd',
                         i < 8 ? i : 0, (int)t->format, t->width, t->height, t->base_layer,
                         (unsigned long long)(m ? e->last : 0), (unsigned long long)(m ? e->prev : 0));
        if (w < 0) break;
        n += (size_t)w;
    }
    if (!n) snprintf(buf, cap, "no targets");
}

// ---- passes and barriers

static int has_image(const VkImage *list, int n, VkImage img) {
    for (int i = 0; i < n; i++)
        if (list[i] == img) return 1;
    return 0;
}

static VkImageMemoryBarrier2 layout_barrier(VkImage img, VkImageAspectFlags aspect, VkImageLayout from, VkImageLayout to) {
    return (VkImageMemoryBarrier2){.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                                   .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
                                   .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                   .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                                   .oldLayout = from, .newLayout = to, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                   .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = img,
                                   .subresourceRange = {aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS}};
}

// ---- barrier tracking (VkExec.hz_*)
int vk_exec_track_on(void) {
    static int on = -1;
    if (on < 0) {
        on = rt_knob("BARRIER_TRACK", "1");
        rt_log("vk_exec: barriers %s", on ? "tracked: passes and clears that touch nothing written or sampled since the "
                                            "last full barrier start without one (TOTK_BARRIER_TRACK=0: always full)"
                                          : "full before every pass, clear, copy and dispatch (TOTK_BARRIER_TRACK=0)");
    }
    return on;
}

static int hz_has(const VkImage *l, int n, VkImage img) {
    for (int i = 0; i < n; i++)
        if (l[i] == img) return 1;
    return 0;
}

static void hz_add_w(VkExec *ex, VkImage img) {
    if (!img || hz_has(ex->hz_w, ex->nhz_w, img)) return;
    if (ex->nhz_w == (int)(sizeof ex->hz_w / sizeof *ex->hz_w)) { VK_HZ_SET(ex, VK_HZ_OVERFLOW); return; }
    ex->hz_w[ex->nhz_w++] = img;
}

static void hz_reset(VkExec *ex) {
    ex->nhz_w = ex->nhz_r = ex->hz_other = ex->hz_rall = ex->pass_deferred = 0;
    ex->hz_gen++;
}

// Attachment-only barrier: earlier attachment writes and clears are done and visible to attachment access and to
// transfer clears that follow. Fragment work waits; vertex and compute work of the next pass may start at once.
static void attach_barrier(VkExec *ex) {
    VkMemoryBarrier2 mb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                           .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT |
                                           VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                           VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                           .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                                            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT,
                           .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT |
                                           VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                           VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                           .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                                            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT |
                                            VK_ACCESS_2_TRANSFER_WRITE_BIT};
    VkDependencyInfo di = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .memoryBarrierCount = 1, .pMemoryBarriers = &mb};
    vkCmdPipelineBarrier2(ex->cb, &di);
    ex->recorded++;
    vk_exec_frame.light_barriers++;
}

// Full barrier. The images in the feedback layout become want[] (pass begin) or none (every other barrier): the others
// go back to GENERAL and the new ones leave it, in the same command.
static void barrier_to(VkExec *ex, const VkImage *want, const VkImageAspectFlags *wasp, int nwant) {
    vk_exec_pass_end(ex);
    VkImageMemoryBarrier2 ib[18];
    uint32_t n = 0;
    for (int i = 0; i < ex->nfbl; i++)
        if (!has_image(want, nwant, ex->fbl_img[i]))
            ib[n++] = layout_barrier(ex->fbl_img[i], ex->fbl_aspect[i], VK_IMAGE_LAYOUT_ATTACHMENT_FEEDBACK_LOOP_OPTIMAL_EXT,
                                     VK_IMAGE_LAYOUT_GENERAL);
    for (int i = 0; i < nwant; i++)
        if (!has_image(ex->fbl_img, ex->nfbl, want[i]))
            ib[n++] = layout_barrier(want[i], wasp[i], VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_ATTACHMENT_FEEDBACK_LOOP_OPTIMAL_EXT);
    for (int i = 0; i < nwant; i++) ex->fbl_img[i] = want[i], ex->fbl_aspect[i] = wasp[i];
    ex->nfbl = nwant;
    if (ex->barrier_at == ex->recorded && !n) { hz_reset(ex); return; }   // nothing recorded since the last one
    vk_exec_frame.fb_layouts += n;
    VkMemoryBarrier2 mb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                           .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                           .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
                           .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                           .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT};
    VkDependencyInfo di = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .memoryBarrierCount = 1, .pMemoryBarriers = &mb,
                           .imageMemoryBarrierCount = n, .pImageMemoryBarriers = ib};
    vkCmdPipelineBarrier2(ex->cb, &di);
    vk_exec_frame.barriers++;
    ex->barrier_at = ++ex->recorded;
    hz_reset(ex);
}

// The full barrier other ops (copies, dispatches, fills, queries, inline commands) take before they record: what they
// write is not tracked, so the next pass or clear takes a full barrier too.
void vk_exec_barrier(VkExec *ex) {
    barrier_to(ex, NULL, NULL, 0);
    VK_HZ_SET(ex, VK_HZ_OP);
}

// The game's own barrier (nvnCommandBufferBarrier). Default: a full barrier. With TOTK_GAME_BARRIER=track it records
// nothing while only tracked writes happened since the last full barrier: the hazards it orders (attachments sampled
// later, attachment after attachment, clears) are what the tracking sees at the next pass, clear and draw. After
// untracked writes (storage writes of draws, transform feedback, copies, dispatches) it is the full barrier as before.
// Measured in Violet's forest (docs/progress.md 2026-11-02): the passes after them then sample what came before and
// restart behind a full barrier (33 restarts a frame, more passes), slower than the full barrier at once.
static void game_barrier(VkExec *ex) {
    static int track = -1;
    if (track < 0) {
        const char *e = cfg_get("TOTK_GAME_BARRIER");
        track = e && !strcmp(e, "track");
        if (vk_exec_track_on())
            rt_log("vk_exec: game barriers %s", track ? "left to the barrier tracking (TOTK_GAME_BARRIER=track)"
                                                      : "full (TOTK_GAME_BARRIER=track leaves them to the tracking)");
    }
    if (track && vk_exec_track_on() && !ex->hz_other) {   // untracked writes since the last full barrier: as before
        vk_exec_frame.game_barriers_tracked++;
        return;
    }
    vk_exec_barrier(ex);
    ex->hz_other = VK_HZ_GAME;
}

// Why a pass or clear needs the full barrier (VK_HZ_WHY_*), -1: it does not; counted per frame.
static int full_why(const VkExec *ex, int fbl) {
    int w = !vk_exec_track_on() ? VK_HZ_WHY_OFF : ex->hz_other ? VK_HZ_WHY_OTHER : ex->hz_rall ? VK_HZ_WHY_UNKNOWN
          : fbl ? VK_HZ_WHY_FBL : ex->barrier_at < 0 ? VK_HZ_WHY_FIRST : -1;
    if (w >= 0) {
        vk_exec_frame.full_why[w]++;
        if (w == VK_HZ_WHY_OTHER && ex->hz_other < VK_HZ_N) vk_exec_frame.other_src[ex->hz_other]++;
    }
    return w;
}

// Feedback-layout transitions a pass begin with want[] needs (barrier_to records them).
static int fbl_changes(const VkExec *ex, const VkImage *want, int nwant) {
    int n = 0;
    for (int i = 0; i < ex->nfbl; i++) n += !has_image(want, nwant, ex->fbl_img[i]);
    for (int i = 0; i < nwant; i++) n += !has_image(ex->fbl_img, ex->nfbl, want[i]);
    return n;
}

// Before a pass begins: the full barrier unless tracking shows the pass touches nothing earlier work wrote or sampled
// (an attachment-only barrier when it renders into an image written since: attachment after attachment).
static void pass_barrier(VkExec *ex, const VkImage *want, const VkImageAspectFlags *wasp, int nwant) {
    int full = full_why(ex, fbl_changes(ex, want, nwant)) >= 0;
    int waw = 0;
    for (uint32_t i = 0; i < 9 && !full; i++) {
        const VkExecTarget *t = i < 8 ? &ex->color[i] : &ex->depth;
        if ((i < 8 && i >= ex->pass_ncolor) || !t->image) continue;
        if (hz_has(ex->hz_r, ex->nhz_r, t->image))   // sampled since: its reads finish before the writes
            full = 1, vk_exec_frame.full_why[VK_HZ_WHY_SAMPLED]++;
        else if (hz_has(ex->hz_w, ex->nhz_w, t->image)) waw = 1;
    }
    if (full) {
        barrier_to(ex, want, wasp, nwant);
        return;
    }
    if (waw) attach_barrier(ex);
    vk_exec_frame.deferred++;
    ex->pass_deferred = ex->nhz_w > 0;   // draws check their sampled images against hz_w (vk_exec_hazard_draw)
}

// Before a whole-image clear outside a pass: as pass_barrier, for the cleared image.
static void clear_barrier(VkExec *ex, VkImage img) {
    int full = full_why(ex, ex->nfbl) >= 0;
    if (!full && hz_has(ex->hz_r, ex->nhz_r, img)) full = 1, vk_exec_frame.full_why[VK_HZ_WHY_SAMPLED]++;
    if (full)
        barrier_to(ex, NULL, NULL, 0);
    else {
        if (hz_has(ex->hz_w, ex->nhz_w, img)) attach_barrier(ex);
        vk_exec_frame.deferred++;
    }
    hz_add_w(ex, img);
}

// vk_exec_draw.c: images a draw samples, against the writes since the last full barrier (vk_exec_hazard_draw).
int vk_exec_hz_written(const VkExec *ex, VkImage img) { return hz_has(ex->hz_w, ex->nhz_w, img); }

void vk_exec_hz_read(VkExec *ex, VkImage img) {
    if (!img || hz_has(ex->hz_r, ex->nhz_r, img)) return;
    if (ex->nhz_r == (int)(sizeof ex->hz_r / sizeof *ex->hz_r)) { ex->hz_rall = 1; return; }
    ex->hz_r[ex->nhz_r++] = img;
}

void vk_exec_pass_begin(VkExec *ex) {
    if (ex->in_pass) return;
    resolve_rt(ex);
    // Learned targets (VkTexImage.fb_learned) are attached in the feedback layout
    VkImage want[9];
    VkImageAspectFlags wasp[9];
    int nwant = 0;
    uint32_t fbl = 0;
    for (uint32_t i = 0; i < 9; i++) {
        const VkExecTarget *t = i < 8 ? &ex->color[i] : &ex->depth;
        if ((i < 8 && i >= ex->pass_ncolor) || !t->image || !t->img || !t->img->fb_learned) continue;
        fbl |= 1u << i;
        if (has_image(want, nwant, t->image)) continue;
        want[nwant] = t->image, wasp[nwant++] = i < 8 ? VK_IMAGE_ASPECT_COLOR_BIT : t->img->aspect;
    }
    pt_stamp(ex, PT_OTHER);
    // KarPur TOTK_HOT_PASS: hot chunks this pass bound last time, copied into the mirror behind their own barriers
    ex->recorded += vk_mem_hot_pass_begin(ex->cb, (uint64_t)(uintptr_t)ex->color[0].image ^
                                                      (uint64_t)(uintptr_t)ex->depth.image << 7 ^ (uint64_t)ex->pass_w << 20 ^ ex->pass_h);
    pass_barrier(ex, want, wasp, nwant);   // passes, clears, copies and dispatches see each other's results
    ex->fbl_mask = fbl;
    ex->fb_dirty = 0;
    ex->pass_serial++;
    VkRenderingAttachmentInfo ca[8], da = {0}, sa = {0};
    for (uint32_t i = 0; i < ex->pass_ncolor; i++) {
        const VkExecTarget *t = &ex->color[i];
        ca[i] = (VkRenderingAttachmentInfo){.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        if (!t->image) continue;
        ca[i].imageView = view_of(ex, t);
        ca[i].imageLayout = fbl >> i & 1 ? VK_IMAGE_LAYOUT_ATTACHMENT_FEEDBACK_LOOP_OPTIMAL_EXT : VK_IMAGE_LAYOUT_GENERAL;
        ca[i].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        ca[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;    }
    const VkExecTarget *d = &ex->depth;
    if (d->image) {
        VkRenderingAttachmentInfo a = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, .imageView = view_of(ex, d),
                                       .imageLayout = fbl >> 8 & 1 ? VK_IMAGE_LAYOUT_ATTACHMENT_FEEDBACK_LOOP_OPTIMAL_EXT
                                                                   : VK_IMAGE_LAYOUT_GENERAL,
                                       .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD, .storeOp = VK_ATTACHMENT_STORE_OP_STORE};
        if (d->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) da = a;
        if (d->aspect & VK_IMAGE_ASPECT_STENCIL_BIT) sa = a;
    }
    VkRenderingInfo ri = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO, .renderArea = {{0, 0}, {ex->pass_w, ex->pass_h}},
                          .layerCount = ex->pass_layers, .colorAttachmentCount = ex->pass_ncolor,
                          .pColorAttachments = ca,
                          .pDepthAttachment = ex->pass_depth ? &da : NULL,
                          .pStencilAttachment = ex->pass_stencil ? &sa : NULL};
    vkCmdBeginRendering(ex->cb, &ri);
    pt_stamp(ex, PT_BARRIER);
    vk_exec_frame.passes++;
    size_begin(ex);
    ex->recorded++;
    ex->in_pass = 1;
}

void vk_exec_pass_end(VkExec *ex) {
    if (!ex->in_pass) return;
    vk_exec_oq_pass_end(ex);
    vkCmdEndRendering(ex->cb);
    vk_mem_hot_pass_end();
    if (pt_state > 0) {   // after the end of the rendering: the pass's fragments and its store count for the pass
        pt_strict_barrier(ex);
        int label = pt_pass_label(ex);
        uint32_t draws = (uint32_t)(vk_exec_frame.draws - psz_draws0);
        pt_prev_pass = (uint32_t)label << 24 | (draws & 0xffffff);
        pt_prev_fs = vk_exec_last_fs;
        pt_aux_next = draws;
        pt_stamp(ex, label);
    }
    size_end();
    ex->recorded++;
    ex->in_pass = 0;
    ex->pass_deferred = 0;
    for (uint32_t i = 0; i < 9; i++) {   // barrier tracking: the pass may have written each attachment
        const VkExecTarget *t = i < 8 ? &ex->color[i] : &ex->depth;
        if ((i < 8 && i >= ex->pass_ncolor) || !t->image) continue;
        hz_add_w(ex, t->image);
    }
    int after_end = ex->recorded;
    for (int i = 0; i < ex->pass_ncolor && i < 8; i++)   // stand-ins of 3D BC targets go to their textures
        if (ex->color[i].image && ex->color[i].img && ex->color[i].img->image != ex->color[i].image)
            bc_copy_back(ex, &ex->color[i]);
    if (vk_dump_armed) vk_dump_pass_end(ex);
    if (ex->recorded != after_end) VK_HZ_SET(ex, VK_HZ_AFTER_PASS);   // copies of stand-ins after the pass: untracked
    uint64_t stamp = __atomic_load_n(&gfx_counters.frames, __ATOMIC_RELAXED) + 1;
    for (int i = 0; i < 8; i++)
        if (ex->color[i].image)
            vk_texture_gpu_written(ex->color[i].tex), tfr_note(&ex->color[i], stamp),
                vk_texture_write_note("render", ex->color[i].tex, 0, 0, NULL);
    if (ex->depth.image) vk_texture_gpu_written(ex->depth.tex), tfr_note(&ex->depth, stamp);
}

VkCommandBuffer vk_exec_inline_begin(int *reopen) {
    VkExec *ex = &X;
    *reopen = 0;
    if (!x_here || !ex->cb) return VK_NULL_HANDLE;
    *reopen = ex->in_pass;
    vk_exec_barrier(ex);   // ends the pass; feedback-layout targets go back to GENERAL
    return ex->cb;
}

void vk_exec_inline_end(int reopen) {
    X.recorded++;
    if (reopen) vk_exec_pass_begin(&X);
}

typedef struct { VkBuffer b; VmaAllocation a; } InlineBuf;
static void free_inline_buf(void *arg) {   // completion thread
    InlineBuf *t = arg;
    vmaDestroyBuffer(vk_ctx()->vma, t->b, t->a);
    free(t);
}

VkBuffer vk_exec_inline_buffer(VkDeviceSize size) {
    InlineBuf *t = calloc(1, sizeof *t);
    VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size,
                             .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                             .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    VmaAllocationCreateInfo ai = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
    if (!t || vmaCreateBuffer(vk_ctx()->vma, &bi, &ai, &t->b, &t->a, NULL) != VK_SUCCESS) { free(t); return VK_NULL_HANDLE; }
    vk_completion_add(X.fence, free_inline_buf, t);
    return t->b;
}

VkRect2D vk_exec_scissor(const VkExec *ex) {
    int64_t w = ex->pass_w, h = ex->pass_h;
    int64_t left = 0, top = 0, right = w, bottom = h;
    if (ex->ui_fit) {
        if (w * 9 > h * 16) {
            int64_t fit = h * 16 / 9;
            left = (w - fit) / 2;
            right = left + fit;
        } else if (w * 9 < h * 16) {
            int64_t fit = w * 9 / 16;
            top = (h - fit) / 2;
            bottom = top + fit;
        }
    }
    if (!ex->has_scissor) return (VkRect2D){{(int32_t)left, (int32_t)top},
                                            {(uint32_t)(right - left), (uint32_t)(bottom - top)}};
    int64_t x0 = ex->scissor.x, y0 = ex->scissor.y;
    int64_t x1 = x0 + ex->scissor.w, y1 = y0 + ex->scissor.h;
    if (ex->ui_fit) {
        double sx = (double)(right - left) / (double)w;
        double sy = (double)(bottom - top) / (double)h;
        x0 = left + (int64_t)(x0 * sx);
        x1 = left + (int64_t)ceil(x1 * sx);
        y0 = top + (int64_t)(y0 * sy);
        y1 = top + (int64_t)ceil(y1 * sy);
    }
    if (x0 < left) x0 = left;
    if (y0 < top) y0 = top;
    if (x1 > right) x1 = right;
    if (y1 > bottom) y1 = bottom;
    if (x1 <= x0 || y1 <= y0) return (VkRect2D){{0, 0}, {0, 0}};
    return (VkRect2D){{(int32_t)x0, (int32_t)y0}, {(uint32_t)(x1 - x0), (uint32_t)(y1 - y0)}};
}

// ---- clears

// Outside a pass, a clear of a whole target (no narrower scissor, the target as large as the render area) clears the
// image directly instead of opening a pass for it.
static int whole_target(const VkExec *ex, const VkExecTarget *t) {
    // vkCmdClear*Image addresses a 3D level whole, not one depth plane: slices of a 3D target clear in a pass; it
    // cannot clear a compressed image: views of BC blocks clear in a pass too
    if (ex->in_pass || t->is3d || t->blocks || t->width != ex->pass_w || t->height != ex->pass_h) return 0;
    if (!ex->has_scissor) return 1;
    const p_scissor *s = &ex->scissor;
    return s->x <= 0 && s->y <= 0 && (int64_t)s->x + s->w >= t->width && (int64_t)s->y + s->h >= t->height;
}

static void clear_attachment(VkExec *ex, const VkClearAttachment *a) {
    if (ex->ui_fit) ex->ui_fit = 0, ex->dirty |= VK_EXEC_DIRTY_VIEWPORT | VK_EXEC_DIRTY_SCISSOR;
    vk_exec_pass_begin(ex);
    VkClearRect cr = {vk_exec_scissor(ex), 0, ex->pass_layers};
    if (!cr.rect.extent.width || !cr.rect.extent.height) return;
    vkCmdClearAttachments(ex->cb, 1, a, 1, &cr);
    ex->recorded++;
    ex->fb_dirty |= a->aspectMask & VK_IMAGE_ASPECT_COLOR_BIT ? 1u << a->colorAttachment : 0x100u;
}

static void clear_color(VkExec *ex, const p_clear_color *c) {
    vk_exec_frame.clears++;
    if (ex->ui_fit) ex->ui_fit = 0, ex->dirty |= VK_EXEC_DIRTY_VIEWPORT | VK_EXEC_DIRTY_SCISSOR;
    if (ex->render_disabled) return;
    resolve_rt(ex);
    int i = c->index;
    if (i < 0 || i >= (int)ex->pass_ncolor || !ex->color[i].image) {
        warn_once(W_CLEAR_TARGET, "colour clear of target %d, which is not bound or not renderable: skipped", i);
        return;
    }
    uint32_t mask = (uint32_t)c->mask & 0xf;
    if (!mask) return;
    if (mask != 0xf) {   // vkCmdClearAttachments ignores write masks; a masked clear needs a draw (vk_exec_draw.c)
        warn_once(W_CLEAR_MASK, "colour clear with channel mask %#x is not replayed: skipped", mask);
        return;
    }
    const VkExecTarget *t = &ex->color[i];
    VkClearColorValue cv;
    memcpy(cv.float32, c->rgba, sizeof cv.float32);   // integer targets: the payload's bits as the front end stored them
    if (whole_target(ex, t)) {
        clear_barrier(ex, t->image);
        VkImageSubresourceRange r = {VK_IMAGE_ASPECT_COLOR_BIT, t->level, 1, t->base_layer, t->layers};
        vkCmdClearColorImage(ex->cb, t->image, VK_IMAGE_LAYOUT_GENERAL, &cv, 1, &r);
        ex->recorded++;
        vk_texture_gpu_written(t->tex);
        vk_texture_write_note("target clear", t->tex, 0, 0, NULL);
        return;
    }
    VkClearAttachment a = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .colorAttachment = (uint32_t)i};
    a.clearValue.color = cv;
    clear_attachment(ex, &a);
}

static void clear_ds(VkExec *ex, const p_clear_ds *c) {
    if (ex->ui_fit) ex->ui_fit = 0, ex->dirty |= VK_EXEC_DIRTY_VIEWPORT | VK_EXEC_DIRTY_SCISSOR;
    if (ex->render_disabled) return;
    resolve_rt(ex);
    const VkExecTarget *t = &ex->depth;
    if (!t->image) return;
    VkImageAspectFlags asp = 0;
    if (c->depth_mask && (t->aspect & VK_IMAGE_ASPECT_DEPTH_BIT)) asp |= VK_IMAGE_ASPECT_DEPTH_BIT;
    uint32_t sm = (uint32_t)c->stencil_mask & 0xff;
    if (sm && (t->aspect & VK_IMAGE_ASPECT_STENCIL_BIT)) {
        if (sm == 0xff) asp |= VK_IMAGE_ASPECT_STENCIL_BIT;
        else warn_once(W_STENCIL_MASK, "stencil clear with mask %#x is not replayed: skipped", sm);
    }
    if (!asp) return;
    float d = c->depth < 0.0f ? 0.0f : c->depth > 1.0f ? 1.0f : c->depth;
    VkClearDepthStencilValue v = {d, (uint32_t)c->stencil & 0xff};
    if (whole_target(ex, t)) {
        clear_barrier(ex, t->image);
        VkImageSubresourceRange r = {asp, t->level, 1, t->base_layer, t->layers};
        vkCmdClearDepthStencilImage(ex->cb, t->image, VK_IMAGE_LAYOUT_GENERAL, &v, 1, &r);
        ex->recorded++;
        vk_texture_gpu_written(t->tex);
        return;
    }
    VkClearAttachment a = {.aspectMask = asp};
    a.clearValue.depthStencil = v;
    clear_attachment(ex, &a);
}

// ---- the op loop

static void set_rt(VkExec *ex, const p_set_rt *r) {
    if (!memcmp(&ex->rt, r, sizeof *r)) return;   // the same targets again: the pass goes on
    vk_exec_pass_end(ex);
    ex->rt = *r;
    ex->rt_resolved = 0;
}

// ---- command buffer parts
// NVN starts the GPU as soon as commands are submitted; one Vulkan command buffer per submission starts it only after
// the whole replay. Between passes, once TOTK_SPLIT_DRAWS (64, as TotK; KarPur off from 2026-11-01 to 2026-11-08) draws and dispatches were recorded since the
// last part, the part goes to the queue (vk_sync_split) and the replay continues in a new command buffer. Only
// command-buffer state is lost there (bindings and dynamic state: vk_exec_draw_new_cb, ex->dirty); barriers, layouts and
// queries are not: a barrier's first scope is everything earlier in submission order, image layouts belong to the image,
// and occlusion queries end with their pass. Fences, completion callbacks and reports stay per submission: only the last
// part signals the value. nxnative commits every 64 draws between passes; MacNativeGames after 30, then every 40.
// A part also spans at least TOTK_SPLIT_US (1000) of replay time: a split costs about 40 us of the render thread
// (vkQueueSubmit2, a command buffer, the mirror prologue), and a fight frame of 2,000 draws made 15 parts on the 7900X;
// a millisecond of recording is a fraction of a millisecond of GPU work, so the GPU still starts early.
// KarPur: off from 2026-11-01 (Violet's trees flickered with parts) and on again since 2026-11-08 (test BT). Parts reach
// the GPU at once, without the window of the deferred submit, so CPU writes the game makes while the GPU reads an early
// part miss the mirror; the chunks they hit turn hot (read from imported memory) after one late or racing write
// (TOTK_HOT_LATE_SUBS, TOTK_RACE_SUBS, both 1 since then), and the flicker no longer showed. In towns the parts cut the
// chain submit -> replay -> GPU that the game waits on once per frame (main+0xcd0eb0): 41-47 fps became 52-60.
static int split_n = -1, split_draws, part_start;
static uint64_t split_ns, part_t0;
static uint64_t n_parts, n_subs;

static void maybe_split(VkExec *ex) {
    if (split_n < 0) {
        // A partial submit takes an early snapshot of shadow-backed vertex and instance buffers. Live host imports
        // can see the game's later writes, but the snapshot misses them, which can flicker during foliage streaming.
        split_n = vk_mem_shadow_active() && !cfg_is_set("TOTK_SPLIT_DRAWS") ? 0 : cfg_int("TOTK_SPLIT_DRAWS");
        if (split_n < 0) split_n = 0;
        int us = cfg_int("TOTK_SPLIT_US");
        split_ns = us > 0 ? (uint64_t)us * 1000 : 0;
        rt_log("vk: replay command buffer split every %d draws between passes, parts of at least %d us%s", split_n,
               us > 0 ? us : 0, split_n ? "" : " (off)");
    }
    if (!split_n || ex->in_pass || split_draws < split_n) return;
    if (split_ns && kern_now_ns() - part_t0 < split_ns) return;   // split_draws stays: the next draw asks again
    split_draws = 0;
    if (ex->recorded == part_start) return;
    ex->recorded += vk_mem_shadow_end(ex->cb);   // explicit shadow splitting: make this part's writebacks host-visible
    ex->cb = vk_sync_split();
    part_start = ex->recorded;
    part_t0 = kern_now_ns();
    ex->dirty = VK_EXEC_DIRTY_ALL;
    vk_exec_draw_new_cb();
    n_parts++;
}

static void walk(VkExec *ex, const op_hdr *h, int depth) {
    for (;;) {
        if (h->size < sizeof *h || (h->size & 7) || h->op >= OP_COUNT_)
            rt_fatal("vk: corrupt command record at %p: op %u size %u", (const void *)h, h->op, h->size);
        const void *a = nvn_op_payload(h);
        switch (h->op) {
        case OP_END: return;
        case OP_BEGIN: break;
        case OP_JUMP: h = (const op_hdr *)(uintptr_t)((const p_jump *)a)->target; continue;
        case OP_CALL:
            if (depth >= CALL_DEPTH) rt_fatal("vk: command buffer calls nested deeper than %d", CALL_DEPTH);
            vk_exec_frame.calls++;
            walk(ex, nvn_op_first(((const p_jump *)a)->target), depth + 1);
            break;
        case OP_SET_RT: set_rt(ex, a); break;
        case OP_CLEAR_COLOR: clear_color(ex, a); break;
        case OP_CLEAR_DS: clear_ds(ex, a); break;
        case OP_VIEWPORTS: {
            const p_viewports *v = a;
            for (int i = 0; i < v->n && v->first + i < 16; i++)
                if (v->first + i >= 0) memcpy(ex->vp[v->first + i], v->v[i], sizeof ex->vp[0]);
            // KarPur diagnostic: several differing viewports in one call (the replay draws with viewport 0 only)
            for (int i = 1; i < v->n && i < 16; i++)
                if (memcmp(v->v[i], v->v[0], 4 * sizeof(float))) {
                    static int once;
                    if (!once++)
                        rt_log("vk_exec: SetViewports with %d differing viewports (first %d): only viewport 0 is replayed",
                               v->n, v->first);
                    break;
                }
            ex->has_vp = 1;
            ex->dirty |= VK_EXEC_DIRTY_VIEWPORT;
            break;
        }
        case OP_SCISSOR:
            ex->scissor = *(const p_scissor *)a;
            ex->has_scissor = 1;
            ex->dirty |= VK_EXEC_DIRTY_SCISSOR;
            break;
        case OP_DEPTH_RANGE: {
            const p_depth_range *r = a;
            ex->depth_range[0] = r->n, ex->depth_range[1] = r->f;
            ex->dirty |= VK_EXEC_DIRTY_VIEWPORT;
            break;
        }
        case OP_STENCIL: {
            const p_stencil *s = a;
            for (int f = 0; f < 2; f++) {
                if (!(s->face & (1 << f))) continue;
                if (s->which == 0) ex->st_vmask[f] = (uint32_t)s->value_mask;
                else if (s->which == 1) ex->st_wmask[f] = (uint32_t)s->mask;
                else ex->st_ref[f] = (uint32_t)s->ref;
            }
            ex->dirty |= VK_EXEC_DIRTY_STENCIL;
            break;
        }
        case OP_POLY_OFFSET:
            ex->poly_offset = *(const p_poly_offset *)a;
            ex->dirty |= VK_EXEC_DIRTY_DEPTH_BIAS;
            break;
        case OP_BLEND_COLOR:
            memcpy(ex->blend_color, ((const p_blend_color *)a)->rgba, sizeof ex->blend_color);
            ex->dirty |= VK_EXEC_DIRTY_BLEND_CONST;
            break;
        case OP_DEPTH_CLAMP: ex->pipe.depth_clamp = ((const p_flag *)a)->value != 0; break;
        case OP_RASTER_DISCARD: ex->pipe.raster_discard = ((const p_flag *)a)->value != 0; break;
        case OP_PRIM_RESTART: ex->prim_restart = *(const p_prim_restart *)a; break;
        case OP_COPY_STRIDE: ex->copy_stride = ((const p_copy_stride *)a)->stride; break;
        case OP_SET_TEX_POOL: ex->tex_pool = ((const p_set_pool *)a)->pool, ex->tex_serial++; break;
        case OP_SET_SAMP_POOL: ex->samp_pool = ((const p_set_pool *)a)->pool; break;
        case OP_BARRIER: game_barrier(ex); break;
        case OP_DRAW: case OP_DRAW_IDX: case OP_DISPATCH: case OP_UPDATE_UB: case OP_DRAW_IDX_IND: case OP_DISPATCH_IND:
            if (h->op == OP_DRAW || h->op == OP_DRAW_IDX || h->op == OP_DRAW_IDX_IND)
                vk_exec_frame.draws_seen++, split_draws++, vk_prog_select(ex, 0);
            if (h->op == OP_DISPATCH || h->op == OP_DISPATCH_IND)
                vk_exec_frame.dispatches++, split_draws++, vk_prog_select(ex, 1);
            // fall through
        case OP_BIND_STATE: case OP_BIND_VATTR: case OP_BIND_VSTREAM: case OP_BIND_VBUF:
        case OP_BIND_UBO: case OP_BIND_SSBO: case OP_BIND_TEX: case OP_BIND_SAMP: case OP_BIND_IMG:
            vk_exec_op_draw(ex, h->op, a);
            // TOTK_PASS_TIME=3: the pass ends after each draw, and the next draw begins a new one (loads keep the image)
            if (pt_split && pt_state > 0 && ex->in_pass && (h->op == OP_DRAW || h->op == OP_DRAW_IDX || h->op == OP_DRAW_IDX_IND))
                vk_exec_pass_end(ex), vk_exec_pass_begin(ex);
            break;
        case OP_BIND_PROGRAM: vk_prog_bind(a); break;   // binds by stage; vk_prog_select passes it on (vk_xfb.c)
        case OP_BIND_XFB: case OP_XFB: case OP_DRAW_XFB: vk_exec_op_xfb(ex, h->op, a); break;
        case OP_COPY_B2B: case OP_COPY_B2T: case OP_COPY_T2B: case OP_COPY_T2T: case OP_CLEAR_TEX:
            if (h->op != OP_CLEAR_TEX) vk_exec_frame.copies++;
            if (vk_dump_armed) vk_dump_copy(ex, h->op, a);
            // fall through
        case OP_SIGNAL_EVENT: case OP_REPORT_COUNTER: case OP_RESET_COUNTER: case OP_RENDER_ENABLE: case OP_RENDER_COND:
        case OP_CLEAR_BUF:
            vk_exec_op_xfer(ex, h->op, a);
            break;
        default: rt_fatal("vk: op %s has no replay handler", nvn_op_name(h->op));
        }
        if (pt_state > 0 && !ex->in_pass) {   // TOTK_PASS_TIME: what this op cost the GPU, its barrier included
            int label = -1;
            switch (h->op) {
            case OP_DISPATCH: {
                const p_dispatch *d = a;
                pt_aux_next = (uint32_t)(d->x & 1023) << 20 | (uint32_t)(d->y & 1023) << 10 | (uint32_t)(d->z & 1023);
                label = PT_DISPATCH;
                break;
            }
            case OP_DISPATCH_IND: label = PT_DISPATCH; break;
            case OP_COPY_B2B: case OP_COPY_B2T: case OP_COPY_T2B: case OP_COPY_T2T: label = PT_COPY; break;
            case OP_CLEAR_TEX: case OP_CLEAR_BUF: case OP_CLEAR_COLOR: case OP_CLEAR_DS: label = PT_CLEAR; break;
            case OP_UPDATE_UB: label = PT_UB; break;
            case OP_BARRIER: pt_aux_next = pt_prev_pass; label = PT_GAME; break;
            default: break;
            }
            if (label == PT_DISPATCH) pt_strict_barrier(ex), pt_id_next = vk_exec_last_cs;
            if (label >= 0) pt_stamp(ex, label);
        }
        if (split_draws >= split_n) maybe_split(ex);
        h = nvn_op_next(h);
    }
}

int vk_exec_record(VkCommandBuffer cb, const uint64_t *handles, int n, uint64_t fence) {
    VkExec *ex = &X;
    if (!x_ready) state_reset(ex), vk_dump_init(), x_ready = 1;
    ex->cb = cb;
    ex->fence = fence;
    ex->recorded = 0;
    ex->in_pass = 0;
    ex->dirty = VK_EXEC_DIRTY_ALL;
    ex->rt_resolved = 0;   // images may have changed since the last command buffer
    ex->nviews = 0;
    ex->barrier_at = -1;
    ex->nfbl = 0;   // the previous command buffer ended with a full barrier: every image is in GENERAL
    hz_reset(ex);
    ex->hz_other = VK_HZ_CB_START;   // the first pass or clear takes the full barrier: copies at the buffer's start
    vk_exec_frame.submits++;
    split_draws = part_start = 0;
    part_t0 = kern_now_ns();
    x_here = 1;
    uint64_t t0 = kern_now_ns();
    if (n) pt_stamp(ex, PT_OTHER);
    if (n) vk_mem_mirror_begin(), vk_usnap_begin(handles, n, fence);
    if (n) pt_stamp(ex, PT_START);
    uint64_t t1 = kern_now_ns();
    for (int i = 0; i < n; i++) {
        if (!handles[i]) rt_fatal("vk: command handle %d of a submission is null", i);
        walk(ex, nvn_op_first(handles[i]), 0);
    }
    vk_exec_pass_end(ex);
    x_here = 0;
    uint64_t t2 = kern_now_ns();
    if (ex->recorded) vk_exec_barrier(ex);   // later submissions see this one's writes
    if (n) {
        ex->recorded += vk_mem_mirror_end(ex->cb, fence) + vk_mem_shadow_end(ex->cb);
        vk_usnap_end(fence);
    }
    replay_walk_ns += t2 - t1;
    replay_sync_ns += t1 - t0 + kern_now_ns() - t2;
    if (ex->nviews) {
        ViewList *l = malloc(sizeof *l + sizeof(VkImageView) * (size_t)ex->nviews);
        if (!l) rt_fatal("vk: out of memory");
        l->n = ex->nviews;
        for (int i = 0; i < ex->nviews; i++) l->v[i] = ((ViewEnt *)ex->views)[i].view;
        vk_completion_add(fence, free_views, l);
        ex->nviews = 0;
    }
    pt_finish(ex, ex->recorded != part_start);
    ex->cb = VK_NULL_HANDLE;
    if (n) n_subs++;
    if ((rt_trace & RT_TRACE_PERF) && n_subs >= 240) {
        rt_log("perf: replay command buffer parts per submission %.2f", (double)(n_subs + n_parts) / (double)n_subs);
        n_subs = n_parts = 0;
    }
    return ex->recorded - part_start;   // what the last part holds
}
