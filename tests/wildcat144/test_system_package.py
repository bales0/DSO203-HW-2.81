"""Read-only verification of installed W1.2 and the matching slot-4 runtime."""
from pathlib import Path
import hashlib, struct, zlib
ROOT = Path(__file__).resolve().parents[2]
checks = 0

def check(condition, label):
    global checks
    assert condition, label
    checks += 1

def read_hex(path):
    result, upper, ended = {}, 0, False
    for line in path.read_text(encoding="ascii").splitlines():
        check(line.startswith(":"), "HEX marker")
        value = bytes.fromhex(line[1:])
        check(not ended and len(value) == value[0]+5 and sum(value)%256 == 0, "HEX record/checksum/EOF order")
        count, address, kind = value[0], int.from_bytes(value[1:3], "big"), value[3]
        data = value[4:-1]
        if kind == 0:
            for i, byte in enumerate(data):
                absolute = upper + address + i
                check(absolute not in result, "unique HEX address")
                result[absolute] = byte
        elif kind == 4:
            check(address == 0 and count == 2, "extended address record")
            upper = int.from_bytes(data, "big") << 16
        elif kind == 1:
            check(address == 0 and count == 0, "EOF format")
            ended = True
        elif kind == 5:
            check(address == 0 and count == 4, "entry point format")
        else:
            raise AssertionError("unsupported HEX record")
    check(ended, "EOF present")
    return result

image = (ROOT/"bin/FPGAW1_2/FPGAW1_2.BIN").read_bytes()
source = bytes.fromhex("ff0000ff") + image
check(len(image) == 135096 and source[4:] == image, "strip dummy prefix only")
check(image[:4] == bytes.fromhex("7eaa997e"), "iCE40 sync at offset 0")
check(zlib.crc32(image) == 0x66B80BD2, "installed W1.2 CRC")
check(hashlib.sha256(source).hexdigest() == "787fff5254366c370ed571da5a5c606986160806a6e1e99164718aa5ef381ddb", "approved source hash")
address = int((ROOT/"bin/FPGAW1_2/FPGAW1_2.ADR").read_text().strip(), 0)
check(address == 0x0805C800, "original FPGA address")
check(address + len(image) <= 0x0807D800, "FPGA sector boundary")
hexdata = read_hex(ROOT/"bin/FPGAW1_2/FPGAW1_2.HEX")
check(len(hexdata) == len(image), "FPGA HEX length")
check(all(hexdata[address+i] == b for i,b in enumerate(image)), "FPGA HEX/BIN equality")
raw = (ROOT/"bin/QuadPawn/PAWN4WA.BIN").read_bytes()
address = int((ROOT/"bin/QuadPawn/PAWN4WA.ADR").read_text().strip(), 0)
check(address == 0x08024000 and 0 < len(raw) <= 32768, "QuadPawn slot-4 bounds")
hexdata = read_hex(ROOT/"bin/QuadPawn/PAWN4W.HEX")
check(len(hexdata) == len(raw), "QuadPawn HEX length")
check(all(hexdata[address+i] == b for i,b in enumerate(raw)), "QuadPawn HEX/BIN equality")
sp, entry = struct.unpack_from("<II", raw)
check(0x20003000 <= sp <= 0x20010000 and sp%8 == 0, "initial stack")
check(entry&1 and address <= entry-1 < address+len(raw), "Thumb entry in slot 4")
version = (ROOT/"QuadPawn/Runtime/build/VERSION.TXT").read_text().strip().encode("ascii")
check(version and version in raw, "configured runtime version")
# Conversion refuses an unreviewed bitstream before producing output.
namespace = {"__file__": str(ROOT/"FPGAW1_2/export-system.py"), "__name__": "audit"}
exec(compile((ROOT/"FPGAW1_2/export-system.py").read_text(), "export-system.py", "exec"), namespace)
check(namespace["installed_image"](source) == image, "approved full bitstream converts to installed image")
try:
    namespace["installed_image"](bytes([source[0]^1])+source[1:])
except ValueError:
    check(True, "unreviewed image rejected")
else:
    raise AssertionError("corrupt source accepted")
print(f"PASS system package: {checks} checks; W1.2 HEX/BIN/ADR and QuadPawn slot 4; no device programming")
