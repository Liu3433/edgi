"""Validate Intel HEX checksums, flash boundaries and M33 address relocation."""
from pathlib import Path
import hashlib

PROJECT = Path(__file__).resolve().parents[2]
M33 = PROJECT.parent / "Edgi_Talk_M33_IPC"


def read_hex(path):
    memory = {}
    base = 0
    eof = False
    for line in path.read_text(encoding="ascii").splitlines():
        assert not eof and line.startswith(":"), path
        record = bytes.fromhex(line[1:])
        assert len(record) == record[0] + 5 and sum(record) % 256 == 0, path
        address = int.from_bytes(record[1:3], "big")
        kind, payload = record[3], record[4:-1]
        if kind == 0:
            for offset, value in enumerate(payload):
                target = base + address + offset
                assert target not in memory or memory[target] == value, path
                memory[target] = value
        elif kind == 1:
            eof = True
        elif kind == 2:
            base = int.from_bytes(payload, "big") << 4
        elif kind == 4:
            base = int.from_bytes(payload, "big") << 16
        else:
            assert kind in (3, 5), path
    assert eof and memory, path
    return memory


def check(path, lower, upper):
    memory = read_hex(path)
    assert min(memory) >= lower and max(memory) < upper, path
    print("PASS:", path.name, len(memory), "bytes", hex(min(memory)), "..", hex(max(memory)))
    print("SHA256:", hashlib.sha256(path.read_bytes()).hexdigest())
    return memory


if __name__ == "__main__":
    check(PROJECT / "Debug/rtthread.hex", 0x60580000, 0x60D80000)
    relocated = check(M33 / "Debug/robot_m33_app.hex", 0x60340000, 0x60540000)
    source = read_hex(M33 / "rtthread.hex")
    assert {address + 0x58000000: value for address, value in source.items()} == relocated
    print("PASS: M33 relocation changes record addresses only; all payload bytes identical")
    check(PROJECT / "build/robot_uno_verify/EdgiTalkUnoServo.hex", 0, 0x7E00)
