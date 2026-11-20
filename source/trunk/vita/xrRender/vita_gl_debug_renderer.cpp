#include "vita_gl_debug_renderer.h"

#include <math.h>
#include <cstring>
#include <psp2/kernel/processmgr.h>

VitaGLDebugRenderer::VitaGLDebugRenderer()
	: m_vertex_buffer(0), m_position_location(-1), m_texcoord_location(-1),
	m_lightmap_texcoord_location(-1), m_texcoord_scales_location(-1), m_color_location(-1),
	m_view_row_locations{-1, -1, -1}, m_texture_location(-1), m_textured_location(-1),
	m_lightmap_location(-1), m_lightmapped_location(-1), m_terrain_location(-1), m_terrain_sample_location(-1),
	m_detail_location(-1), m_detail_enabled_location(-1),
	m_last_texture(0), m_last_lightmap(0), m_last_detail(0),
	m_material_state_valid(false), m_initialized(false)
{
}

bool VitaGLDebugRenderer::initialize(char* error_log, size_t error_log_capacity)
{
	static const char* vertex_shader =
		"attribute vec4 a_position;\n"
		"attribute vec2 a_texcoord;\n"
		"attribute vec2 a_lightmap_texcoord;\n"
		"uniform vec2 u_texcoord_scales;\n"
		"uniform vec4 u_view_x;\n"
		"uniform vec4 u_view_y;\n"
		"uniform vec4 u_view_z;\n"
		"uniform vec4 u_color;\n"
		"varying vec2 v_texcoord;\n"
		"varying vec2 v_lightmap_texcoord;\n"
		"varying vec4 v_color;\n"
		"void main() { vec4 position = vec4(a_position.xyz, 1.0); "
		"vec3 view = vec3(dot(u_view_x, position), dot(u_view_y, position), dot(u_view_z, position)); "
		"float camera_z = 8.0 - view.z; gl_Position = vec4(view.x * 4.53, view.y * 8.0, camera_z * 1.002 - 0.2002, camera_z); v_texcoord = a_texcoord * u_texcoord_scales.x; v_lightmap_texcoord = a_lightmap_texcoord * u_texcoord_scales.y; v_color = u_color; }\n";
	static const char* fragment_shader =
		"precision mediump float;\n"
		"uniform sampler2D u_texture;\n"
		"uniform sampler2D u_lightmap;\n"
		"uniform sampler2D u_detail;\n"
		"uniform float u_detail_enabled;\n"
		"uniform float u_textured;\n"
		"uniform float u_lightmapped;\n"
		"uniform float u_terrain;\n"
		"uniform float u_terrain_sample;\n"
		"varying vec2 v_texcoord;\n"
		"varying vec2 v_lightmap_texcoord;\n"
		"varying vec4 v_color;\n"
		// R1 impl_dt uses shared UVs and stores hemisphere and sun weights in the texture alphas.
		// Approximate default_weather_07 with an upward-facing terrain normal.
		"void main() {\n"
		"  if (u_textured <= 0.5) { gl_FragColor = v_color; return; }\n"
		"  vec4 base = texture2D(u_texture, v_texcoord);\n"
		"  if (u_terrain_sample > 1.5) {\n"
		"    gl_FragColor = u_lightmapped > 0.5 ? vec4(vec3(texture2D(u_lightmap, v_texcoord).a), 1.0) : vec4(1.0, 0.0, 1.0, 1.0);\n"
		"    return;\n"
		"  }\n"
		"  if (u_terrain_sample > 0.5) { gl_FragColor = vec4(vec3(base.a), 1.0); return; }\n"
		"  if (u_lightmapped > 0.5) {\n"
		"    if (u_terrain > 0.5) {\n"
		"      vec4 baked = texture2D(u_lightmap, v_texcoord);\n"
		"      vec3 hemisphere = vec3(0.925, 0.95, 0.975) * base.a;\n"
		"      vec3 sun = vec3(0.435, 0.393, 0.289) * (0.309 * baked.a);\n"
		"      base.rgb *= 2.0 * (vec3(0.11) + baked.rgb + hemisphere + sun);\n"
		"      if (u_detail_enabled > 0.5) base.rgb *= 2.0 * texture2D(u_detail, v_texcoord * 144.0).rgb;\n"
		"    } else {\n"
		"      base.rgb *= texture2D(u_lightmap, v_lightmap_texcoord).rgb * 1.5 + vec3(0.75);\n"
		"    }\n"
		"  }\n"
		"  base.a = mix(base.a, 1.0, u_terrain);\n"
		"  gl_FragColor = base * v_color;\n"
		"}\n";
	static const float triangle_vertices[] = {
		-0.58f, -0.48f, 0.0f, 1.0f,
		 0.58f, -0.48f, 0.0f, 1.0f,
		 0.00f,  0.58f, 0.0f, 1.0f
	};

	release();
	if (!m_program.initialize(vertex_shader, fragment_shader, error_log, error_log_capacity))
		return false;
	m_position_location = m_program.attribute_location("a_position");
	m_texcoord_location = m_program.attribute_location("a_texcoord");
	m_lightmap_texcoord_location = m_program.attribute_location("a_lightmap_texcoord");
	m_texcoord_scales_location = m_program.uniform_location("u_texcoord_scales");
	m_color_location = m_program.uniform_location("u_color");
	m_view_row_locations[0] = m_program.uniform_location("u_view_x");
	m_view_row_locations[1] = m_program.uniform_location("u_view_y");
	m_view_row_locations[2] = m_program.uniform_location("u_view_z");
	m_texture_location = m_program.uniform_location("u_texture");
	m_textured_location = m_program.uniform_location("u_textured");
	m_lightmap_location = m_program.uniform_location("u_lightmap");
	m_lightmapped_location = m_program.uniform_location("u_lightmapped");
	m_terrain_location = m_program.uniform_location("u_terrain");
	m_terrain_sample_location = m_program.uniform_location("u_terrain_sample");
	m_detail_location = m_program.uniform_location("u_detail");
	m_detail_enabled_location = m_program.uniform_location("u_detail_enabled");
	if (m_position_location < 0 || m_texcoord_location < 0 || m_lightmap_texcoord_location < 0 ||
		m_texcoord_scales_location < 0 || m_color_location < 0 ||
		m_view_row_locations[0] < 0 || m_view_row_locations[1] < 0 || m_view_row_locations[2] < 0 || m_texture_location < 0 || m_textured_location < 0 ||
		m_lightmap_location < 0 || m_lightmapped_location < 0 || m_terrain_location < 0 || m_terrain_sample_location < 0 ||
		m_detail_location < 0 || m_detail_enabled_location < 0)
	{
		release();
		return false;
	}

	glGenBuffers(1, &m_vertex_buffer);
	if (!m_vertex_buffer)
	{
		release();
		return false;
	}
	glBindBuffer(GL_ARRAY_BUFFER, m_vertex_buffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(triangle_vertices), triangle_vertices, GL_STATIC_DRAW);
	m_initialized = true;
	return true;
}

