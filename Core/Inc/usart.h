
#ifndef USART_H
#define USART_H

#include <stdint.h>

void USART1_Init(void);

void USART1_SendChar(char data);

void USART1_SendString(char *str);

char USART1_ReceiveChar(void);

int USART1_ReceiveByteTimeout(uint8_t *data,
                              uint32_t timeout_ms);

#endif
