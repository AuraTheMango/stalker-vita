#include "vita_gl_vertex_format.h"

#include <cmath>
#include <algorithm>
#include <limits>
#include <string.h>

static_assert(sizeof(VitaD3D9VertexElement) == 8, "D3D9 vertex element serialization must remain 8 bytes");

bool vita_gl_vertex_stride_from_d3d9_declaration(
	const void* declaration_data,
	size_t declaration_size,
	size_t& vertex_stride)
{
	vertex_stride = 0;
	if (!declaration_data || declaration_size < sizeof(VitaD3D9VertexElement) ||
		declaration_size % sizeof(VitaD3D9VertexElement) != 0)
		return false;

	const uint8_t* element_bytes = static_cast<const uint8_t*>(declaration_data);
	const size_t element_count = declaration_size / sizeof(VitaD3D9VertexElement);
	bool found_end = false;
	for (size_t i = 0; i < element_count; ++i)
	{
		VitaD3D9VertexElement element;
		memcpy(&element, element_bytes + i * sizeof(element), sizeof(element));
		if (element.type == 17) // D3DDECLTYPE_UNUSED terminates the declaration.
		{
			found_end = true;
			break;
		}
		if (element.stream != 0 || element.method != 0)
			return false;

		size_t element_size = 0;
		switch (element.type)
		{
		case 0: element_size = 4; break;  // FLOAT1
		case 1: element_size = 8; break;  // FLOAT2
		case 2: element_size = 12; break; // FLOAT3
		case 3: element_size = 16; break; // FLOAT4
		case 4: element_size = 4; break;  // D3DCOLOR
		case 5: element_size = 4; break;  // UBYTE4
		case 6: element_size = 4; break;  // SHORT2
		case 7: element_size = 8; break;  // SHORT4
		case 8: element_size = 4; break;  // UBYTE4N
		case 9: element_size = 4; break;  // SHORT2N
		case 10: element_size = 8; break; // SHORT4N
		case 11: element_size = 4; break; // USHORT2N
		case 12: element_size = 8; break; // USHORT4N
		case 13: element_size = 4; break; // UDEC3
		case 14: element_size = 4; break; // DEC3N
		case 15: element_size = 4; break; // FLOAT16_2
		case 16: element_size = 8; break; // FLOAT16_4
		default:
			return false;
		}

		const size_t element_end = static_cast<size_t>(element.offset) + element_size;
		if (element_end > vertex_stride)
			vertex_stride = element_end;
	}

	if (!found_end || vertex_stride == 0)
	{
		vertex_stride = 0;
		return false;
	}
	return true;
}

bool vita_gl_attribute_from_d3d9(
	const VitaD3D9VertexElement& element,
	GLuint attribute_index,
	GLsizei vertex_stride,
	VitaGLVertexAttribute& attribute)
{
	// R1's serialized static buffers use stream 0 and the default declaration method.
	if (element.stream != 0 || element.method != 0 || vertex_stride <= 0 || element.offset >= vertex_stride)
		return false;

	GLint components = 0;
	GLenum type = 0;
	GLboolean normalized = GL_FALSE;
	GLsizei attribute_size = 0;

	switch (element.type)
	{
	case 0: components = 1; type = GL_FLOAT; attribute_size = 4; break; // FLOAT1
	case 1: components = 2; type = GL_FLOAT; attribute_size = 8; break; // FLOAT2
	case 2: components = 3; type = GL_FLOAT; attribute_size = 12; break; // FLOAT3
	case 3: components = 4; type = GL_FLOAT; attribute_size = 16; break; // FLOAT4
	case 4: components = 4; type = GL_UNSIGNED_BYTE; normalized = GL_TRUE; attribute_size = 4; break; // D3DCOLOR
	case 5: components = 4; type = GL_UNSIGNED_BYTE; attribute_size = 4; break; // UBYTE4
	case 6: components = 2; type = GL_SHORT; attribute_size = 4; break; // SHORT2
	case 7: components = 4; type = GL_SHORT; attribute_size = 8; break; // SHORT4
	case 8: components = 4; type = GL_UNSIGNED_BYTE; normalized = GL_TRUE; attribute_size = 4; break; // UBYTE4N
	case 9: components = 2; type = GL_SHORT; normalized = GL_TRUE; attribute_size = 4; break; // SHORT2N
	case 10: components = 4; type = GL_SHORT; normalized = GL_TRUE; attribute_size = 8; break; // SHORT4N
	case 11: components = 2; type = GL_UNSIGNED_SHORT; normalized = GL_TRUE; attribute_size = 4; break; // USHORT2N
	case 12: components = 4; type = GL_UNSIGNED_SHORT; normalized = GL_TRUE; attribute_size = 8; break; // USHORT4N
	default:
		return false;
	}
	if (attribute_size > vertex_stride || element.offset > vertex_stride - attribute_size)
		return false;

	attribute.index = attribute_index;
	attribute.components = components;
	attribute.type = type;
	attribute.normalized = normalized;
	attribute.stride = vertex_stride;
	attribute.offset = element.offset;
	return true;
}

