#include "framebuffer.h"
#include "degas_display.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    DEGAS_WIDTH = 1220,
    DEGAS_HEIGHT = 2712,
    DEGAS_PITCH = 4992,
    DEGAS_FRAME_SIZE = 0x00cf3000,
    GUARD_SIZE = 64
};

static void assert_rejected_unchanged(const FbSurface *surface,
                                      uint32_t x,
                                      uint32_t y,
                                      uint32_t width,
                                      uint32_t height,
                                      uint32_t argb,
                                      const uint8_t *memory,
                                      size_t memory_size,
                                      uint8_t *snapshot)
{
    memcpy(snapshot, memory, memory_size);
    assert(!fb_fill_rect(surface, x, y, width, height, argb));
    assert(memcmp(snapshot, memory, memory_size) == 0);
}

static void test_degas_surface_and_rejections(void)
{
    const size_t allocation_size =
        GUARD_SIZE + (size_t)DEGAS_FRAME_SIZE + GUARD_SIZE;
    uint8_t *memory = malloc(allocation_size);
    uint8_t *snapshot = malloc(allocation_size);
    uint8_t *base;
    FbSurface surface;
    FbSurface invalid;
    size_t index;

    assert(memory != NULL);
    assert(snapshot != NULL);
    base = memory + GUARD_SIZE;
    assert(((uintptr_t)base & 3U) == 0U);

    memset(memory, 0xa5, allocation_size);
    surface = degas_fb_surface(base);
    assert(surface.size == DEGAS_FRAME_SIZE);
    assert(surface.width == DEGAS_WIDTH);
    assert(surface.height == DEGAS_HEIGHT);
    assert(surface.pitch == DEGAS_PITCH);

    assert(fb_fill_rect(&surface, 1217U, 2710U, 3U, 2U, 0x7f123456U));

    for (index = 0U; index < allocation_size; ++index) {
        bool is_pixel_byte = false;
        uint8_t expected = 0xa5U;

        if (index >= GUARD_SIZE &&
            index < GUARD_SIZE + (size_t)DEGAS_FRAME_SIZE) {
            size_t frame_offset = index - GUARD_SIZE;
            size_t row = frame_offset / DEGAS_PITCH;
            size_t column_byte = frame_offset % DEGAS_PITCH;

            if (row >= 2710U && row < 2712U && column_byte >= 1217U * 4U &&
                column_byte < 1220U * 4U) {
                static const uint8_t bgra[4] = {0x56U, 0x34U, 0x12U, 0x7fU};
                expected = bgra[column_byte & 3U];
                is_pixel_byte = true;
            }
        }

        assert(memory[index] == (is_pixel_byte ? expected : 0xa5U));
    }

    assert_rejected_unchanged(&surface, 0U, 0U, 0U, 1U, 0xffffffffU,
                              memory, allocation_size, snapshot);
    assert_rejected_unchanged(&surface, 0U, 0U, 1U, 0U, 0xffffffffU,
                              memory, allocation_size, snapshot);
    assert_rejected_unchanged(&surface, DEGAS_WIDTH, 0U, 1U, 1U,
                              0xffffffffU, memory, allocation_size, snapshot);
    assert_rejected_unchanged(&surface, 0U, DEGAS_HEIGHT, 1U, 1U,
                              0xffffffffU, memory, allocation_size, snapshot);
    assert_rejected_unchanged(&surface, 1219U, 0U, 2U, 1U, 0xffffffffU,
                              memory, allocation_size, snapshot);
    assert_rejected_unchanged(&surface, 0U, 2711U, 1U, 2U, 0xffffffffU,
                              memory, allocation_size, snapshot);
    assert_rejected_unchanged(&surface, UINT32_MAX, 0U, UINT32_MAX, 1U,
                              0xffffffffU, memory, allocation_size, snapshot);
    assert_rejected_unchanged(&surface, 0U, UINT32_MAX, 1U, UINT32_MAX,
                              0xffffffffU, memory, allocation_size, snapshot);

    invalid = surface;
    invalid.base = NULL;
    assert_rejected_unchanged(&invalid, 0U, 0U, 1U, 1U, 0xffffffffU,
                              memory, allocation_size, snapshot);

    invalid = surface;
    invalid.base = base + 1U;
    invalid.size--;
    assert_rejected_unchanged(&invalid, 0U, 0U, 1U, 1U, 0xffffffffU,
                              memory, allocation_size, snapshot);

    invalid = surface;
    invalid.width = 0U;
    assert_rejected_unchanged(&invalid, 0U, 0U, 1U, 1U, 0xffffffffU,
                              memory, allocation_size, snapshot);

    invalid = surface;
    invalid.height = 0U;
    assert_rejected_unchanged(&invalid, 0U, 0U, 1U, 1U, 0xffffffffU,
                              memory, allocation_size, snapshot);

    invalid = surface;
    invalid.pitch = DEGAS_WIDTH * 4U - 4U;
    assert_rejected_unchanged(&invalid, 0U, 0U, 1U, 1U, 0xffffffffU,
                              memory, allocation_size, snapshot);

    invalid = surface;
    invalid.pitch = DEGAS_PITCH + 2U;
    assert_rejected_unchanged(&invalid, 0U, 0U, 1U, 1U, 0xffffffffU,
                              memory, allocation_size, snapshot);

    invalid = surface;
    invalid.size = (uint64_t)(DEGAS_HEIGHT - 1U) * DEGAS_PITCH +
                   DEGAS_WIDTH * 4U - 1U;
    assert_rejected_unchanged(&invalid, 0U, 0U, 1U, 1U, 0xffffffffU,
                              memory, allocation_size, snapshot);

    invalid = surface;
    invalid.size = UINT64_MAX;
    invalid.width = UINT32_MAX;
    invalid.height = UINT32_MAX;
    invalid.pitch = UINT32_MAX - 3U;
    assert_rejected_unchanged(&invalid, 0U, 0U, 1U, 1U, 0xffffffffU,
                              memory, allocation_size, snapshot);

    assert(!fb_fill_rect(NULL, 0U, 0U, 1U, 1U, 0xffffffffU));

    free(snapshot);
    free(memory);
}

