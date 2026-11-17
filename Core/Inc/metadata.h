#ifndef METADATA_H
#define METADATA_H

#include <stdint.h>

#define METADATA_MAGIC          0x55424C31U /* UBL1 */
#define METADATA_FORMAT_VERSION 1U

#define METADATA_STATE_EMPTY    0U
#define METADATA_STATE_ACTIVE   1U
#define METADATA_STATE_VALID    2U

typedef struct
{
    uint32_t magic;
    uint32_t format_version;
    uint32_t state;
    uint32_t firmware_version;
    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t next_sequence;
    uint32_t written_bytes;
    uint32_t record_crc32;
    uint8_t reserved[28];
} MetadataRecord;

void Metadata_Init(void);
int Metadata_Read(MetadataRecord *record);
int Metadata_Begin(uint32_t firmware_version,
                   uint32_t image_size,
                   uint32_t image_crc32);
int Metadata_Checkpoint(uint32_t next_sequence, uint32_t written_bytes);
int Metadata_Commit(void);
uint32_t Metadata_NextSequence(void);
uint32_t Metadata_WrittenBytes(void);
uint32_t Metadata_ImageSize(void);
uint32_t Metadata_ImageCRC32(void);
uint32_t Metadata_FirmwareVersion(void);
uint32_t Metadata_IsActive(void);
uint32_t Metadata_IsValid(void);

#endif
