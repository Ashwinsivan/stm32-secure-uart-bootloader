
#include "stm32f1xx.h"
#include "usart.h"
#include "flash.h"
#include "metadata.h"
#include "auth.h"

#include <stdint.h>

/*----------------------------------------------------------
 * CONFIGURATION
 *----------------------------------------------------------*/

#define START_BYTE                  0xAAU
#define MAX_PAYLOAD                 32U

#define PACKET_TIMEOUT_MS           500U
#define START_TIMEOUT_MS            1000U
#define AUTO_JUMP_TIMEOUT_MS        5000U

#define TEXT_COMMAND_MAX_LENGTH     32U
static uint8_t Process_Write_Data(uint8_t *payload, uint16_t length);
static uint8_t Application_IsValid(void);
static void Jump_To_Application(void);
static uint32_t CRC32_Flash(uint32_t address, uint32_t length);
static uint8_t Process_Write_Data_V2(uint8_t *payload, uint16_t length);
static void Send_App_Info(uint8_t command);
static void Send_Auth_Challenge(uint8_t command);

/* Bootloader commands */

#define CMD_GET_VERSION             0x01U
#define CMD_GET_DEVICE_ID           0x02U
#define CMD_GET_FLASH_SIZE          0x03U
#define CMD_ERASE_APP               0x04U
#define CMD_WRITE_DATA              0x05U
#define CMD_JUMP_TO_APP             0x06U
#define CMD_GET_APP_INFO            0x07U
#define CMD_BEGIN_UPDATE            0x08U
#define CMD_WRITE_DATA_V2           0x09U
#define CMD_GET_PROGRESS            0x0AU
#define CMD_END_UPDATE              0x0BU
#define CMD_AUTH_CHALLENGE          0x0CU
#define CMD_AUTH_RESPONSE            0x0DU

/* Response status */

#define ACK                         0x00U
#define NACK_COMMAND                0x01U
#define NACK_LENGTH                 0x02U
#define NACK_CRC                    0x03U
#define NACK_FLASH                  0x04U
#define NACK_AUTH                   0x05U



/*----------------------------------------------------------
 * SYSTEM TIMEBASE
 *----------------------------------------------------------*/

volatile uint32_t system_ms = 0U;

void Timebase_Init(void)
{
    SysTick_Config(SystemCoreClock / 1000U);
}


/*----------------------------------------------------------
 * CRC32
 *----------------------------------------------------------*/

uint32_t CRC32_Calculate(uint8_t *data, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFFU;

    for (uint32_t i = 0U; i < length; i++)
    {
        crc ^= data[i];

        for (uint8_t bit = 0U; bit < 8U; bit++)
        {
            if (crc & 1U)
                crc = (crc >> 1U) ^ 0xEDB88320U;
            else
                crc >>= 1U;
        }
    }

    return crc ^ 0xFFFFFFFFU;
}


/*----------------------------------------------------------
 * SEND BINARY PACKET
 *
 * SOF | CMD | LEN_L | LEN_H | PAYLOAD | CRC32
 *----------------------------------------------------------*/

void Send_Packet(uint8_t command,
                 uint8_t *payload,
                 uint16_t length)
{
    uint8_t crc_data[3U + MAX_PAYLOAD];
    uint32_t crc;

    if (length > MAX_PAYLOAD)
        return;

    USART1_SendChar((char)START_BYTE);
    USART1_SendChar((char)command);

    USART1_SendChar((char)(length & 0xFFU));
    USART1_SendChar((char)((length >> 8U) & 0xFFU));

    crc_data[0] = command;
    crc_data[1] = (uint8_t)(length & 0xFFU);
    crc_data[2] = (uint8_t)((length >> 8U) & 0xFFU);

    for (uint16_t i = 0U; i < length; i++)
    {
        USART1_SendChar((char)payload[i]);
        crc_data[3U + i] = payload[i];
    }

    crc = CRC32_Calculate(crc_data, 3U + length);

    USART1_SendChar((char)(crc & 0xFFU));
    USART1_SendChar((char)((crc >> 8U) & 0xFFU));
    USART1_SendChar((char)((crc >> 16U) & 0xFFU));
    USART1_SendChar((char)((crc >> 24U) & 0xFFU));
}

