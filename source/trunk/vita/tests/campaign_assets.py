
from pathlib import Path
import re,sys,json
root=Path(sys.argv[1])
keys={
"escape":["escape_trader_jobs_6","escape_trader_jobs_611","escape_trader_jobs_61111","escape_trader_jobs_611111111"],
"garbage":["gar_dramm_novice_start_0","gar_dramm_novice_start_31","gar_seriy_start_11","gar_seriy_start_22","gar_seriy_start_222","gar_seriy_start_2221"],
"agroprom":["agr_krot_actor_hasnot_met_seriy_100","agr_factory_krot_start_0","agr_factory_krot_start_2","agr_factory_krot_start_1501","agr_factory_krot_start_3001"]}
for language in ["fra","eng"]:
 for zone,names in keys.items():
  text=(root/f"config/text/{language}/stable_dialogs_{zone}.xml").read_bytes().decode("cp1252")
  strings=dict(re.findall(r'<string id="([^"]+)">\s*<text>(.*?)</text>',text,re.S))
  for name in names:assert strings.get(name),name
print("All campaign dialogue keys exist in the original FR and EN tables.")
