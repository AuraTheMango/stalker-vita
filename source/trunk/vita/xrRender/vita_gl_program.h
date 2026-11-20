#pragma once

#include <vitaGL.h>

#include <stddef.h>

class VitaGLProgram
{
public:
	VitaGLProgram();
	~VitaGLProgram();
	VitaGLProgram(const VitaGLProgram&) = delete;
	VitaGLProgram& operator=(const VitaGLProgram&) = delete;

	bool initialize(
		const char* vertex_source,
		const char* fragment_source,
		char* error_log = 0,
		size_t error_log_capacity = 0);
	void use() const;
	GLint attribute_location(const char* name) const;
	GLint uniform_location(const char* name) const;
	GLuint handle() const;
	void release();

private:
	GLuint m_program;
};
