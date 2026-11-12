/*
 * SurfaceRT_OS - SD/MMC response bit extraction
 *
 * Lives in a header so the host tests can catch the classic mistake of shifting
 * a 64 bit value by 64 or more when decoding the 128 bit CSD.
 */
#ifndef SRTOS_SD_BITS_H
#define SRTOS_SD_BITS_H

#include <srtos/types.h>

/* Bits [start..end] of a 128 bit response, bit 0 = LSB of resp[0]. */
static inline u32 sd_extract_bits(const u32 *resp, int start, int end)
{
    u32 result = 0;
    int bit, out = 0;

    if (start < 0 || end < start || end > 127 || (end - start) >= 32) {
        return 0;
    }
    for (bit = start; bit <= end; bit++) {
        u32 word = resp[bit / 32];
        if ((word >> (bit % 32)) & 1u) {
            result |= (1u << out);
        }
        out++;
    }
    return result;
}

#endif /* SRTOS_SD_BITS_H */