void VitaGLDebugRenderer::set_view_rows(const GLfloat rows[3][4], bool foliage) const
{
	std::memcpy(m_current_rows, rows, sizeof(m_current_rows));
	for (size_t axis = 0; axis < 3; ++axis)
		glUniform4fv(foliage ? m_foliage_rows[axis] : m_view_row_locations[axis], 1, rows[axis]);
}

void VitaGLDebugRenderer::draw() const
{
	if (!m_initialized)
		return;
	static const GLfloat identity[3][4] = {
		{1.f, 0.f, 0.f, 0.f},
		{0.f, 1.f, 0.f, 0.f},
		{0.f, 0.f, 1.f, 0.f}
	};

	m_program.use();
	set_view_rows(identity);
	glUniform4f(m_color_location, 0.30f, 0.85f, 0.50f, 1.0f);
	glUniform1f(m_textured_location, 0.f);
	glBindBuffer(GL_ARRAY_BUFFER, m_vertex_buffer);
	glVertexAttribPointer(static_cast<GLuint>(m_position_location), 4, GL_FLOAT, GL_FALSE,
		4 * sizeof(GLfloat), 0);
	glEnableVertexAttribArray(static_cast<GLuint>(m_position_location));
	glDrawArrays(GL_TRIANGLES, 0, 3);
}

