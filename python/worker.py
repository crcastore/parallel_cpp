#!/usr/bin/env python3
import argparse
from array import array
import math
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
QRC_SEED = 42


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


def apply_ry(state, qubit: int, theta: float) -> None:
    half = theta * 0.5
    c = math.cos(half)
    s = math.sin(half)
    bit = 1 << qubit
    dim = len(state)
    step = bit << 1
    for base in range(0, dim, step):
        for offset in range(bit):
            i0 = base + offset
            i1 = i0 + bit
            a0 = state[i0]
            a1 = state[i1]
            state[i0] = c * a0 - s * a1
            state[i1] = s * a0 + c * a1


def apply_rz(state, qubit: int, theta: float) -> None:
    half = theta * 0.5
    phase0 = complex(math.cos(-half), math.sin(-half))
    phase1 = complex(math.cos(half), math.sin(half))
    bit = 1 << qubit
    for idx in range(len(state)):
        if idx & bit:
            state[idx] *= phase1
        else:
            state[idx] *= phase0


def apply_rx(state, qubit: int, theta: float) -> None:
    half = theta * 0.5
    c = math.cos(half)
    s = math.sin(half)
    imag_s = complex(0.0, -s)
    bit = 1 << qubit
    dim = len(state)
    step = bit << 1
    for base in range(0, dim, step):
        for offset in range(bit):
            i0 = base + offset
            i1 = i0 + bit
            a0 = state[i0]
            a1 = state[i1]
            state[i0] = c * a0 + imag_s * a1
            state[i1] = imag_s * a0 + c * a1


def apply_cz(state, q0: int, q1: int) -> None:
    b0 = 1 << q0
    b1 = 1 << q1
    for idx in range(len(state)):
        if (idx & b0) and (idx & b1):
            state[idx] = -state[idx]


def z_expectations(state, qubits: int):
    out = [0.0] * qubits
    for idx, amp in enumerate(state):
        prob = (amp.real * amp.real) + (amp.imag * amp.imag)
        for q in range(qubits):
            out[q] += prob if ((idx >> q) & 1) == 0 else -prob
    return out


def build_reservoir_params(qubits: int, layers: int, seed: int):
    params = []
    for layer in range(layers):
        row = []
        for q in range(qubits):
            # Deterministic pseudo-random angles from integer hashes.
            h = (seed + 101 * (layer + 1) + 1009 * (q + 1)) & 0xFFFFFFFF
            rx = ((h % 10007) / 10007.0) * (2.0 * math.pi)
            rz = (((h * 17 + 23) % 10009) / 10009.0) * (2.0 * math.pi)
            row.append((rx, rz))
        params.append(row)
    return params


def process_row(values, start: int, cols: int, qubits: int, layers: int, reservoir):
    dim = 1 << qubits
    state = [0j] * dim
    state[0] = 1.0 + 0.0j

    def feature_angle(index: int) -> float:
        v = values[start + (index % cols)]
        return math.pi * math.tanh(v)

    for q in range(qubits):
        theta = feature_angle(q)
        apply_ry(state, q, theta)
        apply_rz(state, q, 0.5 * theta)

    if qubits > 1:
        for q in range(qubits):
            apply_cz(state, q, (q + 1) % qubits)

    for layer in range(layers):
        for q in range(qubits):
            base_rx, base_rz = reservoir[layer][q]
            injection = 0.35 * feature_angle(q + layer)
            apply_rx(state, q, base_rx + injection)
            apply_rz(state, q, base_rz - 0.25 * injection)
        if qubits > 1:
            for q in range(qubits):
                apply_cz(state, q, (q + 1) % qubits)

    return z_expectations(state, qubits)


def main() -> int:
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--qrc-layers", type=int, default=2)
    args, _ = parser.parse_known_args()

    if args.qrc_layers < 1:
        return 1

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
        qubits = int(cols)
        if qubits <= 0:
            send_error(stdout, task_id, "cols must be > 0")
            return 1

        total_values = rows * cols
        payload_nbytes = total_values * 8

        payload = read_exact(stdin, payload_nbytes)
        if not payload:
            return 1

        vals = array("d")
        vals.frombytes(payload)
        if sys.byteorder != "little":
            vals.byteswap()

        reservoir = build_reservoir_params(qubits, args.qrc_layers, QRC_SEED)
        expectations = []
        offset = 0
        for _ in range(rows):
            expectations.extend(
                process_row(vals, offset, cols, qubits, args.qrc_layers, reservoir)
            )
            offset += cols

        result_header = struct.pack(HEADER_FMT, MAGIC, VERSION, MSG_RESULT, task_id, rows, qubits)
        out = array("d", expectations)
        if sys.byteorder != "little":
            out.byteswap()
        result_payload = out.tobytes()
        stdout.write(result_header)
        stdout.write(result_payload)
        stdout.flush()


if __name__ == "__main__":
    raise SystemExit(main())
