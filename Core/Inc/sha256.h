#ifndef SHA256_H
#define SHA256_H

#include <stdint.h>

typedef struct
{
    uint32_t state[8];
    uint64_t bit_count;
    uint8_t buffer[64];
} SHA256_Context;

void SHA256_Init(SHA256_Context *context);
void SHA256_Update(SHA256_Context *context, const uint8_t *data, uint32_t length);
void SHA256_Final(SHA256_Context *context, uint8_t digest[32]);
void HMAC_SHA256(const uint8_t *key, uint32_t key_length,
                 const uint8_t *data, uint32_t data_length,
                 uint8_t digest[32]);

#endif