/*----------------------------------------------------------
 * APPLICATION VALIDATION AND LAUNCH
 *----------------------------------------------------------*/

static uint8_t Application_IsValid(void)
{
    uint32_t stack_pointer = *(volatile uint32_t *)0x08004000U;
    uint32_t reset_handler = *(volatile uint32_t *)0x08004004U;

    if (Metadata_IsActive())
        return 0U;

    if ((stack_pointer < 0x20000000U) ||
        (stack_pointer > 0x20005000U))
    {
        return 0U;
    }

    if ((reset_handler < 0x08004001U) ||
        (reset_handler >= 0x0800FC00U) ||
        ((reset_handler & 1U) == 0U))
    {
        return 0U;
    }

    if (Metadata_IsValid())
    {
        if (Metadata_ImageSize() == 0U ||
            Metadata_ImageSize() > (0x0800FC00U - 0x08004000U) ||
            CRC32_Flash(0x08004000U, Metadata_ImageSize()) != Metadata_ImageCRC32())
            return 0U;
    }

    return 1U;
}

static uint32_t CRC32_Flash(uint32_t address, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFFU;
    for (uint32_t i = 0U; i < length; i++)
    {
        crc ^= *(__IO uint8_t *)(address + i);
        for (uint8_t bit = 0U; bit < 8U; bit++)
            crc = (crc & 1U) ? ((crc >> 1U) ^ 0xEDB88320U) : (crc >> 1U);
    }
    return crc ^ 0xFFFFFFFFU;
}

static void Jump_To_Application(void)
{
    uint32_t application_stack_pointer =
        *(volatile uint32_t *)0x08004000U;
    uint32_t application_reset_handler =
        *(volatile uint32_t *)0x08004004U;

    typedef void (*ApplicationEntry)(void);
    ApplicationEntry application_entry =
        (ApplicationEntry)application_reset_handler;

    __disable_irq();

    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL = 0U;

    USART1->CR1 = 0U;
    USART1->CR2 = 0U;
    USART1->CR3 = 0U;

    SCB->VTOR = 0x08004000U;
    __set_MSP(application_stack_pointer);

    /* The bootloader disabled IRQs before the handoff.  Re-enable them so
     * the application SysTick and other interrupts can run normally. */
    __enable_irq();

    application_entry();

    while (1)
    {
    }
}


/*----------------------------------------------------------
 * PROCESS BINARY COMMAND
 *----------------------------------------------------------*/

void Process_Command(uint8_t command)
{
    uint8_t response[4];

    uint16_t device_id;
    uint16_t flash_size;

    switch (command)
    {
        case CMD_GET_VERSION:

            response[0] = ACK;
            response[1] = '1';
            response[2] = '.';
            response[3] = '0';

            Send_Packet(command | 0x80U, response, 4U);

            break;


        case CMD_GET_DEVICE_ID:

            device_id =
                (uint16_t)(DBGMCU->IDCODE & 0x0FFFU);

            response[0] = ACK;
            response[1] = (uint8_t)(device_id & 0xFFU);
            response[2] = (uint8_t)(device_id >> 8U);

            Send_Packet(command | 0x80U, response, 3U);

            break;


        case CMD_GET_FLASH_SIZE:

            flash_size =
                *(volatile uint16_t *)0x1FFFF7E0U;

            response[0] = ACK;
            response[1] = (uint8_t)(flash_size & 0xFFU);
            response[2] = (uint8_t)(flash_size >> 8U);

            Send_Packet(command | 0x80U, response, 3U);

            break;


        case CMD_ERASE_APP:

            USART1_SendString(
                "\r\nERASE_APP command received\r\n");

            if (!Auth_IsAuthenticated())
            {
                response[0] = NACK_AUTH;
            }
            else if (FLASH_EraseApplication() == 0 && FLASH_EraseMetadataPage() == 0)
            {
                response[0] = ACK;
                Metadata_Init();
            }
            else
            {
                response[0] = NACK_FLASH;
            }

            Send_Packet(command | 0x80U, response, 1U);

            break;

        case CMD_GET_APP_INFO:
        case CMD_GET_PROGRESS:
            Send_App_Info(command);
            break;

        case CMD_AUTH_CHALLENGE:
            Send_Auth_Challenge(command);
            break;

        case CMD_JUMP_TO_APP:

            response[0] =
                Application_IsValid() ? ACK : NACK_FLASH;

            Send_Packet(command | 0x80U, response, 1U);

            if (response[0] == ACK)
            {
                while (!(USART1->SR & (1U << 6)));
                Jump_To_Application();
            }

            break;


        default:

            response[0] = NACK_COMMAND;

            Send_Packet(command | 0x80U, response, 1U);

            break;
    }
}


