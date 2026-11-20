from pathlib import Path
import sys
source=(Path(__file__).resolve().parent.parent/"xrGame/vita_hud.cpp").read_text()
out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
(out/"hud_draw.inc").write_text(source[source.index("void VitaHud::draw_status("):source.index("void VitaHud::draw_pda(")])
