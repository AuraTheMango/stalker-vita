from pathlib import Path
import sys
out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parent.parent/"xrGame"
source=(root/"vita_trader_ui.cpp").read_text()
a=source.index("bool VitaTraderUI::set_campaign_page(");b=source.index("void VitaTraderUI::draw_campaign(",a)
(out/"campaign_cache.inc").write_text(source[a:b])
source=(root/"main.cpp").read_text()
a=source.index(" bool pause_resources_ready=false,pause_music_ready=false;")
b=source.index("\n\tunsigned save_choice",a)
(out/"pause_prepare.inc").write_text(source[a:b])
a=source.index("        if(save_menu_open&&!pause_resources_active){")
b=source.index("\t\tif (!save_menu_open && pause_started",a)
(out/"pause_lifecycle.inc").write_text(source[a:b])
