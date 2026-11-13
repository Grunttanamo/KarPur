// Program binds by stage and transform feedback in the Vulkan replay. Written for KarPur (MIT).
//
// Program binds. nvnCommandBufferBindProgram(cb, program, stages) binds the program's shaders for the stages in the
// mask (NVNshaderStageBits); a stage in the mask the program lacks is unbound (the program may be NULL), stages
// outside the mask keep theirs. Violet's transform feedback passes bind a vertex-only program (mask 0x1) and then
// unbind the fragment stage alone (NULL, 0x2); compute programs come with mask 0x20 between graphics draws.
// vk_exec_draw.c knows one program (ex->prog), so this file keeps the program of every stage and, right before each
// draw or dispatch, hands vk_exec_draw.c the program that holds exactly the bound graphics stages, or the compute
// program: the game's own program when one provides them all, else a composite (a host prog_t whose stage entries
// come from several programs, kept in a table by their programs and serials).
//
// Transform feedback (Violet: GPU skinning of characters and Pokemon into three buffers that later draws read as
// vertex buffers; DrawTransformFeedback is never called). The layout comes from the vertex shader's control section
// (nvn_xfb.h) and is part of the shader translation (gfx_shader.c "xfb"). A draw while transform feedback is active
// binds the buffers at the bytes captured so far, begins transform feedback, draws, ends it, and advances the offsets by
// the vertices it captured times each stride. Pipelines cannot be bound while transform feedback is active, so it is
// begun and ended around every draw, and the offsets are kept here instead of in counter buffers. The end of transform
// feedback adds a full barrier so the captured vertices are visible to the draws that read them. The Vulkan calls go
// through function pointers and are made after rec_drain (vk_rec.h).
//
// Runs on the replay thread under vk_sync_submit's lock (vk_exec.h): no locking inside.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "host.h"
#include "nvn_api.h"
#include "nvn_xfb.h"
#include "vk_exec.h"

// ---- program binds by stage

#define NSTAGE 6         // NVN stage ids (control +0x714): 0 vertex, 1 fragment, 2-4 the other graphics stages, 5 compute
#define CS_STAGE 5
#define COMP_SLOTS 1024

static const prog_t *stage_prog[NSTAGE];   // program whose shader is bound at each stage
static const prog_t *gfx_cur;              // graphics program for the bound stages, valid while !gfx_dirty
static int gfx_dirty = 1;
static int bound_since[2];                 // a bind touched the graphics (0) or compute (1) stages since the last select
static prog_t comp[COMP_SLOTS];            // composites
static struct { const prog_t *p[CS_STAGE]; uint64_t s[CS_STAGE]; } comp_key[COMP_SLOTS];

static int stage_of(const NVNshaderData *sd) {
    if (!sd->control || !sd->data) return -1;
    uint32_t st;
    memcpy(&st, (const uint8_t *)sd->control + 0x714, 4);
    return st < NSTAGE ? (int)st : -1;
}

static const NVNshaderData *stage_entry(const prog_t *p, int st) {
    for (uint32_t i = 0; p && i < p->nstage && i < 6; i++)
        if (stage_of(&p->sd[i]) == st) return &p->sd[i];
    return NULL;
}

void vk_prog_bind(const p_bind_program *b) {
    static struct { int null_prog; int32_t stages; } seen[12];
    static int nseen;
    int null_prog = b->prog == NULL, fresh = 1;
    for (int i = 0; i < nseen && fresh; i++)
        if (seen[i].null_prog == null_prog && seen[i].stages == b->stages) fresh = 0;
    if (fresh && nseen < 12) {
        seen[nseen].null_prog = null_prog, seen[nseen++].stages = b->stages;
        rt_log("vk_exec: program bind with %s program, stage mask %#x (binds by stage)", null_prog ? "no" : "a", b->stages);
    }
    for (int st = 0; st < NSTAGE; st++)
        if (b->stages & (1 << st)) stage_prog[st] = stage_entry(b->prog, st) ? b->prog : NULL;
    if (b->stages & 0x1f) gfx_dirty = 1, bound_since[0] = 1;
    if (b->stages & 0x20) bound_since[1] = 1;
}

