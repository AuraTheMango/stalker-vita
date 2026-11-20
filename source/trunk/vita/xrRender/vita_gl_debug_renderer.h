#pragma once

#include "vita_gl_program.h"
#include "vita_gl_vertex_format.h"

#include <cstdint>

constexpr float vita_player_fov_scale = 0.2631f; // cot(40 degrees) / 4.53

enum class VitaTerrainSample
{
	Shaded,
	BaseAlpha,
	LightmapAlpha,
	Coverage
};

struct VitaGeometryDrawTiming
{
	uint64_t state_us = 0;
	uint64_t indexed_us = 0;
};

class VitaGLDebugRenderer
{
public:
	VitaGLDebugRenderer();
	VitaGLDebugRenderer(const VitaGLDebugRenderer&) = delete;
	VitaGLDebugRenderer& operator=(const VitaGLDebugRenderer&) = delete;

	bool initialize(char* error_log = 0, size_t error_log_capacity = 0);
	void draw() const;
	bool draw_geometry(const VitaGLVertexBuffer& vertices, const VitaGLIndexBuffer& indices) const;
	bool set_geometry_view(const float center[3], float scale, float yaw, float pitch, const float* model = 0, bool player_view = false, bool foliage = false, float projection_scale = 1.f) const;
	bool draw_geometry_range(const VitaGLVertexBuffer& vertices, const VitaGLIndexBuffer& indices,
		size_t first_vertex, size_t vertex_count, size_t first_index, size_t index_count,
		GLuint texture = 0, GLuint lightmap = 0, bool terrain = false, bool tree = false,
		VitaTerrainSample sample = VitaTerrainSample::Shaded, GLuint detail = 0,
		VitaGeometryDrawTiming* timing = nullptr, bool foliage = false) const;
	bool initialize_npc(char* error_log,size_t capacity);
	bool draw_npc(const VitaGLVertexBuffer& vertices,const VitaGLIndexBuffer& indices,
		size_t frame,size_t next,float blend,GLuint texture)const;
	bool initialize_foliage(char* error_log, size_t capacity);
	bool initialize_lod_alpha(char* error_log, size_t capacity);
	bool draw_lod_alpha(const VitaGLVertexBuffer& vertices, const VitaGLIndexBuffer& indices, GLuint texture) const;
	GLint lod_position_attribute() const;
	GLint lod_uv_attribute() const;
	GLint position_attribute_location() const;
	GLint texcoord_attribute_location() const;
	GLint lightmap_texcoord_attribute_location() const;
	void release();

private:
	void set_view_rows(const GLfloat rows[3][4], bool foliage = false) const;

	VitaGLProgram m_program;
	VitaGLProgram m_npc_program;
	VitaGLProgram m_foliage_program;
	GLint m_foliage_rows[3] = {-1, -1, -1};
	GLint m_foliage_texture = -1;
	GLint m_foliage_uv_scale = -1;
	VitaGLProgram m_lod_alpha_program;
	GLint m_lod_rows[3] = {-1, -1, -1};
	GLint m_lod_texture = -1;
	mutable GLfloat m_current_rows[3][4] = {};
	GLuint m_vertex_buffer;
	GLint m_position_location;
	GLint m_texcoord_location;
	GLint m_lightmap_texcoord_location;
	GLint m_texcoord_scales_location;
	GLint m_color_location;
	GLint m_view_row_locations[3];
	GLint m_texture_location;
	GLint m_textured_location;
	GLint m_lightmap_location;
	GLint m_lightmapped_location;
	GLint m_terrain_location;
	GLint m_terrain_sample_location;
	GLint m_detail_location;
	GLint m_detail_enabled_location;
	mutable GLuint m_last_texture;
	mutable GLuint m_last_lightmap;
	mutable GLuint m_last_detail;
	mutable bool m_material_state_valid;
	bool m_initialized;
};
