#include "stm32f1xx.h"
#include "flash.h"
#include "usart.h"
#include "flash_test.h"

#define TEST_PAGE_ADDRESS  0x08004000U

void FLASH_Test(void)
{
    uint16_t value1;
    uint16_t value2;

    USART1_SendString("\r\nFLASH TEST START\r\n");

    /* Unlock Flash */
    FLASH_Unlock();

    if (FLASH->CR & FLASH_CR_LOCK)
    {
        USART1_SendString("FLASH UNLOCK FAILED\r\n");
        return;
    }

    USART1_SendString("FLASH UNLOCK SUCCESS\r\n");

    /* Erase application test page */
    if (FLASH_ErasePage(TEST_PAGE_ADDRESS) != 0)
    {
        USART1_SendString("PAGE ERASE FAILED\r\n");
        FLASH_Lock();
        return;
    }

    USART1_SendString("PAGE ERASE SUCCESS\r\n");

    /* Program two half-words */
    if (FLASH_ProgramHalfWord(TEST_PAGE_ADDRESS, 0x1234U) != 0)
    {
        USART1_SendString("PROGRAM 1 FAILED\r\n");
        FLASH_Lock();
        return;
    }

    if (FLASH_ProgramHalfWord(TEST_PAGE_ADDRESS + 2U, 0xABCDU) != 0)
    {
        USART1_SendString("PROGRAM 2 FAILED\r\n");
        FLASH_Lock();
        return;
    }

    /* Read back */
    value1 = *(__IO uint16_t *)TEST_PAGE_ADDRESS;
    value2 = *(__IO uint16_t *)(TEST_PAGE_ADDRESS + 2U);

    if ((value1 == 0x1234U) && (value2 == 0xABCDU))
    {
        USART1_SendString("FLASH VERIFY SUCCESS\r\n");
    }
    else
    {
        USART1_SendString("FLASH VERIFY FAILED\r\n");
    }

    FLASH_Lock();

    USART1_SendString("FLASH LOCKED\r\n");
    USART1_SendString("FLASH TEST COMPLETE\r\n");
}