bool vita_gl_attributes_from_d3d9_declaration(
	const void* declaration_data,
	size_t declaration_size,
	GLsizei vertex_stride,
	const VitaGLSemanticBinding* bindings,
	size_t binding_count,
	std::vector<VitaGLVertexAttribute>& attributes)
{
	attributes.clear();
	if (!declaration_data || declaration_size < sizeof(VitaD3D9VertexElement) ||
		declaration_size % sizeof(VitaD3D9VertexElement) != 0 || vertex_stride <= 0 ||
		(!bindings && binding_count != 0))
		return false;

	const uint8_t* element_bytes = static_cast<const uint8_t*>(declaration_data);
	const size_t element_count = declaration_size / sizeof(VitaD3D9VertexElement);
	bool found_end = false;
	for (size_t i = 0; i < element_count; ++i)
	{
		VitaD3D9VertexElement element;
		memcpy(&element, element_bytes + i * sizeof(element), sizeof(element));
		if (element.type == 17) // D3DDECLTYPE_UNUSED terminates the declaration.
		{
			found_end = true;
			break;
		}

		GLuint attribute_index = 0;
		bool binding_found = false;
		for (size_t j = 0; j < binding_count; ++j)
		{
			if (bindings[j].usage == element.usage && bindings[j].usage_index == element.usage_index)
			{
				attribute_index = bindings[j].attribute_index;
				binding_found = true;
				break;
			}
		}
		if (!binding_found)
			continue;

		VitaGLVertexAttribute attribute;
		if (!vita_gl_attribute_from_d3d9(element, attribute_index, vertex_stride, attribute))
		{
			attributes.clear();
			return false;
		}
		attributes.push_back(attribute);
	}

	if (!found_end || attributes.empty())
	{
		attributes.clear();
		return false;
	}
	return true;
}

VitaGLVertexBuffer::VitaGLVertexBuffer()
	: m_vertex_count(0), m_vertex_stride(0), m_has_position_bounds(false)
{
}

VitaGLVertexBuffer::~VitaGLVertexBuffer()
{
	release();
}

