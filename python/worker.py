#!/usr/bin/env python3
import struct
import sys

MAGIC = 0x50595043  # "CPYP"
VERSION = 1

MSG_TASK = 1
MSG_QUIT = 2
MSG_RESULT = 3
MSG_ERROR = 4

HEADER_FMT = "<IHHQQQ"
HEADER_SIZE = struct.calcsize(HEADER_FMT)


def read_exact(stream, nbytes: int) -> bytes:
    chunks = bytearray()
    while len(chunks) < nbytes:
        part = stream.read(nbytes - len(chunks))
        if not part:
            return b""
        chunks.extend(part)
    return bytes(chunks)


def send_error(stdout, task_id: int, message: str) -> None:
    payload = message.encode("utf-8")
    header = struct.pack(HEADER_FMT, MAGIC, VERSION, MSG_ERROR, task_id, 0, len(payload))
    stdout.write(header)
    if payload:
        stdout.write(payload)
    stdout.flush()


def main() -> int:
    stdin = sys.stdin.buffer
    stdout = sys.stdout.buffer

    while True:
        header_bytes = read_exact(stdin, HEADER_SIZE)
        if not header_bytes:
            return 0

        magic, version, msg_type, task_id, rows, cols_or_aux = struct.unpack(HEADER_FMT, header_bytes)

        if magic != MAGIC or version != VERSION:
            return 1

        if msg_type == MSG_QUIT:
            return 0

        if msg_type != MSG_TASK:
            send_error(stdout, task_id, f"unexpected message type: {msg_type}")
            return 1

        cols = cols_or_aux
        total_values = rows * cols
        payload_nbytes = total_values * 8

        payload = read_exact(stdin, payload_nbytes)
        if not payload:
            return 1

        vals = struct.unpack("<" + "d" * total_values, payload)

        row_sums = []
        offset = 0
        for _ in range(rows):
            row_total = 0.0
            for _ in range(cols):
                row_total += vals[offset]
                offset += 1
            row_sums.append(row_total)

        result_header = struct.pack(HEADER_FMT, MAGIC, VERSION, MSG_RESULT, task_id, rows, 0)
        result_payload = struct.pack("<" + "d" * rows, *row_sums)
        stdout.write(result_header)
        stdout.write(result_payload)
        stdout.flush()


if __name__ == "__main__":
    raise SystemExit(main())
