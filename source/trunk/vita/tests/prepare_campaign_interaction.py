from pathlib import Path
import sys
source=(Path(__file__).resolve().parent.parent/"xrGame/main.cpp").read_text()
start=source.index("        else if(campaign_open){")
end=source.index("        else if(exchange_open())",start)
out=Path(sys.argv[1])
out.mkdir(parents=True,exist_ok=True)
(out/"campaign_input.inc").write_text(source[start:end])
