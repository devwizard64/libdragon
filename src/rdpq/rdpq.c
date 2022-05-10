#include "rdpq.h"
#include "rdpq_block.h"
#include "rdpq_constants.h"
#include "rspq.h"
#include "rspq/rspq_commands.h"
#include "rdp_commands.h"
#include <string.h>

#define RDPQ_MAX_COMMAND_SIZE 44
#define RDPQ_BLOCK_MIN_SIZE   64
#define RDPQ_BLOCK_MAX_SIZE   4192

#define RDPQ_OVL_ID (0xC << 28)

static void rdpq_assert_handler(rsp_snapshot_t *state, uint16_t assert_code);

DEFINE_RSP_UCODE(rsp_rdpq, 
    .assert_handler=rdpq_assert_handler);

typedef struct rdpq_state_s {
    uint64_t other_modes;
    uint64_t scissor_rect;
    uint32_t fill_color;
    uint8_t target_bitdepth;
} rdpq_state_t;

typedef struct rdpq_block_s {
    rdpq_block_t *next;
    uint32_t padding;
    uint32_t cmds[];
} rdpq_block_t;

bool __rdpq_inited = false;

volatile uint32_t *rdpq_block_pointer;
volatile uint32_t *rdpq_block_sentinel;

rdpq_block_t *rdpq_block;
static int rdpq_block_size;

static volatile uint32_t *last_rdp_cmd;

void rdpq_init()
{
    rdpq_state_t *rdpq_state = UncachedAddr(rspq_overlay_get_state(&rsp_rdpq));

    memset(rdpq_state, 0, sizeof(rdpq_state_t));
    rdpq_state->other_modes = ((uint64_t)RDPQ_OVL_ID << 32) + ((uint64_t)RDPQ_CMD_SET_OTHER_MODES << 56);

    // The (1 << 12) is to prevent underflow in case set other modes is called before any set scissor command.
    // Depending on the cycle mode, 1 subpixel is subtracted from the right edge of the scissor rect.
    rdpq_state->scissor_rect = (((uint64_t)RDPQ_OVL_ID << 32) + ((uint64_t)RDPQ_CMD_SET_SCISSOR_EX << 56)) | (1 << 12);

    rspq_init();
    rspq_overlay_register_static(&rsp_rdpq, RDPQ_OVL_ID);

    rdpq_block = NULL;

    __rdpq_inited = true;
}

void rdpq_close()
{
    rspq_overlay_unregister(RDPQ_OVL_ID);
    __rdpq_inited = false;
}

void rdpq_fence(void)
{
    rdpq_sync_full();
    rspq_int_write(RSPQ_CMD_RDP_WAIT_IDLE);
}

static void rdpq_assert_handler(rsp_snapshot_t *state, uint16_t assert_code)
{
    switch (assert_code)
    {
    case RDPQ_ASSERT_FLIP_COPY:
        printf("TextureRectangleFlip cannot be used in copy mode\n");
        break;
    
    default:
        printf("Unknown assert\n");
        break;
    }
}

void rdpq_reset_buffer()
{
    last_rdp_cmd = NULL;
}

void rdpq_block_flush(uint32_t *start, uint32_t *end)
{
    assertf(((uint32_t)start & 0x7) == 0, "start not aligned to 8 bytes: %lx", (uint32_t)start);
    assertf(((uint32_t)end & 0x7) == 0, "end not aligned to 8 bytes: %lx", (uint32_t)end);

    extern void rspq_rdp(uint32_t start, uint32_t end);

    uint32_t phys_start = PhysicalAddr(start);
    uint32_t phys_end = PhysicalAddr(end);

    // FIXME: Updating the previous command won't work across buffer switches
    uint32_t diff = rdpq_block_pointer - last_rdp_cmd;
    if (diff == 2 && (*last_rdp_cmd&0xFFFFFF) == phys_start) {
        // Update the previous command
        *last_rdp_cmd = (RSPQ_CMD_RDP<<24) | phys_end;
    } else {
        // Put a command in the regular RSP queue that will submit the last buffer of RDP commands.
        last_rdp_cmd = rdpq_block_pointer;
        rspq_write(0, RSPQ_CMD_RDP, phys_end, phys_start);
    }
}

void rdpq_block_switch_buffer(uint32_t *new, uint32_t size)
{
    assert(size >= RDPQ_MAX_COMMAND_SIZE);

    rdpq_block_pointer = new;
    rdpq_block_sentinel = new + size - RDPQ_MAX_COMMAND_SIZE;
}