static uint32_t next_random(uint32_t *state)
{
    *state = *state * 1664525U + 1013904223U;
    return *state;
}

static void test_random_rectangles(void)
{
    enum {
        WIDTH = 37,
        HEIGHT = 29,
        PITCH = 160,
        TAIL_ROWS = 2,
        MEMORY_SIZE = GUARD_SIZE + PITCH * (HEIGHT + TAIL_ROWS) + GUARD_SIZE
    };
    _Alignas(4) uint8_t memory[MEMORY_SIZE];
    FbSurface surface = {
        memory + GUARD_SIZE,
        PITCH * (HEIGHT + TAIL_ROWS),
        WIDTH,
        HEIGHT,
        PITCH
    };
    uint32_t random_state = 0x4d554642U;
    unsigned int iteration;

    for (iteration = 0U; iteration < 64U; ++iteration) {
        uint32_t x = next_random(&random_state) % WIDTH;
        uint32_t y = next_random(&random_state) % HEIGHT;
        uint32_t width = 1U + next_random(&random_state) % (WIDTH - x);
        uint32_t height = 1U + next_random(&random_state) % (HEIGHT - y);
        uint32_t argb = next_random(&random_state);
        size_t index;

        memset(memory, 0xcc, sizeof(memory));
        assert(fb_fill_rect(&surface, x, y, width, height, argb));

        for (index = 0U; index < sizeof(memory); ++index) {
            uint8_t expected = 0xccU;

            if (index >= GUARD_SIZE && index < GUARD_SIZE + surface.size) {
                size_t offset = index - GUARD_SIZE;
                uint32_t row = (uint32_t)(offset / PITCH);
                uint32_t column_byte = (uint32_t)(offset % PITCH);
                uint32_t column = column_byte / 4U;

                if (row >= y && row < y + height && column_byte < WIDTH * 4U &&
                    column >= x && column < x + width) {
                    expected = (uint8_t)(argb >> ((column_byte & 3U) * 8U));
                }
            }

            assert(memory[index] == expected);
        }
    }
}

int main(void)
{
    test_degas_surface_and_rejections();
    test_random_rectangles();
    puts("framebuffer tests passed");
    return 0;
}
