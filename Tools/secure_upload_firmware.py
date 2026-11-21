"""Versioned, resumable uploader for the advanced STM32 bootloader protocol."""

import argparse
import struct
import time
from pathlib import Path

import serial
import hmac
import hashlib
from auth_key_local import UPDATE_KEY

PORT = "COM7"
BAUDRATE = 115200
APP_START = 0x08004000
APP_END = 0x0800FC00
SOF = 0xAA
CMD_GET_APP_INFO = 0x07
CMD_BEGIN_UPDATE = 0x08
CMD_WRITE_DATA_V2 = 0x09
CMD_GET_PROGRESS = 0x0A
CMD_END_UPDATE = 0x0B
CMD_AUTH_CHALLENGE = 0x0C
CMD_AUTH_RESPONSE = 0x0D
CMD_JUMP_TO_APP = 0x06
ACK = 0x00
STATE_EMPTY = 0
STATE_ACTIVE = 1
STATE_VALID = 2
CHUNK_SIZE = 26
MAX_PAYLOAD = 32
REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
APPLICATION_ROOT = REPOSITORY_ROOT.parent / "STM32F103_Bootloader_Application"
DEFAULT_FIRMWARE = (APPLICATION_ROOT / "Debug" /
                    "STM32F103_Bootloader_Application.bin")


def crc32_stm32(data):
    crc = 0xFFFFFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0xEDB88320 if crc & 1 else crc >> 1
    return crc ^ 0xFFFFFFFF


def packet(command, payload=b""):
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("payload exceeds 32 bytes")
    body = bytes([command]) + struct.pack("<H", len(payload)) + payload
    return bytes([SOF]) + body + struct.pack("<I", crc32_stm32(body))


def read_exact(ser, count, deadline):
    result = bytearray()
    while len(result) < count:
        if time.monotonic() >= deadline:
            raise TimeoutError(f"expected {count} bytes, received {len(result)}")
        result.extend(ser.read(count - len(result)))
    return bytes(result)


def receive_frame(ser, expected_command):
    deadline = time.monotonic() + 4.0
    while True:
        if read_exact(ser, 1, deadline)[0] != SOF:
            continue
        command = read_exact(ser, 1, deadline)[0]
        length = struct.unpack("<H", read_exact(ser, 2, deadline))[0]
        if length > MAX_PAYLOAD:
            continue
        payload = read_exact(ser, length, deadline)
        received = struct.unpack("<I", read_exact(ser, 4, deadline))[0]
        body = bytes([command]) + struct.pack("<H", length) + payload
        if received != crc32_stm32(body) or command != expected_command:
            continue
        return payload


def request(ser, command, payload=b""):
    ser.write(packet(command, payload))
    ser.flush()
    response = receive_frame(ser, command | 0x80)
    if not response or response[0] != ACK:
        raise RuntimeError(f"bootloader NACK for 0x{command:02X}: {response.hex()}")
    return response


def app_info(ser):
    response = request(ser, CMD_GET_PROGRESS)
    if len(response) != 25:
        raise RuntimeError(f"unexpected app-info length: {len(response)}")
    return {
        "state": response[1],
        "version": struct.unpack_from("<I", response, 5)[0],
        "size": struct.unpack_from("<I", response, 9)[0],
        "crc": struct.unpack_from("<I", response, 13)[0],
        "sequence": struct.unpack_from("<I", response, 17)[0],
        "written": struct.unpack_from("<I", response, 21)[0],
    }


def authenticate(ser):
    response = request(ser, CMD_AUTH_CHALLENGE)
    if len(response) != 17:
        raise RuntimeError("invalid authentication challenge")
    nonce = response[1:]
    digest = hmac.new(UPDATE_KEY, nonce, hashlib.sha256).digest()
    request(ser, CMD_AUTH_RESPONSE, digest)
    print("Authentication: PASS")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--firmware", type=Path, default=DEFAULT_FIRMWARE)
    parser.add_argument("--version", type=int, required=True,
                        help="monotonically increasing firmware version")
    parser.add_argument("--stop-after", type=int, default=0,
                        help="simulate interruption after this many packets")
    parser.add_argument("--status-only", action="store_true",
                        help="authenticate and print device update status without modifying Flash")
    args = parser.parse_args()

    image = args.firmware.read_bytes()
    if not image or len(image) > APP_END - APP_START:
        raise RuntimeError("invalid image size")
    image_crc = crc32_stm32(image)
    chunks = [image[i:i + CHUNK_SIZE] for i in range(0, len(image), CHUNK_SIZE)]

    print(f"Firmware : {args.firmware}")
    print(f"Version  : {args.version}")
    print(f"Size/CRC : {len(image)} / 0x{image_crc:08X}")

    with serial.Serial(PORT, BAUDRATE, 8, serial.PARITY_NONE, 1,
                       timeout=0.10, write_timeout=2) as ser:
        time.sleep(0.3)
        print("COM7 is ready. Press RESET now, then press Enter here.")
        input()
        ser.reset_input_buffer()

        authenticate(ser)
        info = app_info(ser)
        print("Device status: state={state}, version={version}, size={size}, "
              "crc=0x{crc:08X}, next_sequence={sequence}, written={written}".format(**info))
        if args.status_only:
            return
        if not (info["state"] == STATE_ACTIVE and
                info["version"] == args.version and
                info["size"] == len(image) and
                info["crc"] == image_crc):
            print("Starting a new update...")
            request(ser, CMD_BEGIN_UPDATE,
                    struct.pack("<III", args.version, len(image), image_crc))
            info = app_info(ser)

        sequence = info["sequence"]
        offset = info["written"]
        if offset > len(image) or sequence > len(chunks):
            raise RuntimeError("invalid device progress")
        print(f"Resuming at packet {sequence}, byte {offset}")

        for index in range(sequence, len(chunks)):
            chunk = chunks[index]
            payload = struct.pack("<IH", APP_START + offset, index) + chunk
            request(ser, CMD_WRITE_DATA_V2, payload)
            offset += len(chunk)
            print(f"\rProgress: {offset * 100 // len(image):3d}% ({offset}/{len(image)})",
                  end="", flush=True)
            if args.stop_after and (index + 1) >= args.stop_after:
                print("\nIntentional interruption. Run the same command again to resume.")
                return

        request(ser, CMD_END_UPDATE)
        print("\nImage CRC verified and metadata committed.")
        request(ser, CMD_JUMP_TO_APP)
        print("Application launched.")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"UPLOAD FAILED: {error}")
