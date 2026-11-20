from pathlib import Path
import sys
root=Path(__file__).resolve().parent.parent/"xrGame"
output=Path(sys.argv[1]);output.mkdir(exist_ok=True,parents=True)
s=(root/"vita_hud.cpp").read_text()
(output/"vita_map_parse.inc").write_text(s[s.index("bool read_vita_hud_map("):s.index("bool VitaHud::initialize()")])
