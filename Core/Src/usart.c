
#include <stdint.h>
#include "stm32f1xx.h"
#include "usart.h"

/* Millisecond counter defined in main.c */
extern volatile uint32_t system_ms;

/* ================= USART1 INITIALIZATION ================= */

void USART1_Init(void)
{
    /* Enable GPIOA and USART1 clocks */
    RCC->APB2ENR |= (1U << 2);
    RCC->APB2ENR |= (1U << 14);

    /* PA9 = Alternate function push-pull */
    GPIOA->CRH &= ~(0xFU << 4);
    GPIOA->CRH |=  (0xAU << 4);

    /* PA10 = Input floating */
    GPIOA->CRH &= ~(0xFU << 8);
    GPIOA->CRH |=  (0x4U << 8);

    /* 115200 baud at 8 MHz */
    USART1->BRR = 0x0045;

    /* Enable USART, transmitter and receiver */
    USART1->CR1 = (1U << 13) |
                  (1U << 3)  |
                  (1U << 2);
}

/* ================= TRANSMISSION ================= */

void USART1_SendChar(char data)
{
    while (!(USART1->SR & (1U << 7)));

    USART1->DR = data;
}

void USART1_SendString(char *str)
{
    while (*str)
    {
        USART1_SendChar(*str);
        str++;
    }
}

/* ================= BLOCKING RECEPTION ================= */

char USART1_ReceiveChar(void)
{
    while (!(USART1->SR & (1U << 5)));

    return (char)USART1->DR;
}

/* ================= TIMEOUT RECEPTION ================= */

int USART1_ReceiveByteTimeout(uint8_t *data,
                              uint32_t timeout_ms)
{
    uint32_t start_time = system_ms;

    while (1)
    {
        uint32_t status = USART1->SR;

        /* ORE, NE, FE and PE error flags */

        if (status & 0x0FU)
        {
            /* Read SR followed by DR to clear error flags */
            volatile uint32_t dummy = USART1->DR;
            (void)dummy;

            return -1;
        }

        /* RXNE: Receive data available */

        if (status & (1U << 5))
        {
            *data = (uint8_t)USART1->DR;

            return 1;
        }

        /* Timeout check */

        if ((uint32_t)(system_ms - start_time) >= timeout_ms)
        {
            return 0;
        }
    }
}
