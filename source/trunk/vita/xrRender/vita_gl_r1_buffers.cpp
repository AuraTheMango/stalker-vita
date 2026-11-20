#include "../../xrCore/stdafx.h"
#include "vita_gl_r1_buffers.h"
#include "../../xrCore/stream_reader.h"

#include <limits>
#include <string.h>

namespace
{
	bool read_u32(const uint8_t* data, size_t data_size, size_t& offset, uint32_t& value)
	{
		if (offset > data_size || sizeof(value) > data_size - offset)
			return false;
		memcpy(&value, data + offset, sizeof(value));
		offset += sizeof(value);
		return true;
	}
}

VitaGLR1StaticBuffers::VitaGLR1StaticBuffers()
{
}

VitaGLR1StaticBuffers::~VitaGLR1StaticBuffers()
{
	release();
}

bool VitaGLR1StaticBuffers::load(
	const void* vertex_chunk,
	size_t vertex_chunk_size,
	const void* index_chunk,
	size_t index_chunk_size,
	const VitaGLSemanticBinding* bindings,
	size_t binding_count)
{
	release();
	if (!vertex_chunk || !index_chunk || (!bindings && binding_count != 0))
		return false;

	const uint8_t* vertex_bytes = static_cast<const uint8_t*>(vertex_chunk);
	size_t vertex_offset = 0;
	uint32_t vertex_count = 0;
	if (!read_u32(vertex_bytes, vertex_chunk_size, vertex_offset, vertex_count) ||
		vertex_count == 0 || vertex_count > vertex_chunk_size / (sizeof(VitaD3D9VertexElement) + sizeof(uint32_t)))
		return false;
	m_vertex_buffers.reserve(vertex_count);

	for (uint32_t i = 0; i < vertex_count; ++i)
	{
		std::vector<VitaD3D9VertexElement> declaration;
		bool found_end = false;
		for (size_t element_index = 0; element_index < 65; ++element_index)
		{
			if (vertex_offset > vertex_chunk_size ||
				sizeof(VitaD3D9VertexElement) > vertex_chunk_size - vertex_offset)
			{
				release();
				return false;
			}
			VitaD3D9VertexElement element;
			memcpy(&element, vertex_bytes + vertex_offset, sizeof(element));
			vertex_offset += sizeof(element);
			declaration.push_back(element);
			if (element.type == 17)
			{
				found_end = true;
				break;
			}
		}
		if (!found_end)
		{
			release();
			return false;
		}

		uint32_t vertex_item_count = 0;
		if (!read_u32(vertex_bytes, vertex_chunk_size, vertex_offset, vertex_item_count) || vertex_item_count == 0)
		{
			release();
			return false;
		}
		size_t stride = 0;
		if (!vita_gl_vertex_stride_from_d3d9_declaration(
			declaration.data(), declaration.size() * sizeof(VitaD3D9VertexElement), stride) ||
			vertex_item_count > (std::numeric_limits<size_t>::max)() / stride)
		{
			release();
			return false;
		}
		const size_t vertex_data_size = static_cast<size_t>(vertex_item_count) * stride;
		if (vertex_offset > vertex_chunk_size || vertex_data_size > vertex_chunk_size - vertex_offset)
		{
			release();
			return false;
		}

		std::unique_ptr<VitaGLVertexBuffer> buffer(new VitaGLVertexBuffer());
		if (!buffer->initialize(vertex_bytes + vertex_offset, vertex_data_size,
			declaration.data(), declaration.size() * sizeof(VitaD3D9VertexElement), bindings, binding_count))
		{
			release();
			return false;
		}
		m_vertex_buffers.push_back(std::move(buffer));
		vertex_offset += vertex_data_size;
	}

	const uint8_t* index_bytes = static_cast<const uint8_t*>(index_chunk);
	size_t index_offset = 0;
	uint32_t index_buffer_count = 0;
	if (!read_u32(index_bytes, index_chunk_size, index_offset, index_buffer_count) ||
		index_buffer_count == 0 || index_buffer_count > index_chunk_size / sizeof(uint32_t))
	{
		release();
		return false;
	}
	m_index_buffers.reserve(index_buffer_count);

	for (uint32_t i = 0; i < index_buffer_count; ++i)
	{
		uint32_t item_count = 0;
		if (!read_u32(index_bytes, index_chunk_size, index_offset, item_count) || item_count == 0 ||
			item_count > (std::numeric_limits<size_t>::max)() / sizeof(uint16_t))
		{
			release();
			return false;
		}
		const size_t index_data_size = static_cast<size_t>(item_count) * sizeof(uint16_t);
		if (index_offset > index_chunk_size || index_data_size > index_chunk_size - index_offset)
		{
			release();
			return false;
		}

		std::unique_ptr<VitaGLIndexBuffer> buffer(new VitaGLIndexBuffer());
		if (!buffer->initialize(index_bytes + index_offset, item_count, GL_UNSIGNED_SHORT))
		{
			release();
			return false;
		}
		m_index_buffers.push_back(std::move(buffer));
		index_offset += index_data_size;
	}

	return true;
}

