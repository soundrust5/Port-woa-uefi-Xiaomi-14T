#include "marker.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    WIDTH = 643,
    HEIGHT = 200,
    PITCH = 2600,
    MARKER_WIDTH = 640,
    BAND_HEIGHT = 64,
    BIT_WIDTH = 20,
    MARKER_HEIGHT = 192,
    GUARD_SIZE = 64,
    TAIL_ROWS = 2,
    SURFACE_SIZE = PITCH * (HEIGHT + TAIL_ROWS),
    MEMORY_SIZE = GUARD_SIZE + SURFACE_SIZE + GUARD_SIZE
};

static const uint8_t SENTINEL = 0xa5U;
static const uint32_t MAGENTA = UINT32_C(0xffff00ff);
static const uint32_t CYAN = UINT32_C(0xff00ffff);

static uint32_t expected_marker_color(uint32_t x,
                                      uint32_t y,
                                      const unsigned char build_id[4])
{
    if (y < BAND_HEIGHT) {
        return MAGENTA;
    }
    if (y < BAND_HEIGHT * 2U) {
        return CYAN;
    }

    {
        uint32_t bit_index = x / BIT_WIDTH;
        uint32_t byte_index = bit_index / 8U;
        uint32_t mask = UINT32_C(1) << (7U - bit_index % 8U);

        return ((uint32_t)build_id[byte_index] & mask) != 0U
                   ? MAGENTA
                   : CYAN;
    }
}

static void test_marker_pixels_and_boundaries(void)
{
    _Alignas(4) uint8_t memory[MEMORY_SIZE];
    const unsigned char build_id[4] = {0xa5U, 0x5aU, 0xf0U, 0x0fU};
    FbSurface surface = {
        memory + GUARD_SIZE,
        SURFACE_SIZE,
        WIDTH,
        HEIGHT,
        PITCH
    };
    size_t index;

    memset(memory, SENTINEL, sizeof(memory));
    marker_draw(&surface, build_id);

    for (index = 0U; index < sizeof(memory); ++index) {
        uint8_t expected = SENTINEL;

        if (index >= GUARD_SIZE && index < GUARD_SIZE + SURFACE_SIZE) {
            size_t offset = index - GUARD_SIZE;
            uint32_t y = (uint32_t)(offset / PITCH);
            uint32_t row_byte = (uint32_t)(offset % PITCH);
            uint32_t x = row_byte / 4U;

            if (y < MARKER_HEIGHT && x < MARKER_WIDTH &&
                row_byte < WIDTH * 4U) {
                uint32_t color = expected_marker_color(x, y, build_id);
                expected = (uint8_t)(color >> ((row_byte & 3U) * 8U));
            }
        }

        assert(memory[index] == expected);
    }
}

static void test_null_inputs_do_not_write(void)
{
    _Alignas(4) uint8_t memory[MEMORY_SIZE];
    const unsigned char build_id[4] = {0U, 0U, 0U, 0U};
    FbSurface surface = {
        memory + GUARD_SIZE,
        SURFACE_SIZE,
        WIDTH,
        HEIGHT,
        PITCH
    };
    uint8_t snapshot[MEMORY_SIZE];

    memset(memory, SENTINEL, sizeof(memory));
    memcpy(snapshot, memory, sizeof(memory));
    marker_draw(NULL, build_id);
    assert(memcmp(memory, snapshot, sizeof(memory)) == 0);

    marker_draw(&surface, NULL);
    assert(memcmp(memory, snapshot, sizeof(memory)) == 0);
}

int main(void)
{
    test_marker_pixels_and_boundaries();
    test_null_inputs_do_not_write();
    puts("marker tests passed");
    return 0;
}
