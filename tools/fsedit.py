"""Add / remove files in a Zoom MS file system image (updater part BIN/137) for the emulator.

    python -I fsedit.py IN.bin OUT.bin [--add F.ZDL ...] [--remove NAME ...] [--free-for F.ZDL ...]
                        [--list]

Layout (Zoom-Firmware-Editor / zoomms updater.py): blocks of 4 KiB, file tables at blocks
3/5/7/9 (live copy has 0xFF at +4), 32-byte entries (u16 first block, u32 size, name),
data block N at image block 10+N with a 6-byte header (u16 prev, u16 next, u16 length).
Added effects are also listed in FLST_SEQ.ZDT under their type (">>>" T ... "<<<" T,
13-byte names), as Effect Manager does. --free-for removes stock delay/reverb ZDLs
(largest first) until the given files fit. Only for emulator images: never flash these.
Standard library only.
"""
import argparse
import struct

BLOCK, PAYLOAD, END = 4096, 4090, 0xFFFF
FIRST, TABLES, ENTRY = 10, (3, 5, 7, 9), 32


class Fs:
    def __init__(self, img):
        self.img = bytearray(img)
        n = struct.unpack_from("<H", img, 8)[0]
        assert img[:6] == bytes.fromhex("55AA00010400") and len(img) == n * BLOCK
        live = [b for b in TABLES if img[b * BLOCK + 1] == 0xA5 and img[b * BLOCK + 5] == 0xFF
                and img[b * BLOCK + 4] == 0xFF]
        self.table = live[0]
        self.files = {}                     # name -> [size, blocks, content]
        self.order = []
        base = self.table * BLOCK
        for pos in range(base + 8, base + 2 * BLOCK, ENTRY):
            raw = img[pos:pos + ENTRY]
            if raw[:8] == b"\xff" * 8 or not raw[8:20].split(b"\0")[0]:
                continue
            name = raw[8:20].split(b"\0")[0].decode("latin-1")
            first, size = struct.unpack_from("<H", raw, 0)[0], struct.unpack_from("<I", raw, 4)[0]
            blocks, data, b = [], b"", first
            while b != END:
                off = (FIRST + b) * BLOCK
                _, nxt, ln = struct.unpack_from("<HHH", img, off)
                blocks.append(b)
                data += img[off + 6:off + 6 + ln]
                b = nxt
            assert len(data) == size, name
            self.files[name] = [raw, blocks, data]
            self.order.append(name)
        self.nblocks = n - FIRST

    def free(self):
        used = {b for f in self.files.values() for b in f[1]} | {0}
        return [b for b in range(1, self.nblocks) if b not in used]

    def remove(self, name):
        self.order.remove(name)
        del self.files[name]

    def add(self, name, data):
        if name in self.files:
            self.remove(name)
        need = max(1, -(-len(data) // PAYLOAD))
        free = self.free()
        if len(free) < need:
            raise SystemExit(f"{name}: needs {need} blocks, {len(free)} free")
        raw = bytearray(b"\xff" * ENTRY)          # as the stock entries: first, 01 FF, size, name
        struct.pack_into("<HBBI", raw, 0, free[0], 1, 0xFF, len(data))
        raw[8:8 + 12] = name.encode("latin-1").ljust(12, b"\0")
        self.files[name] = [bytes(raw), free[:need], data]
        self.order.append(name)

    def image(self):
        out = self.img
        pos = self.table * BLOCK
        out[pos + 8:pos + 2 * BLOCK] = b"\xff" * (2 * BLOCK - 8)
        for i, name in enumerate(self.order):
            raw, blocks, data = self.files[name]
            raw = bytearray(raw)
            struct.pack_into("<H", raw, 0, blocks[0])
            struct.pack_into("<I", raw, 4, len(data))
            out[pos + 8 + i * ENTRY:pos + 8 + (i + 1) * ENTRY] = raw
            for j, b in enumerate(blocks):
                off = (FIRST + b) * BLOCK
                chunk = data[j * PAYLOAD:(j + 1) * PAYLOAD]
                out[off:off + BLOCK] = b"\xff" * BLOCK
                out[off:off + 6] = struct.pack("<HHH", blocks[j - 1] if j else END,
                                               blocks[j + 1] if j + 1 < len(blocks) else END, len(chunk))
                out[off + 6:off + 6 + len(chunk)] = chunk
        return bytes(out)


def zdl_type(data):
    """ZDL INFO real_type (byte 28+32+4 of the file header, as zoomms zdl.py)."""
    return struct.unpack_from("<I", data, 28 + 32)[0] & 0xFF


def flst_add(flst, name, typ):
    """Insert NAME (13-byte field) at the end of the type's section of FLST_SEQ.ZDT."""
    size = len(flst)
    body = bytearray(flst.rstrip(b"\0"))
    entry = name.encode("latin-1").ljust(13, b"\0")
    end = bytes([0x3C] * 3) + b"\0" + bytes([typ])
    at = body.find(end)
    if at < 0:                                      # no section yet: append one before the padding
        body += b">>>\0" + bytes([typ]) + b"\0" * 8 + entry + b"<<<\0" + bytes([typ]) + b"\0" * 8
    else:
        body[at:at] = entry
    assert len(body) <= size, "FLST_SEQ.ZDT full"
    return bytes(body) + b"\0" * (size - len(body))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("inp")
    ap.add_argument("out")
    ap.add_argument("--add", nargs="*", default=[])
    ap.add_argument("--remove", nargs="*", default=[])
    ap.add_argument("--free-for", action="store_true", help="remove stock delays/reverbs to make room")
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args()
    fs = Fs(open(a.inp, "rb").read())
    for n in a.remove:
        fs.remove(n)
    def fsname(p):                                   # 8.3, as the pedal stores it
        base, _, ext = p.replace("\\", "/").split("/")[-1].upper().rpartition(".")
        return f"{base[:8]}.{ext[:3]}"

    adds = [(fsname(p), open(p, "rb").read()) for p in a.add]
    need = sum(max(1, -(-len(d) // PAYLOAD)) for n, d in adds if n not in fs.files)
    if a.free_for:
        victims = sorted((n for n in fs.order if n.endswith(".ZDL") and zdl_type(fs.files[n][2]) in (8, 9)
                          and n not in dict(adds)), key=lambda n: -len(fs.files[n][2]))
        while len(fs.free()) < need and victims:
            v = victims.pop(0)
            fs.remove(v)
            flst = fs.files["FLST_SEQ.ZDT"][2]
            ent = v.encode("latin-1").ljust(13, b"\0")
            i = flst.find(ent)
            if i >= 0:
                flst = flst[:i] + flst[i + 13:] + b"\0" * 13
            fs.files["FLST_SEQ.ZDT"][2] = flst
            print("removed", v)
    for n, d in adds:
        new = n not in fs.files
        fs.add(n, d)
        if new and "FLST_SEQ.ZDT" in fs.files:
            fs.files["FLST_SEQ.ZDT"][2] = flst_add(fs.files["FLST_SEQ.ZDT"][2], n, zdl_type(d))
    if a.list:
        for n in fs.order:
            print(f"{n:<14} {len(fs.files[n][2]):>7}")
    print(f"{len(fs.order)} files, {len(fs.free())} free blocks")
    open(a.out, "wb").write(fs.image())


main()