bool VitaGLVertexBuffer::initialize(
	const void* vertex_data,
	size_t vertex_byte_count,
	const void* declaration_data,
	size_t declaration_size,
	const VitaGLSemanticBinding* bindings,
	size_t binding_count)
{
	release();
	if (!vertex_data || vertex_byte_count == 0)
		return false;

	size_t stride = 0;
	if (!vita_gl_vertex_stride_from_d3d9_declaration(declaration_data, declaration_size, stride) ||
		stride > static_cast<size_t>((std::numeric_limits<GLsizei>::max)()) ||
		vertex_byte_count % stride != 0)
		return false;

	std::vector<VitaGLVertexAttribute> attributes;
	if (!vita_gl_attributes_from_d3d9_declaration(
		declaration_data, declaration_size, static_cast<GLsizei>(stride),
		bindings, binding_count, attributes))
		return false;
	if (vertex_byte_count > static_cast<size_t>((std::numeric_limits<GLsizeiptr>::max)()))
		return false;
	if (attributes.empty())
		return false;

	float position_minimum[3] = {0.f, 0.f, 0.f};
	float position_maximum[3] = {0.f, 0.f, 0.f};
	bool has_position_bounds = false;
	const uint8_t* vertex_bytes = static_cast<const uint8_t*>(vertex_data);
	const uint8_t* declaration_bytes = static_cast<const uint8_t*>(declaration_data);
	const size_t declaration_count = declaration_size / sizeof(VitaD3D9VertexElement);
	const size_t input_vertex_count = vertex_byte_count / stride;
	if (input_vertex_count <= (std::numeric_limits<size_t>::max)() / 3)
		m_positions.assign(input_vertex_count * 3, (std::numeric_limits<float>::quiet_NaN)());
	for (size_t element_index = 0; element_index < declaration_count; ++element_index)
	{
		VitaD3D9VertexElement element;
		memcpy(&element, declaration_bytes + element_index * sizeof(element), sizeof(element));
		if (element.type == 17)
			break;
		if (element.usage != 0 || element.usage_index != 0 || (element.type != 2 && element.type != 3))
			continue;

		for (size_t vertex_index = 0; vertex_index < input_vertex_count; ++vertex_index)
		{
			float position[3];
			memcpy(position, vertex_bytes + vertex_index * stride + element.offset, sizeof(position));
			if (!m_positions.empty())
			{
				if (std::isfinite(position[0]) && std::isfinite(position[1]) && std::isfinite(position[2]))
					memcpy(&m_positions[vertex_index * 3], position, sizeof(position));
			}
			if (!std::isfinite(position[0]) || !std::isfinite(position[1]) || !std::isfinite(position[2]))
				continue;
			if (!has_position_bounds)
			{
				memcpy(position_minimum, position, sizeof(position));
				memcpy(position_maximum, position, sizeof(position));
				has_position_bounds = true;
			}
			else
			{
				for (size_t axis = 0; axis < 3; ++axis)
				{
					if (position[axis] < position_minimum[axis]) position_minimum[axis] = position[axis];
					if (position[axis] > position_maximum[axis]) position_maximum[axis] = position[axis];
				}
			}
		}
		break;
	}

	size_t base_uv_offset = stride;
	size_t tangent_alpha_offset = stride;
	size_t binormal_alpha_offset = stride;
	for (size_t i = 0; i < declaration_count; ++i)
	{
		VitaD3D9VertexElement element;
		memcpy(&element, declaration_bytes + i * sizeof(element), sizeof(element));
		if (element.type == 17)
			break;
		if (element.usage_index != 0)
			continue;
		if (element.usage == 5 && element.type == 6) // TEXCOORD0, SHORT2
			base_uv_offset = element.offset;
		if (element.usage == 6 && element.type == 4) // TANGENT0, D3DCOLOR
			tangent_alpha_offset = element.offset + 3;
		if (element.usage == 7 && element.type == 4) // BINORMAL0, D3DCOLOR
			binormal_alpha_offset = element.offset + 3;
	}

	// vitaGL packs attributes from the same VBO into GXM's 16-bit attribute offsets.
	// Distinct VBOs keep the mesh's base vertex in the stream address instead.
	m_attribute_buffers.assign(attributes.size(), 0);
	for (size_t i = 0; i < attributes.size(); ++i)
	{
		VitaGLVertexAttribute& attribute = attributes[i];
		const bool unpack_base_uv = attribute.offset == base_uv_offset && attribute.type == GL_SHORT &&
			attribute.components == 2 && !attribute.normalized &&
			tangent_alpha_offset < stride && binormal_alpha_offset < stride;
		if (unpack_base_uv)
			attribute.type = GL_FLOAT;
		const size_t component_size = attribute.type == GL_FLOAT ? sizeof(float) :
			(attribute.type == GL_UNSIGNED_BYTE ? sizeof(uint8_t) : sizeof(uint16_t));
		const size_t attribute_size = attribute.components * component_size;
		if (input_vertex_count > static_cast<size_t>((std::numeric_limits<GLsizeiptr>::max)()) / attribute_size)
		{
			release();
			return false;
		}
		std::vector<uint8_t> stream(input_vertex_count * attribute_size);
		for (size_t vertex = 0; vertex < input_vertex_count; ++vertex)
		{
			const uint8_t* source = vertex_bytes + vertex * stride;
			uint8_t* target = stream.data() + vertex * attribute_size;
			if (unpack_base_uv)
			{
				// R1 unpack_tc_base adds the normalized D3DCOLOR alpha fractions.
				int16_t packed[2];
				memcpy(packed, source + base_uv_offset, sizeof(packed));
				const float uv[2] = {
					(packed[0] + source[tangent_alpha_offset] / 255.f) / 1024.f,
					(packed[1] + source[binormal_alpha_offset] / 255.f) / 1024.f
				};
				memcpy(target, uv, sizeof(uv));
			}
			else
				memcpy(target, source + attribute.offset, attribute_size);
		}

		GLuint& buffer = m_attribute_buffers[i];
		glGenBuffers(1, &buffer);
		if (!buffer)
		{
			release();
			return false;
		}
		glBindBuffer(GL_ARRAY_BUFFER, buffer);
		glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(stream.size()), stream.data(), GL_STATIC_DRAW);
		if (glGetError() != GL_NO_ERROR)
		{
			release();
			return false;
		}
		attribute.stride = static_cast<GLsizei>(attribute_size);
		attribute.offset = 0;
	}
	m_attributes.swap(attributes);
	m_vertex_stride = stride;
	m_vertex_count = vertex_byte_count / stride;
	m_has_position_bounds = has_position_bounds;
	memcpy(m_position_minimum, position_minimum, sizeof(m_position_minimum));
	memcpy(m_position_maximum, position_maximum, sizeof(m_position_maximum));
	return true;
}

