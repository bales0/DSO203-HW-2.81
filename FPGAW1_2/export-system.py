"""Prepare the installed W1.2 image; never access the device."""
from pathlib import Path
import hashlib, zlib
ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "build/FPGAW1_2/FIFO.bin"
EXPECTED = "787fff5254366c370ed571da5a5c606986160806a6e1e99164718aa5ef381ddb"
ADDRESS = 0x0805C800
LIMIT = 0x0807D800

def installed_image(data):
    if hashlib.sha256(data).hexdigest() != EXPECTED:
        raise ValueError("Unreviewed W1.2 build; update the approved manifest first")
    if len(data) != 135100 or data[:8] != bytes.fromhex("ff0000ff7eaa997e"):
        raise ValueError("Wrong iCE40 configuration format")
    raw = data[4:]
    if len(raw) != 135096 or ADDRESS + len(raw) > LIMIT:
        raise ValueError("Image outside the original FPGA region")
    return raw

def record(address, kind, data):
    value = bytes([len(data), address >> 8, address & 255, kind]) + data
    return ":" + (value + bytes([-sum(value) & 255])).hex().upper()

def hex_image(data):
    rows, upper = [], None
    for i in range(0, len(data), 16):
        address = ADDRESS + i
        if address >> 16 != upper:
            upper = address >> 16
            rows.append(record(0, 4, upper.to_bytes(2, "big")))
        # 16-byte records never straddle a 64KiB boundary.
        rows.append(record(address & 65535, 0, data[i:i+16]))
    return "\r\n".join(rows + [record(0, 1, b"")]) + "\r\n"

def main():
    raw = installed_image(SOURCE.read_bytes())
    target = ROOT / "bin/FPGAW1_2"
    target.mkdir(parents=True, exist_ok=True)
    (target / "FPGAW1_2.BIN").write_bytes(raw)
    (target / "FPGAW1_2.ADR").write_text("0x0805C800\r\n", encoding="ascii", newline="")
    (target / "FPGAW1_2.HEX").write_text(hex_image(raw), encoding="ascii", newline="")
    print(f"PASS W1.2 system image: {len(raw)} B, {ADDRESS:#010x}..{ADDRESS+len(raw)-1:#010x}")
    print(f"CRC32 {zlib.crc32(raw):08X}; SHA256 {hashlib.sha256(raw).hexdigest()}")

if __name__ == "__main__":
    main()
