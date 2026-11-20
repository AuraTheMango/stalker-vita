
#pragma once
#include "vita_gl_program.h"
#include <vitaGL.h>
class VitaPostProcess
{
public:
	~VitaPostProcess(){release();}
	bool begin(int width,int height,bool fxaa,bool dof);
	void finish();
	void release();
	const char* error()const{return m_error;}
private:
	VitaGLProgram m_program;
	GLuint m_fbo=0,m_color=0,m_depth=0,m_depth_texture=0,m_quad=0;
	int m_width=0,m_height=0;
	bool m_active=false,m_dof=false;
	char m_error[1024]={};
};
