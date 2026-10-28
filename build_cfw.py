#!/usr/bin/env python3
"""Build the Doom CFW for the MS-60B / MS-70CDR (MS-70CDR 2.10 OS underneath).

    python -I build_cfw.py [--emu DIR]

Pieces (see loader/loader.c for the boot sequence):
  out/doom_pedal.out    Doom built with ZD_PEDAL, linked at 0xC0000000 (lnk_pedal.cmd)
  out/DOOM.BIN          its loadable segments: "ZDOM", entry, count, {addr, len, data}
  out/DOOM.WDZ          "ZWDZ", raw size, raw-deflated doom1.wad
  out/fs_doom.bin       BIN/137: the stock file system with every file removed and
                        DOOM.BIN, DOOM.WDZ and an empty FLST_SEQ.ZDT added
  out/loader.out        loader in shared RAM 0x80000000
  patches/doom-*.yaml   loader + trampoline + hook (callp at 0xC00BB144), logo, no "v2.10"
  out/DialUp-DOOM-ms70cdr-2.10.exe   updater: OS + BIN/133 + BIN/137, bootloader skipped.
Nothing is flashed. To go back: hold up+down at power-on and run any updater.
"""
import struct
import subprocess
import sys
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE
sys.path.insert(0, str(ROOT))
from paths import CL6X, DOOM_WAD as WAD, TI_ROOT as TI, UPDATER_70CDR as UPDATER, ZOOMMS  # noqa: E402
STOCK = ROOT / "stock" / "ms70cdr-2.10"     # stock OS parts, extracted from UPDATER on first use
MODEL = "ms70cdr-2.10"
OUT = HERE / "out"
EXE = OUT / "DialUp-DOOM-ms70cdr-2.10.exe"

TRAMP = 0xC00F4000            # free DDR next to the OS, in reach of a callp from the hook
HOOK_SITE = 0xC00BB144        # boot step: callp 0xC00CD680 (boot screen)
HOOK_STOCK = "12a82410"       # = callp 0xC00CD680 from fetch packet 0xC00BB140
SDRAM_END = 0xC0800000

sys.path.insert(0, str(HERE))
import build as doom_build    # noqa: E402  (compile helpers and the source list)


def elf_sections(path):
    e = path.read_bytes()
    shoff, = struct.unpack_from("<I", e, 32)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", e, 46)
    secs = [struct.unpack_from("<10I", e, shoff + i * shentsize) for i in range(shnum)]
    st = secs[shstrndx]
    name = lambda o: e[st[4] + o:e.index(b"\0", st[4] + o)].decode()
    entry, = struct.unpack_from("<I", e, 24)
    out = []
    for s in secs:
        n, typ, flags, addr, off, size = s[0], s[1], s[2], s[3], s[4], s[5]
        if flags & 2 and size:                  # SHF_ALLOC
            out.append((name(n), addr, size, e[off:off + size] if typ != 8 else None))   # 8 = NOBITS
    return entry, out


def build_doom():
    objdir = HERE / "obj_pedal"
    objdir.mkdir(exist_ok=True)
    flags = doom_build.CFLAGS + ["-DZD_PEDAL"]
    srcs = [doom_build.DG / f"{n}.c" for n in doom_build.DOOM] +         [HERE / "zd_platform.c", HERE / "zd_pedal.c", HERE / "zd_vectors.asm"]
    objs = []
    for s in srcs:
        o = objdir / (s.stem + ".obj")
        if not (o.exists() and o.stat().st_mtime > s.stat().st_mtime):
            extra = ["--no_compress"] if s.suffix == ".asm" else []     # one fetch packet per vector
            r = subprocess.run([str(CL6X), *flags, *extra, "-c", str(s), f"--output_file={o}"],
                               capture_output=True, text=True)
            if r.returncode:
                sys.exit(f"--- {s.name}\n{r.stdout}{r.stderr}")
        objs.append(o)
    out = OUT / "doom_pedal.out"
    r = subprocess.run([str(CL6X), "-mv6740", "--abi=eabi", "-z", str(HERE / "lnk_pedal.cmd"), *map(str, objs),
                        f"--search_path={TI / 'lib'}", "--library=rts6740_elf.lib", f"--output_file={out}",
                        f"--map_file={OUT / 'doom_pedal.map'}"], capture_output=True, text=True)
    if r.returncode:
        sys.exit(r.stdout + r.stderr)
    entry, secs = elf_sections(out)
    end = max(a + n for _, a, n, _ in secs)
    blob = struct.pack("<III", 0x4D4F445A, entry, sum(1 for s in secs if s[3] is not None))
    for name, addr, size, data in secs:
        if data is None:
            continue
        blob += struct.pack("<II", addr, size) + data + b"\0" * (-size % 4)
    (OUT / "DOOM.BIN").write_bytes(blob)
    print(f"Doom: entry {entry:#x}, image end {end:#x}, DOOM.BIN {len(blob)} B")
    return end


