#pragma once

#include "vita_gl_vertex_format.h"

#include <memory>
#include <vector>

class CStreamReader;

// Loads the serialized fsL_VB and fsL_IB chunks used by the R1 renderer.
class VitaGLR1StaticBuffers
{
public:
	VitaGLR1StaticBuffers();
	~VitaGLR1StaticBuffers();
	VitaGLR1StaticBuffers(const VitaGLR1StaticBuffers&) = delete;
	VitaGLR1StaticBuffers& operator=(const VitaGLR1StaticBuffers&) = delete;

	bool load(
		const void* vertex_chunk,
		size_t vertex_chunk_size,
		const void* index_chunk,
		size_t index_chunk_size,
		const VitaGLSemanticBinding* bindings,
		size_t binding_count);
	bool load_chunks(
		CStreamReader* vertex_chunk,
		CStreamReader* index_chunk,
		const VitaGLSemanticBinding* bindings,
		size_t binding_count);
	void release();

	size_t vertex_buffer_count() const;
	size_t index_buffer_count() const;
	VitaGLVertexBuffer* vertex_buffer(size_t index);
	VitaGLIndexBuffer* index_buffer(size_t index);

private:
	std::vector<std::unique_ptr<VitaGLVertexBuffer> > m_vertex_buffers;
	std::vector<std::unique_ptr<VitaGLIndexBuffer> > m_index_buffers;
};
