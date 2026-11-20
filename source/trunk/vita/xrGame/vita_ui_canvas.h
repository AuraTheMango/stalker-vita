#pragma once

#include "vita_gl_program.h"
#ifdef quad
#undef quad
#endif

class VitaUiCanvas
{
public:
	bool initialize();
	void begin() const;
	void quad(GLuint texture, const float (&destination)[4], const float (&source)[4],
		const float (&color)[4], float mask = 0.f) const;
	void end() const;
	void release();

private:
	VitaGLProgram m_program;
	GLuint m_quad = 0;
	GLint m_position = -1, m_rect = -1, m_uv = -1, m_mask = -1, m_color = -1, m_texture = -1;
};
