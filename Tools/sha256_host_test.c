#include "sha256.h"
#include <stdio.h>
#include <string.h>

static int check_hex(const uint8_t *value, const char *expected)
{
    static const char hex[] = "0123456789abcdef";
    char actual[65];
    for (int i = 0; i < 32; i++) {
        actual[i * 2] = hex[value[i] >> 4];
        actual[i * 2 + 1] = hex[value[i] & 0x0F];
    }
    actual[64] = '\0';
    return strcmp(actual, expected) == 0;
}

int main(void)
{
    SHA256_Context context;
    uint8_t digest[32];
    const uint8_t key[] = "key";
    const uint8_t message[] = "The quick brown fox jumps over the lazy dog";

    SHA256_Init(&context);
    SHA256_Update(&context, (const uint8_t *)"abc", 3U);
    SHA256_Final(&context, digest);
    if (!check_hex(digest, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"))
        return 1;

    HMAC_SHA256(key, 3U, message, (uint32_t)(sizeof(message) - 1U), digest);
    if (!check_hex(digest, "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8"))
        return 2;

    puts("SHA-256/HMAC-SHA256 host tests passed.");
    return 0;
}
