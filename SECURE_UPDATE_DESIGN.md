# Secure and Resumable Update Design

## Objective

Extend the working UART bootloader with version control, resumable transfers, and authenticated update authorization without breaking the current STM32F103C8T6 memory layout.

## Memory constraints

The device has 64 KiB of internal Flash:

```text
0x08000000 - 0x08003FFF  Bootloader, 16 KiB
0x08004000 - 0x0800FBFF  Application image, up to 47 KiB
0x0800FC00 - 0x0800FFFF  Update metadata, 1 KiB
```

The metadata page is reserved for update state. A/B application slots are intentionally not used because two complete application images do not fit in the remaining Flash.

## Metadata record

The metadata page will contain an atomic record with:

```text
magic              32 bits
format_version     16 bits
record_state       16 bits
firmware_version   32 bits
image_size         32 bits
image_crc32        32 bits
last_sequence      32 bits
image_hash         32 bytes, reserved for SHA-256
record_crc32       32 bits
```

Records are written in two alternating locations so an interrupted metadata update does not leave a partially valid record. The bootloader treats only a record with valid magic, state, and CRC as authoritative.

## Update state machine

```text
VALID application
        |
        | BEGIN_UPDATE accepted
        v
RECEIVING image  -- packet retry/resume --> RECEIVING image
        |
        | END_UPDATE + size/CRC/hash verified
        v
VALID application
```

If power is removed during `RECEIVING`, the bootloader remains in recovery mode and does not jump to a partially written image.

## Protocol additions

The existing SOF, length, payload, and CRC32 framing remains unchanged. The implemented secure update commands are:

| Command | Purpose |
|---|---|
| `GET_APP_INFO` | Read installed version, size, and image status |
| `BEGIN_UPDATE` | Declare version, size, and image CRC |
| `WRITE_DATA_V2` | Write a sequence-numbered data chunk |
| `GET_PROGRESS` | Report the last accepted sequence and address |
| `END_UPDATE` | Verify and commit the image |
| `AUTH_CHALLENGE` | Start update authorization |
| `AUTH_RESPONSE` | Prove knowledge of the update secret |

Every secure write packet contains a sequence number. Repeating an already accepted sequence is treated as an idempotent retry; an unexpected future sequence is rejected. The host uploader resumes from the progress reported by `GET_PROGRESS`.

Progress checkpoints are written every 128 accepted packets. With the 16 records in the reserved metadata page, this covers the complete 47 KiB application region while bounding the amount of data retransmitted after an interruption.

## Security stages

1. Version and image CRC validation are implemented.
2. Challenge-response authorization using HMAC-SHA256 is implemented.
3. SHA-256 is used internally by HMAC.
4. Whole-image CRC verification is required before metadata commit and boot.
5. Anti-rollback policy is implemented: a committed version must be strictly increased.

The embedded HMAC key is a development key for this learning project. A production device must provision a device-specific secret or use signed images with a protected public key; source-code constants are not a production root of trust.

## Recovery policy

- Never erase the bootloader region.
- Never jump when the application vector table is invalid.
- Never mark an image valid before complete verification.
- Keep the bootloader available after interrupted updates.
- Reject versions lower than the installed version once anti-rollback is enabled.
