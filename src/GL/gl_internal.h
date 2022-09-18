#ifndef __GL_INTERNAL
#define __GL_INTERNAL

#include "GL/gl.h"
#include "GL/gl_integration.h"
#include "obj_map.h"
#include "surface.h"
#include "utils.h"
#include <stdbool.h>
#include <math.h>
#include "gl_constants.h"
#include "rspq.h"
#include "rdpq.h"

#define RADIANS(x) ((x) * M_PI / 180.0f)

#define CLAMP01(x) CLAMP((x), 0, 1)

#define CLAMPF_TO_BOOL(x)  ((x)!=0.0)

#define CLAMPF_TO_U8(x)  ((x)*0xFF)
#define CLAMPF_TO_I8(x)  ((x)*0x7F)
#define CLAMPF_TO_U16(x) ((x)*0xFFFF)
#define CLAMPF_TO_I16(x) ((x)*0x7FFF)
#define CLAMPF_TO_U32(x) ((x)*0xFFFFFFFF)
#define CLAMPF_TO_I32(x) ((x)*0x7FFFFFFF)

#define FLOAT_TO_U8(x)  (CLAMP((x), 0.f, 1.f)*0xFF)

#define U8_TO_FLOAT(x) ((x)/(float)(0xFF))
#define U16_TO_FLOAT(x) ((x)/(float)(0xFFFF))
#define U32_TO_FLOAT(x) ((x)/(float)(0xFFFFFFFF))
#define I8_TO_FLOAT(x) MAX((x)/(float)(0x7F),-1.f)
#define I16_TO_FLOAT(x) MAX((x)/(float)(0x7FFF),-1.f)
#define I32_TO_FLOAT(x) MAX((x)/(float)(0x7FFFFFFF),-1.f)

#define GL_SET_STATE(var, value) ({ \
    typeof(value) _v = (value); \
    bool dirty_flag = _v != var; \
    var = _v; \
    dirty_flag; \
})

extern uint32_t gl_overlay_id;

#define gl_write(cmd_id, ...) rspq_write(gl_overlay_id, cmd_id, ##__VA_ARGS__)

enum {
    GL_CMD_SET_FLAG     = 0x0,
    GL_CMD_SET_BYTE     = 0x1,
    GL_CMD_SET_SHORT    = 0x2,
    GL_CMD_SET_WORD     = 0x3,
    GL_CMD_SET_LONG     = 0x4,
    GL_CMD_UPDATE       = 0x5,
    GL_CMD_BIND_TEXTURE = 0x6,
    GL_CMD_GET_VALUE    = 0x7,
};

typedef enum {
    GL_UPDATE_NONE                  = 0x0,
    GL_UPDATE_DEPTH_TEST            = 0x1,
    GL_UPDATE_DEPTH_MASK            = 0x2,
    GL_UPDATE_BLEND                 = 0x3,
    GL_UPDATE_DITHER                = 0x4,
    GL_UPDATE_POINTS                = 0x5,
    GL_UPDATE_ALPHA_TEST            = 0x6,
    GL_UPDATE_BLEND_CYCLE           = 0x7,
    GL_UPDATE_FOG_CYCLE             = 0x8,
    GL_UPDATE_SCISSOR               = 0x9,
    GL_UPDATE_COMBINER              = 0xA,
    GL_UPDATE_TEXTURE               = 0xB,
    GL_UPDATE_TEXTURE_COMPLETENESS  = 0xC,
    GL_UPDATE_TEXTURE_UPLOAD        = 0xD,
} gl_update_func_t;

enum {
    ATTRIB_VERTEX,
    ATTRIB_COLOR,
    ATTRIB_TEXCOORD,
    ATTRIB_NORMAL,
    ATTRIB_COUNT
};

typedef struct {
    surface_t *color_buffer;
    void *depth_buffer;
} gl_framebuffer_t;

