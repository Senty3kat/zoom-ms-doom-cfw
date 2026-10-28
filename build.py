#!/usr/bin/env python3
"""Build Doom (doomgeneric) for the Zoom MS pedals' C674x with TI cl6x.

    python -I build.py            # -> out/doom_emu.out (stage 1: bare metal, emulator runner)

doomgeneric itself (DOOMGENERIC in paths.py, GPL-2+) is compiled unchanged; the platform
layer is zd_platform.c, and zd_shim.h is preincluded so file calls reach it. Objects are
rebuilt only when their source is newer.
"""
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE
sys.path.insert(0, str(ROOT))
from paths import CL6X, DOOMGENERIC as DG, TI_ROOT as TI  # noqa: E402
OBJ = HERE / "obj"
OUT = HERE / "out"

# the doomgeneric Makefile's list, minus the platform and file backends we replace
DOOM = """dummy am_map doomdef doomstat dstrings d_event d_items d_iwad d_loop d_main d_mode d_net
f_finale f_wipe g_game hu_lib hu_stuff info i_cdmus i_endoom i_joystick i_scale i_sound i_system
i_timer memio m_argv m_bbox m_cheat m_config m_controls m_fixed m_menu m_misc m_random p_ceilng
p_doors p_enemy p_floor p_inter p_lights p_map p_maputl p_mobj p_plats p_pspr p_saveg p_setup
p_sight p_spec p_switch p_telept p_tick p_user r_bsp r_data r_draw r_main r_plane r_segs r_sky
r_things sha1 sounds statdump st_lib st_stuff s_sound tables v_video wi_stuff w_checksum w_file
w_main w_wad z_zone i_input i_video doomgeneric""".split()

CFLAGS = ["-mv6740", "--abi=eabi", "--opt_level=2", "--c99", "--mem_model:data=far",
          "--diag_suppress=94,112,179,190,225,232,238,552,880,1311,1116,1119", "--display_error_number",
          "-DCMAP256", "-DDOOMGENERIC_RESX=320", "-DDOOMGENERIC_RESY=200",
          f"--preinclude={HERE / 'zd_shim.h'}", f"--include_path={DG}", f"--include_path={HERE / 'posix'}", f"--include_path={TI / 'include'}"]


def cc(src, obj):
    if obj.exists() and obj.stat().st_mtime > src.stat().st_mtime and \
            obj.stat().st_mtime > (HERE / "zd_shim.h").stat().st_mtime:
        return True
    r = subprocess.run([str(CL6X), *CFLAGS, "-c", str(src), f"--output_file={obj}"],
                       capture_output=True, text=True)
    msg = (r.stdout + r.stderr).strip()
    if r.returncode or "error" in msg.lower():
        print(f"--- {src.name}\n{msg}")
    return r.returncode == 0


def main():
    OBJ.mkdir(exist_ok=True)
    OUT.mkdir(exist_ok=True)
    srcs = [DG / f"{n}.c" for n in DOOM] + [HERE / "zd_platform.c"]
    objs = [OBJ / (s.stem + ".obj") for s in srcs]
    bad = [s.name for s, o in zip(srcs, objs) if not cc(s, o)]
    if bad:
        sys.exit(f"compile failed: {' '.join(bad)}")
    out = OUT / "doom_emu.out"
    r = subprocess.run([str(CL6X), "-mv6740", "--abi=eabi", "-z", str(HERE / "lnk_emu.cmd"),
                        *map(str, objs), f"--search_path={TI / 'lib'}", "--library=rts6740_elf.lib",
                        f"--output_file={out}", f"--map_file={OUT / 'doom_emu.map'}", "--warn_sections"],
                       capture_output=True, text=True)
    print((r.stdout + r.stderr).strip()[-3000:])
    if r.returncode:
        sys.exit("link failed")
    print(f"built {out} ({out.stat().st_size} B)")


if __name__ == "__main__":
    main()
