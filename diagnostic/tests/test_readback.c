#include "marker.h"
#include "readback.h"

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
    MARKER_HEIGHT = 192,
    GUARD_SIZE = 64,
    TAIL_ROWS = 2,
    SURFACE_SIZE = PITCH * (HEIGHT + TAIL_ROWS),
    MEMORY_SIZE = GUARD_SIZE + SURFACE_SIZE + GUARD_SIZE
};

static void corrupt_pixel(uint8_t *base, uint32_t x, uint32_t y)
{
    base[(size_t)y * PITCH + (size_t)x * 4U] ^= 1U;
}

static void test_good_marker_and_read_only(void)
{
    _Alignas(4) uint8_t memory[MEMORY_SIZE];
    uint8_t snapshot[MEMORY_SIZE];
    const unsigned char build_id[4] = {0xa5U, 0x5aU, 0xf0U, 0x0fU};
    FbSurface surface = {
        memory + GUARD_SIZE, SURFACE_SIZE, WIDTH, HEIGHT, PITCH
    };

    memset(memory, 0xa5, sizeof(memory));
    marker_draw(&surface, build_id);
    memcpy(snapshot, memory, sizeof(memory));

    assert(marker_verify(&surface, build_id));
    assert(memcmp(memory, snapshot, sizeof(memory)) == 0);
    assert(!marker_verify(&surface, NULL));
    assert(memcmp(memory, snapshot, sizeof(memory)) == 0);
}

static void test_pixel_corruption(void)
{
    static const uint32_t points[][2] = {
        {0U, 0U},
        {MARKER_WIDTH - 1U, BAND_HEIGHT - 1U},
        {0U, BAND_HEIGHT},
        {MARKER_WIDTH - 1U, BAND_HEIGHT * 2U - 1U},
        {0U, BAND_HEIGHT * 2U},
        {MARKER_WIDTH - 1U, MARKER_HEIGHT - 1U}
    };
    _Alignas(4) uint8_t memory[MEMORY_SIZE];
    const unsigned char build_id[4] = {0x96U, 0x69U, 0xc3U, 0x3cU};
    FbSurface surface = {
        memory + GUARD_SIZE, SURFACE_SIZE, WIDTH, HEIGHT, PITCH
    };
    size_t point;

    for (point = 0U; point < sizeof(points) / sizeof(points[0]); ++point) {
        memset(memory, 0x5a, sizeof(memory));
        marker_draw(&surface, build_id);
        assert(marker_verify(&surface, build_id));
        corrupt_pixel((uint8_t *)surface.base,
                      points[point][0],
                      points[point][1]);
        assert(!marker_verify(&surface, build_id));
    }
}

static void test_padding_is_ignored(void)
{
    _Alignas(4) uint8_t memory[MEMORY_SIZE];
    uint8_t snapshot[MEMORY_SIZE];
    const unsigned char build_id[4] = {0xdeU, 0xadU, 0xbeU, 0xefU};
    FbSurface surface = {
        memory + GUARD_SIZE, SURFACE_SIZE, WIDTH, HEIGHT, PITCH
    };
    uint32_t y;

    memset(memory, 0xcc, sizeof(memory));
    marker_draw(&surface, build_id);
    for (y = 0U; y < MARKER_HEIGHT; ++y) {
        uint8_t *padding = (uint8_t *)surface.base +
                           (size_t)y * PITCH + WIDTH * 4U;
        padding[0] ^= 0xffU;
    }
    memcpy(snapshot, memory, sizeof(memory));

    assert(marker_verify(&surface, build_id));
    assert(memcmp(memory, snapshot, sizeof(memory)) == 0);
}

static void test_malformed_surfaces_reject_without_read(void)
{
    const unsigned char build_id[4] = {0U, 0U, 0U, 0U};
    FbSurface invalid;

    assert(!marker_verify(NULL, build_id));

    invalid.base = NULL;
    invalid.size = SURFACE_SIZE;
    invalid.width = WIDTH;
    invalid.height = HEIGHT;
    invalid.pitch = PITCH;
    assert(!marker_verify(&invalid, build_id));

    invalid.base = (volatile uint8_t *)(uintptr_t)4U;
    invalid.size = (uint64_t)(MARKER_HEIGHT - 1U) * PITCH +
                   MARKER_WIDTH * 4U - 1U;
    invalid.width = MARKER_WIDTH;
    invalid.height = MARKER_HEIGHT;
    invalid.pitch = PITCH;
    assert(!marker_verify(&invalid, build_id));

    invalid.base = (volatile uint8_t *)(uintptr_t)4U;
    invalid.size = UINT64_MAX;
    invalid.width = UINT32_MAX;
    invalid.height = UINT32_MAX;
    invalid.pitch = UINT32_MAX - 3U;
    assert(!marker_verify(&invalid, build_id));

    invalid.base = (volatile uint8_t *)(uintptr_t)5U;
    invalid.size = SURFACE_SIZE;
    invalid.width = WIDTH;
    invalid.height = HEIGHT;
    invalid.pitch = PITCH;
    assert(!marker_verify(&invalid, build_id));

    invalid.base = (volatile uint8_t *)(uintptr_t)4U;
    invalid.size = SURFACE_SIZE;
    invalid.width = MARKER_WIDTH - 1U;
    invalid.height = MARKER_HEIGHT;
    invalid.pitch = PITCH;
    assert(!marker_verify(&invalid, build_id));
}

int main(void)
{
    test_good_marker_and_read_only();
    test_pixel_corruption();
    test_padding_is_ignored();
    test_malformed_surfaces_reject_without_read();
    puts("readback tests passed");
    return 0;
}
