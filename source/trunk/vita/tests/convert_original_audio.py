from pathlib import Path
import ctypes as c,array,sys
root=Path(sys.argv[1]).resolve();libs=root/'audio-libs/usr/lib/x86_64-linux-gnu'
c.CDLL(str(libs/'libogg.so.0'),mode=c.RTLD_GLOBAL);c.CDLL(str(libs/'libvorbis.so.0'),mode=c.RTLD_GLOBAL)
v=c.CDLL(str(libs/'libvorbisfile.so.3'))
class Info(c.Structure):_fields_=[('version',c.c_int),('channels',c.c_int),('rate',c.c_long)]
v.ov_fopen.argtypes=[c.c_char_p,c.c_void_p];v.ov_info.argtypes=[c.c_void_p,c.c_int];v.ov_info.restype=c.POINTER(Info)
v.ov_read.argtypes=[c.c_void_p,c.c_void_p,c.c_int,c.c_int,c.c_int,c.c_int,c.POINTER(c.c_int)];v.ov_read.restype=c.c_long
v.ov_clear.argtypes=[c.c_void_p]
destination=Path(sys.argv[2]);destination.mkdir(exist_ok=True)
for source,name in [('ak74_shot_0','rifle_shot'),('ak74_reload','rifle_reload'),('bm16_reload','shotgun_reload'),
 ('t_rgd5_explosion','grenade_explosion'),('w_ak74_shot1','silenced_shot')]:
 opaque=(c.c_uint64*1024)()
 assert v.ov_fopen(str(root/'sounds/weapons'/f'{source}.ogg').encode(),opaque)==0
 info=v.ov_info(opaque,-1).contents;channels,rate=info.channels,info.rate
 assert channels in (1,2) and 8000<=rate<=96000
 raw=bytearray();buf=c.create_string_buffer(8192);section=c.c_int()
 while True:
  n=v.ov_read(opaque,buf,8192,0,2,1,c.byref(section))
  if n==0:break
  assert n>0;raw+=buf.raw[:n];assert len(raw)<rate*channels*2*20
 v.ov_clear(opaque)
 samples=array.array('h');samples.frombytes(raw)
 mono=[sum(samples[i:i+channels])/channels for i in range(0,len(samples),channels)]
 output=array.array('h')
 for i in range(int(len(mono)*24000/rate)):
  position=i*rate/24000;j=int(position);fraction=position-j
  value=mono[j]*(1-fraction)+mono[min(j+1,len(mono)-1)]*fraction
  output.append(max(-32768,min(32767,round(value))))
 (destination/f'{name}.pcm').write_bytes(output.tobytes())
 print(name,rate,channels,len(output)/24000,len(output)*2)
