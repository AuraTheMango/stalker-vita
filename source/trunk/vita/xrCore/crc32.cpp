#include <stddef.h>
#include <stdint.h>

typedef uint32_t u32;

/* Interface compatible avec xrCore de STALKER */
u32 crc32(const void *P, u32 len)
{
    const uint8_t *buffer = (const uint8_t *)P;
    u32 crc = 0xFFFFFFFFu;

    while (len--) {
        crc ^= *buffer++;

        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 1u)
                crc = (crc >> 1) ^ 0xEDB88320u;
            else
                crc >>= 1;
        }
    }

    return crc ^ 0xFFFFFFFFu;
}