/*----------------------------------------------------------
 * TEXT COMMAND COMPARISON
 *----------------------------------------------------------*/

static void Send_App_Info(uint8_t command)
{
    uint8_t response[25];
    uint32_t state = Metadata_IsValid() ? METADATA_STATE_VALID :
                     (Metadata_IsActive() ? METADATA_STATE_ACTIVE : METADATA_STATE_EMPTY);
    uint32_t version = Metadata_FirmwareVersion();
    uint32_t size = Metadata_ImageSize();
    uint32_t crc = Metadata_ImageCRC32();
    uint32_t sequence = Metadata_NextSequence();

    response[0] = ACK;
    response[1] = (uint8_t)state;
    response[2] = 0U;
    response[3] = 0U;
    response[4] = 0U;
    response[5] = (uint8_t)version;
    response[6] = (uint8_t)(version >> 8U);
    response[7] = (uint8_t)(version >> 16U);
    response[8] = (uint8_t)(version >> 24U);
    response[9] = (uint8_t)size;
    response[10] = (uint8_t)(size >> 8U);
    response[11] = (uint8_t)(size >> 16U);
    response[12] = (uint8_t)(size >> 24U);
    response[13] = (uint8_t)crc;
    response[14] = (uint8_t)(crc >> 8U);
    response[15] = (uint8_t)(crc >> 16U);
    response[16] = (uint8_t)(crc >> 24U);
    response[17] = (uint8_t)sequence;
    response[18] = (uint8_t)(sequence >> 8U);
    response[19] = (uint8_t)(sequence >> 16U);
    response[20] = (uint8_t)(sequence >> 24U);
    response[21] = (uint8_t)Metadata_WrittenBytes();
    response[22] = (uint8_t)(Metadata_WrittenBytes() >> 8U);
    response[23] = (uint8_t)(Metadata_WrittenBytes() >> 16U);
    response[24] = (uint8_t)(Metadata_WrittenBytes() >> 24U);
    Send_Packet(command | 0x80U, response, sizeof(response));
}

static void Send_Auth_Challenge(uint8_t command)
{
    uint8_t response[17];
    response[0] = ACK;
    Auth_CreateChallenge(&response[1]);
    Send_Packet(command | 0x80U, response, sizeof(response));
}

static int TextCommand_Compare(const char *a, const char *b)
{
    while ((*a != '\0') && (*b != '\0'))
    {
        if (*a != *b)
            return 0;

        a++;
        b++;
    }

    return ((*a == '\0') && (*b == '\0'));
}


/*----------------------------------------------------------
 * PRINT DECIMAL NUMBER
 *----------------------------------------------------------*/

static void Print_Decimal(uint32_t number)
{
    char digits[10];
    uint8_t count = 0U;

    if (number == 0U)
    {
        USART1_SendChar('0');
        return;
    }

    while ((number > 0U) && (count < sizeof(digits)))
    {
        digits[count++] = (char)('0' + (number % 10U));
        number /= 10U;
    }

    while (count > 0U)
    {
        USART1_SendChar(digits[--count]);
    }
}


/*----------------------------------------------------------
 * PRINT HEXADECIMAL NUMBER
 *----------------------------------------------------------*/

