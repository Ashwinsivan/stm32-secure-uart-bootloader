# Secure Bootloader Hardware Test Procedure

## Hardware setup

- STM32F103C8T6 Blue Pill
- CP2102 connected: TXD -> PA10, RXD -> PA9, GND -> GND
- COM7, 115200 baud, 8 data bits, no parity, 1 stop bit
- BOOT0 LOW for normal custom-bootloader operation

Close Tera Term before running a Python test. Only one program may own COM7.

## 1. Install the secured bootloader

1. Set BOOT0 HIGH.
2. Use STM32CubeProgrammer UART on COM7 at 115200.
3. Program `Debug/STM32F103_USART_Bootloader.bin` at `0x08000000`.
4. Enable verification.
5. Set BOOT0 LOW and press RESET.
6. Open Tera Term and confirm one startup banner.

## 2. Authentication negative test

With BOOT0 LOW and the banner visible, close Tera Term and run:

```powershell
py Tools\bootloader_secure_tests.py
```

Expected results:

```text
Challenge: PASS
Wrong key rejected: PASS
Unauthenticated update rejected: PASS
Secure negative-path tests passed; application Flash was not modified.
```

## 3. Authenticated firmware update

Run the uploader with a version number:

```powershell
py Tools\secure_upload_firmware.py --version 1
```

The uploader authenticates, erases the application region, writes the image,
checks the complete image CRC, commits metadata, and launches the application.
The PC13 LED should blink at the application's configured interval.

## 4. Resume test

Start a deliberately interrupted version 2 update:

```powershell
py Tools\secure_upload_firmware.py --version 2 --stop-after 16
```

Reset the board, then run the same command without `--stop-after`:

```powershell
py Tools\secure_upload_firmware.py --version 2
```

Expected result: the uploader reports a non-zero resume packet or byte offset,
finishes the image, verifies CRC, commits it, and launches the application.

## 5. Anti-rollback test

After version 2 is committed, attempt version 1 again:

```powershell
py Tools\secure_upload_firmware.py --version 1
```

Expected result: the bootloader rejects the update and does not erase or modify
the installed application.

## Development-key limitation

The HMAC key is intentionally embedded as a development key for this learning
project. A production design must provision a device-specific secret or use
signed images with a protected public key.
