import ctypes as C,re,json
from pathlib import Path
e=C.CDLL('libEGL.so.1');g=C.CDLL('libGL.so.1')
def bind(lib,name,rest,args):
 f=getattr(lib,name);f.restype=rest;f.argtypes=args;return f
P=C.c_void_p;I=C.c_int;U=C.c_uint;F=C.c_float
getproc=bind(e,'eglGetProcAddress',P,[C.c_char_p])
platform=C.CFUNCTYPE(P,U,P,C.POINTER(I))(getproc(b'eglGetPlatformDisplayEXT'))
display=platform(0x31dd,None,None)
major=I();minor=I()
assert bind(e,'eglInitialize',U,[P,C.POINTER(I),C.POINTER(I)])(display,C.byref(major),C.byref(minor))
assert bind(e,'eglBindAPI',U,[U])(0x30a0)
attrs=(I*13)(0x3033,1,0x3040,4,0x3024,8,0x3023,8,0x3022,8,0x3021,8,0x3038)
config=P();n=I()
assert bind(e,'eglChooseConfig',U,[P,C.POINTER(I),C.POINTER(P),I,C.POINTER(I)])(display,attrs,C.byref(config),1,C.byref(n)) and n.value
surface=bind(e,'eglCreatePbufferSurface',P,[P,P,C.POINTER(I)])(display,config,(I*5)(0x3057,128,0x3056,64,0x3038))
context=bind(e,'eglCreateContext',P,[P,P,P,C.POINTER(I)])(display,config,None,(I*3)(0x3098,2,0x3038))
assert bind(e,'eglMakeCurrent',U,[P,P,P,P])(display,surface,surface,context)
print(bind(g,'glGetString',C.c_char_p,[U])(0x1f02).decode())
create=bind(g,'glCreateShader',U,[U]);source=bind(g,'glShaderSource',None,[U,I,C.POINTER(C.c_char_p),C.POINTER(I)])
compile=bind(g,'glCompileShader',None,[U]);check=bind(g,'glGetShaderiv',None,[U,U,C.POINTER(I)])
def shader(text,kind):
 handle=create(kind);p=C.c_char_p(text.encode());source(handle,1,C.byref(p),None);compile(handle);ok=I();check(handle,0x8b81,C.byref(ok))
 if not ok.value:
  log=C.create_string_buffer(8192);bind(g,'glGetShaderInfoLog',None,[U,I,C.POINTER(I),P])(handle,8192,None,log);raise AssertionError(log.value)
 return handle
def program(vertex,fragment):
 handle=bind(g,'glCreateProgram',U,[])()
 attach=bind(g,'glAttachShader',None,[U,U]);attach(handle,shader(vertex,0x8b31));attach(handle,shader(fragment,0x8b30))
 bind(g,'glLinkProgram',None,[U])(handle);ok=I();bind(g,'glGetProgramiv',None,[U,U,C.POINTER(I)])(handle,0x8b82,C.byref(ok));assert ok.value
 return handle