def build_wad(doom_end):
    wad = WAD.read_bytes()
    at = (SDRAM_END - len(wad)) & ~0xFFF
    if at < doom_end:
        sys.exit(f"IWAD at {at:#x} would overlap Doom (ends {doom_end:#x})")
    c = zlib.compressobj(9, zlib.DEFLATED, -15)
    blob = struct.pack("<II", 0x5A44575A, len(wad)) + c.compress(wad) + c.flush()
    (OUT / "DOOM.WDZ").write_bytes(blob)
    print(f"IWAD {len(wad)} B -> DOOM.WDZ {len(blob)} B, loads at {at:#x}")


def stock_parts():
    """Folder with the four parts of Zoom's MS-70CDR 2.10 updater (not in the repository)."""
    if not (STOCK / "137_file_system.bin").exists():
        sys.path[:0] = [str(p) for p in ZOOMMS]
        from zoomms import cli
        if cli.main(["updater-parts", str(UPDATER), "--out", str(STOCK)]):
            sys.exit("cannot extract the stock parts")
    return STOCK


def build_fs():
    sys.path.insert(0, str(ROOT / "tools"))
    src = (ROOT / "tools" / "fsedit.py").read_text()
    ns = {"__name__": "fsedit_lib"}
    exec(src.split("\ndef main")[0], ns)
    fs = ns["Fs"]((stock_parts() / "137_file_system.bin").read_bytes())
    flst = fs.files["FLST_SEQ.ZDT"][2]
    for name in list(fs.order):
        fs.remove(name)
    empty = b"".join(b">>>\0" + bytes([t]) + b"\0" * 8 + b"<<<\0" + bytes([t]) + b"\0" * 8 for t in (0, 1, 2, 6, 7, 8, 9))
    fs.add("FLST_SEQ.ZDT", empty + b"\0" * (len(flst) - len(empty)))
    fs.add("DOOM.BIN", (OUT / "DOOM.BIN").read_bytes())
    fs.add("DOOM.WDZ", (OUT / "DOOM.WDZ").read_bytes())
    img = fs.image()
    (OUT / "fs_doom.bin").write_bytes(img)
    used = (10 + max(b for n in fs.order for b in fs.files[n][1]) + 1) * 4096
    print(f"file system: {len(fs.order)} files, {len(fs.free())} free blocks, used up to +{used:#x}")
    return used


def build_loader():
    ld = HERE / "loader"
    objs = []
    for src in ("loader.c", "inflate.c", "loader_entry.asm"):
        o = ld / (Path(src).stem + ".obj")
        r = subprocess.run([str(CL6X), "-mv6740", "--abi=eabi", "--opt_level=2", "--c99", "--mem_model:data=far",
                            "--mem_model:const=far", "--no_compress", f"--include_path={TI / 'include'}", f"--include_path={HERE}",
                            "-c", str(ld / src), f"--output_file={o}"], capture_output=True, text=True, cwd=ld)
        if r.returncode:
            sys.exit(r.stdout + r.stderr)
        objs.append(o)
    out = OUT / "loader.out"
    r = subprocess.run([str(CL6X), "-mv6740", "--abi=eabi", "-z", str(ld / "lnk_loader.cmd"), *map(str, objs),
                        f"--output_file={out}", f"--map_file={OUT / 'loader.map'}"], capture_output=True, text=True)
    if r.returncode:
        sys.exit(r.stdout + r.stderr)
    entry, secs = elf_sections(out)
    base = 0x80000000
    end = max(a + n for _, a, n, _ in secs)
    img = bytearray(end - base)                 # .far/.bss come out as zeros
    for _, addr, size, data in secs:
        if data is not None:
            img[addr - base:addr - base + size] = data
    img += b"\0" * (-len(img) % 32)
    assert entry == base, f"doom_hook must be first ({entry:#x})"
    assert end < 0x8001F000, "loader overlaps its stack"
    print(f"loader: {len(img)} B at {base:#x}")
    return bytes(img)


def callp(site, target):
    disp = (target - (site & ~31)) >> 2
    return struct.pack("<I", 0x10000012 | ((disp & 0x1FFFFF) << 7)).hex()


