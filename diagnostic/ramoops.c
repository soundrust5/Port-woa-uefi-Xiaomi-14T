#include "ramoops.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

enum {
    RAMOOPS_SIG_OFFSET = 0,
    RAMOOPS_START_OFFSET = 4,
    RAMOOPS_SIZE_OFFSET = 8,
    RAMOOPS_DATA_OFFSET = RAMOOPS_HEADER_SIZE
};

typedef struct {
    volatile uint8_t *memory;
    size_t capacity;
    size_t start;
    size_t size;
} RamoopsZone;

static void ramoops_barrier(void)
{
#if defined(__aarch64__)
    __asm__ volatile("dsb sy" ::: "memory");
#else
    __asm__ volatile("" ::: "memory");
#endif
}

static uint32_t load_u32_le(const volatile uint8_t *source)
{
    return (uint32_t)source[0] |
           ((uint32_t)source[1] << 8U) |
           ((uint32_t)source[2] << 16U) |
           ((uint32_t)source[3] << 24U);
}

static void store_u32_le(volatile uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8U);
    destination[2] = (uint8_t)(value >> 16U);
    destination[3] = (uint8_t)(value >> 24U);
}

static bool zone_dimensions(volatile void *zone,
                            size_t zone_length,
                            volatile uint8_t **memory,
                            size_t *capacity)
{
    uintptr_t zone_address;

    if (zone == NULL || zone_length <= RAMOOPS_HEADER_SIZE) {
        return false;
    }

    zone_address = (uintptr_t)zone;
    if (zone_length - 1U > UINTPTR_MAX - zone_address) {
        return false;
    }

    *capacity = zone_length - RAMOOPS_HEADER_SIZE;
    if (*capacity > (size_t)INT32_MAX) {
        return false;
    }

    *memory = (volatile uint8_t *)zone;
    return true;
}

static bool readable_range(const void *data, size_t data_length)
{
    uintptr_t data_address;

    if (data_length == 0U) {
        return true;
    }
    if (data == NULL) {
        return false;
    }

    data_address = (uintptr_t)data;
    return data_length - 1U <= UINTPTR_MAX - data_address;
}

static bool load_valid_zone(volatile void *zone,
                            size_t zone_length,
                            RamoopsZone *result)
{
    uint32_t start;
    uint32_t size;

    if (!zone_dimensions(zone, zone_length, &result->memory,
                         &result->capacity)) {
        return false;
    }

    if (load_u32_le(result->memory + RAMOOPS_SIG_OFFSET) !=
        RAMOOPS_PERSISTENT_RAM_SIG) {
        return false;
    }

    start = load_u32_le(result->memory + RAMOOPS_START_OFFSET);
    size = load_u32_le(result->memory + RAMOOPS_SIZE_OFFSET);
    if ((size_t)size > result->capacity || start > size) {
        return false;
    }

    result->start = start;
    result->size = size;
    return true;
}

static size_t advanced_start(size_t start, size_t count, size_t capacity)
{
    if (start == capacity) {
        start = 0U;
    }

    if (count >= capacity - start) {
        return count - (capacity - start);
    }

    return start + count;
}

static size_t grown_size(size_t size, size_t count, size_t capacity)
{
    if (count >= capacity - size) {
        return capacity;
    }

    return size + count;
}

static void begin_update(RamoopsZone *zone)
{
    store_u32_le(zone->memory + RAMOOPS_SIG_OFFSET, 0U);
    ramoops_barrier();
}

static void finish_update(RamoopsZone *zone,
                          size_t new_start,
                          size_t new_size)
{
    store_u32_le(zone->memory + RAMOOPS_START_OFFSET, (uint32_t)new_start);
    store_u32_le(zone->memory + RAMOOPS_SIZE_OFFSET, (uint32_t)new_size);
    ramoops_barrier();
    store_u32_le(zone->memory + RAMOOPS_SIG_OFFSET,
                 RAMOOPS_PERSISTENT_RAM_SIG);
    ramoops_barrier();
}

static void store_ring_byte(const RamoopsZone *zone,
                            size_t write_start,
                            size_t index,
                            uint8_t value)
{
    size_t position;

    if (index >= zone->capacity - write_start) {
        position = index - (zone->capacity - write_start);
    } else {
        position = write_start + index;
    }
    zone->memory[RAMOOPS_DATA_OFFSET + position] = value;
}

bool ramoops_console_init(volatile void *zone, size_t zone_length)
{
    volatile uint8_t *memory;
    size_t capacity;

    if (!zone_dimensions(zone, zone_length, &memory, &capacity)) {
        return false;
    }

    (void)capacity;
    store_u32_le(memory + RAMOOPS_SIG_OFFSET, 0U);
    ramoops_barrier();
    store_u32_le(memory + RAMOOPS_START_OFFSET, 0U);
    store_u32_le(memory + RAMOOPS_SIZE_OFFSET, 0U);
    ramoops_barrier();
    store_u32_le(memory + RAMOOPS_SIG_OFFSET, RAMOOPS_PERSISTENT_RAM_SIG);
    ramoops_barrier();
    return true;
}

bool ramoops_console_append(volatile void *zone,
                            size_t zone_length,
                            const void *data,
                            size_t data_length)
{
    RamoopsZone target;
    const uint8_t *source;
    size_t count;
    size_t index;
    size_t new_start;
    size_t new_size;

    if (!readable_range(data, data_length) ||
        !load_valid_zone(zone, zone_length, &target)) {
        return false;
    }
    if (data_length == 0U) {
        return true;
    }

    source = (const uint8_t *)data;
    count = data_length;
    if (count > target.capacity) {
        source += count - target.capacity;
        count = target.capacity;
    }

    new_start = advanced_start(target.start, count, target.capacity);
    new_size = grown_size(target.size, count, target.capacity);
    begin_update(&target);
    for (index = 0U; index < count; ++index) {
        store_ring_byte(&target, target.start, index, source[index]);
    }
    finish_update(&target, new_start, new_size);
    return true;
}

static uint8_t hex_digit(uint8_t value)
{
    if (value < 10U) {
        return (uint8_t)('0' + value);
    }
    return (uint8_t)('a' + value - 10U);
}

bool ramoops_console_append_hex(volatile void *zone,
                                size_t zone_length,
                                const uint8_t *data,
                                size_t data_length)
{
    RamoopsZone target;
    size_t text_length;
    size_t first_text_index;
    size_t count;
    size_t index;
    size_t new_start;
    size_t new_size;

    if (!readable_range(data, data_length) ||
        data_length > SIZE_MAX / 2U ||
        !load_valid_zone(zone, zone_length, &target)) {
        return false;
    }
    if (data_length == 0U) {
        return true;
    }

    text_length = data_length * 2U;
    count = text_length;
    first_text_index = 0U;
    if (count > target.capacity) {
        first_text_index = count - target.capacity;
        count = target.capacity;
    }

    new_start = advanced_start(target.start, count, target.capacity);
    new_size = grown_size(target.size, count, target.capacity);
    begin_update(&target);
    for (index = 0U; index < count; ++index) {
        size_t text_index = first_text_index + index;
        uint8_t byte = data[text_index / 2U];
        uint8_t nibble = (text_index & 1U) == 0U
                             ? (uint8_t)(byte >> 4U)
                             : (uint8_t)(byte & 0x0fU);

        store_ring_byte(&target, target.start, index, hex_digit(nibble));
    }
    finish_update(&target, new_start, new_size);
    return true;
}