static void Print_Hex16(uint16_t value)
{
    const char hex[] = "0123456789ABCDEF";

    USART1_SendChar(hex[(value >> 12U) & 0x0FU]);
    USART1_SendChar(hex[(value >> 8U) & 0x0FU]);
    USART1_SendChar(hex[(value >> 4U) & 0x0FU]);
    USART1_SendChar(hex[value & 0x0FU]);
}


/*----------------------------------------------------------
 * TEXT COMMAND HANDLER
 *----------------------------------------------------------*/

static void Process_Text_Command(uint8_t first_character)
{
    char command[TEXT_COMMAND_MAX_LENGTH + 1U];

    uint8_t received_char;
    uint32_t index = 0U;

    int result;
    uint8_t overflow = 0U;

    /* Store and echo the first character */

    command[index++] = (char)first_character;

    USART1_SendChar((char)first_character);

    /*
     * Receive remaining characters until Enter.
     */

    while (1)
    {
        result = USART1_ReceiveByteTimeout(
                     &received_char,
                     10000U);

        /* Check reception before using received_char */

        if (result != 1)
        {
            USART1_SendString(
                "\r\nERROR: Command receive timeout/error\r\n");

            USART1_SendString("> ");

            return;
        }

        /* Handle Enter */

        if ((received_char == '\r') ||
            (received_char == '\n'))
        {
            break;
        }

        /* Support Backspace */

        if ((received_char == 0x08U) ||
            (received_char == 0x7FU))
        {
            if (index > 0U)
            {
                index--;

                USART1_SendString("\b \b");
            }

            continue;
        }

        /* Store and echo normal characters */

        if (index < TEXT_COMMAND_MAX_LENGTH)
        {
            command[index++] = (char)received_char;

            USART1_SendChar((char)received_char);
        }
        else
        {
            overflow = 1U;
        }
    }

    /* Null terminate command */

    command[index] = '\0';

    USART1_SendString("\r\n");

    if (overflow)
    {
        USART1_SendString("ERROR: Command too long\r\n");
        USART1_SendString("> ");

        return;
    }


    /* GET VERSION */

    if (TextCommand_Compare(command, "GET_VERSION"))
    {
        USART1_SendString("Bootloader Version: 1.0\r\n");
    }


    /* GET DEVICE ID */

    else if (TextCommand_Compare(command, "GET_DEVICE_ID"))
    {
        uint16_t device_id =
            (uint16_t)(DBGMCU->IDCODE & 0x0FFFU);

        USART1_SendString("Device ID: 0x");

        Print_Hex16(device_id);

        USART1_SendString("\r\n");
    }


    /* GET FLASH SIZE */

    else if (TextCommand_Compare(command, "GET_FLASH_SIZE"))
    {
        uint16_t flash_size =
            *(volatile uint16_t *)0x1FFFF7E0U;

        USART1_SendString("Flash Size: ");

        Print_Decimal(flash_size);

        USART1_SendString(" KB\r\n");
    }


    /* ERASE APPLICATION */

    else if (TextCommand_Compare(command, "ERASE_APP"))
    {
        if (!Auth_IsAuthenticated())
        {
            USART1_SendString(
                "ERROR: Authentication required\r\n");
        }
        else if (FLASH_EraseApplication() == 0 && FLASH_EraseMetadataPage() == 0)
        {
            USART1_SendString(
                "APPLICATION ERASE SUCCESS\r\n");
        }
        else
        {
            USART1_SendString(
                "APPLICATION ERASE FAILED\r\n");
        }
    }


    /* HELP */

    else if (TextCommand_Compare(command, "HELP"))
    {
        USART1_SendString("Available Commands:\r\n");
        USART1_SendString("GET_VERSION\r\n");
        USART1_SendString("GET_DEVICE_ID\r\n");
        USART1_SendString("GET_FLASH_SIZE\r\n");
        USART1_SendString("ERASE_APP\r\n");
        USART1_SendString("HELP\r\n");
    }


    /* UNKNOWN COMMAND */

    else
    {
        USART1_SendString("ERROR: Unknown Command\r\n");
        USART1_SendString("Type HELP\r\n");
    }

    USART1_SendString("\r\n> ");
}