static const prog_t *composite(void) {
    uint64_t h = 0;
    for (int st = 0; st < CS_STAGE; st++)
        h = (h ^ (uintptr_t)stage_prog[st] ^ (stage_prog[st] ? stage_prog[st]->serial << 20 : 0)) * 0x9E3779B97F4A7C15ull;
    unsigned slot = (unsigned)(h >> 54) % COMP_SLOTS;
    int match = comp[slot].magic == NVN_PROG_MAGIC;
    for (int st = 0; st < CS_STAGE && match; st++)
        if (comp_key[slot].p[st] != stage_prog[st] || (stage_prog[st] && comp_key[slot].s[st] != stage_prog[st]->serial))
            match = 0;
    if (match) return &comp[slot];
    prog_t *c = &comp[slot];
    memset(c, 0, sizeof *c);
    for (int st = 0; st < CS_STAGE; st++) {
        comp_key[slot].p[st] = stage_prog[st];
        comp_key[slot].s[st] = stage_prog[st] ? stage_prog[st]->serial : 0;
        const NVNshaderData *sd = stage_entry(stage_prog[st], st);
        if (sd) c->sd[c->nstage++] = *sd;
    }
    c->magic = NVN_PROG_MAGIC;
    c->serial = nvn_new_serial();
    static int ncomp;
    if (++ncomp <= 4 || ncomp % 1000 == 0)
        rt_log("vk_exec: composite program %d (%u stages from several binds)", ncomp, c->nstage);
    return c;
}

static const prog_t *gfx_program(void) {
    const prog_t *one = NULL;
    int mixed = 0;
    for (int st = 0; st < CS_STAGE; st++) {
        if (!stage_prog[st]) continue;
        if (!one) one = stage_prog[st];
        else if (stage_prog[st] != one) mixed = 1;
    }
    if (!one) return NULL;
    for (uint32_t i = 0; !mixed && i < one->nstage && i < 6; i++) {   // a stage of one that is not bound from it
        int st = stage_of(&one->sd[i]);
        if (st >= 0 && st < CS_STAGE && stage_prog[st] != one) mixed = 1;
    }
    return mixed ? composite() : one;
}

void vk_prog_select(VkExec *ex, int compute) {
    if (!compute && gfx_dirty) gfx_cur = gfx_program(), gfx_dirty = 0;
    const prog_t *want = compute ? stage_prog[CS_STAGE] : gfx_cur;
    // Passed on after every bind, as before binds by stage (vk_exec_draw.c bumps its bind generation), and whenever
    // the program differs (a dispatch between draws).
    if (ex->prog == want && !bound_since[compute]) return;
    bound_since[compute] = 0;
    p_bind_program b = {want, compute ? 0x20 : 0x1f};
    vk_exec_op_draw(ex, OP_BIND_PROGRAM, &b);
}

// ---- transform feedback

static PFN_vkCmdBindTransformFeedbackBuffersEXT fn_bind;
static PFN_vkCmdBeginTransformFeedbackEXT fn_begin;
static PFN_vkCmdEndTransformFeedbackEXT fn_end;
static int fn_state;                // 0 not looked up, 1 ready, -1 unavailable
static uint64_t written[4];         // bytes captured into each buffer since its bind or the begin
static VkMemSpan span[4];           // the buffers of the draw in flight
static uint64_t span_addr[4];
static int nbound, captured_any;
static NvnXfb lay;                  // layout of the draw in flight
static int tf_draws, tf_skipped;

