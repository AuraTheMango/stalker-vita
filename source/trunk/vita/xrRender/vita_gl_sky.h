#pragma once

#include "vita_gl_program.h"

class VitaGLSky
{
public:
	~VitaGLSky();
	bool initialize(const void* dds, size_t size, char* error, size_t error_capacity);
	bool draw(float yaw, float pitch, float projection_scale = 1.f) const;
	void release();

private:
	VitaGLProgram m_program;
	GLuint m_texture = 0;
	GLuint m_vertices = 0;
	GLsizei m_vertex_count = 0;
	GLint m_direction = -1;
	GLint m_position = -1;
	GLint m_forward = -1;
	GLint m_right = -1;
	GLint m_up = -1;
	GLint m_cube = -1;
};