bool VitaGLDebugRenderer::draw_geometry(
	const VitaGLVertexBuffer& vertices,
	const VitaGLIndexBuffer& indices) const
{
	if (!m_initialized || vertices.vertex_count() == 0 || indices.index_count() == 0)
		return false;

	float minimum[3];
	float maximum[3];
	if (!vertices.position_bounds(minimum, maximum))
		return false;
	const float center[3] = {
		(minimum[0] + maximum[0]) * 0.5f,
		(minimum[1] + maximum[1]) * 0.5f,
		(minimum[2] + maximum[2]) * 0.5f
	};
	float extent = maximum[0] - minimum[0];
	if (maximum[2] - minimum[2] > extent) extent = maximum[2] - minimum[2];
	if (maximum[1] - minimum[1] > extent) extent = maximum[1] - minimum[1];
	if (!(extent > 0.f))
		return false;
	const float scale = 1.6f / extent;
	const GLfloat transform[3][4] = {
		{scale, 0.f, 0.f, -center[0] * scale},
		{0.f, 0.f, scale, -center[2] * scale},
		{0.f, scale, 0.f, -center[1] * scale}
	};

	m_program.use();
	set_view_rows(transform);
	glUniform4f(m_color_location, 0.30f, 0.85f, 0.50f, 1.0f);
	glUniform1f(m_textured_location, 0.f);
	vertices.bind();
	return indices.draw(GL_TRIANGLES, indices.index_count());
}

bool VitaGLDebugRenderer::set_geometry_view(const float center[3], float scale, float yaw, float pitch, const float* model, bool player_view, bool foliage, float projection_scale) const
{
	if (!m_initialized || !center || !(scale > 0.f))
		return false;
	const float cy = cosf(yaw);
	const float sy = sinf(yaw);
	const float cp = cosf(pitch);
	const float sp = sinf(pitch);
	GLfloat transform[3][4] = {
		{cy * scale, 0.f, -sy * scale, -(cy * center[0] - sy * center[2]) * scale},
		{cp * sy * scale, sp * scale, cp * cy * scale,
			-(cp * sy * center[0] + sp * center[1] + cp * cy * center[2]) * scale},
		{-sp * sy * scale, cp * scale, -sp * cy * scale,
			-(-sp * sy * center[0] + cp * center[1] - sp * cy * center[2]) * scale}
	};
	// Keep the depth transform and change the projection for the player or weapon view.
	if (player_view)
		projection_scale *= vita_player_fov_scale;
	if (projection_scale != 1.f)
		for (size_t row = 0; row < 2; ++row)
			for (float& component : transform[row])
				component *= projection_scale;
	if (foliage)
		m_foliage_program.use();
	else
		m_program.use();
	if (model)
	{
		GLfloat combined[3][4];
		for (size_t row = 0; row < 3; ++row)
			for (size_t column = 0; column < 4; ++column)
			{
				combined[row][column] = transform[row][0] * model[column * 4] +
					transform[row][1] * model[column * 4 + 1] +
					transform[row][2] * model[column * 4 + 2];
				if (column == 3) combined[row][column] += transform[row][3];
			}
		set_view_rows(combined, foliage);
	}
	else
		set_view_rows(transform, foliage);
	if (foliage)
	{
		glUniform1i(m_foliage_texture, 0);
		glActiveTexture(GL_TEXTURE0);
		m_material_state_valid = false;
		return true;
	}
	glUniform4f(m_color_location, 0.30f, 0.85f, 0.50f, 1.0f);
	glUniform1i(m_texture_location, 0);
	glUniform1f(m_textured_location, 0.f);
	glUniform1i(m_lightmap_location, 1);
	glUniform1i(m_detail_location, 2);
	glUniform1f(m_lightmapped_location, 0.f);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);
	m_last_texture = 0;
	m_last_lightmap = 0;
	m_material_state_valid = false;
	return true;
}

