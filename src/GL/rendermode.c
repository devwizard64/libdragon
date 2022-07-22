#include "gl_internal.h"
#include "rdpq.h"

extern gl_state_t state;

#define BLENDER_CYCLE(a1, b1, a2, b2) \
    (((SOM_BLEND_A_ ## a1) << 12) | ((SOM_BLEND_B1_ ## b1) << 8) | ((SOM_BLEND_A_ ## a2) << 4) | ((SOM_BLEND_B2_ ## b2) << 0))

// All possible combinations of blend functions. Configs that cannot be supported by the RDP are set to 0.
// NOTE: We always set fog alpha to one to support GL_ONE in both factors
static const uint32_t blend_configs[64] = {
    BLENDER_CYCLE(IN_RGB, ZERO, MEMORY_RGB, ZERO),                 // src = ZERO, dst = ZERO
    BLENDER_CYCLE(IN_RGB, ZERO, MEMORY_RGB, ONE),                  // src = ZERO, dst = ONE
    BLENDER_CYCLE(MEMORY_RGB, IN_ALPHA, IN_RGB, ZERO),             // src = ZERO, dst = SRC_ALPHA
    0,                                                             // src = ZERO, dst = ONE_MINUS_SRC_ALPHA
    0,                                                             // src = ZERO, dst = GL_DST_COLOR
    0,                                                             // src = ZERO, dst = GL_ONE_MINUS_DST_COLOR
    BLENDER_CYCLE(IN_RGB, ZERO, MEMORY_RGB, MEMORY_ALPHA),         // src = ZERO, dst = DST_ALPHA
    0,                                                             // src = ZERO, dst = ONE_MINUS_DST_ALPHA

    BLENDER_CYCLE(IN_RGB, FOG_ALPHA, MEMORY_RGB, ZERO),            // src = ONE, dst = ZERO
    BLENDER_CYCLE(IN_RGB, FOG_ALPHA, MEMORY_RGB, ONE),             // src = ONE, dst = ONE
    BLENDER_CYCLE(MEMORY_RGB, IN_ALPHA, IN_RGB, ONE),              // src = ONE, dst = SRC_ALPHA
    0,                                                             // src = ONE, dst = ONE_MINUS_SRC_ALPHA
    0,                                                             // src = ONE, dst = GL_DST_COLOR
    0,                                                             // src = ONE, dst = GL_ONE_MINUS_DST_COLOR
    BLENDER_CYCLE(IN_RGB, FOG_ALPHA, MEMORY_RGB, MEMORY_ALPHA),    // src = ONE, dst = DST_ALPHA
    0,                                                             // src = ONE, dst = ONE_MINUS_DST_ALPHA

    BLENDER_CYCLE(IN_RGB, IN_ALPHA, MEMORY_RGB, ZERO),             // src = SRC_ALPHA, dst = ZERO
    BLENDER_CYCLE(IN_RGB, IN_ALPHA, MEMORY_RGB, ONE),              // src = SRC_ALPHA, dst = ONE
    0,                                                             // src = SRC_ALPHA, dst = SRC_ALPHA
    BLENDER_CYCLE(IN_RGB, IN_ALPHA, MEMORY_RGB, INV_MUX_A),        // src = SRC_ALPHA, dst = ONE_MINUS_SRC_ALPHA
    0,                                                             // src = SRC_ALPHA, dst = GL_DST_COLOR
    0,                                                             // src = SRC_ALPHA, dst = GL_ONE_MINUS_DST_COLOR
    BLENDER_CYCLE(IN_RGB, IN_ALPHA, MEMORY_RGB, MEMORY_ALPHA),     // src = SRC_ALPHA, dst = DST_ALPHA
    0,                                                             // src = SRC_ALPHA, dst = ONE_MINUS_DST_ALPHA

    0,                                                             // src = ONE_MINUS_SRC_ALPHA, dst = ZERO
    0,                                                             // src = ONE_MINUS_SRC_ALPHA, dst = ONE
    BLENDER_CYCLE(MEMORY_RGB, IN_ALPHA, IN_RGB, INV_MUX_A),        // src = ONE_MINUS_SRC_ALPHA, dst = SRC_ALPHA
    0,                                                             // src = ONE_MINUS_SRC_ALPHA, dst = ONE_MINUS_SRC_ALPHA
    0,                                                             // src = ONE_MINUS_SRC_ALPHA, dst = GL_DST_COLOR
    0,                                                             // src = ONE_MINUS_SRC_ALPHA, dst = GL_ONE_MINUS_DST_COLOR
    0,                                                             // src = ONE_MINUS_SRC_ALPHA, dst = DST_ALPHA
    0,                                                             // src = ONE_MINUS_SRC_ALPHA, dst = ONE_MINUS_DST_ALPHA

    0, 0, 0, 0, 0, 0, 0, 0,                                        // src = GL_DST_COLOR, dst = ...
    0, 0, 0, 0, 0, 0, 0, 0,                                        // src = GL_ONE_MINUS_DST_COLOR, dst = ...

    BLENDER_CYCLE(MEMORY_RGB, ZERO, IN_RGB, MEMORY_ALPHA),         // src = DST_ALPHA, dst = ZERO
    BLENDER_CYCLE(MEMORY_RGB, FOG_ALPHA, IN_RGB, MEMORY_ALPHA),    // src = DST_ALPHA, dst = ONE
    BLENDER_CYCLE(MEMORY_RGB, IN_ALPHA, IN_RGB, MEMORY_ALPHA),     // src = DST_ALPHA, dst = SRC_ALPHA
    0,                                                             // src = DST_ALPHA, dst = ONE_MINUS_SRC_ALPHA
    0,                                                             // src = DST_ALPHA, dst = GL_DST_COLOR
    0,                                                             // src = DST_ALPHA, dst = GL_ONE_MINUS_DST_COLOR
    0,                                                             // src = DST_ALPHA, dst = DST_ALPHA
    0,                                                             // src = DST_ALPHA, dst = ONE_MINUS_DST_ALPHA

    0, 0, 0, 0, 0, 0, 0, 0,                                        // src = ONE_MINUS_DST_ALPHA, dst = ...
};

inline bool blender_reads_memory(uint32_t bl)
{
    return ((bl>>12)&3) == SOM_BLEND_A_MEMORY_RGB ||
           ((bl>>4)&3) == SOM_BLEND_A_MEMORY_RGB ||
           (bl&3) == SOM_BLEND_B2_MEMORY_ALPHA;
}

inline rdpq_blender_t blender1(uint32_t bl, bool force_blend)
{
    rdpq_blender_t blend = (bl << 18) | (bl << 16);
    if (blender_reads_memory(bl))
        blend |= SOM_READ_ENABLE;
    if (force_blend)
        blend |= SOM_BLENDING;
    return blend;
}

inline rdpq_blender_t blender2(uint32_t bl0, uint32_t bl1, bool force_blend)
{
    rdpq_blender_t blend = (bl0 << 18) | (bl1 << 16);
    if (blender_reads_memory(bl0) || blender_reads_memory(bl1))
        blend |= SOM_READ_ENABLE;
    if (force_blend)
        blend |= SOM_BLENDING;
    return blend | RDPQ_BLENDER_2PASS;
}

void gl_rendermode_init()
{
    state.dither = true;

    state.fog_start = 0.0f;
    state.fog_end = 1.0f;

    state.tex_env_mode = GL_MODULATE;

    state.is_rendermode_dirty = true;
    state.is_scissor_dirty = true;

    glBlendFunc(GL_ONE, GL_ZERO);
    glDepthFunc(GL_LESS);
    glAlphaFunc(GL_ALWAYS, 0.0f);

    GLfloat fog_color[] = {0, 0, 0, 0};
    glFogfv(GL_FOG_COLOR, fog_color);
}

bool gl_is_invisible()
{
    return state.draw_buffer == GL_NONE 
        || (state.depth_test && state.depth_func == GL_NEVER)
        || (state.alpha_test && state.alpha_func == GL_NEVER);
}

void gl_update_scissor()
{
    if (!state.is_scissor_dirty) {
        return;
    }

    uint32_t w = state.cur_framebuffer->color_buffer->width;
    uint32_t h = state.cur_framebuffer->color_buffer->height;

    if (state.scissor_test) {
        rdpq_set_scissor(
            state.scissor_box[0],
            h - state.scissor_box[1] - state.scissor_box[3],
            state.scissor_box[0] + state.scissor_box[2],
            h - state.scissor_box[1]
        );
    } else {
        rdpq_set_scissor(0, 0, w, h);
    }

    state.is_scissor_dirty = false;
}

void gl_update_render_mode()
{
    if (!state.is_rendermode_dirty) {
        return;
    }

    uint64_t modes = SOM_CYCLE_1;
    rdpq_combiner_t comb;
    rdpq_blender_t blend = 0;

    if (state.dither) {
        modes |= SOM_RGBDITHER_SQUARE | SOM_ALPHADITHER_SQUARE;
    } else {
        modes |= SOM_RGBDITHER_NONE | SOM_ALPHADITHER_NONE;
    }

    if (state.depth_test) {
        if (state.is_points) {
            modes |= SOM_Z_SOURCE_PRIM;
        } else {
            modes |= SOM_Z_SOURCE_PIXEL;
        }

        if (state.depth_func == GL_LESS) {
            modes |= SOM_Z_COMPARE;
        }

        if (state.blend) {
            modes |= SOM_Z_TRANSPARENT;
        } else {
            modes |= SOM_Z_OPAQUE | SOM_Z_WRITE;
        }
    }

    if (state.multisample) {
        modes |= SOM_AA_ENABLE | SOM_READ_ENABLE;
        if (state.blend) {
            modes |= SOM_COLOR_ON_COVERAGE | SOM_COVERAGE_DEST_WRAP;
        } else {
            modes |= SOM_ALPHA_USE_CVG | SOM_COVERAGE_DEST_CLAMP;
        }
    } else {
        modes |= SOM_COVERAGE_DEST_SAVE;
    }

    uint32_t blend_cycle = 0;

    if (state.blend) {
        blend_cycle = state.blend_cycle;
    } else if (state.multisample) {
        blend_cycle = BLENDER_CYCLE(IN_RGB, IN_ALPHA, MEMORY_RGB, MEMORY_ALPHA);
    }

    if (state.fog) {
        uint32_t fog_blend = BLENDER_CYCLE(IN_RGB, SHADE_ALPHA, FOG_RGB, INV_MUX_A);

        if (state.blend || state.multisample) {
            blend = blender2(fog_blend, blend_cycle, state.blend);
        } else {
            blend = blender1(fog_blend, true);
        }
    } else {
        blend = blender1(blend_cycle, state.blend);
    }

    if (state.alpha_test && state.alpha_func == GL_GREATER) {
        modes |= SOM_ALPHA_COMPARE;
    }
    
    gl_texture_object_t *tex_obj = gl_get_active_texture();
    if (tex_obj != NULL && tex_obj->is_complete) {
        modes |= SOM_TC_FILTER;
        
        if (!state.is_points) {
            modes |= SOM_TEXTURE_PERSP;
        }

        // We can't use separate modes for minification and magnification, so just use bilinear sampling when at least one of them demands it
        if (tex_obj->mag_filter == GL_LINEAR || 
            tex_obj->min_filter == GL_LINEAR || 
            tex_obj->min_filter == GL_LINEAR_MIPMAP_LINEAR || 
            tex_obj->min_filter == GL_LINEAR_MIPMAP_NEAREST) {
            modes |= SOM_SAMPLE_2X2;
        }

        if (tex_obj->min_filter != GL_LINEAR && tex_obj->min_filter != GL_NEAREST && !state.is_points) {
            modes |= SOM_TEXTURE_LOD;
        }

        if ((tex_obj->min_filter == GL_LINEAR_MIPMAP_LINEAR || tex_obj->min_filter == GL_NEAREST_MIPMAP_LINEAR) && !state.is_points) {
            // Trilinear
            if (state.tex_env_mode == GL_REPLACE) {
                comb = RDPQ_COMBINER2((TEX1, TEX0, LOD_FRAC, TEX0), (TEX1, TEX0, LOD_FRAC, TEX0), (ZERO, ZERO, ZERO, COMBINED), (ZERO, ZERO, ZERO, COMBINED));
            } else if (state.fog) {
                comb = RDPQ_COMBINER2((TEX1, TEX0, LOD_FRAC, TEX0), (TEX1, TEX0, LOD_FRAC, TEX0), (COMBINED, ZERO, SHADE, ZERO), (ZERO, ZERO, ZERO, COMBINED));
            } else {
                comb = RDPQ_COMBINER2((TEX1, TEX0, LOD_FRAC, TEX0), (TEX1, TEX0, LOD_FRAC, TEX0), (COMBINED, ZERO, SHADE, ZERO), (COMBINED, ZERO, SHADE, ZERO));
            }
        } else {
            if (state.tex_env_mode == GL_REPLACE) {
                comb = RDPQ_COMBINER1((ZERO, ZERO, ZERO, TEX0), (ZERO, ZERO, ZERO, TEX0));
            } else if (state.is_points) {
                comb = RDPQ_COMBINER1((TEX0, ZERO, PRIM, ZERO), (TEX0, ZERO, PRIM, ZERO));
            } else if (state.fog) {
                comb = RDPQ_COMBINER1((TEX0, ZERO, SHADE, ZERO), (ZERO, ZERO, ZERO, TEX0));
            } else {
                comb = RDPQ_COMBINER1((TEX0, ZERO, SHADE, ZERO), (TEX0, ZERO, SHADE, ZERO));
            }
        }
    } else {
        if (state.is_points) {
            comb = RDPQ_COMBINER1((ZERO, ZERO, ZERO, PRIM), (ZERO, ZERO, ZERO, PRIM));
        } else if (state.fog) {
            // When fog is enabled, the shade alpha is (ab)used to encode the fog blending factor, so it cannot be used in the color combiner
            // (same above)
            comb = RDPQ_COMBINER1((ZERO, ZERO, ZERO, SHADE), (ZERO, ZERO, ZERO, ONE));
        } else {
            comb = RDPQ_COMBINER1((ZERO, ZERO, ZERO, SHADE), (ZERO, ZERO, ZERO, SHADE));
        }
    }

    rdpq_set_other_modes_raw(modes);
    rdpq_mode_blender(blend);
    rdpq_mode_combiner(comb);

    state.is_rendermode_dirty = false;
}

void glFogi(GLenum pname, GLint param)
{
    switch (pname) {
    case GL_FOG_MODE:
        assertf(param == GL_LINEAR, "Only linear fog is supported!");
        break;
    case GL_FOG_START:
        state.fog_start = param;
        break;
    case GL_FOG_END:
        state.fog_end = param;
        break;
    case GL_FOG_DENSITY:
    case GL_FOG_INDEX:
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
}

void glFogf(GLenum pname, GLfloat param)
{
    switch (pname) {
    case GL_FOG_MODE:
        assertf(param == GL_LINEAR, "Only linear fog is supported!");
        break;
    case GL_FOG_START:
        state.fog_start = param;
        break;
    case GL_FOG_END:
        state.fog_end = param;
        break;
    case GL_FOG_DENSITY:
    case GL_FOG_INDEX:
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
}

void glFogiv(GLenum pname, const GLint *params)
{
    switch (pname) {
    case GL_FOG_COLOR:
        rdpq_set_fog_color(RGBA32(
            MAX(params[0]>>23, 0),
            MAX(params[1]>>23, 0),
            MAX(params[2]>>23, 0),
            0xFF
        ));
        break;
    case GL_FOG_MODE:
    case GL_FOG_START:
    case GL_FOG_END:
    case GL_FOG_DENSITY:
    case GL_FOG_INDEX:
        glFogi(pname, params[0]);
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
}

void glFogfv(GLenum pname, const GLfloat *params)
{
    switch (pname) {
    case GL_FOG_COLOR:
        rdpq_set_fog_color(RGBA32(
            FLOAT_TO_U8(params[0]),
            FLOAT_TO_U8(params[1]),
            FLOAT_TO_U8(params[2]),
            0xFF
        ));
        break;
    case GL_FOG_MODE:
    case GL_FOG_START:
    case GL_FOG_END:
    case GL_FOG_DENSITY:
    case GL_FOG_INDEX:
        glFogf(pname, params[0]);
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
}

void glScissor(GLint left, GLint bottom, GLsizei width, GLsizei height)
{
    if (left < 0 || bottom < 0) {
        gl_set_error(GL_INVALID_VALUE);
        return;
    }

    state.scissor_box[0] = left;
    state.scissor_box[1] = bottom;
    state.scissor_box[2] = width;
    state.scissor_box[3] = height;

    state.is_scissor_dirty = true;
}

void glBlendFunc(GLenum src, GLenum dst)
{
    switch (src) {
    case GL_ZERO:
    case GL_ONE:
    case GL_SRC_ALPHA:
    case GL_ONE_MINUS_SRC_ALPHA: 
    case GL_DST_ALPHA:
        break;
    case GL_DST_COLOR:
    case GL_ONE_MINUS_DST_COLOR: 
    case GL_ONE_MINUS_DST_ALPHA:
    case GL_SRC_ALPHA_SATURATE:
        assertf(0, "Unsupported blend source factor");
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        return;
    }

    switch (dst) {
    case GL_ZERO:
    case GL_ONE:
    case GL_SRC_ALPHA:
    case GL_ONE_MINUS_SRC_ALPHA:
    case GL_DST_ALPHA:
        break;
    case GL_SRC_COLOR:
    case GL_ONE_MINUS_DST_ALPHA:
    case GL_ONE_MINUS_SRC_COLOR:
        assertf(0, "Unsupported blend destination factor");
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        return;
    }

    uint32_t config_index = ((src & 0x7) << 3) | (dst & 0x7);

    uint32_t cycle = blend_configs[config_index];
    assertf(cycle != 0, "Unsupported blend function");

    state.blend_src = src;
    state.blend_dst = dst;
    state.blend_cycle = cycle;
    state.is_rendermode_dirty = true;
}

void glDepthFunc(GLenum func)
{
    switch (func) {
    case GL_NEVER:
    case GL_LESS:
    case GL_ALWAYS:
        GL_SET_STATE(state.depth_func, func, state.is_rendermode_dirty);
        break;
    case GL_EQUAL:
    case GL_LEQUAL:
    case GL_GREATER:
    case GL_NOTEQUAL:
    case GL_GEQUAL:
        assertf(0, "Depth func not supported: %lx", func);
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
}

void glAlphaFunc(GLenum func, GLclampf ref)
{
    switch (func) {
    case GL_NEVER:
    case GL_GREATER:
    case GL_ALWAYS:
        GL_SET_STATE(state.alpha_func, func, state.is_rendermode_dirty);
        state.alpha_ref = ref;
        rdpq_set_blend_color(RGBA32(0, 0, 0, FLOAT_TO_U8(ref)));
        break;
    case GL_EQUAL:
    case GL_LEQUAL:
    case GL_LESS:
    case GL_NOTEQUAL:
    case GL_GEQUAL:
        assertf(0, "Alpha func not supported: %lx", func);
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
}

void glTexEnvi(GLenum target, GLenum pname, GLint param)
{
    if (target != GL_TEXTURE_ENV || pname != GL_TEXTURE_ENV_MODE) {
        gl_set_error(GL_INVALID_ENUM);
        return;
    }

    switch (param) {
    case GL_MODULATE:
    case GL_REPLACE:
        state.tex_env_mode = param;
        state.is_rendermode_dirty = true;
        break;
    case GL_DECAL:
    case GL_BLEND:
        assertf(0, "Unsupported Tex Env mode!");
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
}
void glTexEnvf(GLenum target, GLenum pname, GLfloat param)
{
    glTexEnvi(target, pname, param);
}

void glTexEnviv(GLenum target, GLenum pname, const GLint *params)
{
    if (target != GL_TEXTURE_ENV) {
        gl_set_error(GL_INVALID_ENUM);
        return;
    }

    switch (pname) {
    case GL_TEXTURE_ENV_COLOR:
        state.tex_env_color[0] = I32_TO_FLOAT(params[0]);
        state.tex_env_color[1] = I32_TO_FLOAT(params[1]);
        state.tex_env_color[2] = I32_TO_FLOAT(params[2]);
        state.tex_env_color[3] = I32_TO_FLOAT(params[3]);
        state.is_rendermode_dirty = true;
        break;
    default:
        glTexEnvi(target, pname, params[0]);
        break;
    }
}

void glTexEnvfv(GLenum target, GLenum pname, const GLfloat *params)
{
    if (target != GL_TEXTURE_ENV) {
        gl_set_error(GL_INVALID_ENUM);
        return;
    }

    switch (pname) {
    case GL_TEXTURE_ENV_COLOR:
        state.tex_env_color[0] = params[0];
        state.tex_env_color[1] = params[1];
        state.tex_env_color[2] = params[2];
        state.tex_env_color[3] = params[3];
        state.is_rendermode_dirty = true;
        break;
    default:
        glTexEnvf(target, pname, params[0]);
        break;
    }
}
