"""Negative test for the authenticated update path."""

import struct
import sys
import time

import serial

PORT = "COM7"
BAUDRATE = 115200
SOF = 0xAA
CMD_AUTH_CHALLENGE = 0x0C
CMD_AUTH_RESPONSE = 0x0D
CMD_BEGIN_UPDATE = 0x08
NACK_AUTH = 0x05


def crc32_stm32(data):
    crc = 0xFFFFFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0xEDB88320 if crc & 1 else crc >> 1
    return crc ^ 0xFFFFFFFF


def packet(command, payload=b""):
    body = bytes([command]) + struct.pack("<H", len(payload)) + payload
    return bytes([SOF]) + body + struct.pack("<I", crc32_stm32(body))


def read_exact(ser, count, deadline):
    data = bytearray()
    while len(data) < count and time.monotonic() < deadline:
        data.extend(ser.read(count - len(data)))
    if len(data) != count:
        raise TimeoutError("response timeout")
    return bytes(data)


def response(ser, expected_command):
    deadline = time.monotonic() + 3.0
    while time.monotonic() < deadline:
        if read_exact(ser, 1, deadline)[0] != SOF:
            continue
        command = read_exact(ser, 1, deadline)[0]
        length = struct.unpack("<H", read_exact(ser, 2, deadline))[0]
        payload = read_exact(ser, length, deadline)
        received = struct.unpack("<I", read_exact(ser, 4, deadline))[0]
        body = bytes([command]) + struct.pack("<H", length) + payload
        if command == expected_command and received == crc32_stm32(body):
            return payload
    raise TimeoutError("no matching response")


def main():
    with serial.Serial(PORT, BAUDRATE, timeout=0.1, write_timeout=2) as ser:
        ser.reset_input_buffer()
        input("COM7 is ready. Press RESET now, then press Enter here: ")
        ser.reset_input_buffer()

        ser.write(packet(CMD_AUTH_CHALLENGE))
        ser.flush()
        challenge = response(ser, CMD_AUTH_CHALLENGE | 0x80)
        if len(challenge) != 17 or challenge[0] != 0:
            raise RuntimeError("invalid challenge response")
        print("Challenge: PASS")

        ser.write(packet(CMD_AUTH_RESPONSE, bytes(32)))
        ser.flush()
        auth_result = response(ser, CMD_AUTH_RESPONSE | 0x80)
        if auth_result != bytes([NACK_AUTH]):
            raise RuntimeError(f"wrong-key response was {auth_result.hex()}")
        print("Wrong key rejected: PASS")

        begin = struct.pack("<III", 999, 4604, 0)
        ser.write(packet(CMD_BEGIN_UPDATE, begin))
        ser.flush()
        begin_result = response(ser, CMD_BEGIN_UPDATE | 0x80)
        if begin_result != bytes([NACK_AUTH]):
            raise RuntimeError(f"unauthenticated update was {begin_result.hex()}")
        print("Unauthenticated update rejected: PASS")

    print("Secure negative-path tests passed; application Flash was not modified.")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"TEST FAILED: {error}")
        sys.exit(1)
