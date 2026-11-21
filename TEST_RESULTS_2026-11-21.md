# Hardware Test Evidence - 2026-11-21

## Test setup

- Target: STM32F103C8T6 Blue Pill
- Programmer and communication bridge: CP2102 USB-to-UART
- Port: COM7
- Custom bootloader UART: 115200 baud, 8 data bits, no parity, 1 stop bit
- Custom bootloader mode: BOOT0 LOW, PB2 LOW
- Application start address: `0x08004000`

No secret key, private credential, or local authentication-key file is included in this evidence.

## Bootloader installation

STM32CubeProgrammer programmed the custom bootloader through the STM32 factory ROM UART bootloader:

```text
File: STM32F103_USART_Bootloader.bin
Size: 11484 Bytes
Address: 0x08000000
Download verified successfully
```

## UART communication

After switching BOOT0 LOW and resetting the board, the custom startup banner appeared in Tera Term.

```text
Bootloader Version: 1.0
Device ID: 0x0410
Flash Size: 128 KB
```

The reported device flash size is retained as device output only; the project continues to use its conservative 64 KiB application layout.

## Security negative-path test

```text
Challenge: PASS
Wrong key rejected: PASS
Unauthenticated update rejected: PASS
Secure negative-path tests passed; application Flash was not modified.
```

## Authenticated application update

```text
Firmware size: 4604 bytes
Image CRC: 0xB8EDB1C4
Authentication: PASS
Progress: 100% (4604/4604)
Image CRC verified and metadata committed.
Application launched.
```

## Protocol negative-path test

The test utility authenticated before exercising the protected address check.

```text
Invalid CRC: PASS
Authentication: PASS
Invalid address: PASS
Negative-path tests passed; no application Flash was modified.
```

## Application handoff

After reset with BOOT0 LOW, the PC13 application LED resumed blinking after approximately 5 seconds. The delay corresponds to the bootloader startup/decision period observed during the test; the measurement should be replaced with a timed value if a precise metric is required.

## Reproducibility

Use the procedures in [`SECURE_TEST_PROCEDURE.md`](../SECURE_TEST_PROCEDURE.md). Close Tera Term before running a Python tool because COM7 can be opened by only one application at a time.

Visual evidence is indexed in [`docs/evidence/README.md`](evidence/README.md).