/*----------------------------------------------------------
 * UART DRAIN
 *----------------------------------------------------------*/

void Drain_UART(void)
{
    uint8_t dummy;

    while (USART1_ReceiveByteTimeout(&dummy, 20U) == 1)
    {
        /* Discard remaining bytes */
    }
}


/*----------------------------------------------------------
 * BINARY PACKET RECEIVER
 *----------------------------------------------------------*/

void Bootloader_Receive(void)
{
    uint8_t start_byte;
    uint8_t command;

    uint8_t length_low;
    uint8_t length_high;

    uint8_t payload[MAX_PAYLOAD];
    uint8_t crc_bytes[4];

    uint8_t response[1];

    uint16_t payload_length;

    uint32_t received_crc;
    uint32_t calculated_crc;

    uint8_t crc_data[3U + MAX_PAYLOAD];

    int result;
    uint32_t boot_wait_start = system_ms;

    while (1)
    {
        /* Wait for incoming data */

        result = USART1_ReceiveByteTimeout(
                     &start_byte,
                     START_TIMEOUT_MS);

        if (result != 1)
        {
            if (((system_ms - boot_wait_start) >= AUTO_JUMP_TIMEOUT_MS) &&
                Application_IsValid())
            {
                Jump_To_Application();
            }

            continue;
        }

        /* Any received byte means the user is interacting with the
         * bootloader, so keep the update window open. */
        boot_wait_start = system_ms;


        /*
         * Binary protocol starts with 0xAA.
         * Other characters are treated as text commands.
         */

        if (start_byte != START_BYTE)
        {
            if ((start_byte == '\r') ||
                (start_byte == '\n'))
            {
                continue;
            }

            Process_Text_Command(start_byte);

            continue;
        }


        /* RECEIVE COMMAND */

        if (USART1_ReceiveByteTimeout(
                &command,
                PACKET_TIMEOUT_MS) != 1)
        {
            Drain_UART();
            continue;
        }


        /* RECEIVE LENGTH */

        if (USART1_ReceiveByteTimeout(
                &length_low,
                PACKET_TIMEOUT_MS) != 1)
        {
            Drain_UART();
            continue;
        }

        if (USART1_ReceiveByteTimeout(
                &length_high,
                PACKET_TIMEOUT_MS) != 1)
        {
            Drain_UART();
            continue;
        }

        payload_length =
            (uint16_t)length_low |
            ((uint16_t)length_high << 8U);


        /* CHECK MAXIMUM PAYLOAD */

        if (payload_length > MAX_PAYLOAD)
        {
            response[0] = NACK_LENGTH;

            Send_Packet(command | 0x80U, response, 1U);

            Drain_UART();
            continue;
        }


        /* PREPARE CRC DATA */

        crc_data[0] = command;
        crc_data[1] = length_low;
        crc_data[2] = length_high;


        /* RECEIVE PAYLOAD */

        for (uint16_t i = 0U; i < payload_length; i++)
        {
            if (USART1_ReceiveByteTimeout(
                    &payload[i],
                    PACKET_TIMEOUT_MS) != 1)
            {
                Drain_UART();
                goto NEXT_PACKET;
            }

            crc_data[3U + i] = payload[i];
        }


        /* RECEIVE CRC */

        for (uint8_t i = 0U; i < 4U; i++)
        {
            if (USART1_ReceiveByteTimeout(
                    &crc_bytes[i],
                    PACKET_TIMEOUT_MS) != 1)
            {
                Drain_UART();
                goto NEXT_PACKET;
            }
        }


        /* RECONSTRUCT CRC */

        received_crc =
              (uint32_t)crc_bytes[0]
            | ((uint32_t)crc_bytes[1] << 8U)
            | ((uint32_t)crc_bytes[2] << 16U)
            | ((uint32_t)crc_bytes[3] << 24U);


        /* CALCULATE CRC */

        calculated_crc =
            CRC32_Calculate(
                crc_data,
                3U + payload_length);


        /* VALIDATE CRC */

        if (received_crc != calculated_crc)
        {
            response[0] = NACK_CRC;

            Send_Packet(command | 0x80U, response, 1U);

            goto NEXT_PACKET;
        }


        /* Handle the legacy firmware data write command. */
        if (command == CMD_WRITE_DATA)
        {
            response[0] = Auth_IsAuthenticated() ?
                          Process_Write_Data(payload, payload_length) : NACK_AUTH;

            Send_Packet(command | 0x80U, response, 1U);

            goto NEXT_PACKET;
        }

        if (command == CMD_BEGIN_UPDATE)
        {
            if (!Auth_IsAuthenticated())
                response[0] = NACK_AUTH;
            else if (payload_length != 12U)
                response[0] = NACK_LENGTH;
            else
            {
                int begin_result;
                uint32_t version = (uint32_t)payload[0] | ((uint32_t)payload[1] << 8U) |
                                   ((uint32_t)payload[2] << 16U) | ((uint32_t)payload[3] << 24U);
                uint32_t size = (uint32_t)payload[4] | ((uint32_t)payload[5] << 8U) |
                                ((uint32_t)payload[6] << 16U) | ((uint32_t)payload[7] << 24U);
                uint32_t crc = (uint32_t)payload[8] | ((uint32_t)payload[9] << 8U) |
                               ((uint32_t)payload[10] << 16U) | ((uint32_t)payload[11] << 24U);
                if (size == 0U || size > (0x0800FC00U - 0x08004000U))
                {
                    response[0] = NACK_LENGTH;
                }
                else
                {
                    begin_result = Metadata_Begin(version, size, crc);
                    response[0] = (begin_result == 0) ? ACK : NACK_FLASH;
                    if (begin_result != 0)
                        response[1] = (uint8_t)(-begin_result);
                }
            }
            Send_Packet(command | 0x80U, response,
                        (response[0] == NACK_FLASH) ? 2U : 1U);
            goto NEXT_PACKET;
        }

        if (command == CMD_WRITE_DATA_V2)
        {
            response[0] = Auth_IsAuthenticated() ?
                          Process_Write_Data_V2(payload, payload_length) : NACK_AUTH;
            Send_Packet(command | 0x80U, response, 1U);
            goto NEXT_PACKET;
        }

        if (command == CMD_END_UPDATE)
        {
            if (!Auth_IsAuthenticated())
                response[0] = NACK_AUTH;
            else if (payload_length != 0U || Metadata_WrittenBytes() != Metadata_ImageSize() ||
                CRC32_Flash(0x08004000U, Metadata_ImageSize()) != Metadata_ImageCRC32())
                response[0] = NACK_FLASH;
            else
                response[0] = (Metadata_Commit() == 0) ? ACK : NACK_FLASH;
            Send_Packet(command | 0x80U, response, 1U);
            goto NEXT_PACKET;
        }

        if (command == CMD_AUTH_RESPONSE)
        {
            if (payload_length != 32U)
                response[0] = NACK_LENGTH;
            else
                response[0] = (Auth_VerifyResponse(payload) == 0) ? ACK : NACK_AUTH;
            Send_Packet(command | 0x80U, response, 1U);
            goto NEXT_PACKET;
        }

        /* Existing commands must have zero payload */
        if (payload_length != 0U)
        {
            response[0] = NACK_LENGTH;

            Send_Packet(command | 0x80U, response, 1U);

            goto NEXT_PACKET;
        }

        /* Process normal bootloader commands */
        Process_Command(command);


        /* PROCESS VALID COMMAND */




        NEXT_PACKET:

        ;
    }
}

