// AMD FSR 2.2 Vulkan shader lookup. SPIR-V comes from AMD's MIT GLSL through
// tools/build_fsr2_shaders.py; the game needs no shader compiler or extra DLL.
// Descriptor reflection follows PokeV-Recomp's fsr2_shaders.cpp (MIT, 529ef860).
#include <string>
#include <vector>
#include <stdint.h>
extern "C" void rt_log(const char *fmt, ...);
#include "third_party/fsr2/vk/shaders/ffx_fsr2_shaders_vk.h"
#include "fsr2_permutations.h"

struct Blob {
    unsigned pass, options;
    const uint32_t *spv;
    unsigned bytes;
    std::vector<std::string> names[3];
    std::vector<const char *> cnames[3];
    std::vector<uint32_t> bindings[3];
};

// Set 1: storage images, sampled images, uniform buffers. Set 0 holds backend samplers.
static void spv_reflect(Blob &b) {
    const uint32_t *w = b.spv;
    size_t n = b.bytes / 4;
    if (n < 5) return;
    uint32_t bound = w[3];
    std::vector<std::string> name(bound);
    std::vector<int32_t> binding(bound, -1), set(bound, -1);
    std::vector<uint32_t> op(bound), a1(bound), a3(bound);
    struct Var { uint32_t id, type; };
    std::vector<Var> vars;
    for (size_t i = 5; i < n;) {
        uint32_t len = w[i] >> 16, code = w[i] & 0xffff;
        if (!len || i + len > n) break;
        const uint32_t *o = &w[i + 1];
        switch (code) {
        case 5: if (o[0] < bound) name[o[0]] = (const char *)&o[1]; break;
        case 71:
            if (o[0] < bound && o[1] == 33) binding[o[0]] = (int32_t)o[2];
            if (o[0] < bound && o[1] == 34) set[o[0]] = (int32_t)o[2];
            break;
        case 25: if (o[0] < bound) op[o[0]] = 25, a1[o[0]] = o[6]; break;
        case 28:
        case 29: if (o[0] < bound) op[o[0]] = 28, a1[o[0]] = o[1]; break;
        case 30: if (o[0] < bound) op[o[0]] = 30; break;
        case 32: if (o[0] < bound) op[o[0]] = 32, a3[o[0]] = o[2]; break;
        case 59: vars.push_back({o[1], o[0]}); break;
        }
        i += len;
    }
    for (const Var &v : vars) {
        if (v.id >= bound || set[v.id] != 1 || binding[v.id] < 0 || v.type >= bound || op[v.type] != 32) continue;
        uint32_t t = a3[v.type];
        while (t < bound && op[t] == 28) t = a1[t];
        int kind = t >= bound ? -1 : op[t] == 25 ? (a1[t] == 2 ? 0 : 1) : op[t] == 30 ? 2 : -1;
        if (kind < 0) continue;
        b.names[kind].push_back(name[v.id]);
        b.bindings[kind].push_back((uint32_t)binding[v.id]);
    }
    for (int k = 0; k < 3; k++)
        for (const std::string &s : b.names[k]) b.cnames[k].push_back(s.c_str());
}

extern "C" Fsr2ShaderBlobVK fsr2GetPermutationBlobByIndexVK(FfxFsr2Pass pass, uint32_t options) {
    static std::vector<Blob *> cache;
    Fsr2ShaderBlobVK out = {};
    // KarPur does not enable 16-bit Vulkan shader types. Wave64 only changes AMD's table choice.
    options &= ~(uint32_t)(FSR2_SHADER_PERMUTATION_FORCE_WAVE64 | FSR2_SHADER_PERMUTATION_ALLOW_FP16);
    Blob *b = nullptr;
    for (Blob *c : cache) if (c->pass == (unsigned)pass && c->options == options) { b = c; break; }
    if (!b) {
        for (const Fsr2Compiled &entry : fsr2_compiled)
            if (entry.pass == (unsigned)pass && entry.options == options) {
                b = new Blob;
                b->pass = entry.pass, b->options = entry.options, b->spv = entry.data, b->bytes = entry.bytes;
                spv_reflect(*b);
                cache.push_back(b);
                rt_log("fsr2: pass %u options %#x: %u bytes, %zu storage / %zu sampled / %zu buffers",
                       b->pass, b->options, b->bytes, b->names[0].size(), b->names[1].size(), b->names[2].size());
                break;
            }
        if (!b) { rt_log("fsr2: no precompiled shader for pass %u options %#x", (unsigned)pass, options); return out; }
    }
    out.data = (const uint8_t *)b->spv;
    out.size = b->bytes;
    out.storageImageCount = (uint32_t)b->names[0].size();
    out.sampledImageCount = (uint32_t)b->names[1].size();
    out.uniformBufferCount = (uint32_t)b->names[2].size();
    out.boundStorageImageNames = b->cnames[0].data(), out.boundStorageImageBindings = b->bindings[0].data();
    out.boundSampledImageNames = b->cnames[1].data(), out.boundSampledImageBindings = b->bindings[1].data();
    out.boundUniformBufferNames = b->cnames[2].data(), out.boundUniformBufferBindings = b->bindings[2].data();
    return out;
}