static int fns_ready(void) {
    if (!fn_state) {
        VkCtx *c = vk_ctx();
        fn_state = -1;
        int on = rt_knob("XFB", "1");   // TOTK_XFB=0: transform feedback draws are drawn without capturing
        if (c && c->has_transform_feedback && on) {
            fn_bind = (PFN_vkCmdBindTransformFeedbackBuffersEXT)vkGetDeviceProcAddr(c->device, "vkCmdBindTransformFeedbackBuffersEXT");
            fn_begin = (PFN_vkCmdBeginTransformFeedbackEXT)vkGetDeviceProcAddr(c->device, "vkCmdBeginTransformFeedbackEXT");
            fn_end = (PFN_vkCmdEndTransformFeedbackEXT)vkGetDeviceProcAddr(c->device, "vkCmdEndTransformFeedbackEXT");
            if (fn_bind && fn_begin && fn_end) fn_state = 1;
        }
        rt_log("tf: transform feedback %s", fn_state > 0 ? "recorded with VK_EXT_transform_feedback (TOTK_XFB=0 turns it off)"
                                            : !on ? "off (TOTK_XFB=0): those draws capture nothing"
                                                  : "unavailable (no VK_EXT_transform_feedback): those draws capture nothing");
    }
    return fn_state > 0;
}

static void skip_note(const char *why) {
    if (++tf_skipped <= 6) rt_log("tf: a transform feedback draw captures nothing: %s (%d so far)", why, tf_skipped);
}

int vk_xfb_draw_begin(VkExec *ex) {
    if (!ex->xfb_active || !fns_ready()) return 0;
    const NVNshaderData *vs = stage_entry(ex->prog, 0);
    if (!vs || !nvn_xfb_layout(vs->control, &lay)) return skip_note("the vertex shader has no transform feedback layout"), 0;
    VkBuffer buf[4];
    VkDeviceSize off[4], size[4];
    nbound = 0;
    for (int b = 0; b < 4; b++) {
        if (!lay.stride[b]) continue;
        const p_bind_xfb *x = &ex->xfb_buf[b];
        if (b != nbound) return skip_note("its buffers are not bound from index 0 on"), 0;
        if (!x->addr || written[b] >= x->size) return skip_note("a buffer is unbound or full"), 0;
        span_addr[b] = x->addr + written[b];
        if (!vk_mem_resolve(span_addr[b], 1, &span[b]) || span[b].offset % 4) return skip_note("a buffer is not backed"), 0;
        uint64_t n = x->size - written[b];
        vk_mem_use_kind = VK_MUSE_GPU_WRITE;   // transform feedback output
        VK_MEM_USE_N(span[b], n < span[b].avail ? n : span[b].avail);
        buf[b] = span[b].buffer, off[b] = span[b].offset, size[b] = n < span[b].avail ? n : span[b].avail;
        nbound++;
    }
    if (!nbound) return 0;
    // Each distinct layout once (up to 16): a model kind with other outputs (trees, effects) shows up here.
    static NvnXfb seen_lay[16];
    static int nseen_lay;
    int known = 0;
    for (int i = 0; i < nseen_lay && !known; i++) known = !memcmp(&seen_lay[i], &lay, sizeof lay);
    if (!known && nseen_lay < 16) {
        seen_lay[nseen_lay++] = lay;
        char d[400];
        int o = 0;
        for (int b = 0; b < 4 && o < (int)sizeof d - 60; b++) {
            if (!lay.stride[b]) continue;
            o += snprintf(d + o, sizeof d - o, " [%d: stride %u:", b, lay.stride[b]);
            for (int i = 0; i < lay.n[b] && o < (int)sizeof d - 8; i++) o += snprintf(d + o, sizeof d - o, " %02x", lay.loc[b][i]);
            o += snprintf(d + o, sizeof d - o, "]");
        }
        rt_log("tf: layout %d:%s (buffer sizes %#llx %#llx %#llx)", nseen_lay, d, (unsigned long long)ex->xfb_buf[0].size,
               (unsigned long long)ex->xfb_buf[1].size, (unsigned long long)ex->xfb_buf[2].size);
    }
    if (++tf_draws <= 4) {
        char d[200];
        int o = 0;
        for (int b = 0; b < nbound; b++)
            o += snprintf(d + o, sizeof d - o, " [%d: stride %u, %u components from %#x]", b, lay.stride[b], lay.n[b],
                          lay.loc[b][0]);
        rt_log("tf: draw %d captures into %d buffers:%s", tf_draws, nbound, d);
    }
    rec_drain();
    if (tf_draws <= 2) rt_log("tf: draw %d: binding the buffers", tf_draws);
    fn_bind(ex->cb, 0, (uint32_t)nbound, buf, off, size);
    if (tf_draws <= 2) rt_log("tf: draw %d: beginning", tf_draws);
    fn_begin(ex->cb, 0, 0, NULL, NULL);
    if (tf_draws <= 2) rt_log("tf: draw %d: begun, drawing", tf_draws);
    ex->recorded += 2;
    return 1;
}

