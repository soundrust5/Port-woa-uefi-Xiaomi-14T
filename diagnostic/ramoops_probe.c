/* Exact degas console slice, derived from running kernel ramoops parameters.
 * Intended only before Android runs; never write over its live console.
 * Entry requires EL1/2, little endian, stage-1 MMU and D-cache disabled.
 */
#include "ramoops.h"
#include <stdint.h>

#define CONSOLE_BASE UINT64_C(0x480af000)
#define CONSOLE_LENGTH UINT32_C(0x40000)

static size_t text(char *out, size_t n, const char *s)
{
    while (*s) out[n++] = *s++;
    return n;
}

static size_t hex64(char *out, size_t n, uint64_t value)
{
    const char digits[] = "0123456789abcdef";
    for (unsigned i = 0; i < 16; ++i)
        out[n++] = digits[(value >> (60 - 4 * i)) & 15];
    return n;
}

unsigned diagnostic_ramoops(const uint64_t *record)
{
    char message[384];
    const uint8_t *id = (const uint8_t *)record + 128;
    const char digits[] = "0123456789abcdef";
    volatile uint8_t *zone = (volatile uint8_t *)(uintptr_t)CONSOLE_BASE;
    size_t n = text(message, 0, "DEGAS-RAMOOPS-V1 BUILD=");
    for (unsigned i = 0; i < 32; ++i) {
        message[n++] = digits[id[i] >> 4];
        message[n++] = digits[id[i] & 15];
    }
    n = text(message, n, "\nP0 ENTRY EL=");
    n = hex64(message, n, record[3]);
    n = text(message, n, " SCTLR=");
    n = hex64(message, n, record[6]);
    n = text(message, n, " BASE=");
    n = hex64(message, n, record[14]);
    n = text(message, n, "\nP1 OWN_STACK RECORD_READY\nP2 RAMOOPS_PUBLISH\n");
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < n; ++i) {
        crc ^= (uint8_t)message[i];
        for (unsigned b = 0; b < 8; ++b)
            crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & (0U - (crc & 1U)));
    }
    crc = ~crc;
    n = text(message, n, "CRC32=");
    for (unsigned i = 0; i < 8; ++i)
        message[n++] = digits[(crc >> (28 - 4 * i)) & 15];
    message[n++] = '\n';
    /* Re-entering UEFI after its watchdog must retain the preceding BSP log.
     * This early writer runs before APs; no concurrent console writer exists.
     * Match ramoops_console_append's validity rules, including start==capacity.
     */
    const volatile uint32_t *header = (const volatile uint32_t *)zone;
    const uint32_t capacity = CONSOLE_LENGTH - RAMOOPS_HEADER_SIZE;
    uint32_t old_start = header[1], old_size = header[2];
    if (header[0] != RAMOOPS_PERSISTENT_RAM_SIG ||
        old_size > capacity || old_start > old_size) {
        if (!ramoops_console_init(zone, CONSOLE_LENGTH)) return 1;
        old_start = old_size = 0;
    }
    if (!ramoops_console_append(zone, CONSOLE_LENGTH, message, n)) return 1;
    /* Readback is a CPU-visible publication check, not proof of retention. */
    uint32_t write_start = old_start == capacity ? 0 : old_start;
    uint32_t expected_start = (write_start + (uint32_t)n) % capacity;
    uint32_t expected_size = old_size + (uint32_t)n;
    if (expected_size > capacity) expected_size = capacity;
    if (header[0] != RAMOOPS_PERSISTENT_RAM_SIG ||
        header[1] != expected_start || header[2] != expected_size)
        return 1;
    for (size_t i = 0; i < n; ++i)
        if (zone[RAMOOPS_HEADER_SIZE + (write_start+i)%capacity] !=
            (uint8_t)message[i]) return 1;
    return 0;
}
