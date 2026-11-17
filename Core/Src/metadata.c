#include "metadata.h"
#include "flash.h"

#include <string.h>

#define METADATA_ADDRESS 0x0800FC00U
#define METADATA_SLOT_SIZE 64U
#define METADATA_SLOT_COUNT 16U
#define CHECKPOINT_INTERVAL 128U

static MetadataRecord current_record;
static uint32_t current_slot;

static uint32_t CRC32_Local(const uint8_t *data, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFFU;
    for (uint32_t i = 0U; i < length; i++)
    {
        crc ^= data[i];
        for (uint8_t bit = 0U; bit < 8U; bit++)
            crc = (crc & 1U) ? ((crc >> 1U) ^ 0xEDB88320U) : (crc >> 1U);
    }
    return crc ^ 0xFFFFFFFFU;
}

static uint32_t RecordCRC(const MetadataRecord *record)
{
    return CRC32_Local((const uint8_t *)record,
                       (uint32_t)((uint8_t *)&record->record_crc32 - (uint8_t *)record));
}

static int RecordValid(const MetadataRecord *record)
{
    return record->magic == METADATA_MAGIC &&
           record->format_version == METADATA_FORMAT_VERSION &&
           record->state <= METADATA_STATE_VALID &&
           record->record_crc32 == RecordCRC(record);
}

static int WriteRecord(uint32_t slot, const MetadataRecord *source)
{
    MetadataRecord record = *source;
    uint32_t address = METADATA_ADDRESS + slot * METADATA_SLOT_SIZE;
    record.record_crc32 = RecordCRC(&record);

    for (uint32_t offset = 0U; offset < sizeof(record); offset += 2U)
    {
        uint16_t halfword = (uint16_t)record.reserved[0];
        const uint8_t *bytes = (const uint8_t *)&record;
        halfword = bytes[offset];
        if ((offset + 1U) < sizeof(record))
            halfword |= (uint16_t)bytes[offset + 1U] << 8U;
        if (FLASH_ProgramMetadataHalfWord(address + offset, halfword) != 0)
            return -1;
    }
    return 0;
}

void Metadata_Init(void)
{
    MetadataRecord candidate;
    int found = 0;
    current_slot = 0U;
    memset(&current_record, 0, sizeof(current_record));

    for (uint32_t slot = 0U; slot < METADATA_SLOT_COUNT; slot++)
    {
        const MetadataRecord *stored = (const MetadataRecord *)(METADATA_ADDRESS + slot * METADATA_SLOT_SIZE);
        candidate = *stored;
        if (RecordValid(&candidate))
        {
            if (!found || candidate.next_sequence >= current_record.next_sequence)
            {
                current_record = candidate;
                current_slot = slot;
                found = 1;
            }
        }
    }
}

int Metadata_Read(MetadataRecord *record)
{
    if (!record || !RecordValid(&current_record))
        return -1;
    *record = current_record;
    return 0;
}

int Metadata_Begin(uint32_t firmware_version, uint32_t image_size, uint32_t image_crc32)
{
    MetadataRecord previous;
    if (Metadata_Read(&previous) == 0 &&
        previous.state == METADATA_STATE_VALID &&
        firmware_version <= previous.firmware_version)
        return -2;

    if (FLASH_EraseApplication() != 0)
        return -3;

    if (FLASH_EraseMetadataPage() != 0)
        return -4;

    memset(&current_record, 0, sizeof(current_record));
    current_record.magic = METADATA_MAGIC;
    current_record.format_version = METADATA_FORMAT_VERSION;
    current_record.state = METADATA_STATE_ACTIVE;
    current_record.firmware_version = firmware_version;
    current_record.image_size = image_size;
    current_record.image_crc32 = image_crc32;
    current_record.next_sequence = 0U;
    current_record.written_bytes = 0U;
    current_record.record_crc32 = RecordCRC(&current_record);
    current_slot = 0U;

    FLASH_Unlock();
    int result = WriteRecord(current_slot, &current_record);
    FLASH_Lock();
    return (result == 0) ? 0 : -5;
}

int Metadata_Checkpoint(uint32_t next_sequence, uint32_t written_bytes)
{
    if (!Metadata_IsActive())
        return -1;
    current_record.next_sequence = next_sequence;
    current_record.written_bytes = written_bytes;
    current_record.record_crc32 = RecordCRC(&current_record);
    if ((next_sequence % CHECKPOINT_INTERVAL) != 0U && written_bytes < current_record.image_size)
        return 0;

    current_slot = (current_slot + 1U) % METADATA_SLOT_COUNT;
    FLASH_Unlock();
    int result = WriteRecord(current_slot, &current_record);
    FLASH_Lock();
    return result;
}

int Metadata_Commit(void)
{
    if (!Metadata_IsActive())
        return -1;
    current_record.state = METADATA_STATE_VALID;
    current_record.record_crc32 = RecordCRC(&current_record);
    current_slot = (current_slot + 1U) % METADATA_SLOT_COUNT;
    FLASH_Unlock();
    int result = WriteRecord(current_slot, &current_record);
    FLASH_Lock();
    return result;
}

uint32_t Metadata_NextSequence(void) { return current_record.next_sequence; }
uint32_t Metadata_WrittenBytes(void) { return current_record.written_bytes; }
uint32_t Metadata_ImageSize(void) { return current_record.image_size; }
uint32_t Metadata_ImageCRC32(void) { return current_record.image_crc32; }
uint32_t Metadata_FirmwareVersion(void) { return current_record.firmware_version; }
uint32_t Metadata_IsActive(void) { return RecordValid(&current_record) && current_record.state == METADATA_STATE_ACTIVE; }
uint32_t Metadata_IsValid(void) { return RecordValid(&current_record) && current_record.state == METADATA_STATE_VALID; }
