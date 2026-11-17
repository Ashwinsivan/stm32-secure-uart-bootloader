#include "auth.h"
#include "sha256.h"
#include "stm32f1xx.h"
#include "auth_key_local.h"

extern volatile uint32_t system_ms;

static uint8_t challenge[16];
static uint32_t authenticated;
static uint32_t challenge_counter;

void Auth_Init(void)
{
    authenticated = 0U;
    challenge_counter = 0U;
    for (uint32_t i=0U;i<16U;i++)
        challenge[i] = (uint8_t)(system_ms ^ (DBGMCU->IDCODE >> ((i & 3U)*8U)) ^
                                 *(volatile uint8_t *)(0x1FFFF7E8U + (i % 12U)) ^ (i*37U));
}

void Auth_CreateChallenge(uint8_t nonce[16])
{
    authenticated = 0U;
    challenge_counter++;
    for (uint32_t i=0U;i<16U;i++)
    {
        challenge[i] ^= (uint8_t)(challenge_counter + system_ms + i * 13U);
        nonce[i] = challenge[i];
    }
}

int Auth_VerifyResponse(const uint8_t response[32])
{
    uint8_t expected[32];
    HMAC_SHA256(update_key,sizeof(update_key),challenge,sizeof(challenge),expected);
    uint8_t difference=0U;
    for (uint32_t i=0U;i<32U;i++) difference |= (uint8_t)(expected[i]^response[i]);
    authenticated = (difference == 0U) ? 1U : 0U;
    return authenticated ? 0 : -1;
}

uint32_t Auth_IsAuthenticated(void) { return authenticated; }
