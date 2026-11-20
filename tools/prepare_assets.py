"""Generate non-retail build placeholders or import private PCM samples."""
from pathlib import Path
import argparse,re,struct,zlib,shutil
ROOT=Path(__file__).resolve().parent.parent
def png(path,w,h):
 glyphs=["11110 10001","10000 10001","10000 10001","11110 10001","00001 10001","00001 01010","11110 00100"]
 scale=max(1,min(w//16,h//12));left=(w-11*scale)//2;top=(h-7*scale)//2;rows=[]
 for y in range(h):
  row=bytearray()
  for x in range(w):
   c=(18,30+int(y/h*12),27,255);gx=(x-left)//scale;gy=(y-top)//scale
   if 0<=gx<11 and 0<=gy<7 and glyphs[gy][gx]=="1":c=(168,199,159,255)
   row.extend(c)
  rows.append(b"\0"+row)
 def chunk(tag,data):return struct.pack(">I",len(data))+tag+data+struct.pack(">I",zlib.crc32(tag+data)&0xffffffff)
 path.parent.mkdir(parents=True,exist_ok=True)
 path.write_bytes(b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR",struct.pack(">IIBBBBB",w,h,8,6,0,0,0))+chunk(b"IDAT",zlib.compress(b"".join(rows)))+chunk(b"IEND",b""))
def main():
 p=argparse.ArgumentParser();choice=p.add_mutually_exclusive_group(required=True)
 choice.add_argument("--silent",action="store_true");choice.add_argument("--audio-dir",type=Path)
 p.add_argument("--output",type=Path,default=ROOT/"assets");args=p.parse_args();out=args.output.resolve()
 source=ROOT/"source/trunk/vita/xrGame"
 names=re.findall(r'FILE\s+"\$\{STALKER_ASSET_DIR\}/audio/([^"]+)"',(source/"CMakeLists.txt").read_text())
 if not names:raise SystemExit("No audio assets found.")
 (out/"audio").mkdir(parents=True,exist_ok=True)
 for name in names:
  target=out/"audio"/name
  if args.silent:target.write_bytes(b"\0\0"*24000)
  else:
   original=args.audio_dir/name
   if not original.is_file():raise SystemExit(f"Missing private PCM: {name}")
   if not original.stat().st_size or original.stat().st_size%2:raise SystemExit(f"Invalid PCM: {name}")
   shutil.copy2(original,target)
 for rel,w,h in [("icon0.png",128,128),("livearea/contents/bg.png",840,500),("livearea/contents/startup.png",280,158)]:png(out/"sce_sys"/rel,w,h)
 shutil.copy2(source/"sce_sys/livearea/contents/template.xml",out/"sce_sys/livearea/contents/template.xml")
 print(f"Prepared {len(names)} samples and generated placeholder artwork in {out}")
if __name__=="__main__":main()