bool VitaGLDebugRenderer::draw_geometry_range(
	const VitaGLVertexBuffer& vertices, const VitaGLIndexBuffer& indices,
	size_t first_vertex, size_t vertex_count, size_t first_index, size_t index_count,
	GLuint texture, GLuint lightmap, bool terrain, bool tree, VitaTerrainSample sample, GLuint detail,
	VitaGeometryDrawTiming* timing, bool foliage) const
{
	if (!m_initialized || vertex_count == 0 || index_count < 3 ||
		vertices.vertex_stride() == 0 ||
		first_vertex > vertices.vertex_count() || vertex_count > vertices.vertex_count() - first_vertex ||
		first_index > indices.index_count() || index_count > indices.index_count() - first_index ||
		first_vertex > (static_cast<size_t>(-1) / vertices.vertex_stride()))
		return false;
	const uint64_t state_start = timing ? sceKernelGetProcessTimeWide() : 0;
	vertices.bind(first_vertex);
	const GLuint base_uv = static_cast<GLuint>(m_texcoord_location);
	const GLuint lightmap_uv = static_cast<GLuint>(m_lightmap_texcoord_location);
	// R1 uses 11 fractional bits for tree UVs, 10 for other base UVs, and 15 for lightmaps.
	const float base_uv_scale = tree ? 1.f / 2048.f : 1.f / 1024.f;
	const float uv_scale = vertices.has_integer_short_attribute(base_uv) ? base_uv_scale : 1.f;
	if (foliage)
	{
		glUniform1f(m_foliage_uv_scale, uv_scale);
		glBindTexture(GL_TEXTURE_2D, texture);
	}
	else
	{
		if (!vertices.has_attribute(base_uv))
		{
			glDisableVertexAttribArray(base_uv);
			glVertexAttrib2f(base_uv, 0.f, 0.f);
			texture = 0;
		}
		if (!vertices.has_attribute(lightmap_uv))
		{
			glDisableVertexAttribArray(lightmap_uv);
			glVertexAttrib2f(lightmap_uv, 0.f, 0.f);
			if (!terrain) lightmap = 0;
		}
		glUniform2f(m_texcoord_scales_location,
			uv_scale,
			vertices.has_integer_short_attribute(lightmap_uv) ? 1.f / 32768.f : 1.f);
		// R1's terrain pass shares base UVs with its lighting texture and writes opaque pixels.
		glUniform1f(m_terrain_location, terrain ? 1.f : 0.f);
		glUniform1f(m_terrain_sample_location, static_cast<GLfloat>(sample));
		if (!m_material_state_valid || texture != m_last_texture || lightmap != m_last_lightmap || detail != m_last_detail)
		{
			glUniform1f(m_textured_location, texture ? 1.f : 0.f);
			glUniform1f(m_lightmapped_location, texture && lightmap ? 1.f : 0.f);
			glUniform4f(m_color_location, texture ? 1.f : 0.30f,
				texture ? 1.f : 0.85f, texture ? 1.f : 0.50f, 1.f);
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, texture);
			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, lightmap);
			glActiveTexture(GL_TEXTURE2);
			glBindTexture(GL_TEXTURE_2D, detail);
			glActiveTexture(GL_TEXTURE0);
			glUniform1f(m_detail_enabled_location, detail ? 1.f : 0.f);
			m_last_texture = texture;
			m_last_lightmap = lightmap;
			m_last_detail = detail;
			m_material_state_valid = true;
		}
		if (sample == VitaTerrainSample::Coverage)
		{
			glUniform4f(m_color_location, 1.f, 0.12f, 0.8f, 1.f);
			m_material_state_valid = false;
		}
	}
	const uint64_t indexed_start = timing ? sceKernelGetProcessTimeWide() : 0;
	const bool drawn = indices.draw(GL_TRIANGLES, index_count, first_index * sizeof(uint16_t));
	if (timing)
	{
		timing->indexed_us = sceKernelGetProcessTimeWide() - indexed_start;
		timing->state_us = indexed_start - state_start;
	}
	return drawn;
}


bool VitaGLDebugRenderer::initialize_npc(char* error_log,size_t capacity)
{
	const char*vertex=R"(
attribute vec4 a_position;
attribute vec2 a_texcoord;
attribute vec3 a_next;
uniform float u_blend;
uniform vec4 u_view_x;
uniform vec4 u_view_y;
uniform vec4 u_view_z;
varying vec2 v_uv;
void main(){
	vec4 p=vec4(mix(a_position.xyz,a_next,u_blend),1.0);
	vec3 v=vec3(dot(u_view_x,p),dot(u_view_y,p),dot(u_view_z,p));
	float z=8.0-v.z;
	gl_Position=vec4(v.x*4.53,v.y*8.0,z*1.002-0.2002,z);v_uv=a_texcoord;
}
)";
	const char*fragment=R"(