typedef struct {
    GLfloat position[4];
    GLfloat screen_pos[2];
    GLfloat color[4];
    GLfloat texcoord[2];
    GLfloat inverse_w;
    GLfloat depth;
    uint8_t clip;
} gl_vertex_t;

typedef struct {
    GLfloat m[4][4];
} gl_matrix_t;

typedef struct {
    GLfloat scale[3];
    GLfloat offset[3];
} gl_viewport_t;

typedef struct {
    gl_matrix_t *storage;
    int32_t size;
    int32_t cur_depth;
} gl_matrix_stack_t;

typedef struct {
    uint32_t tex_image;
    void *data;
    uint32_t set_load_tile;
    uint32_t load_block;
    uint32_t set_tile;
    uint16_t width;
    uint16_t height;
    uint16_t stride;
    uint16_t internal_format;
    uint16_t tmem_size;
    uint8_t width_log;
    uint8_t height_log;
} __attribute__((aligned(16), packed)) gl_texture_image_t;
_Static_assert(sizeof(gl_texture_image_t) == TEXTURE_IMAGE_SIZE, "Texture image has incorrect size!");
_Static_assert(offsetof(gl_texture_image_t, tex_image) == IMAGE_TEX_IMAGE_OFFSET, "Texture image has incorrect layout!");
_Static_assert(offsetof(gl_texture_image_t, data) == IMAGE_DATA_OFFSET, "Texture image has incorrect layout!");
_Static_assert(offsetof(gl_texture_image_t, set_load_tile) == IMAGE_SET_LOAD_TILE_OFFSET, "Texture image has incorrect layout!");
_Static_assert(offsetof(gl_texture_image_t, load_block) == IMAGE_LOAD_BLOCK_OFFSET, "Texture image has incorrect layout!");
_Static_assert(offsetof(gl_texture_image_t, set_tile) == IMAGE_SET_TILE_OFFSET, "Texture image has incorrect layout!");
_Static_assert(offsetof(gl_texture_image_t, width) == IMAGE_WIDTH_OFFSET, "Texture image has incorrect layout!");
_Static_assert(offsetof(gl_texture_image_t, height) == IMAGE_HEIGHT_OFFSET, "Texture image has incorrect layout!");
_Static_assert(offsetof(gl_texture_image_t, stride) == IMAGE_STRIDE_OFFSET, "Texture image has incorrect layout!");
_Static_assert(offsetof(gl_texture_image_t, internal_format) == IMAGE_INTERNAL_FORMAT_OFFSET, "Texture image has incorrect layout!");
_Static_assert(offsetof(gl_texture_image_t, tmem_size) == IMAGE_TMEM_SIZE_OFFSET, "Texture image has incorrect layout!");
_Static_assert(offsetof(gl_texture_image_t, width_log) == IMAGE_WIDTH_LOG_OFFSET, "Texture image has incorrect layout!");
_Static_assert(offsetof(gl_texture_image_t, height_log) == IMAGE_HEIGHT_LOG_OFFSET, "Texture image has incorrect layout!");

typedef struct {
    gl_texture_image_t levels[MAX_TEXTURE_LEVELS];

    uint32_t flags;
    int32_t priority;
    uint16_t wrap_s;
    uint16_t wrap_t;
    uint16_t min_filter;
    uint16_t mag_filter;

    // These properties are not DMA'd
    uint16_t dimensionality;
    uint16_t padding[7];
} __attribute__((aligned(16), packed)) gl_texture_object_t;
_Static_assert(sizeof(gl_texture_object_t) == TEXTURE_OBJECT_SIZE, "Texture object has incorrect size!");
_Static_assert((1 << TEXTURE_OBJECT_SIZE_LOG) == TEXTURE_OBJECT_SIZE, "Texture object has incorrect size!");
_Static_assert(offsetof(gl_texture_object_t, flags)             == TEXTURE_FLAGS_OFFSET, "Texture object has incorrect layout!");
_Static_assert(offsetof(gl_texture_object_t, priority)          == TEXTURE_PRIORITY_OFFSET, "Texture object has incorrect layout!");
_Static_assert(offsetof(gl_texture_object_t, wrap_s)            == TEXTURE_WRAP_S_OFFSET, "Texture object has incorrect layout!");
_Static_assert(offsetof(gl_texture_object_t, wrap_t)            == TEXTURE_WRAP_T_OFFSET, "Texture object has incorrect layout!");
_Static_assert(offsetof(gl_texture_object_t, min_filter)        == TEXTURE_MIN_FILTER_OFFSET, "Texture object has incorrect layout!");
_Static_assert(offsetof(gl_texture_object_t, mag_filter)        == TEXTURE_MAG_FILTER_OFFSET, "Texture object has incorrect layout!");
_Static_assert(offsetof(gl_texture_object_t, dimensionality)    == TEXTURE_DIMENSIONALITY_OFFSET, "Texture object has incorrect layout!");

