#ifndef AUTH_H
#define AUTH_H

#include <stdint.h>

void Auth_Init(void);
void Auth_CreateChallenge(uint8_t nonce[16]);
int Auth_VerifyResponse(const uint8_t response[32]);
uint32_t Auth_IsAuthenticated(void);

#endif
