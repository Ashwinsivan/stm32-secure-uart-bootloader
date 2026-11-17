
#include "stm32f1xx.h"
#include "flash.h"

#include <stdint.h>

/*----------------------------------------------------------
 * FLASH MEMORY CONFIGURATION
 *----------------------------------------------------------*/

#define FLASH_BASE_ADDRESS       0x08000000U
#define FLASH_TOTAL_SIZE         (64U * 1024U)

#define FLASH_END_ADDRESS        (FLASH_BASE_ADDRESS + FLASH_TOTAL_SIZE - 1U)

#define BOOTLOADER_SIZE          (16U * 1024U)

#define APPLICATION_START        (FLASH_BASE_ADDRESS + BOOTLOADER_SIZE)
#define DEVICE_PAGE_SIZE         1024U
#define METADATA_PAGE_ADDRESS    (FLASH_BASE_ADDRESS + 63U * DEVICE_PAGE_SIZE)
#define APPLICATION_END          (METADATA_PAGE_ADDRESS - 1U)

#define FLASH_TIMEOUT            1000000U


/*----------------------------------------------------------
 * ADDRESS VALIDATION
 *----------------------------------------------------------*/

static int FLASH_IsValidAddress(uint32_t address)
{
    return (address >= APPLICATION_START &&
            address <= APPLICATION_END);
}


static int FLASH_IsValidPage(uint32_t address)
{
    if (address < APPLICATION_START)
        return 0;

    if (address > (APPLICATION_END - DEVICE_PAGE_SIZE + 1U))
        return 0;

    if (((address - FLASH_BASE_ADDRESS) % DEVICE_PAGE_SIZE) != 0U)
        return 0;

    return 1;
}


/*----------------------------------------------------------
 * CLEAR FLASH STATUS FLAGS
 *----------------------------------------------------------*/

static void FLASH_ClearStatusFlags(void)
{
    FLASH->SR = FLASH_SR_PGERR |
                FLASH_SR_WRPRTERR |
                FLASH_SR_EOP;
}


/*----------------------------------------------------------
 * WAIT FOR FLASH OPERATION
 *----------------------------------------------------------*/

int FLASH_WaitForOperationComplete(void)
{
    uint32_t timeout = FLASH_TIMEOUT;

    while ((FLASH->SR & FLASH_SR_BSY) && timeout > 0U)
    {
        timeout--;
    }

    if (timeout == 0U)
        return -1;

    if (FLASH->SR & (FLASH_SR_PGERR | FLASH_SR_WRPRTERR))
        return -2;

    return 0;
}


/*----------------------------------------------------------
 * UNLOCK FLASH
 *----------------------------------------------------------*/

void FLASH_Unlock(void)
{
    if (FLASH->CR & FLASH_CR_LOCK)
    {
        FLASH->KEYR = 0x45670123U;
        FLASH->KEYR = 0xCDEF89ABU;
    }
}


/*----------------------------------------------------------
 * LOCK FLASH
 *----------------------------------------------------------*/

void FLASH_Lock(void)
{
    FLASH->CR |= FLASH_CR_LOCK;
}


/*----------------------------------------------------------
 * ERASE ONE FLASH PAGE
 *----------------------------------------------------------*/

int FLASH_ErasePage(uint32_t page_address)
{
    int result;

    if (!FLASH_IsValidPage(page_address))
        return -3;

    if (FLASH->CR & FLASH_CR_LOCK)
        return -4;

    /* Wait until Flash is idle before clearing old flags */
    if (FLASH_WaitForOperationComplete() != 0)
        return -1;

    FLASH_ClearStatusFlags();

    /* Page erase configuration */
    FLASH->CR |= FLASH_CR_PER;

    FLASH->AR = page_address;

    /* Start erase */
    FLASH->CR |= FLASH_CR_STRT;

    result = FLASH_WaitForOperationComplete();

    /* Disable page erase mode */
    FLASH->CR &= ~FLASH_CR_PER;

    if (result != 0)
        return -2;

    /* Verify erased page */
    return FLASH_VerifyErasedPage(page_address);
}


/*----------------------------------------------------------
 * PROGRAM HALF-WORD
 *----------------------------------------------------------*/