typedef struct {
    gl_vertex_t *vertices[CLIPPING_PLANE_COUNT + 3];
    uint32_t count;
} gl_clipping_list_t;

typedef struct {
    GLfloat ambient[4];
    GLfloat diffuse[4];
    GLfloat specular[4];
    GLfloat emissive[4];
    GLfloat shininess;
    GLenum color_target;
} gl_material_t;

typedef struct {
    GLfloat ambient[4];
    GLfloat diffuse[4];
    GLfloat specular[4];
    GLfloat position[4];
    GLfloat direction[3];
    GLfloat spot_exponent;
    GLfloat spot_cutoff;
    GLfloat constant_attenuation;
    GLfloat linear_attenuation;
    GLfloat quadratic_attenuation;
    bool enabled;
} gl_light_t;

typedef struct {
    GLvoid *data;
    uint32_t size;
} gl_storage_t;

typedef struct {
    GLenum usage;
    GLenum access;
    GLvoid *pointer;
    gl_storage_t storage;
    bool mapped;
} gl_buffer_object_t;

typedef struct {
    GLint size;
    GLenum type;
    GLsizei stride;
    const GLvoid *pointer;
    gl_buffer_object_t *binding;
    gl_storage_t tmp_storage;
    bool normalize;
    bool enabled;
} gl_array_t;

typedef void (*read_attrib_func)(GLfloat*,const void*,uint32_t);

typedef struct {
    const GLvoid *pointer;
    read_attrib_func read_func;
    uint16_t offset;
    uint16_t stride;
    uint8_t size;
} gl_attrib_source_t;

typedef struct {
    GLenum mode;
    GLfloat eye_plane[4];
    GLfloat object_plane[4];
    bool enabled;
} gl_tex_gen_t;

typedef struct {
    GLsizei size;
    GLfloat entries[MAX_PIXEL_MAP_SIZE];
} gl_pixel_map_t;

typedef struct {
    int frame_id;
    uint32_t count;
    uint64_t *slots;
} gl_deletion_list_t;

