from pathlib import Path
import sys
target=Path(sys.argv[1]);target.mkdir(parents=True,exist_ok=True)
source=(Path(__file__).resolve().parent.parent/"xrGame/vita_trader_ui.cpp").read_text()
start=source.index("void VitaInventoryUI::draw(")
end=source.index("void VitaInventoryUI::draw_scope()",start)
(target/"vita_inventory_draw.inc").write_text(source[start:end])

start=source.index("void VitaTraderUI::draw_panel(")
end=source.index("\tglDisable(GL_DEPTH_TEST);",start)
(target/"vita_dialog_draw.inc").write_text(source[start:end]+"}\n")