/*----------------------------------------------------------
 * WRITE FIRMWARE DATA TO FLASH
 *
 * Payload:
 * [0..3] = Flash address (little-endian)
 * [4..N] = Firmware data
 *----------------------------------------------------------*/

static uint8_t Process_Write_Data(uint8_t *payload,
                                 uint16_t length)
{
    uint32_t address;
    uint16_t data_length;
    uint16_t halfword;
    uint8_t result = ACK;

    /* Address requires 4 bytes and data must not be empty */
    if ((payload == 0) || (length < 5U))
    {
        return NACK_LENGTH;
    }

    data_length = length - 4U;

    /* Decode 32-bit little-endian Flash address */
    address =
          (uint32_t)payload[0]
        | ((uint32_t)payload[1] << 8U)
        | ((uint32_t)payload[2] << 16U)
        | ((uint32_t)payload[3] << 24U);

    /* Require half-word aligned address */
    if ((address & 1U) != 0U)
    {
        return NACK_FLASH;
    }

    /*
     * Keep writes inside the currently configured
     * application region: 0x08004000 to 0x0800FBFF.
     *
     * Check using subtraction to avoid address overflow.
     */
    if ((address < 0x08004000U) ||
        (address >= 0x0800FC00U) ||
        (data_length > (0x0800FC00U - address)))
    {
        return NACK_FLASH;
    }

    FLASH_Unlock();

    for (uint16_t i = 0U; i < data_length; i += 2U)
    {
        /* Construct little-endian half-word */
        halfword = payload[4U + i];

        if ((i + 1U) < data_length)
        {
            halfword |=
                (uint16_t)payload[4U + i + 1U] << 8U;
        }
        else
        {
            /* Pad final odd byte with erased value */
            halfword |= 0xFF00U;
        }

        if (FLASH_ProgramHalfWord(
                address + i,
                halfword) != 0)
        {
            result = NACK_FLASH;
            break;
        }
    }

    /* Always lock Flash after programming attempt */
    FLASH_Lock();

    return result;
}