int FLASH_ProgramHalfWord(uint32_t address, uint16_t data)
{
    uint16_t current_value;
    int result;

    if (!FLASH_IsValidAddress(address))
        return -5;

    if ((address & 1U) != 0U)
        return -1;

    if (address > (APPLICATION_END - 1U))
        return -5;

    if (FLASH->CR & FLASH_CR_LOCK)
        return -6;

    if (FLASH_WaitForOperationComplete() != 0)
        return -2;

    current_value = *(__IO uint16_t *)address;

    /* A resumable transfer may retransmit a packet after a reset.  Treat an
     * already-correct half-word as success without issuing another program
     * operation to Flash. */
    if (current_value == data)
        return 0;

    /* Only allow 1-to-0 programming */
    if ((current_value & data) != data)
        return -7;

    FLASH_ClearStatusFlags();

    /* Enable programming */
    FLASH->CR |= FLASH_CR_PG;

    *(__IO uint16_t *)address = data;

    result = FLASH_WaitForOperationComplete();

    /* Disable programming */
    FLASH->CR &= ~FLASH_CR_PG;

    if (result != 0)
        return -3;

    /* Verify programmed data */
    if (*(__IO uint16_t *)address != data)
        return -4;

    return 0;
}


/*----------------------------------------------------------
 * VERIFY ERASED PAGE
 *----------------------------------------------------------*/

int FLASH_VerifyErasedPage(uint32_t page_address)
{
    uint32_t offset;
    uint16_t value;

    if (!FLASH_IsValidPage(page_address))
        return -1;

    for (offset = 0U;
         offset < DEVICE_PAGE_SIZE;
         offset += 2U)
    {
        value = *(__IO uint16_t *)(page_address + offset);

        if (value != 0xFFFFU)
            return -2;
    }

    return 0;
}


/*----------------------------------------------------------
 * ERASE COMPLETE APPLICATION REGION
 *----------------------------------------------------------*/

int FLASH_EraseApplication(void)
{
    uint32_t address;

    /* Unlock Flash */
    FLASH_Unlock();

    if (FLASH->CR & FLASH_CR_LOCK)
        return -1;

    /* Erase application pages only */
    for (address = APPLICATION_START;
         address <= (APPLICATION_END - DEVICE_PAGE_SIZE + 1U);
         address += DEVICE_PAGE_SIZE)
    {
        if (FLASH_ErasePage(address) != 0)
        {
            FLASH_Lock();
            return -2;
        }
    }

    /* Lock Flash after successful erase */
    FLASH_Lock();

    return 0;
}

int FLASH_EraseMetadataPage(void)
{
    int result;
    FLASH_Unlock();
    if (FLASH->CR & FLASH_CR_LOCK)
        return -1;
    if (FLASH_WaitForOperationComplete() != 0)
        return -2;
    FLASH_ClearStatusFlags();
    FLASH->CR |= FLASH_CR_PER;
    FLASH->AR = METADATA_PAGE_ADDRESS;
    FLASH->CR |= FLASH_CR_STRT;
    result = FLASH_WaitForOperationComplete();
    FLASH->CR &= ~FLASH_CR_PER;
    if (result != 0)
    {
        FLASH_Lock();
        return -3;
    }
    FLASH_Lock();
    /* Metadata record programming performs the subsequent 1-to-0 checks. */
    return 0;
}

int FLASH_ProgramMetadataHalfWord(uint32_t address, uint16_t data)
{
    uint16_t current_value;
    int result;
    if (address < METADATA_PAGE_ADDRESS || address > (FLASH_END_ADDRESS - 1U) || (address & 1U))
        return -1;
    if (FLASH->CR & FLASH_CR_LOCK || FLASH_WaitForOperationComplete() != 0)
        return -2;
    current_value = *(__IO uint16_t *)address;
    if ((current_value & data) != data)
        return -3;
    FLASH_ClearStatusFlags();
    FLASH->CR |= FLASH_CR_PG;
    *(__IO uint16_t *)address = data;
    result = FLASH_WaitForOperationComplete();
    FLASH->CR &= ~FLASH_CR_PG;
    if (result != 0 || *(__IO uint16_t *)address != data)
        return -4;
    return 0;
}
