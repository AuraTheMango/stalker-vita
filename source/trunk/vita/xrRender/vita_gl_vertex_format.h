#pragma once

#include "vita_gl_geometry.h"

#include <stdint.h>
#include <vector>

// On-disk R1 vertex declarations use the 8-byte D3D9 D3DVERTEXELEMENT9 layout.
struct VitaD3D9VertexElement
{
	uint16_t stream;
	uint16_t offset;
	uint8_t type;
	uint8_t method;
	uint8_t usage;
	uint8_t usage_index;
};

struct VitaGLSemanticBinding
{
	uint8_t usage;
	uint8_t usage_index;
	GLuint attribute_index;
};

bool vita_gl_vertex_stride_from_d3d9_declaration(
	const void* declaration_data,
	size_t declaration_size,
	size_t& vertex_stride);

bool vita_gl_attribute_from_d3d9(
	const VitaD3D9VertexElement& element,
	GLuint attribute_index,
	GLsizei vertex_stride,
	VitaGLVertexAttribute& attribute);

bool vita_gl_attributes_from_d3d9_declaration(
	const void* declaration_data,
	size_t declaration_size,
	GLsizei vertex_stride,
	const VitaGLSemanticBinding* bindings,
	size_t binding_count,
	std::vector<VitaGLVertexAttribute>& attributes);

class VitaGLVertexBuffer
{
public:
	VitaGLVertexBuffer();
	~VitaGLVertexBuffer();
	VitaGLVertexBuffer(const VitaGLVertexBuffer&) = delete;
	VitaGLVertexBuffer& operator=(const VitaGLVertexBuffer&) = delete;

	bool initialize(
		const void* vertex_data,
		size_t vertex_byte_count,
		const void* declaration_data,
		size_t declaration_size,
		const VitaGLSemanticBinding* bindings,
		size_t binding_count);
	bool upload_position_frames(GLuint attribute_index, const float* positions, size_t frames);
	void select_position_frame(size_t frame);
	bool bind_position_frame(GLuint attribute,size_t frame)const;
	size_t position_frame_count() const;
	void bind(size_t first_vertex = 0) const;
	void release();
	GLuint handle() const;
	size_t vertex_count() const;
	size_t vertex_stride() const;
	bool has_attribute(GLuint attribute_index) const;
	bool has_integer_short_attribute(GLuint attribute_index) const;
	bool position_bounds(float minimum[3], float maximum[3]) const;
	bool range_position_bounds(size_t first_vertex, size_t vertex_count,
		float minimum[3], float maximum[3]) const;
	bool indexed_position_bounds(size_t first_vertex, size_t range_vertex_count,
		const uint16_t* indices, size_t index_count, float minimum[3], float maximum[3]) const;
	const float* position_data() const;
	void discard_position_data();

private:
	std::vector<GLuint> m_attribute_buffers;
	size_t m_vertex_count;
	size_t m_vertex_stride;
	std::vector<VitaGLVertexAttribute> m_attributes;
	std::vector<float> m_positions;
	size_t m_position_stream = 0;
	size_t m_position_frame = 0;
	size_t m_position_frame_count = 1;
	float m_position_minimum[3];
	float m_position_maximum[3];
	bool m_has_position_bounds;
};

class VitaGLIndexBuffer
{
public:
	VitaGLIndexBuffer();
	~VitaGLIndexBuffer();
	VitaGLIndexBuffer(const VitaGLIndexBuffer&) = delete;
	VitaGLIndexBuffer& operator=(const VitaGLIndexBuffer&) = delete;

	bool initialize(const void* index_data, size_t index_count, GLenum index_type = GL_UNSIGNED_SHORT);
	void bind() const;
	bool draw(GLenum primitive, size_t index_count, uintptr_t index_offset = 0) const;
	const uint16_t* cpu_indices() const;
	size_t cpu_index_count() const;
	void discard_cpu_copy();
	void release();
	GLuint handle() const;
	size_t index_count() const;
	GLenum index_type() const;

private:
	GLuint m_buffer;
	size_t m_index_count;
	GLenum m_index_type;
	std::vector<uint16_t> m_cpu_indices;
};

class VitaGLStaticGeometry
{
public:
	VitaGLStaticGeometry();
	VitaGLStaticGeometry(const VitaGLStaticGeometry&) = delete;
	VitaGLStaticGeometry& operator=(const VitaGLStaticGeometry&) = delete;

	bool initialize_vertices(
		const void* vertex_data,
		size_t vertex_byte_count,
		const void* declaration_data,
		size_t declaration_size,
		const VitaGLSemanticBinding* bindings,
		size_t binding_count);
	bool upload_indices(const void* index_data, size_t index_count, GLenum index_type = GL_UNSIGNED_SHORT);
	bool initialize(
		const void* vertex_data,
		size_t vertex_byte_count,
		const void* index_data,
		size_t index_count,
		GLenum index_type,
		const void* declaration_data,
		size_t declaration_size,
		const VitaGLSemanticBinding* bindings,
		size_t binding_count);
	void bind() const;
	bool draw(GLenum primitive, size_t index_count, uintptr_t index_offset = 0) const;
	void release();
	size_t vertex_stride() const;

private:
	VitaGLGeometry m_geometry;
	std::vector<VitaGLVertexAttribute> m_attributes;
	size_t m_vertex_stride;
	GLenum m_index_type;
};