typedef struct {
    gl_open_surf_func_t open_surface;
    gl_close_surf_func_t close_surface;
    gl_framebuffer_t default_framebuffer;
    gl_framebuffer_t *cur_framebuffer;

    GLenum current_error;

    GLenum draw_buffer;

    GLenum primitive_mode;

    GLfloat point_size;
    GLfloat line_width;

    GLclampf clear_color[4];
    GLclampd clear_depth;

    bool cull_face;
    GLenum cull_face_mode;
    GLenum front_face;
    GLenum polygon_mode;

    GLenum depth_func;

    GLenum alpha_func;

    GLfloat fog_start;
    GLfloat fog_end;

    bool depth_test;
    bool alpha_test;

    bool texture_1d;
    bool texture_2d;

    bool lighting;
    bool fog;
    bool color_material;
    bool normalize;

    gl_array_t arrays[ATTRIB_COUNT];

    gl_vertex_t vertex_cache[VERTEX_CACHE_SIZE];
    uint32_t vertex_cache_indices[VERTEX_CACHE_SIZE];
    uint32_t lru_age_table[VERTEX_CACHE_SIZE];
    uint32_t lru_next_age;
    uint8_t next_cache_index;
    bool lock_next_vertex;
    uint8_t locked_vertex;

    uint8_t prim_size;
    uint8_t prim_indices[3];
    uint8_t prim_progress;
    uint32_t prim_counter;
    uint8_t (*prim_func)(void);

    uint16_t prim_tex_width;
    uint16_t prim_tex_height;
    bool prim_texture;
    bool prim_bilinear;
    uint8_t prim_mipmaps;

    GLfloat current_attribs[ATTRIB_COUNT][4];

    gl_attrib_source_t attrib_sources[ATTRIB_COUNT];
    gl_storage_t tmp_index_storage;

    gl_viewport_t current_viewport;

    GLenum matrix_mode;
    gl_matrix_t final_matrix;
    gl_matrix_t *current_matrix;
    bool final_matrix_dirty;

    gl_matrix_t modelview_stack_storage[MODELVIEW_STACK_SIZE];
    gl_matrix_t projection_stack_storage[PROJECTION_STACK_SIZE];
    gl_matrix_t texture_stack_storage[TEXTURE_STACK_SIZE];

    gl_matrix_stack_t modelview_stack;
    gl_matrix_stack_t projection_stack;
    gl_matrix_stack_t texture_stack;
    gl_matrix_stack_t *current_matrix_stack;

    gl_texture_object_t *default_textures;

    gl_texture_object_t *texture_1d_object;
    gl_texture_object_t *texture_2d_object;

    gl_material_t material;
    gl_light_t lights[LIGHT_COUNT];

    GLfloat light_model_ambient[4];
    bool light_model_local_viewer;

    GLenum shade_model;

    gl_tex_gen_t s_gen;
    gl_tex_gen_t t_gen;
    gl_tex_gen_t r_gen;
    gl_tex_gen_t q_gen;

    obj_map_t list_objects;
    GLuint next_list_name;
    GLuint list_base;
    GLuint current_list;

    gl_buffer_object_t *array_buffer;
    gl_buffer_object_t *element_array_buffer;

    bool immediate_active;
/*
    GLboolean unpack_swap_bytes;
    GLboolean unpack_lsb_first;
    GLint unpack_row_length;
    GLint unpack_skip_rows;
    GLint unpack_skip_pixels;
    GLint unpack_alignment;

    GLboolean map_color;
    GLfloat transfer_scale[4];
    GLfloat transfer_bias[4];

    gl_pixel_map_t pixel_maps[4];

    bool transfer_is_noop;

    gl_deletion_list_t deletion_lists[MAX_DELETION_LISTS];
    gl_deletion_list_t *current_deletion_list;

    int frame_id;
    volatile int frames_complete;
*/
} gl_state_t;

typedef struct {
    gl_texture_object_t bound_textures[2];
    uint16_t scissor_rect[4];
    uint32_t flags;
    uint32_t blend_cycle;
    uint32_t fog_color;
    uint32_t texture_ids[2];
    uint32_t uploaded_tex;
    uint16_t fb_size[2];
    uint16_t depth_func;
    uint16_t alpha_func;
    uint16_t blend_src;
    uint16_t blend_dst;
    uint16_t tex_env_mode;
    uint16_t polygon_mode;
    uint16_t prim_type;
    uint8_t alpha_ref;
} __attribute__((aligned(8), packed)) gl_server_state_t;

void gl_matrix_init();
void gl_texture_init();
void gl_lighting_init();
void gl_rendermode_init();
void gl_array_init();
void gl_primitive_init();
void gl_pixel_init();
void gl_list_init();

void gl_texture_close();
void gl_primitive_close();
void gl_list_close();

void gl_set_error(GLenum error);

gl_matrix_t * gl_matrix_stack_get_matrix(gl_matrix_stack_t *stack);

void gl_update_final_matrix();

