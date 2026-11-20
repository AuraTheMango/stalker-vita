
#include "vita_postprocess.h"
namespace {
const char* vertex=R"(
attribute vec2 a_position;
varying vec2 v_uv;
void main(){gl_Position=vec4(a_position,0.0,1.0);v_uv=a_position*0.5+0.5;}
)";
const char* fragment=R"(
precision highp float;
uniform sampler2D u_scene;
uniform sampler2D u_depth;
uniform vec2 u_pixel;
uniform float u_fxaa;
uniform float u_dof;
varying vec2 v_uv;
float luma(vec3 c){return dot(c,vec3(0.299,0.587,0.114));}
float distance_at(vec2 uv){return 0.1001/max(0.0001,1.001-texture2D(u_depth,uv).r);}
void main(){
	vec3 center=texture2D(u_scene,v_uv).rgb;
	vec3 color=center;
	if(u_fxaa>0.5){
		vec3 nw=texture2D(u_scene,v_uv+vec2(-1.0,-1.0)*u_pixel).rgb;
		vec3 ne=texture2D(u_scene,v_uv+vec2(1.0,-1.0)*u_pixel).rgb;
		vec3 sw=texture2D(u_scene,v_uv+vec2(-1.0,1.0)*u_pixel).rgb;
		vec3 se=texture2D(u_scene,v_uv+vec2(1.0,1.0)*u_pixel).rgb;
		float a=luma(nw),b=luma(ne),c=luma(sw),d=luma(se),e=luma(center);
		float lo=min(e,min(min(a,b),min(c,d))),hi=max(e,max(max(a,b),max(c,d)));
		if(hi-lo>max(0.0312,hi*0.125)){
			vec2 dir=vec2(-((a+b)-(c+d)),(a+c)-(b+d));
			float reduce=max((a+b+c+d)*0.03125,0.0078125);
			float inverse=1.0/(min(abs(dir.x),abs(dir.y))+reduce);
			dir=clamp(dir*inverse,vec2(-8.0),vec2(8.0))*u_pixel;
			vec3 rgbA=0.5*(texture2D(u_scene,v_uv+dir*(1.0/3.0-0.5)).rgb+texture2D(u_scene,v_uv+dir*(2.0/3.0-0.5)).rgb);
			vec3 rgbB=rgbA*0.5+0.25*(texture2D(u_scene,v_uv-dir*0.5).rgb+texture2D(u_scene,v_uv+dir*0.5).rgb);
			float value=luma(rgbB);color=value<lo||value>hi?rgbA:rgbB;
		}
	}
	if(u_dof>0.5){
		float focus=clamp(distance_at(vec2(0.5)),2.0,40.0);
		float distance=distance_at(v_uv);
		float blur=clamp((abs(distance-focus)-3.0)/18.0,0.0,1.0);
		vec2 step=u_pixel*(1.0+blur*2.0);
		vec3 soft=(texture2D(u_scene,v_uv+vec2(step.x,0.0)).rgb+texture2D(u_scene,v_uv-vec2(step.x,0.0)).rgb+
			texture2D(u_scene,v_uv+vec2(0.0,step.y)).rgb+texture2D(u_scene,v_uv-vec2(0.0,step.y)).rgb)*0.25;
		color=mix(color,soft,blur*0.75);
	}
	gl_FragColor=vec4(color,1.0);
}
)";
}
bool VitaPostProcess::begin(int width,int height,bool fxaa,bool dof)
{
	m_active=false;
	if(!fxaa&&!dof)return false;
	if(width!=m_width||height!=m_height)
	{
		release();
		if(!m_program.initialize(vertex,fragment,m_error,sizeof(m_error)))return false;
		m_width=width;m_height=height;
		glGenTextures(1,&m_color);glBindTexture(GL_TEXTURE_2D,m_color);
		glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
		glGenRenderbuffers(1,&m_depth);glBindRenderbuffer(GL_RENDERBUFFER,m_depth);
		glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT16,width,height);
		glGenFramebuffers(1,&m_fbo);glBindFramebuffer(GL_FRAMEBUFFER,m_fbo);
		glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,m_color,0);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,m_depth);
		if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){glBindFramebuffer(GL_FRAMEBUFFER,0);release();return false;}
		glGenTextures(1,&m_depth_texture);
		const float quad[]={-1,-1,1,-1,-1,1,1,1};
		glGenBuffers(1,&m_quad);glBindBuffer(GL_ARRAY_BUFFER,m_quad);glBufferData(GL_ARRAY_BUFFER,sizeof(quad),quad,GL_STATIC_DRAW);
	}
	glBindFramebuffer(GL_FRAMEBUFFER,m_fbo);
	m_program.use();
	glUniform2f(m_program.uniform_location("u_pixel"),1.f/width,1.f/height);
	glUniform1f(m_program.uniform_location("u_fxaa"),fxaa?1.f:0.f);
	glUniform1f(m_program.uniform_location("u_dof"),dof?1.f:0.f);
	m_active=true;m_dof=dof;
	return true;
}
void VitaPostProcess::finish()
{
	if(!m_active)return;
	if(m_dof)
	{
		glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,m_depth_texture);
		vglTexImageDepthBuffer(GL_TEXTURE_2D);
		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
	}
	glBindFramebuffer(GL_FRAMEBUFFER,0);
	glViewport(0,0,m_width,m_height);glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);
	m_program.use();
	glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,m_color);
	glUniform1i(m_program.uniform_location("u_scene"),0);glUniform1i(m_program.uniform_location("u_depth"),1);
	glBindBuffer(GL_ARRAY_BUFFER,m_quad);
	const auto position=m_program.attribute_location("a_position");
	glEnableVertexAttribArray(position);glVertexAttribPointer(position,2,GL_FLOAT,GL_FALSE,0,nullptr);
	glDrawArrays(GL_TRIANGLE_STRIP,0,4);
	glEnable(GL_DEPTH_TEST);glEnable(GL_BLEND);glActiveTexture(GL_TEXTURE0);m_active=false;
}
void VitaPostProcess::release()
{
	glBindFramebuffer(GL_FRAMEBUFFER,0);
	if(m_quad)glDeleteBuffers(1,&m_quad);
	if(m_depth_texture)glDeleteTextures(1,&m_depth_texture);
	if(m_fbo)glDeleteFramebuffers(1,&m_fbo);
	if(m_depth)glDeleteRenderbuffers(1,&m_depth);
	if(m_color)glDeleteTextures(1,&m_color);
	m_quad=m_depth_texture=m_fbo=m_depth=m_color=0;m_width=m_height=0;m_active=false;m_program.release();
}
