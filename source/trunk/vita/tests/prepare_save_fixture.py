from pathlib import Path
import sys
source=(Path(__file__).resolve().parent.parent/"xrGame/vita_trader_ui.cpp").read_text()
target=Path(sys.argv[1]);target.mkdir(exist_ok=True,parents=True)
(target/"vita_save_draw.inc").write_text(source[source.index("void VitaSaveUI::draw("):source.index("void VitaSaveUI::release()")])