bool VitaGLVertexBuffer::upload_position_frames(GLuint attribute_index, const float* positions, size_t frames)
{
	constexpr size_t bytes_per_vertex = 3 * sizeof(float);
	if (!positions || !m_vertex_count || frames < 2 ||
		frames > static_cast<size_t>((std::numeric_limits<GLsizeiptr>::max)()) / bytes_per_vertex / m_vertex_count)
		return false;
	const auto attribute = std::find_if(m_attributes.begin(), m_attributes.end(), [attribute_index](const VitaGLVertexAttribute& entry) {
		return entry.index == attribute_index && entry.type == GL_FLOAT && entry.components == 3 &&
			entry.stride == 3 * sizeof(float);
	});
	if (attribute == m_attributes.end())
		return false;

	GLuint buffer = 0;
	glGenBuffers(1, &buffer);
	if (!buffer)
		return false;
	glBindBuffer(GL_ARRAY_BUFFER, buffer);
	glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(frames * m_vertex_count * bytes_per_vertex), positions, GL_STATIC_DRAW);
	if (glGetError() != GL_NO_ERROR)
	{
		glDeleteBuffers(1, &buffer);
		return false;
	}
	m_position_stream = static_cast<size_t>(attribute - m_attributes.begin());
	glDeleteBuffers(1, &m_attribute_buffers[m_position_stream]);
	m_attribute_buffers[m_position_stream] = buffer;
	m_position_frame_count = frames;
	m_position_frame = 0;
	return true;
}