post=Path('source/trunk/vita/xrGame/vita_postprocess.cpp').read_text();strings=re.findall(r'R"\((.*?)\)"',post,re.S);p=program(*strings[:2])
npc=Path('source/trunk/vita/xrRender/vita_gl_debug_renderer.cpp').read_text();npc=npc[npc.index('bool VitaGLDebugRenderer::initialize_npc'):];texts=re.findall(r'R"\((.*?)\)"',npc,re.S);npc_program=program(*texts[:2])
print('Actual FXAA/DOF and NPC interpolation shaders compiled and linked')
gen=bind(g,'glGenTextures',None,[I,C.POINTER(U)]);active=bind(g,'glActiveTexture',None,[U]);texbind=bind(g,'glBindTexture',None,[U,U])
image=bind(g,'glTexImage2D',None,[U,I,I,I,I,I,U,U,P]);param=bind(g,'glTexParameteri',None,[U,U,I])
width,height=128,64
pixels=bytearray()
depth=bytearray()
for y in range(height):
 for x in range(width):
  value=255 if ((x//8+y//8)%2) else 0
  # Diagonal test edge covers part of the image.
  if x<40:value=255 if x>y*.6 else 0
  pixels.extend((value,value,value,255))
  distance=242 if abs(x-width//2)<20 and abs(y-height//2)<16 else 254
  depth.extend((distance,distance,distance,255))
for slot,data in [(0,pixels),(1,depth)]:
 active(0x84c0+slot);texture=U();gen(1,C.byref(texture));texbind(0x0de1,texture)
 buffer=(C.c_ubyte*len(data)).from_buffer(data);image(0x0de1,0,0x1908,width,height,0,0x1908,0x1401,buffer)
 for name,val in [(0x2801,0x2601),(0x2800,0x2601),(0x2802,0x812f),(0x2803,0x812f)]:param(0x0de1,name,val)
use=bind(g,'glUseProgram',None,[U]);use(p)
uniform=bind(g,'glGetUniformLocation',I,[U,C.c_char_p])
u1i=bind(g,'glUniform1i',None,[I,I]);u1f=bind(g,'glUniform1f',None,[I,F]);u2f=bind(g,'glUniform2f',None,[I,F,F])
u1i(uniform(p,b'u_scene'),0);u1i(uniform(p,b'u_depth'),1);u2f(uniform(p,b'u_pixel'),1/width,1/height)
quad=(F*8)(-1,-1,1,-1,-1,1,1,1)
attribute=bind(g,'glGetAttribLocation',I,[U,C.c_char_p])(p,b'a_position')
bind(g,'glVertexAttribPointer',None,[U,I,U,U,I,P])(attribute,2,0x1406,0,0,quad)
bind(g,'glEnableVertexAttribArray',None,[U])(attribute)
bind(g,'glViewport',None,[I,I,I,I])(0,0,width,height)
draw=bind(g,'glDrawArrays',None,[U,I,I]);read=bind(g,'glReadPixels',None,[I,I,I,I,U,U,P])
def render(aa,dof):
 u1f(uniform(p,b'u_fxaa'),aa);u1f(uniform(p,b'u_dof'),dof);draw(5,0,4)
 out=(C.c_ubyte*(width*height*4))();read(0,0,width,height,0x1908,0x1401,out);return bytes(out)
baseline=render(0,0);assert baseline==pixels
aa=render(1,0);blur=render(0,1);both=render(1,1)
changed=sum(a!=b for a,b in zip(aa,baseline));assert changed>100,changed
near_changes=far_changes=0
for y in range(height):
 for x in range(width):
  i=(y*width+x)*4
  if blur[i]!=baseline[i]:
   if abs(x-width//2)<15 and abs(y-height//2)<10:near_changes+=1
   else:far_changes+=1
assert near_changes==0 and far_changes>100,(near_changes,far_changes)
print('Pixel tests passed: identity preserved, FXAA changed',changed,'channels; DOF preserved focus and changed',far_changes,'far pixels')
assert bind(g,'glGetError',U,[])()==0

use(npc_program)
for name,row in [(b'u_view_x',(0.1,0.,0.,0.)),(b'u_view_y',(0.,0.1,0.,0.)),(b'u_view_z',(0.,0.,0.,7.))]:
 bind(g,'glUniform4f',None,[I,F,F,F,F])(uniform(npc_program,name),*row)
u1i(uniform(npc_program,b'u_texture'),0);u1f(uniform(npc_program,b'u_textured'),0)
first=(F*9)(-.9,-.2,0,-.3,-.2,0,-.6,.3,0)
second=(F*9)(.3,-.2,0,.9,-.2,0,.6,.3,0)
uv=(F*6)(0,0,0,0,0,0)
for name,data,size in [(b'a_position',first,3),(b'a_next',second,3),(b'a_texcoord',uv,2)]:
 at=bind(g,'glGetAttribLocation',I,[U,C.c_char_p])(npc_program,name)
 bind(g,'glVertexAttribPointer',None,[U,I,U,U,I,P])(at,size,0x1406,0,0,data)
 bind(g,'glEnableVertexAttribArray',None,[U])(at)
def centroid(mix):
 u1f(uniform(npc_program,b'u_blend'),mix)
 bind(g,'glClearColor',None,[F,F,F,F])(0,0,0,1);bind(g,'glClear',None,[U])(0x4000);draw(4,0,3)
 out=(C.c_ubyte*(width*height*4))();read(0,0,width,height,0x1908,0x1401,out)
 points=[(i//4)%width for i in range(0,len(out),4) if out[i+1]>128]
 assert points
 return sum(points)/len(points)
a,b,c=centroid(0),centroid(.5),centroid(1)
assert a<b<c and abs(b-(a+c)/2)<1,(a,b,c)
print('NPC GPU interpolation pixel centroids',a,b,c)
