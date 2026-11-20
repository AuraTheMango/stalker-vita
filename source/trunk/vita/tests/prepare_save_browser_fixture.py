from pathlib import Path
import sys
out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parent.parent/"xrGame"
s=(root/"vita_trader_ui.cpp").read_text()
a=s.index("bool VitaSaveUI::initialize(");b=s.index("bool VitaTraderUI::set_campaign_page(",a)
(out/"save_browser_ui.inc").write_text(s[a:b])
s=(root/"main.cpp").read_text()
a=s.index("static bool prepare_vita_save_browser(");b=s.index("// Modal original save/load dialog.",a)
(out/"save_browser_prepare.inc").write_text(s[a:b])