bool VitaGLVertexBuffer::bind_position_frame(GLuint attribute,size_t frame)const
{
	if(frame>=m_position_frame_count||m_attributes.empty()||m_position_stream>=m_attributes.size())return false;
	const auto& source=m_attributes[m_position_stream];
	glBindBuffer(GL_ARRAY_BUFFER,m_attribute_buffers[m_position_stream]);
	glVertexAttribPointer(attribute,source.components,source.type,source.normalized,source.stride,
		reinterpret_cast<const void*>(frame*m_vertex_count*source.stride));
	glEnableVertexAttribArray(attribute);return true;
}
void VitaGLVertexBuffer::select_position_frame(size_t frame)
{
	m_position_frame = frame < m_position_frame_count ? frame : 0;
}

size_t VitaGLVertexBuffer::position_frame_count() const { return m_position_frame_count; }

void VitaGLVertexBuffer::bind(size_t first_vertex) const
{
	for (size_t i = 0; i < m_attributes.size(); ++i)
	{
		const VitaGLVertexAttribute& attribute = m_attributes[i];
		glBindBuffer(GL_ARRAY_BUFFER, m_attribute_buffers[i]);
		const size_t stream_vertex = first_vertex + (i == m_position_stream ? m_position_frame * m_vertex_count : 0);
		const uintptr_t offset = static_cast<uintptr_t>(stream_vertex) * attribute.stride;
		glVertexAttribPointer(attribute.index, attribute.components, attribute.type, attribute.normalized,
			attribute.stride, reinterpret_cast<const void*>(offset));
		glEnableVertexAttribArray(attribute.index);
	}
}

void VitaGLVertexBuffer::release()
{
	if (!m_attribute_buffers.empty())
		glDeleteBuffers(static_cast<GLsizei>(m_attribute_buffers.size()), m_attribute_buffers.data());
	m_attribute_buffers.clear();
	m_position_frame = 0;
	m_position_frame_count = 1;
	m_position_stream = 0;
	m_vertex_count = 0;
	m_vertex_stride = 0;
	m_attributes.clear();
	std::vector<float>().swap(m_positions);
	m_has_position_bounds = false;
	memset(m_position_minimum, 0, sizeof(m_position_minimum));
	memset(m_position_maximum, 0, sizeof(m_position_maximum));
}

GLuint VitaGLVertexBuffer::handle() const { return m_attribute_buffers.empty() ? 0 : m_attribute_buffers.front(); }
size_t VitaGLVertexBuffer::vertex_count() const { return m_vertex_count; }
size_t VitaGLVertexBuffer::vertex_stride() const { return m_vertex_stride; }

bool VitaGLVertexBuffer::has_attribute(GLuint attribute_index) const
{
	for (size_t i = 0; i < m_attributes.size(); ++i)
		if (m_attributes[i].index == attribute_index)
			return true;
	return false;
}

bool VitaGLVertexBuffer::has_integer_short_attribute(GLuint attribute_index) const
{
	for (const VitaGLVertexAttribute& attribute : m_attributes)
		if (attribute.index == attribute_index)
			return attribute.type == GL_SHORT && !attribute.normalized;
	return false;
}

bool VitaGLVertexBuffer::position_bounds(float minimum[3], float maximum[3]) const
{
	if (!m_has_position_bounds || !minimum || !maximum)
		return false;
	memcpy(minimum, m_position_minimum, sizeof(m_position_minimum));
	memcpy(maximum, m_position_maximum, sizeof(m_position_maximum));
	return true;
}

bool VitaGLVertexBuffer::range_position_bounds(
	size_t first_vertex, size_t vertex_count, float minimum[3], float maximum[3]) const
{
	if (m_positions.empty() || !vertex_count || !minimum || !maximum ||
		first_vertex > m_vertex_count || vertex_count > m_vertex_count - first_vertex)
		return false;
	bool found = false;
	for (size_t i = first_vertex; i < first_vertex + vertex_count; ++i)
	{
		const float* position = &m_positions[i * 3];
		if (!std::isfinite(position[0]) || !std::isfinite(position[1]) || !std::isfinite(position[2]))
			continue;
		if (!found)
		{
			memcpy(minimum, position, sizeof(float) * 3);
			memcpy(maximum, position, sizeof(float) * 3);
			found = true;
		}
		else for (size_t axis = 0; axis < 3; ++axis)
		{
			if (position[axis] < minimum[axis]) minimum[axis] = position[axis];
			if (position[axis] > maximum[axis]) maximum[axis] = position[axis];
		}
	}
	return found;
}

