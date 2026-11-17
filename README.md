# Secure UART Bootloader for STM32F103C8T6

A custom UART bootloader for the STM32F103C8T6 Blue Pill. Firmware updates are transferred over USART1 using a framed binary protocol with CRC32, HMAC-SHA256 authentication, version metadata, resumable writes, anti-rollback protection, and a validated application handoff.

## Repository layout

```text
.
├── Core/                 # Bootloader source and headers
├── Drivers/              # STM32 HAL and CMSIS sources
├── Tools/                # Host uploaders and protocol tests
├── Application/          # Separate LED demonstration firmware project
├── STM32F103C8TX_FLASH.ld
├── SECURE_UPDATE_DESIGN.md
└── SECURE_TEST_PROCEDURE.md
```

## Memory map

| Region | Address | Size |
|---|---:|---:|
| Custom bootloader | `0x08000000–0x08003FFF` | 16 KiB |
| Application image | `0x08004000–0x0800FBFF` | 47 KiB |
| Update metadata | `0x0800FC00–0x0800FFFF` | 1 KiB |

The bootloader project is linked at `0x08000000`. The application project is linked at `0x08004000`. The metadata page is reserved by the bootloader and must not be used by the application.

## Hardware

| CP2102 | Blue Pill |
|---|---|
| TXD | PA10 / USART1_RX |
| RXD | PA9 / USART1_TX |
| GND | GND |

Custom bootloader UART settings: **115200 baud, 8 data bits, no parity, 1 stop bit, no flow control**.

`BOOT0 = LOW` starts the custom bootloader. `BOOT0 = HIGH` selects the STM32 factory system-memory bootloader for initial programming or recovery.

## Update protocol

Packets use the following layout:

```text
SOF | COMMAND | LENGTH_L | LENGTH_H | PAYLOAD | CRC32_LSB ... CRC32_MSB
```

- SOF: `0xAA`
- Length: 16-bit little-endian payload length
- CRC coverage: command, length, and payload; SOF is excluded
- CRC: reflected polynomial `0xEDB88320`, initial value `0xFFFFFFFF`, final XOR `0xFFFFFFFF`

The secure update commands authenticate the host, erase and begin an update, write numbered chunks, report progress, verify the complete image CRC, commit metadata, and launch the application. Metadata states are `EMPTY`, `ACTIVE`, and `VALID`.

## Build and program

1. Open the bootloader project in STM32CubeIDE and build it.
2. Program `Debug/STM32F103_USART_Bootloader.bin` at `0x08000000` using STM32CubeProgrammer with verification enabled and `BOOT0 = HIGH`.
3. Set `BOOT0 = LOW`, reset the board, and confirm the UART banner in Tera Term.
4. Build the application project under `Application/`.
5. Close Tera Term and run the secure uploader:

```powershell
py Tools\secure_upload_firmware.py --version 1
```

Use `--status-only` to inspect metadata without modifying application Flash.

## Hardware verification completed

- Bootloader banner after reset with `BOOT0 = LOW`
- HMAC challenge-response authentication
- Wrong-key and unauthenticated-update rejection
- Application Flash erase and programming
- CRC32 image verification
- Application launch at `0x08004000`
- LED blink application execution
- Resumption after an interruption at a persisted checkpoint
- Version 1 rejected after version 2 was committed
- Persistence after power removal and reset

Detailed procedures are in [`SECURE_TEST_PROCEDURE.md`](SECURE_TEST_PROCEDURE.md). The design rationale is in [`SECURE_UPDATE_DESIGN.md`](SECURE_UPDATE_DESIGN.md).

## Security note

The HMAC key is intentionally excluded from version control. For local hardware testing, copy both key templates to their local filenames, then place the same 32-byte key in:

```text
Core/Inc/auth_key_local.h
Tools/auth_key_local.py
```

A production device should use device-specific key provisioning and protected key storage. Never commit a real device key.
