
from pathlib import Path
import struct,sys
assets,destination=map(Path,sys.argv[1:3]);destination.mkdir(parents=True,exist_ok=True)
def chunks(data):
 offset=0
 while offset<len(data):
  tag,size=struct.unpack_from("<II",data,offset);offset+=8
  assert size<=len(data)-offset
  yield tag,data[offset:offset+size];offset+=size
root=dict(chunks((assets/"stalker_animation.omf").read_bytes()))
(destination/"params").write_bytes(root[15]);clips=dict(chunks(root[14]))
for name,tag in [("idle",77),("walk",127),("unarmed",1414)]:
 (destination/name).write_bytes(clips[tag])
for tag in [78,262,263,1340,1342,1349,269,270,1350,1352,1359,259,260,1330,1332,1339]:
 (destination/("clip-"+str(tag))).write_bytes(clips[tag])
