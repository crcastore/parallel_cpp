#!/usr/bin/env python3
"""Quantum reservoir computing worker using Qiskit Aer simulator."""
import os

# Pin all threading to 1 — must be set BEFORE importing numpy/qiskit so that
# BLAS/OpenMP libraries pick up the values at load time.
os.environ["OMP_NUM_THREADS"] = "1"
os.environ["OPENBLAS_NUM_THREADS"] = "1"
os.environ["MKL_NUM_THREADS"] = "1"
os.environ["BLAS_NUM_THREADS"] = "1"
os.environ["VECLIB_MAXIMUM_THREADS"] = "1"
os.environ["NUMEXPR_NUM_THREADS"] = "1"

import argparse
from array import array
import math
import struct
import sys

from qiskit import QuantumCircuit
from qiskit_aer import AerSimulator

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


def build_reservoir_params(qubits: int, layers: int, seed: int):
    """Generate deterministic pseudo-random rotation angles for reservoir layers."""
    params = []
    for layer in range(layers):
        row = []
        for q in range(qubits):
            h = (seed + 101 * (layer + 1) + 1009 * (q + 1)) & 0xFFFFFFFF
            rx = ((h % 10007) / 10007.0) * (2.0 * math.pi)
            rz = (((h * 17 + 23) % 10009) / 10009.0) * (2.0 * math.pi)
            row.append((rx, rz))
        params.append(row)
    return params


def build_circuit(values, start: int, cols: int, qubits: int, layers: int, reservoir):
    """Construct a Qiskit QuantumCircuit for one row of data."""
    qc = QuantumCircuit(qubits, qubits)

    def feature_angle(index: int) -> float:
        v = values[start + (index % cols)]
        return math.pi * math.tanh(v)

    # Feature encoding: RY + RZ on each qubit
    for q in range(qubits):
        theta = feature_angle(q)
        qc.ry(theta, q)
        qc.rz(0.5 * theta, q)

    # Entangling layer: CZ ring
    if qubits > 1:
        for q in range(qubits):
            qc.cz(q, (q + 1) % qubits)

    # Reservoir layers
    for layer in range(layers):
        for q in range(qubits):
            base_rx, base_rz = reservoir[layer][q]
            injection = 0.35 * feature_angle(q + layer)
            qc.rx(base_rx + injection, q)
            qc.rz(base_rz - 0.25 * injection, q)
        if qubits > 1:
            for q in range(qubits):
                qc.cz(q, (q + 1) % qubits)

    # Measure all qubits in the Z basis
    qc.measure(range(qubits), range(qubits))
    return qc


def compute_z_expectations(counts, qubits: int, shots: int):
    """Compute <Z> expectation for each qubit from measurement counts."""
    expectations = [0.0] * qubits
    for bitstring, count in counts.items():
        # bitstring is e.g. '101' where bit 0 is qubit 0 (little-endian from Qiskit)
        for q in range(qubits):
            bit_val = int(bitstring[q])
            expectations[q] += count * (1.0 - 2.0 * bit_val)
    return [e / shots for e in expectations]


def process_row(simulator, values, start: int, cols: int, qubits: int, layers: int, reservoir, shots: int):
    """Run one row through the quantum reservoir and return Z expectations."""
    qc = build_circuit(values, start, cols, qubits, layers, reservoir)
    result = simulator.run(qc, shots=shots).result()
    counts = result.get_counts()
    return compute_z_expectations(counts, qubits, shots)


def main() -> int:
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--qrc-layers", type=int, default=2)
    parser.add_argument("--shots", type=int, default=8192,
                        help="Number of shots for Aer simulator measurements")
    args, _ = parser.parse_known_args()

    if args.qrc_layers < 1:
        return 1

    simulator = AerSimulator(method="statevector", max_parallel_threads=1)

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
                process_row(simulator, vals, offset, cols, qubits, args.qrc_layers, reservoir, args.shots)
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
