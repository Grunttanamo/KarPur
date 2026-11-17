#pragma once

#include <stdint.h>
#include "vk_exec.h"

// Optional host-only screen-space effects. They use the scene depth and the
// tone-mapped picture; no game-visible pool or texture allocation is changed.
int vk_lighting_on(void);
int vk_lighting_need_scene(void);
void vk_lighting_note_scene(const VkExec *ex, const float proj[16]);
void vk_lighting_after_draw(VkExec *ex, uint32_t fs);
void vk_lighting_frame(void);
