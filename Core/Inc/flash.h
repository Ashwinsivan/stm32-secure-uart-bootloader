
#ifndef FLASH_H
#define FLASH_H
#include "stm32f1xx.h"
#include "usart.h"
#include "flash.h"
#include "flash_test.h"

#include <stdint.h>
#include <stdint.h>

/* Flash controller */
void FLASH_Unlock(void);
void FLASH_Lock(void);

int FLASH_WaitForOperationComplete(void);

/* Flash operations */
int FLASH_ErasePage(uint32_t page_address);

int FLASH_ProgramHalfWord(uint32_t address,
                          uint16_t data);

int FLASH_VerifyErasedPage(uint32_t page_address);

int FLASH_EraseApplication(void);

/* Reserved metadata page at the top of the 64 KiB device. */
int FLASH_EraseMetadataPage(void);
int FLASH_ProgramMetadataHalfWord(uint32_t address, uint16_t data);

#endif /* FLASH_H */
