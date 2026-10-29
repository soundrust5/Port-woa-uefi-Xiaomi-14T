#include "ramoops.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    GUARD_SIZE = 19,
    HEADER_SIZE = 12,
    TEST_CAPACITY = 8,
    ZONE_SIZE = HEADER_SIZE + TEST_CAPACITY,
    ALLOCATION_SIZE = GUARD_SIZE + ZONE_SIZE + GUARD_SIZE
};

static uint32_t load_u32_le(const uint8_t *source)
{
    return (uint32_t)source[0] |
           ((uint32_t)source[1] << 8U) |
           ((uint32_t)source[2] << 16U) |
           ((uint32_t)source[3] << 24U);
}

static void store_u32_le(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8U);
    destination[2] = (uint8_t)(value >> 16U);
    destination[3] = (uint8_t)(value >> 24U);
}

static void assert_guards(const uint8_t *allocation)
{
    size_t index;

    for (index = 0U; index < GUARD_SIZE; ++index) {
        assert(allocation[index] == 0xa5U);
        assert(allocation[GUARD_SIZE + ZONE_SIZE + index] == 0xa5U);
    }
}

/* Mirrors persistent_ram_save_old() in the matched Linux ram_core.c. */
static size_t linux_save_old(const uint8_t *zone, uint8_t *output)
{
    uint32_t start = load_u32_le(zone + 4U);
    uint32_t size = load_u32_le(zone + 8U);

    memcpy(output, zone + HEADER_SIZE + start, size - start);
    memcpy(output + size - start, zone + HEADER_SIZE, start);
    return size;
}

static void assert_reader_text(const uint8_t *zone, const char *expected)
{
    uint8_t output[TEST_CAPACITY];
    size_t expected_length = strlen(expected);
    size_t actual_length = linux_save_old(zone, output);

    assert(actual_length == expected_length);
    assert(memcmp(output, expected, expected_length) == 0);
}

static void test_header_layout_and_guards(void)
{
    uint8_t allocation[ALLOCATION_SIZE];
    uint8_t *zone = allocation + GUARD_SIZE;
    size_t index;

    memset(allocation, 0xa5, sizeof(allocation));
    assert(RAMOOPS_HEADER_SIZE == HEADER_SIZE);
    assert(ramoops_console_init(zone, ZONE_SIZE));
    assert(zone[0] == 0x44U);
    assert(zone[1] == 0x42U);
    assert(zone[2] == 0x47U);
    assert(zone[3] == 0x43U);
    assert(load_u32_le(zone) == RAMOOPS_PERSISTENT_RAM_SIG);
    assert(load_u32_le(zone + 4U) == 0U);
    assert(load_u32_le(zone + 8U) == 0U);
    for (index = HEADER_SIZE; index < ZONE_SIZE; ++index) {
        assert(zone[index] == 0xa5U);
    }
    assert_guards(allocation);
}

static void test_wrap_and_linux_reader_order(void)
{
    uint8_t allocation[ALLOCATION_SIZE];
    uint8_t *zone = allocation + GUARD_SIZE;

    memset(allocation, 0xa5, sizeof(allocation));
    assert(ramoops_console_init(zone, ZONE_SIZE));
    assert(ramoops_console_append(zone, ZONE_SIZE, "abcde", 5U));
    assert(load_u32_le(zone + 4U) == 5U);
    assert(load_u32_le(zone + 8U) == 5U);
    assert_reader_text(zone, "abcde");

    assert(ramoops_console_append(zone, ZONE_SIZE, "FGHIJ", 5U));
    assert(load_u32_le(zone + 4U) == 2U);
    assert(load_u32_le(zone + 8U) == TEST_CAPACITY);
    assert_reader_text(zone, "cdeFGHIJ");
    assert_guards(allocation);
}

