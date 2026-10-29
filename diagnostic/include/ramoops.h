#ifndef DIAGNOSTIC_RAMOOPS_H
#define DIAGNOSTIC_RAMOOPS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RAMOOPS_PERSISTENT_RAM_SIG UINT32_C(0x43474244)
#define RAMOOPS_HEADER_SIZE 12U

/*
 * Initialize a mapped Linux persistent-RAM console zone as an empty log.
 * zone_length includes the 12-byte header and must leave at least one data
 * byte. No ECC area is supported; the target ramoops configuration uses
 * ecc=0.
 */
bool ramoops_console_init(volatile void *zone, size_t zone_length);

/*
 * Append bytes using Linux persistent_ram_write() circular-buffer semantics.
 * An append larger than the data capacity retains its final capacity bytes.
 *
 * The valid signature is cleared while data and metadata are changed, then
 * published only after the completed update. Consequently, an interrupted
 * append deliberately makes the whole prior log unreadable rather than
 * exposing internally inconsistent metadata or mixed old/new contents.
 */
bool ramoops_console_append(volatile void *zone,
                            size_t zone_length,
                            const void *data,
                            size_t data_length);

/* Append lowercase hexadecimal text (two characters per input byte). */
bool ramoops_console_append_hex(volatile void *zone,
                                size_t zone_length,
                                const uint8_t *data,
                                size_t data_length);

#endif
