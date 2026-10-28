"""Where the external tools live. Every path can be overridden by an environment variable."""
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parent


def _p(env, default):
    return Path(os.environ.get(env, default))


TI_ROOT = _p("TI_CGT_C6000", r"C:\ti\ccs2101\ccs\tools\compiler\ti-cgt-c6000_8.5.0.LTS")
CL6X = TI_ROOT / "bin" / "cl6x.exe"
UPDATER_70CDR = _p("ZOOM_UPDATER_70CDR", ROOT / "ZOOM MS-70CDR v2.10 Updater.exe")
# zoomms from the submodule; shims/ holds small stand-ins for pyyaml and pefile (no pip needed)
ZOOMMS = [_p("ZOOM_MS_MIDISTOMP", ROOT / "zoom-ms-midistomp"), ROOT / "shims"]
DOOMGENERIC = _p("DOOMGENERIC", ROOT / "doomgeneric" / "doomgeneric")
DOOM_WAD = _p("DOOM_WAD", ROOT / "doom1.wad")