bool VitaGLVertexBuffer::indexed_position_bounds(
	size_t first_vertex, size_t range_vertex_count, const uint16_t* indices, size_t index_count,
	float minimum[3], float maximum[3]) const
{
	if (m_positions.empty() || !indices || !index_count || !minimum || !maximum ||
		first_vertex > m_vertex_count || range_vertex_count > m_vertex_count - first_vertex)
		return false;
	for (size_t i = 0; i < index_count; ++i)
		if (indices[i] >= range_vertex_count)
			return false;
	bool found = false;
	for (size_t i = 0; i < index_count; ++i)
	{
		const float* position = &m_positions[(first_vertex + indices[i]) * 3];
		if (!std::isfinite(position[0]) || !std::isfinite(position[1]) || !std::isfinite(position[2]))
			continue;
		if (!found)
		{
			memcpy(minimum, position, sizeof(float) * 3);
			memcpy(maximum, position, sizeof(float) * 3);
			found = true;
		}
		else for (size_t axis = 0; axis < 3; ++axis)
		{
			if (position[axis] < minimum[axis]) minimum[axis] = position[axis];
			if (position[axis] > maximum[axis]) maximum[axis] = position[axis];
		}
	}
	return found;
}

const float* VitaGLVertexBuffer::position_data() const
{
	return m_positions.empty() ? 0 : m_positions.data();
}

void VitaGLVertexBuffer::discard_position_data()
{
	std::vector<float>().swap(m_positions);
}

VitaGLIndexBuffer::VitaGLIndexBuffer()
	: m_buffer(0), m_index_count(0), m_index_type(GL_UNSIGNED_SHORT)
{
}

VitaGLIndexBuffer::~VitaGLIndexBuffer()
{
	release();
}

bool VitaGLIndexBuffer::initialize(const void* index_data, size_t index_count, GLenum index_type)
{
	release();
	const size_t index_size = index_type == GL_UNSIGNED_SHORT ? sizeof(uint16_t) :
		(index_type == GL_UNSIGNED_INT ? sizeof(uint32_t) : 0);
	if (!index_data || index_count == 0 || index_size == 0 ||
		index_count > static_cast<size_t>((std::numeric_limits<GLsizeiptr>::max)()) / index_size)
		return false;

	glGenBuffers(1, &m_buffer);
	if (!m_buffer)
		return false;
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_buffer);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(index_count * index_size), index_data, GL_STATIC_DRAW);
	m_index_count = index_count;
	m_index_type = index_type;
	if (index_type == GL_UNSIGNED_SHORT)
		m_cpu_indices.assign(static_cast<const uint16_t*>(index_data),
			static_cast<const uint16_t*>(index_data) + index_count);
	return true;
}

void VitaGLIndexBuffer::bind() const
{
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_buffer);
}

bool VitaGLIndexBuffer::draw(GLenum primitive, size_t index_count, uintptr_t index_offset) const
{
	const size_t index_size = m_index_type == GL_UNSIGNED_SHORT ? sizeof(uint16_t) :
		(m_index_type == GL_UNSIGNED_INT ? sizeof(uint32_t) : 0);
	if (!m_buffer || index_size == 0 || index_count == 0 || index_count > m_index_count ||
		index_count > static_cast<size_t>((std::numeric_limits<GLsizei>::max)()) ||
		index_offset > m_index_count * index_size ||
		index_count > (m_index_count * index_size - index_offset) / index_size)
		return false;

	bind();
	glDrawElements(primitive, static_cast<GLsizei>(index_count), m_index_type,
		reinterpret_cast<const void*>(index_offset));
	return true;
}