def write_patches(loader):
    def mvk(reg, v, high, side_s2=True):
        return (reg << 23) | ((v & 0xFFFF) << 7) | (0x6A if high else 0x2A)
    # trampoline: mvkl/mvkh 0x80000000, b0 ; b .s2 b0 ; nop 5   (B3 still points into the OS)
    words = [mvk(0, 0x80000000, False), mvk(0, 0x8000, True), 0x00000362, 0x00008000] + [0] * 4
    tramp = b"".join(struct.pack("<I", w) for w in words)
    hook = f"""# Doom CFW hook (MS-70CDR 2.10). Generated by doom/build_cfw.py.
# The boot step's callp to the boot screen (0xC00BB144) goes to a trampoline at
# 0x{TRAMP:08X}, which branches to doom_hook in shared RAM (loader/loader_entry.asm).
name: doom-hook
description: After the boot logo, load and start Doom from the file system
builds: [{MODEL}]
write:
  - model: {MODEL}
    addr: 0x{HOOK_SITE:08X}
    expect: "{HOOK_STOCK}"
    data: "{callp(HOOK_SITE, TRAMP)}"
section:
  - model: {MODEL}
    addr: 0x{TRAMP:08X}
    source: doom/build_cfw.py
    data: "{tramp.hex()}"
  - model: {MODEL}
    addr: 0x80000000
    source: doom/loader
    data: "{loader.hex()}"
"""
    (HERE / "patches").mkdir(exist_ok=True)
    (HERE / "patches" / "doom-hook.yaml").write_text(hook, newline="\n")
    (HERE / "patches" / "doom-version.yaml").write_text(f"""# Doom CFW: no OS version line over the boot logo.
name: doom-version
description: Boot screen without the "v2.10" line
builds: [{MODEL}]
write:
  - model: {MODEL}
    addr: 0xC00E4914
    expect: "76257300"
    data: "00257300"
""", newline="\n")
    subprocess.run([sys.executable, "-I", str(HERE / "logo" / "make_doom_logo.py"), str(OUT / "doom_logo.bin"),
                    str(OUT / "doom_logo.png")], check=True)
    import hashlib
    stock = (stock_parts() / "133_data_133.bin").read_bytes()[:1024]
    (HERE / "patches" / "doom-logo.yaml").write_text(f"""# Doom CFW boot logo (logo/make_doom_logo.py: the DOOM lettering).
name: doom-logo
description: Boot screen shows the DOOM logo
builds: [{MODEL}]
bin133:
  - model: {MODEL}
    offset: 0
    expect_sha256: "{hashlib.sha256(stock).hexdigest()}"
    data: "{(OUT / 'doom_logo.bin').read_bytes().hex()}"
""", newline="\n")


def build_updater(fs_used):
    sys.path[:0] = [str(p) for p in ZOOMMS]
    from zoomms import cli, flash, parts
    argv = ["build", str(UPDATER), "--build-id", MODEL, "--out", str(EXE)]
    for p in ("doom-version", "doom-logo", "doom-hook"):
        argv += ["--patch", str(HERE / "patches" / f"{p}.yaml")]
    if cli.main(argv):
        sys.exit(1)
    exe = bytearray(EXE.read_bytes())
    p137 = next(p for p in parts.find_parts(str(EXE)) if p.resource_id == 137)
    fs = (OUT / "fs_doom.bin").read_bytes()
    assert len(fs) == p137.size
    exe[p137.offset:p137.offset + p137.size] = fs
    stock = {s.offset: s.kind for s in flash.find_script(UPDATER.read_bytes())}
    for s in flash.find_script(bytes(exe)):     # the file system steps are wanted here
        if s.addr == 0xF0000 and s.kind == flash.SKIP:
            struct.pack_into("<I", exe, s.offset, stock[s.offset])
            # The write stops after the last used block (the old 2.9 MB took long on the pedal);
            # the erase still covers the whole area, so even an updater that sent the whole
            # resource regardless of the length would write onto erased flash.
            if stock[s.offset] == flash.WRITE:
                struct.pack_into("<I", exe, s.offset + 8, fs_used)
    EXE.write_bytes(bytes(exe))
    print("flash steps: " + "; ".join(s.describe() for s in flash.find_script(bytes(exe))))
    if "--emu" in sys.argv:
        cli.main(["updater-parts", str(EXE), "--out", sys.argv[sys.argv.index("--emu") + 1]])


def main():
    OUT.mkdir(exist_ok=True)
    end = build_doom()
    build_wad(end)
    fs_used = build_fs()
    loader = build_loader()
    write_patches(loader)
    build_updater(fs_used)


if __name__ == "__main__":
    main()
