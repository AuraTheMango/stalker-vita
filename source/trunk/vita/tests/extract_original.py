"""Extract selected resources from the user's original SOC archives; never downloads game data."""
from pathlib import Path
import ctypes,struct,json,sys,re
root=Path(sys.argv[1]);out=Path(sys.argv[2]);out.mkdir(parents=True,exist_ok=True)
pattern=re.compile(sys.argv[3],re.I)
lz=ctypes.CDLL(str((out/'lz.so').resolve()));lzo=ctypes.CDLL('liblzo2.so.2')
lz.unpack.argtypes=[ctypes.c_void_p,ctypes.c_uint,ctypes.c_void_p,ctypes.c_uint]
def rand(seed):return (seed*0x08088405+1)&0xffffffff
alphabet=list(range(256));seed=6011979
for _ in range(1024):
 seed=rand(seed);j=seed>>24;seed=rand(seed);k=seed>>24
 while j==k:seed=rand(seed);k=seed>>24
 alphabet[j],alphabet[k]=alphabet[k],alphabet[j]
back=[0]*256
for i,n in enumerate(alphabet):back[n]=i
def decode(b):
 seed=24031979;out=bytearray()
 for n in b:seed=rand(seed);out.append(back[n^(seed>>24)])
 return bytes(out)
for archive in sorted(root.glob('gamedata.db?')):
 cache=out/(archive.name+'.json')
 if cache.exists():entries=json.loads(cache.read_text())
 else:
  entries=[]
  with archive.open('rb') as f:
   while True:
    h=f.read(8)
    if len(h)!=8:break
    tag,n=struct.unpack('<II',h)
    if tag&0x7fffffff!=1:f.seek(n,1);continue
    data=f.read(n)
    if tag&0x80000000:
     data=decode(data);size=struct.unpack_from('<I',data)[0];assert 0<size<64*1024*1024
     buf=ctypes.create_string_buffer(size);assert lz.unpack(data,len(data),buf,size)==size;data=buf.raw
    p=0
    while p<len(data):
     size=struct.unpack_from('<H',data,p)[0];p+=2
     real,packed,crc=struct.unpack_from('<III',data,p)
     name=data[p+12:p+size-4].decode('cp1252')
     offset=struct.unpack_from('<I',data,p+size-4)[0];p+=size
     entries.append((name,real,packed,offset))
    break
  cache.write_text(json.dumps(entries))
 with archive.open('rb') as f:
  for name,real,packed,offset in entries:
   if not pattern.search(name):continue
   target=out/Path(name.replace('\\','/'));target.parent.mkdir(parents=True,exist_ok=True)
   f.seek(offset);data=f.read(packed)
   if real!=packed:
    buf=ctypes.create_string_buffer(real);length=ctypes.c_size_t(real)
    assert lzo.lzo1x_decompress_safe(data,len(data),buf,ctypes.byref(length),None)==0
    assert length.value==real;data=buf.raw
   target.write_bytes(data);print(archive.name,name,real)