// Vertices a draw of n vertices and inst instances sends down the pipeline (what transform feedback captures).
static uint64_t captured_vertices(int prim, uint32_t n, uint32_t inst) {
    uint64_t v;
    switch (prim) {
    case NVN_DRAW_PRIMITIVE_LINE_STRIP: v = n >= 2 ? 2ull * (n - 1) : 0; break;
    case NVN_DRAW_PRIMITIVE_LINE_LOOP: v = n >= 2 ? 2ull * n : 0; break;
    case NVN_DRAW_PRIMITIVE_TRIANGLE_STRIP: case NVN_DRAW_PRIMITIVE_TRIANGLE_FAN: v = n >= 3 ? 3ull * (n - 2) : 0; break;
    case NVN_DRAW_PRIMITIVE_LINES: v = n / 2 * 2ull; break;
    case NVN_DRAW_PRIMITIVE_TRIANGLES: v = n / 3 * 3ull; break;
    default: v = n; break;   // points; the rest is not expected with transform feedback
    }
    return v * (inst ? inst : 1);
}

// TOTK_XFB_SYNC=1 (off): a full barrier after every captured draw, not only when transform feedback ends.
// For the device loss on RDNA 3 during character creation (issue #18): there, TOTK_XFB=0 avoids it (and ruins the
// skinned models), while AMD's Crash Analysis avoids it with the models intact - and that instrumentation mainly
// serializes the GPU's work. Forcing full barriers elsewhere (TOTK_BARRIER_TRACK=0) did not help, and the barrier
// this file already adds comes once per pass, so the one place left without any ordering is between the captured
// draws of one pass. Transform feedback is inactive here, so the barrier may end the pass.
static int xfb_sync(void) {
    static int on = -1;
    if (on < 0) {
        on = rt_knob("XFB_SYNC", "0");
        if (on) rt_log("tf: a full barrier after every captured draw (TOTK_XFB_SYNC=1)");
    }
    return on;
}

void vk_xfb_draw_end(VkExec *ex, int prim, uint32_t n, uint32_t inst) {
    rec_drain();
    if (tf_draws <= 2) rt_log("tf: draw %d: drawn, ending", tf_draws);
    fn_end(ex->cb, 0, 0, NULL, NULL);
    if (tf_draws <= 2) rt_log("tf: draw %d: ended", tf_draws);
    ex->recorded++;
    if (xfb_sync()) vk_exec_barrier(ex);
    uint64_t verts = captured_vertices(prim, n, inst);
    for (int b = 0; b < nbound; b++) {
        uint64_t bytes = verts * lay.stride[b], left = ex->xfb_buf[b].size - written[b];
        if (bytes > left) bytes = left;
        vk_mem_gpu_wrote(&span[b], bytes);
        vk_usnap_drop(span_addr[b], bytes);
        written[b] += bytes;
    }
    captured_any = 1;
}

void vk_exec_op_xfb(VkExec *ex, uint32_t op, const void *payload) {
    switch (op) {
    case OP_BIND_XFB: {
        const p_bind_xfb *b = payload;
        if (b->index >= 0 && b->index < 4) ex->xfb_buf[b->index] = *b, written[b->index] = 0;
        break;
    }
    case OP_XFB: {
        int kind = ((const p_xfb *)payload)->kind;
        if (kind == XFB_BEGIN) memset(written, 0, sizeof written);
        ex->xfb_active = kind == XFB_BEGIN || kind == XFB_RESUME;
        if (!ex->xfb_active && captured_any) vk_exec_barrier(ex), captured_any = 0;   // captured vertices -> vertex fetch
        break;
    }
    case OP_DRAW_XFB: {
        static int once;
        if (!once++) rt_log("tf: DrawTransformFeedback is not replayed yet: nothing drawn (logged once)");
        break;
    }
    }
}
