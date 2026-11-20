#include "vita_gl_program.h"

#include <limits>

namespace
{
	void clear_log(char* log, size_t capacity)
	{
		if (log && capacity)
			log[0] = '\0';
	}

	void shader_log(GLuint shader, char* log, size_t capacity)
	{
		clear_log(log, capacity);
		if (!shader || !log || capacity < 2)
			return;
		const GLsizei limit = static_cast<GLsizei>((capacity - 1) > static_cast<size_t>((std::numeric_limits<GLsizei>::max)())
			? (std::numeric_limits<GLsizei>::max)() : capacity - 1);
		GLsizei written = 0;
		glGetShaderInfoLog(shader, limit, &written, log);
		log[written >= 0 && static_cast<size_t>(written) < capacity ? written : capacity - 1] = '\0';
	}

	void program_log(GLuint program, char* log, size_t capacity)
	{
		clear_log(log, capacity);
		if (!program || !log || capacity < 2)
			return;
		const GLsizei limit = static_cast<GLsizei>((capacity - 1) > static_cast<size_t>((std::numeric_limits<GLsizei>::max)())
			? (std::numeric_limits<GLsizei>::max)() : capacity - 1);
		GLsizei written = 0;
		glGetProgramInfoLog(program, limit, &written, log);
		log[written >= 0 && static_cast<size_t>(written) < capacity ? written : capacity - 1] = '\0';
	}

	GLuint compile_shader(GLenum type, const char* source, char* error_log, size_t error_log_capacity)
	{
		if (!source)
			return 0;
		const GLuint shader = glCreateShader(type);
		if (!shader)
			return 0;
		glShaderSource(shader, 1, &source, 0);
		glCompileShader(shader);
		GLint compiled = GL_FALSE;
		glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
		if (compiled != GL_TRUE)
		{
			shader_log(shader, error_log, error_log_capacity);
			glDeleteShader(shader);
			return 0;
		}
		return shader;
	}
}

VitaGLProgram::VitaGLProgram() : m_program(0)
{
}

VitaGLProgram::~VitaGLProgram()
{
	release();
}

bool VitaGLProgram::initialize(
	const char* vertex_source,
	const char* fragment_source,
	char* error_log,
	size_t error_log_capacity)
{
	release();
	clear_log(error_log, error_log_capacity);
	const GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_source, error_log, error_log_capacity);
	if (!vertex_shader)
		return false;
	const GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_source, error_log, error_log_capacity);
	if (!fragment_shader)
	{
		glDeleteShader(vertex_shader);
		return false;
	}

	const GLuint program = glCreateProgram();
	if (!program)
	{
		glDeleteShader(vertex_shader);
		glDeleteShader(fragment_shader);
		return false;
	}
	glAttachShader(program, vertex_shader);
	glAttachShader(program, fragment_shader);
	glLinkProgram(program);
	glDeleteShader(vertex_shader);
	glDeleteShader(fragment_shader);

	GLint linked = GL_FALSE;
	glGetProgramiv(program, GL_LINK_STATUS, &linked);
	if (linked != GL_TRUE)
	{
		program_log(program, error_log, error_log_capacity);
		glDeleteProgram(program);
		return false;
	}
	m_program = program;
	return true;
}

void VitaGLProgram::use() const
{
	glUseProgram(m_program);
}

GLint VitaGLProgram::attribute_location(const char* name) const
{
	return m_program && name ? glGetAttribLocation(m_program, name) : -1;
}

GLint VitaGLProgram::uniform_location(const char* name) const
{
	return m_program && name ? glGetUniformLocation(m_program, name) : -1;
}

GLuint VitaGLProgram::handle() const
{
	return m_program;
}

void VitaGLProgram::release()
{
	if (m_program)
		glDeleteProgram(m_program);
	m_program = 0;
}