void gl_matrix_mult(GLfloat *d, const gl_matrix_t *m, const GLfloat *v);
void gl_matrix_mult3x3(GLfloat *d, const gl_matrix_t *m, const GLfloat *v);
void gl_matrix_mult4x2(GLfloat *d, const gl_matrix_t *m, const GLfloat *v);

void gl_perform_lighting(GLfloat *color, const GLfloat *input, const GLfloat *v, const GLfloat *n, const gl_material_t *material);

gl_texture_object_t * gl_get_active_texture();

float dot_product3(const float *a, const float *b);
void gl_normalize(GLfloat *d, const GLfloat *v);

uint32_t gl_get_type_size(GLenum type);

bool gl_storage_alloc(gl_storage_t *storage, uint32_t size);
void gl_storage_free(gl_storage_t *storage);
bool gl_storage_resize(gl_storage_t *storage, uint32_t new_size);

uint64_t * gl_reserve_deletion_slot();

inline bool is_in_heap_memory(void *ptr)
{
    ptr = CachedAddr(ptr);
    return ptr >= HEAP_START_ADDR && ptr < ((void*)KSEG0_START_ADDR + get_memory_size());
}

inline bool is_valid_object_id(GLuint id)
{
    return is_in_heap_memory((void*)id);
}

inline bool gl_tex_is_complete(const gl_texture_object_t *obj)
{
    return obj->flags & TEX_FLAG_COMPLETE;
}

inline uint8_t gl_tex_get_levels(const gl_texture_object_t *obj)
{
    return obj->flags & 0x7;
}

inline void gl_set_flag_raw(gl_update_func_t update_func, uint32_t offset, uint32_t flag, bool value)
{
    gl_write(GL_CMD_SET_FLAG, _carg(update_func, 0x7FF, 13) | _carg(offset, 0xFFC, 0) | _carg(value, 0x1, 0), value ? flag : ~flag);
}

inline void gl_set_flag(gl_update_func_t update_func, uint32_t flag, bool value)
{
    gl_set_flag_raw(update_func, offsetof(gl_server_state_t, flags), flag, value);
}

inline void gl_set_byte(gl_update_func_t update_func, uint32_t offset, uint8_t value)
{
    gl_write(GL_CMD_SET_BYTE, _carg(update_func, 0x7FF, 13) | _carg(offset, 0xFFF, 0), value);
}

inline void gl_set_short(gl_update_func_t update_func, uint32_t offset, uint16_t value)
{
    gl_write(GL_CMD_SET_SHORT, _carg(update_func, 0x7FF, 13) | _carg(offset, 0xFFF, 0), value);
}

inline void gl_set_word(gl_update_func_t update_func, uint32_t offset, uint32_t value)
{
    gl_write(GL_CMD_SET_WORD, _carg(update_func, 0x7FF, 13) | _carg(offset, 0xFFF, 0), value);
}

inline void gl_set_long(gl_update_func_t update_func, uint32_t offset, uint64_t value)
{
    gl_write(GL_CMD_SET_LONG, _carg(update_func, 0x7FF, 13) | _carg(offset, 0xFFF, 0), value >> 32, value & 0xFFFFFFFF);
}

inline void gl_update(gl_update_func_t update_func)
{
    gl_write(GL_CMD_UPDATE, _carg(update_func, 0x7FF, 13));
}

inline void gl_get_value(void *dst, uint32_t offset, uint32_t size)
{
    gl_write(GL_CMD_GET_VALUE, _carg(size-1, 0xFFF, 12) | _carg(offset, 0xFFF, 0), PhysicalAddr(dst));
}

inline void gl_bind_texture(GLenum target, gl_texture_object_t *texture)
{
    uint32_t is_2d = target == GL_TEXTURE_2D ? 1 : 0;
    gl_write(GL_CMD_BIND_TEXTURE, is_2d, PhysicalAddr(texture));
}

inline void gl_update_texture_completeness(uint32_t offset)
{
    gl_write(GL_CMD_UPDATE, _carg(GL_UPDATE_TEXTURE_COMPLETENESS, 0x7FF, 13) | offset);
}

#endif