bool VitaGLR1StaticBuffers::load_chunks(
	CStreamReader* vertex_chunk,
	CStreamReader* index_chunk,
	const VitaGLSemanticBinding* bindings,
	size_t binding_count)
{
	release();
	if (!vertex_chunk || !index_chunk || (!bindings && binding_count != 0))
		return false;

	if (vertex_chunk->tell() > vertex_chunk->length() ||
		vertex_chunk->length() - vertex_chunk->tell() < sizeof(u32))
		return false;
	const u32 vertex_buffer_count = vertex_chunk->r_u32();
	if (vertex_buffer_count == 0 || vertex_buffer_count > vertex_chunk->length() / 12)
		return false;
	m_vertex_buffers.reserve(vertex_buffer_count);

	for (u32 i = 0; i < vertex_buffer_count; ++i)
	{
		std::vector<VitaD3D9VertexElement> declaration;
		bool found_end = false;
		for (size_t element_index = 0; element_index < 65; ++element_index)
		{
			if (vertex_chunk->tell() > vertex_chunk->length() ||
				vertex_chunk->length() - vertex_chunk->tell() < sizeof(VitaD3D9VertexElement))
			{
				release();
				return false;
			}
			VitaD3D9VertexElement element;
			vertex_chunk->r(&element, sizeof(element));
			declaration.push_back(element);
			if (element.type == 17)
			{
				found_end = true;
				break;
			}
		}
		if (!found_end || vertex_chunk->tell() > vertex_chunk->length() ||
			vertex_chunk->length() - vertex_chunk->tell() < sizeof(u32))
		{
			release();
			return false;
		}

		const u32 vertex_item_count = vertex_chunk->r_u32();
		size_t stride = 0;
		if (vertex_item_count == 0 || !vita_gl_vertex_stride_from_d3d9_declaration(
			declaration.data(), declaration.size() * sizeof(VitaD3D9VertexElement), stride) ||
			vertex_item_count > (std::numeric_limits<size_t>::max)() / stride)
		{
			release();
			return false;
		}
		const size_t vertex_data_size = static_cast<size_t>(vertex_item_count) * stride;
		if (vertex_chunk->tell() > vertex_chunk->length() ||
			vertex_data_size > vertex_chunk->length() - vertex_chunk->tell())
		{
			release();
			return false;
		}

		std::vector<uint8_t> vertex_data(vertex_data_size);
		vertex_chunk->r(vertex_data.data(), static_cast<u32>(vertex_data_size));
		std::unique_ptr<VitaGLVertexBuffer> buffer(new VitaGLVertexBuffer());
		if (!buffer->initialize(vertex_data.data(), vertex_data.size(),
			declaration.data(), declaration.size() * sizeof(VitaD3D9VertexElement), bindings, binding_count))
		{
			release();
			return false;
		}
		m_vertex_buffers.push_back(std::move(buffer));
	}

	if (index_chunk->tell() > index_chunk->length() ||
		index_chunk->length() - index_chunk->tell() < sizeof(u32))
	{
		release();
		return false;
	}
	const u32 index_buffer_count = index_chunk->r_u32();
	if (index_buffer_count == 0 || index_buffer_count > index_chunk->length() / sizeof(u32))
	{
		release();
		return false;
	}
	m_index_buffers.reserve(index_buffer_count);

	for (u32 i = 0; i < index_buffer_count; ++i)
	{
		if (index_chunk->tell() > index_chunk->length() ||
			index_chunk->length() - index_chunk->tell() < sizeof(u32))
		{
			release();
			return false;
		}
		const u32 index_item_count = index_chunk->r_u32();
		if (index_item_count == 0 || index_item_count > (std::numeric_limits<size_t>::max)() / sizeof(uint16_t))
		{
			release();
			return false;
		}
		const size_t index_data_size = static_cast<size_t>(index_item_count) * sizeof(uint16_t);
		if (index_chunk->tell() > index_chunk->length() ||
			index_data_size > index_chunk->length() - index_chunk->tell())
		{
			release();
			return false;
		}

		std::vector<uint16_t> index_data(index_item_count);
		index_chunk->r(index_data.data(), static_cast<u32>(index_data_size));
		std::unique_ptr<VitaGLIndexBuffer> buffer(new VitaGLIndexBuffer());
		if (!buffer->initialize(index_data.data(), index_item_count, GL_UNSIGNED_SHORT))
		{
			release();
			return false;
		}
		m_index_buffers.push_back(std::move(buffer));
	}

	return true;
}

void VitaGLR1StaticBuffers::release()
{
	for (size_t i = 0; i < m_vertex_buffers.size(); ++i)
		m_vertex_buffers[i]->release();
	for (size_t i = 0; i < m_index_buffers.size(); ++i)
		m_index_buffers[i]->release();
	m_vertex_buffers.clear();
	m_index_buffers.clear();
}

size_t VitaGLR1StaticBuffers::vertex_buffer_count() const { return m_vertex_buffers.size(); }
size_t VitaGLR1StaticBuffers::index_buffer_count() const { return m_index_buffers.size(); }

VitaGLVertexBuffer* VitaGLR1StaticBuffers::vertex_buffer(size_t index)
{
	return index < m_vertex_buffers.size() ? m_vertex_buffers[index].get() : 0;
}

VitaGLIndexBuffer* VitaGLR1StaticBuffers::index_buffer(size_t index)
{
	return index < m_index_buffers.size() ? m_index_buffers[index].get() : 0;
}
