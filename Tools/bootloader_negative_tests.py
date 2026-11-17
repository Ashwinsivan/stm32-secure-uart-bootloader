import struct
import sys
import time

import serial


PORT = "COM7"
BAUDRATE = 115200
SOF = 0xAA
CMD_WRITE_DATA = 0x05
RESPONSE_WRITE_DATA = CMD_WRITE_DATA | 0x80
NACK_CRC = 0x03
NACK_FLASH = 0x04


def crc32_stm32(data):
    crc = 0xFFFFFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc >> 1) ^ 0xEDB88320) if (crc & 1) else (crc >> 1)
    return crc ^ 0xFFFFFFFF


def make_packet(command, payload=b""):
    body = bytes([command]) + struct.pack("<H", len(payload)) + payload
    return bytes([SOF]) + body + struct.pack("<I", crc32_stm32(body))


def read_exact(ser, count, deadline):
    data = bytearray()
    while len(data) < count and time.monotonic() < deadline:
        data.extend(ser.read(count - len(data)))
    if len(data) != count:
        raise TimeoutError(f"Expected {count} bytes, received {len(data)}")
    return bytes(data)


def receive_response(ser, deadline):
    while time.monotonic() < deadline:
        if read_exact(ser, 1, deadline)[0] != SOF:
            continue
        command = read_exact(ser, 1, deadline)[0]
        length = struct.unpack("<H", read_exact(ser, 2, deadline))[0]
        payload = read_exact(ser, length, deadline)
        received_crc = struct.unpack("<I", read_exact(ser, 4, deadline))[0]
        body = bytes([command]) + struct.pack("<H", length) + payload
        if received_crc == crc32_stm32(body):
            return command, payload
    raise TimeoutError("No valid response frame received")


def send_and_check(ser, packet, expected_status, label):
    ser.write(packet)
    ser.flush()
    command, payload = receive_response(ser, time.monotonic() + 3.0)
    if command != RESPONSE_WRITE_DATA or payload != bytes([expected_status]):
        raise RuntimeError(
            f"{label} failed: command=0x{command:02X}, payload={payload.hex()}"
        )
    print(f"{label}: PASS")


def main():
    with serial.Serial(PORT, BAUDRATE, timeout=0.1, write_timeout=2) as ser:
        ser.reset_input_buffer()
        input("COM7 is ready. Press RESET now, then press Enter here: ")
        ser.reset_input_buffer()
        time.sleep(0.1)

        valid = make_packet(CMD_WRITE_DATA, struct.pack("<I", 0x08004000) + b"\xAA")
        invalid_crc = bytearray(valid)
        invalid_crc[-1] ^= 0xFF
        send_and_check(ser, invalid_crc, NACK_CRC, "Invalid CRC")

        invalid_address = make_packet(
            CMD_WRITE_DATA, struct.pack("<I", 0x08000000) + b"\xAA"
        )
        send_and_check(ser, invalid_address, NACK_FLASH, "Invalid address")

    print("Negative-path tests passed; no application Flash was modified.")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"TEST FAILED: {error}")
        sys.exit(1)
