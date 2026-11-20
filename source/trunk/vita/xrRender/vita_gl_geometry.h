#pragma once

#include <vitaGL.h>

#include <stddef.h>
#include <stdint.h>

struct VitaGLVertexAttribute
{
	GLuint index;
	GLint components;
	GLenum type;
	GLboolean normalized;
	GLsizei stride;
	uintptr_t offset;
};

class VitaGLGeometry
{
public:
	VitaGLGeometry();
	VitaGLGeometry(const VitaGLGeometry&) = delete;
	VitaGLGeometry& operator=(const VitaGLGeometry&) = delete;

	bool upload_vertices(const void* data, size_t byte_count);
	bool upload_indices(const void* data, size_t index_count, GLenum index_type = GL_UNSIGNED_SHORT);
	void bind() const;
	void enable_attribute(const VitaGLVertexAttribute& attribute) const;
	bool draw(GLenum primitive, size_t index_count, GLenum index_type = GL_UNSIGNED_SHORT, uintptr_t index_offset = 0) const;
	void release();

	GLuint vertex_buffer() const;
	GLuint index_buffer() const;

private:
	GLuint m_vertex_buffer;
	GLuint m_index_buffer;
	size_t m_index_count;
	GLenum m_index_type;
};