static void test_oversized_append_keeps_tail(void)
{
    uint8_t allocation[ALLOCATION_SIZE];
    uint8_t *zone = allocation + GUARD_SIZE;

    memset(allocation, 0xa5, sizeof(allocation));
    assert(ramoops_console_init(zone, ZONE_SIZE));
    assert(ramoops_console_append(zone, ZONE_SIZE, "old", 3U));
    assert(ramoops_console_append(zone, ZONE_SIZE, "0123456789ABC", 13U));
    assert(load_u32_le(zone + 4U) == 3U);
    assert(load_u32_le(zone + 8U) == TEST_CAPACITY);
    assert_reader_text(zone, "56789ABC");
    assert_guards(allocation);
}

static void test_hex_append(void)
{
    uint8_t allocation[ALLOCATION_SIZE];
    uint8_t *zone = allocation + GUARD_SIZE;
    const uint8_t bytes[] = {0x00U, 0x12U, 0xabU, 0xffU};

    memset(allocation, 0xa5, sizeof(allocation));
    assert(ramoops_console_init(zone, ZONE_SIZE));
    assert(ramoops_console_append_hex(zone, ZONE_SIZE, bytes, sizeof(bytes)));
    assert_reader_text(zone, "0012abff");
    assert_guards(allocation);
}

static void assert_rejected_unchanged(uint8_t *zone,
                                      size_t zone_length,
                                      const void *data,
                                      size_t data_length)
{
    uint8_t snapshot[ZONE_SIZE];

    memcpy(snapshot, zone, sizeof(snapshot));
    assert(!ramoops_console_append(zone, zone_length, data, data_length));
    assert(memcmp(snapshot, zone, sizeof(snapshot)) == 0);
}

static void test_invalid_parameters_do_not_write(void)
{
    uint8_t allocation[ALLOCATION_SIZE];
    uint8_t snapshot[ALLOCATION_SIZE];
    uint8_t *zone = allocation + GUARD_SIZE;

    memset(allocation, 0xa5, sizeof(allocation));
    memcpy(snapshot, allocation, sizeof(snapshot));
    assert(!ramoops_console_init(NULL, ZONE_SIZE));
    assert(!ramoops_console_init(zone, HEADER_SIZE));
#if SIZE_MAX > INT32_MAX
    assert(!ramoops_console_init(zone, (size_t)INT32_MAX + HEADER_SIZE + 1U));
#endif
    assert(!ramoops_console_init(
        (volatile void *)(uintptr_t)(UINTPTR_MAX - 4U), ZONE_SIZE));
    assert(memcmp(snapshot, allocation, sizeof(snapshot)) == 0);

    assert(ramoops_console_init(zone, ZONE_SIZE));
    assert_rejected_unchanged(zone, ZONE_SIZE, NULL, 1U);
    assert_rejected_unchanged(
        zone, ZONE_SIZE, (const void *)(uintptr_t)(UINTPTR_MAX - 2U), 4U);
    assert_rejected_unchanged(zone, HEADER_SIZE, "x", 1U);

    memcpy(snapshot, allocation, sizeof(snapshot));
    assert(!ramoops_console_append_hex(
        zone, ZONE_SIZE, (const uint8_t *)(uintptr_t)UINTPTR_MAX, 2U));
    assert(memcmp(snapshot, allocation, sizeof(snapshot)) == 0);

    store_u32_le(zone, 0xdeadbeefU);
    assert_rejected_unchanged(zone, ZONE_SIZE, "x", 1U);
    store_u32_le(zone, RAMOOPS_PERSISTENT_RAM_SIG);
    store_u32_le(zone + 4U, 4U);
    store_u32_le(zone + 8U, 3U);
    assert_rejected_unchanged(zone, ZONE_SIZE, "x", 1U);
    store_u32_le(zone + 4U, 0U);
    store_u32_le(zone + 8U, TEST_CAPACITY + 1U);
    assert_rejected_unchanged(zone, ZONE_SIZE, "x", 1U);
    assert_guards(allocation);
}

int main(void)
{
    test_header_layout_and_guards();
    test_wrap_and_linux_reader_order();
    test_oversized_append_keeps_tail();
    test_hex_append();
    test_invalid_parameters_do_not_write();
    puts("ramoops tests passed");
    return 0;
}