void rdpq_block_next_buffer()
{
    // Allocate next chunk (double the size of the current one).
    // We use doubling here to reduce overheads for large blocks
    // and at the same time start small.
    if (rdpq_block_size < RDPQ_BLOCK_MAX_SIZE) rdpq_block_size *= 2;
    rdpq_block->next = malloc_uncached(sizeof(rdpq_block_t) + rdpq_block_size*sizeof(uint32_t));
    rdpq_block = rdpq_block->next;

    // Switch to new buffer
    rdpq_block_switch_buffer(rdpq_block->cmds, rdpq_block_size);
}

rdpq_block_t* rdpq_block_begin()
{
    rdpq_block_size = RDPQ_BLOCK_MIN_SIZE;
    rdpq_block = malloc_uncached(sizeof(rdpq_block_t) + rdpq_block_size*sizeof(uint32_t));
    rdpq_block->next = NULL;
    rdpq_block_switch_buffer(rdpq_block->cmds, rdpq_block_size);
    rdpq_reset_buffer();
    return rdpq_block;
}

void rdpq_block_end()
{
    rdpq_block = NULL;
}

void rdpq_block_free(rdpq_block_t *block)
{
    while (block) {
        void *b = block;
        block = block->next;
        free_uncached(b);
    }
}

/// @cond

#define _rdpq_write_arg(arg) \
    *ptr++ = (arg);

/// @endcond

#define rdpq_dynamic_write(cmd_id, ...) ({ \
    rspq_write(RDPQ_OVL_ID, (cmd_id), ##__VA_ARGS__); \
})

#define rdpq_static_write(cmd_id, arg0, ...) ({ \
    volatile uint32_t *ptr = rdpq_block_pointer; \
    *ptr++ = (RDPQ_OVL_ID + ((cmd_id)<<24)) | (arg0); \
    __CALL_FOREACH(_rdpq_write_arg, ##__VA_ARGS__); \
    rdpq_block_flush((uint32_t*)rdpq_block_pointer, (uint32_t*)ptr); \
    rdpq_block_pointer = ptr; \
    if (__builtin_expect(rdpq_block_pointer > rdpq_block_sentinel, 0)) \
        rdpq_block_next_buffer(); \
})

static inline bool in_block(void) {
    return rdpq_block != NULL;
}

#define rdpq_fixup_write(cmd_id_dyn, cmd_id_fix, arg0, ...) ({ \
    if (in_block()) { \
        rdpq_static_write(cmd_id_fix, arg0, ##__VA_ARGS__); \
    } else { \
        rdpq_dynamic_write(cmd_id_dyn, arg0, ##__VA_ARGS__); \
    } \
})

#define rdpq_write(cmd_id, arg0, ...) rdpq_fixup_write(cmd_id, cmd_id, arg0, ##__VA_ARGS__)

__attribute__((noinline))
void __rdpq_write8(uint32_t cmd_id, uint32_t arg0, uint32_t arg1)
{
    rdpq_write(cmd_id, arg0, arg1);
}

__attribute__((noinline))
void __rdpq_write16(uint32_t cmd_id, uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3)
{
    rdpq_write(cmd_id, arg0, arg1, arg2, arg3);
}

__attribute__((noinline))
void __rdpq_fixup_write8(uint32_t cmd_id_dyn, uint32_t cmd_id_fix, uint32_t arg0, uint32_t arg1)
{
    rdpq_fixup_write(cmd_id_dyn, cmd_id_fix, arg0, arg1);
}

__attribute__((noinline))
void __rdpq_fill_triangle(uint32_t w0, uint32_t w1, uint32_t w2, uint32_t w3, uint32_t w4, uint32_t w5, uint32_t w6, uint32_t w7)
{
    rdpq_write(RDPQ_CMD_TRI, w0, w1, w2, w3, w4, w5, w6, w7);
}

__attribute__((noinline))
void __rdpq_modify_other_modes(uint32_t w0, uint32_t w1, uint32_t w2)
{
    rdpq_dynamic_write(RDPQ_CMD_MODIFY_OTHER_MODES, w0, w1, w2);
}

/* Extern inline instantiations. */
extern inline void rdpq_set_fill_color(color_t color);
extern inline void rdpq_set_color_image(void* dram_ptr, uint32_t format, uint32_t size, uint32_t width, uint32_t height, uint32_t stride);
extern inline void rdpq_sync_tile(void);
extern inline void rdpq_sync_load(void);
extern inline void rdpq_sync_pipe(void);
extern inline void rdpq_sync_full(void);