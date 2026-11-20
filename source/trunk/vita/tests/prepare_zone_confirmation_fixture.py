from pathlib import Path
import sys
out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parent.parent/"xrGame"
s=(root/"main.cpp").read_text()
a=s.index("        if(zone_dialog_active){")
b=s.index("        else if (save_consumed_input)",a)
(out/"zone_input.inc").write_text(s[a:b])
a=s.index("        if(near_exit&&interact_pressed")
b=s.index("        if(loot_visual_ready",a)
(out/"zone_transition.inc").write_text(s[a:b])
s=(root/"vita_trader_ui.cpp").read_text()
a=s.index("void VitaTraderUI::draw_zone_confirmation(")
(out/"zone_draw.inc").write_text(s[a:])