static uint8_t Process_Write_Data_V2(uint8_t *payload, uint16_t length)
{
    uint8_t legacy_payload[4U + 26U];
    uint32_t sequence;
    uint32_t address;
    uint16_t data_length;

    if (!Metadata_IsActive() || length < 7U)
        return NACK_FLASH;

    address = (uint32_t)payload[0] | ((uint32_t)payload[1] << 8U) |
              ((uint32_t)payload[2] << 16U) | ((uint32_t)payload[3] << 24U);
    sequence = (uint32_t)payload[4] | ((uint32_t)payload[5] << 8U);
    data_length = length - 6U;

    /* A repeated packet is safe to acknowledge after a lost response. */
    if (sequence < Metadata_NextSequence())
        return ACK;

    if (sequence != Metadata_NextSequence() ||
        address != (0x08004000U + Metadata_WrittenBytes()) ||
        data_length > 26U ||
        data_length > (0x0800FC00U - address))
        return NACK_LENGTH;

    /* Reuse the legacy flash writer, removing the V2 sequence field first. */
    for (uint16_t i = 0U; i < 4U; i++)
        legacy_payload[i] = payload[i];
    for (uint16_t i = 0U; i < data_length; i++)
        legacy_payload[4U + i] = payload[6U + i];

    uint8_t result = Process_Write_Data(legacy_payload,
                                        (uint16_t)(data_length + 4U));
    if (result != ACK)
        return result;

    if (Metadata_Checkpoint(sequence + 1U,
                            Metadata_WrittenBytes() + data_length) != 0)
        return NACK_FLASH;

    return ACK;
}


/*----------------------------------------------------------
 * MAIN
 *----------------------------------------------------------*/

int main(void)
{
    Timebase_Init();

    USART1_Init();
    Metadata_Init();
    Auth_Init();

    /* Bootloader startup message */

    USART1_SendString("\r\n");
    USART1_SendString("================================\r\n");
    USART1_SendString("       STM32F103 BOOTLOADER\r\n");
    USART1_SendString("================================\r\n");
    USART1_SendString("Binary and Text Interface Ready\r\n");
    USART1_SendString("Type HELP for commands\r\n");
    USART1_SendString("> ");

    /* Start bootloader receiver */

    Bootloader_Receive();

    while (1)
    {
    }
}