precision mediump float;
uniform sampler2D u_texture;
uniform float u_textured;
varying vec2 v_uv;
void main(){gl_FragColor=u_textured>0.5?texture2D(u_texture,v_uv):vec4(0.3,0.85,0.5,1.0);}
)";
	if(!m_npc_program.initialize(vertex,fragment,error_log,capacity))return false;
	if(m_npc_program.attribute_location("a_position")!=m_position_location||
		m_npc_program.attribute_location("a_texcoord")!=m_texcoord_location)
	{m_npc_program.release();return false;}
	return true;
}
bool VitaGLDebugRenderer::draw_npc(const VitaGLVertexBuffer& vertices,const VitaGLIndexBuffer& indices,
	size_t frame,size_t next,float blend,GLuint texture)const
{
	if(!m_npc_program.handle())
	{
		m_program.use();
		return draw_geometry_range(vertices,indices,0,vertices.vertex_count(),0,indices.index_count(),texture);
	}
	m_npc_program.use();
	const char*rows[]={"u_view_x","u_view_y","u_view_z"};
	for(unsigned i=0;i<3;++i)glUniform4fv(m_npc_program.uniform_location(rows[i]),1,m_current_rows[i]);
	vertices.bind();
	if(!vertices.bind_position_frame(m_position_location,frame))return false;
	if(!vertices.bind_position_frame(m_npc_program.attribute_location("a_next"),next))return false;
	glUniform1f(m_npc_program.uniform_location("u_blend"),blend);
	glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,texture);
	glUniform1i(m_npc_program.uniform_location("u_texture"),0);glUniform1f(m_npc_program.uniform_location("u_textured"),texture?1.f:0.f);
	return indices.draw(GL_TRIANGLES,indices.index_count());
}

bool VitaGLDebugRenderer::initialize_foliage(char* error_log, size_t capacity)
{
	static const char* vertex =
		"attribute vec4 a_position;\n"
		"attribute vec2 a_texcoord;\n"
		"uniform vec4 u_view_x, u_view_y, u_view_z;\n"
		"uniform float u_uv_scale;\n"
		"varying vec2 v_uv;\n"
		"void main() { vec4 p = vec4(a_position.xyz, 1.0); float z = 8.0 - dot(u_view_z, p); "
		"gl_Position = vec4(dot(u_view_x, p) * 4.53, dot(u_view_y, p) * 8.0, z * 1.002 - 0.2002, z); "
		"v_uv = a_texcoord * u_uv_scale; }\n";
	static const char* fragment =
		"precision mediump float;\n"
		"uniform sampler2D u_texture;\n"
		"varying vec2 v_uv;\n"
		"void main() { vec4 leaf = texture2D(u_texture, v_uv); "
		"if (leaf.a < 0.5) discard; gl_FragColor = vec4(leaf.rgb, 1.0); }\n";
	if (!m_initialized || !m_foliage_program.initialize(vertex, fragment, error_log, capacity))
		return false;

	m_foliage_rows[0] = m_foliage_program.uniform_location("u_view_x");
	m_foliage_rows[1] = m_foliage_program.uniform_location("u_view_y");
	m_foliage_rows[2] = m_foliage_program.uniform_location("u_view_z");
	m_foliage_texture = m_foliage_program.uniform_location("u_texture");
	m_foliage_uv_scale = m_foliage_program.uniform_location("u_uv_scale");
	// Level vertex streams are already bound to the primary program's attribute indices.
	if (m_foliage_program.attribute_location("a_position") == m_position_location &&
		m_foliage_program.attribute_location("a_texcoord") == m_texcoord_location &&
		m_foliage_rows[0] >= 0 && m_foliage_rows[1] >= 0 && m_foliage_rows[2] >= 0 &&
		m_foliage_texture >= 0 && m_foliage_uv_scale >= 0)
		return true;
	m_foliage_program.release();
	return false;
}

