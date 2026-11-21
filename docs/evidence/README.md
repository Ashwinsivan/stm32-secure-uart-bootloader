# Hardware Evidence

These files document the custom STM32F103C8T6 UART bootloader tests performed with a CP2102 USB-to-UART bridge.

Video demonstration: [Google Drive evidence folder](https://drive.google.com/drive/u/0/folders/1HsUetWEzfH-januyjNme9uaeLOIFNop0)

## Evidence index

1. [`01_bootloader_banner.png`](01_bootloader_banner.png) - Custom bootloader startup banner after reset with BOOT0 LOW.
2. [`02_uart_commands.png`](02_uart_commands.png) - Text command interface showing version, device ID, Flash-size response, and authentication protection.
3. [`03_secure_negative_tests.png`](03_secure_negative_tests.png) - Challenge-response authentication, wrong-key rejection, and unauthenticated-update rejection.
4. [`04_secure_upload_and_launch.png`](04_secure_upload_and_launch.png) - Authenticated 4604-byte application upload, CRC verification, metadata commit, and application launch.
5. [`05_final_negative_tests.png`](05_final_negative_tests.png) - Final invalid-CRC and authenticated invalid-address rejection tests.
The screenshots contain local terminal paths and the COM port used during development. No authentication key, password, or private credential is included. The obsolete intermediate failed-address screenshot was intentionally excluded.