const uint16_t* VitaGLIndexBuffer::cpu_indices() const
{
	return m_cpu_indices.empty() ? 0 : m_cpu_indices.data();
}

size_t VitaGLIndexBuffer::cpu_index_count() const
{
	return m_cpu_indices.size();
}

void VitaGLIndexBuffer::discard_cpu_copy()
{
	std::vector<uint16_t>().swap(m_cpu_indices);
}

void VitaGLIndexBuffer::release()
{
	if (m_buffer)
		glDeleteBuffers(1, &m_buffer);
	m_buffer = 0;
	m_index_count = 0;
	m_index_type = GL_UNSIGNED_SHORT;
	std::vector<uint16_t>().swap(m_cpu_indices);
}

GLuint VitaGLIndexBuffer::handle() const { return m_buffer; }
size_t VitaGLIndexBuffer::index_count() const { return m_index_count; }
GLenum VitaGLIndexBuffer::index_type() const { return m_index_type; }

VitaGLStaticGeometry::VitaGLStaticGeometry()
	: m_vertex_stride(0), m_index_type(GL_UNSIGNED_SHORT)
{
}

bool VitaGLStaticGeometry::initialize_vertices(
	const void* vertex_data,
	size_t vertex_byte_count,
	const void* declaration_data,
	size_t declaration_size,
	const VitaGLSemanticBinding* bindings,
	size_t binding_count)
{
	release();
	if (!vertex_data || vertex_byte_count == 0)
		return false;

	size_t stride = 0;
	if (!vita_gl_vertex_stride_from_d3d9_declaration(declaration_data, declaration_size, stride) ||
		stride > static_cast<size_t>((std::numeric_limits<GLsizei>::max)()) ||
		vertex_byte_count % stride != 0)
		return false;

	std::vector<VitaGLVertexAttribute> attributes;
	if (!vita_gl_attributes_from_d3d9_declaration(
		declaration_data, declaration_size, static_cast<GLsizei>(stride),
		bindings, binding_count, attributes))
		return false;

	if (!m_geometry.upload_vertices(vertex_data, vertex_byte_count))
	{
		m_geometry.release();
		return false;
	}

	m_attributes.swap(attributes);
	m_vertex_stride = stride;
	return true;
}

bool VitaGLStaticGeometry::upload_indices(const void* index_data, size_t index_count, GLenum index_type)
{
	if (!m_vertex_stride || !m_geometry.upload_indices(index_data, index_count, index_type))
		return false;
	m_index_type = index_type;
	return true;
}

bool VitaGLStaticGeometry::initialize(
	const void* vertex_data,
	size_t vertex_byte_count,
	const void* index_data,
	size_t index_count,
	GLenum index_type,
	const void* declaration_data,
	size_t declaration_size,
	const VitaGLSemanticBinding* bindings,
	size_t binding_count)
{
	if (!initialize_vertices(vertex_data, vertex_byte_count, declaration_data, declaration_size, bindings, binding_count))
		return false;
	if (!upload_indices(index_data, index_count, index_type))
	{
		release();
		return false;
	}
	return true;
}

void VitaGLStaticGeometry::bind() const
{
	m_geometry.bind();
	for (size_t i = 0; i < m_attributes.size(); ++i)
		m_geometry.enable_attribute(m_attributes[i]);
}

bool VitaGLStaticGeometry::draw(GLenum primitive, size_t index_count, uintptr_t index_offset) const
{
	return m_geometry.draw(primitive, index_count, m_index_type, index_offset);
}

void VitaGLStaticGeometry::release()
{
	m_geometry.release();
	m_attributes.clear();
	m_vertex_stride = 0;
	m_index_type = GL_UNSIGNED_SHORT;
}

size_t VitaGLStaticGeometry::vertex_stride() const
{
	return m_vertex_stride;
}
