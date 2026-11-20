
from pathlib import Path
import sys
destination=Path(sys.argv[1]);destination.mkdir(parents=True,exist_ok=True)
source=(Path(__file__).resolve().parent.parent/"xrGame/vita_trader_ui.cpp").read_text()
def method(start,end):
 return source[source.index(start):source.index(end,source.index(start))]
(destination/"vita_menu_draw.inc").write_text(
 method("void VitaMainMenu::draw(","void VitaMainMenu::release()")+
 method("void VitaOptionsMenu::draw(","void VitaOptionsMenu::release()"))
