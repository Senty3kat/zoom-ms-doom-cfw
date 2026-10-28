"""Minimal pefile shim: PE resource extraction + unsign support for zoomms.

API used by parts.py.find_parts, flash.accept_models and flash._unsign:
  pe = pefile.PE(path)  or  pefile.PE(data=bytes, fast_load=True)
  pe.__data__                  # full file bytes
  pe.OPTIONAL_HEADER.ImageBase / .CheckSum / .DATA_DIRECTORY
  pe.DIRECTORY_ENTRY_RESOURCE.entries
  pe.get_offset_from_rva(rva)
  pe.write()                   # bytes with OPTIONAL_HEADER changes applied
  pe.generate_checksum()
  pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_SECURITY"]
"""
from __future__ import annotations

DIRECTORY_ENTRY = {
    "IMAGE_DIRECTORY_ENTRY_EXPORT": 0,
    "IMAGE_DIRECTORY_ENTRY_IMPORT": 1,
    "IMAGE_DIRECTORY_ENTRY_RESOURCE": 2,
    "IMAGE_DIRECTORY_ENTRY_EXCEPTION": 3,
    "IMAGE_DIRECTORY_ENTRY_SECURITY": 4,
    "IMAGE_DIRECTORY_ENTRY_BASERELOC": 5,
    "IMAGE_DIRECTORY_ENTRY_DEBUG": 6,
    "IMAGE_DIRECTORY_ENTRY_ARCHITECTURE": 7,
    "IMAGE_DIRECTORY_ENTRY_GLOBALPTR": 8,
    "IMAGE_DIRECTORY_ENTRY_TLS": 9,
    "IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG": 10,
    "IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT": 11,
    "IMAGE_DIRECTORY_ENTRY_IAT": 12,
    "IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT": 13,
    "IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR": 14,
}


def _u16(d, o):
    return int.from_bytes(d[o:o + 2], "little")


def _u32(d, o):
    return int.from_bytes(d[o:o + 4], "little")


class _E:
    def __init__(self, **kw):
        self.__dict__.update(kw)


class PE:
    def __init__(self, path=None, data=None, fast_load=False):
        if data is not None:
            self.__data__ = bytes(data)
        else:
            self.__data__ = open(path, "rb").read()
        d = self.__data__
        if d[:2] != b"MZ":
            raise ValueError("not PE")
        e_lfanew = _u32(d, 0x3C)
        if d[e_lfanew:e_lfanew + 4] != b"PE\0\0":
            raise ValueError("not PE")
        self._e_lfanew = e_lfanew
        coff = e_lfanew + 4
        nsec = _u16(d, coff + 2)
        optsz = _u16(d, coff + 16)
        self._opt = opt = coff + 20
        magic = _u16(d, opt)
        if magic == 0x10B:
            image_base = _u32(d, opt + 28)
            dd = opt + 96
            ndir = _u16(d, opt + 92)
        elif magic == 0x20B:
            image_base = int.from_bytes(d[opt + 24:opt + 32], "little")
            dd = opt + 112
            ndir = _u16(d, opt + 108)
        else:
            raise ValueError("bad magic")
        self._dd_off = dd
        dirs = []
        for i in range(16):
            o = dd + i * 8
            if i < ndir and o + 8 <= len(d):
                dirs.append(_E(VirtualAddress=_u32(d, o), Size=_u32(d, o + 4)))
            else:
                dirs.append(_E(VirtualAddress=0, Size=0))
        self.OPTIONAL_HEADER = _E(
            ImageBase=image_base,
            Magic=magic,
            CheckSum=_u32(d, opt + 64),
            DATA_DIRECTORY=dirs,
        )
        self._sects = []
        sh = opt + optsz
        for i in range(nsec):
            s = sh + i * 40
            vsz = _u32(d, s + 8)
            va = _u32(d, s + 12)
            rawsz = _u32(d, s + 16)
            raw = _u32(d, s + 20)
            self._sects.append((va, vsz, raw, rawsz))
        self._res_rva = dirs[2].VirtualAddress
        self._res_sz = dirs[2].Size
        if not fast_load:
            self._parse_res()
        else:
            self.DIRECTORY_ENTRY_RESOURCE = None

    def get_offset_from_rva(self, rva):
        for va, vsz, raw, rawsz in self._sects:
            if va <= rva < va + max(vsz, rawsz):
                return raw + (rva - va)
        return rva

    def write(self):
        d = bytearray(self.__data__)
        opt = self._opt
        oh = self.OPTIONAL_HEADER
        d[opt + 64:opt + 68] = (oh.CheckSum & 0xFFFFFFFF).to_bytes(4, "little")
        for i, e in enumerate(oh.DATA_DIRECTORY):
            o = self._dd_off + i * 8
            if o + 8 <= len(d):
                d[o:o + 4] = (e.VirtualAddress & 0xFFFFFFFF).to_bytes(4, "little")
                d[o + 4:o + 8] = (e.Size & 0xFFFFFFFF).to_bytes(4, "little")
        return bytes(d)

    def generate_checksum(self):
        data = bytearray(self.__data__)
        opt = self._opt
        data[opt + 64:opt + 68] = b"\0\0\0\0"
        s = 0
        n = len(data)
        for i in range(0, n - 1, 2):
            s += data[i] | (data[i + 1] << 8)
            s = (s & 0xFFFF) + (s >> 16)
        if n & 1:
            s += data[-1]
            s = (s & 0xFFFF) + (s >> 16)
        s = (s & 0xFFFF) + (s >> 16)
        return (s + n) & 0xFFFFFFFF

    def _rva_off(self, rva):
        return self.get_offset_from_rva(rva)

    def _read_name(self, off):
        n = _u16(self.__data__, off)
        raw = self.__data__[off + 2:off + 2 + n * 2]
        return raw.decode("utf-16le", "replace")

    def _parse_res(self):
        d = self.__data__
        if not self._res_rva:
            self.DIRECTORY_ENTRY_RESOURCE = None
            return
        base = self._rva_off(self._res_rva)

        def read_dir(file_off, depth=0):
            if depth > 5 or file_off + 16 > len(d):
                return []
            n_named = _u16(d, file_off + 12)
            n_id = _u16(d, file_off + 14)
            if n_named + n_id > 64:
                return []
            ents = []
            p = file_off + 16
            for _ in range(n_named + n_id):
                if p + 8 > len(d):
                    break
                name_or_id = _u32(d, p)
                off2 = _u32(d, p + 4)
                p += 8
                if name_or_id & 0x80000000:
                    name = self._read_name(base + (name_or_id & 0x7FFFFFFF))
                    eid = None
                else:
                    name = None
                    eid = name_or_id & 0xFFFF
                is_dir = bool(off2 & 0x80000000)
                off2 &= 0x7FFFFFFF
                e = _E(id=eid, name=name)
                if is_dir:
                    e.directory = _E(entries=read_dir(base + off2, depth + 1))
                else:
                    de = base + off2
                    if de + 16 > len(d):
                        continue
                    rva = _u32(d, de)
                    sz = _u32(d, de + 4)
                    e.data = _E(struct=_E(OffsetToData=rva, Size=sz))
                ents.append(e)
            return ents

        self.DIRECTORY_ENTRY_RESOURCE = _E(entries=read_dir(base))
