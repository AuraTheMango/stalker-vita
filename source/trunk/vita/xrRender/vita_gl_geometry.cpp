#include "vita_gl_geometry.h"

#include <limits>

VitaGLGeometry::VitaGLGeometry()
	: m_vertex_buffer(0), m_index_buffer(0), m_index_count(0), m_index_type(GL_UNSIGNED_SHORT)
{
}

bool VitaGLGeometry::upload_vertices(const void* data, size_t byte_count)
{
	if (!data || byte_count == 0 || byte_count > (std::numeric_limits<GLsizeiptr>::max)())
		return false;

	if (!m_vertex_buffer)
		glGenBuffers(1, &m_vertex_buffer);
	if (!m_vertex_buffer)
		return false;

	glBindBuffer(GL_ARRAY_BUFFER, m_vertex_buffer);
	glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(byte_count), data, GL_STATIC_DRAW);
	return true;
}

bool VitaGLGeometry::upload_indices(const void* data, size_t index_count, GLenum index_type)
{
	const size_t index_size = index_type == GL_UNSIGNED_SHORT ? sizeof(uint16_t) :
		(index_type == GL_UNSIGNED_INT ? sizeof(uint32_t) : 0);
	if (!data || index_count == 0 || index_size == 0 || index_count > (std::numeric_limits<GLsizeiptr>::max)() / index_size)
		return false;

	if (!m_index_buffer)
		glGenBuffers(1, &m_index_buffer);
	if (!m_index_buffer)
		return false;

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_index_buffer);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(index_count * index_size), data, GL_STATIC_DRAW);
	m_index_count = index_count;
	m_index_type = index_type;
	return true;
}

void VitaGLGeometry::bind() const
{
	glBindBuffer(GL_ARRAY_BUFFER, m_vertex_buffer);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_index_buffer);
}

void VitaGLGeometry::enable_attribute(const VitaGLVertexAttribute& attribute) const
{
	glVertexAttribPointer(attribute.index, attribute.components, attribute.type, attribute.normalized,
		attribute.stride, reinterpret_cast<const void*>(attribute.offset));
	glEnableVertexAttribArray(attribute.index);
}

bool VitaGLGeometry::draw(GLenum primitive, size_t index_count, GLenum index_type, uintptr_t index_offset) const
{
	if (!m_index_buffer || index_type != m_index_type || index_count == 0 || index_count > m_index_count ||
		index_count > static_cast<size_t>((std::numeric_limits<GLsizei>::max)()))
		return false;

	const size_t index_size = index_type == GL_UNSIGNED_SHORT ? sizeof(uint16_t) :
		(index_type == GL_UNSIGNED_INT ? sizeof(uint32_t) : 0);
	if (index_size == 0 || index_offset > m_index_count * index_size || index_count > (m_index_count * index_size - index_offset) / index_size)
		return false;

	bind();
	glDrawElements(primitive, static_cast<GLsizei>(index_count), index_type,
		reinterpret_cast<const void*>(index_offset));
	return true;
}

void VitaGLGeometry::release()
{
	if (m_index_buffer)
		glDeleteBuffers(1, &m_index_buffer);
	if (m_vertex_buffer)
		glDeleteBuffers(1, &m_vertex_buffer);
	m_vertex_buffer = 0;
	m_index_buffer = 0;
	m_index_count = 0;
	m_index_type = GL_UNSIGNED_SHORT;
}

GLuint VitaGLGeometry::vertex_buffer() const
{
	return m_vertex_buffer;
}

GLuint VitaGLGeometry::index_buffer() const
{
	return m_index_buffer;
}