bool VitaGLDebugRenderer::initialize_lod_alpha(char* error_log, size_t capacity)
{
	static const char* vertex =
		"attribute vec4 a_position;\n"
		"attribute vec2 a_texcoord;\n"
		"uniform vec4 u_view_x, u_view_y, u_view_z;\n"
		"varying vec2 v_uv;\n"
		"void main() { vec4 p = vec4(a_position.xyz, 1.0); float z = 8.0 - dot(u_view_z, p); "
		"gl_Position = vec4(dot(u_view_x, p) * 4.53, dot(u_view_y, p) * 8.0, z * 1.002 - 0.2002, z); "
		"v_uv = a_texcoord; }\n";
	static const char* fragment =
		"precision mediump float;\n"
		"uniform sampler2D u_texture;\n"
		"varying vec2 v_uv;\n"
		"void main() { vec4 color = texture2D(u_texture, v_uv); "
		"if (color.a <= 8.0 / 255.0) discard; gl_FragColor = color; }\n";
	if (!m_initialized || !m_lod_alpha_program.initialize(vertex, fragment, error_log, capacity))
		return false;
	m_lod_rows[0] = m_lod_alpha_program.uniform_location("u_view_x");
	m_lod_rows[1] = m_lod_alpha_program.uniform_location("u_view_y");
	m_lod_rows[2] = m_lod_alpha_program.uniform_location("u_view_z");
	m_lod_texture = m_lod_alpha_program.uniform_location("u_texture");
	if (m_lod_rows[0] >= 0 && m_lod_rows[1] >= 0 && m_lod_rows[2] >= 0 &&
		m_lod_texture >= 0 && lod_position_attribute() >= 0 && lod_uv_attribute() >= 0)
		return true;
	m_lod_alpha_program.release();
	return false;
}

bool VitaGLDebugRenderer::draw_lod_alpha(const VitaGLVertexBuffer& vertices, const VitaGLIndexBuffer& indices, GLuint texture) const
{
	if (!m_lod_alpha_program.handle() || !texture)
		return false;
	m_lod_alpha_program.use();
	for (size_t axis = 0; axis < 3; ++axis)
		glUniform4fv(m_lod_rows[axis], 1, m_current_rows[axis]);
	glUniform1i(m_lod_texture, 0);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, texture);
	vertices.bind();
	const bool drawn = indices.draw(GL_TRIANGLES, 6);
	m_program.use();
	m_material_state_valid = false;
	return drawn;
}

GLint VitaGLDebugRenderer::lod_position_attribute() const
{
	return m_lod_alpha_program.attribute_location("a_position");
}

GLint VitaGLDebugRenderer::lod_uv_attribute() const
{
	return m_lod_alpha_program.attribute_location("a_texcoord");
}

GLint VitaGLDebugRenderer::position_attribute_location() const
{
	return m_position_location;
}

GLint VitaGLDebugRenderer::texcoord_attribute_location() const
{
	return m_texcoord_location;
}

GLint VitaGLDebugRenderer::lightmap_texcoord_attribute_location() const
{
	return m_lightmap_texcoord_location;
}

void VitaGLDebugRenderer::release()
{
	if (m_vertex_buffer)
		glDeleteBuffers(1, &m_vertex_buffer);
	m_vertex_buffer = 0;
	m_foliage_program.release();
	m_npc_program.release();
	m_lod_alpha_program.release();
	m_program.release();
	m_position_location = -1;
	m_texcoord_location = -1;
	m_lightmap_texcoord_location = -1;
	m_texcoord_scales_location = -1;
	m_color_location = -1;
	for (size_t axis = 0; axis < 3; ++axis)
		m_view_row_locations[axis] = -1;
	m_texture_location = -1;
	m_textured_location = -1;
	m_lightmap_location = -1;
	m_lightmapped_location = -1;
	m_terrain_location = -1;
	m_terrain_sample_location = -1;
	m_detail_location = -1;
	m_detail_enabled_location = -1;
	m_last_detail = 0;
	m_last_texture = 0;
	m_last_lightmap = 0;
	m_material_state_valid = false;
	m_initialized = false;
}
