#include "vita_gl_sky.h"
#include "vita_gl_debug_renderer.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace
{
	uint32_t dds_word(const uint8_t* data, size_t offset)
	{
		uint32_t word;
		std::memcpy(&word, data + offset, sizeof(word));
		return word;
	}
}

VitaGLSky::~VitaGLSky()
{
	release();
}

bool VitaGLSky::initialize(const void* dds, size_t size, char* error, size_t error_capacity)
{
	release();
	// The original Cordon morning DDS stores six DXT1 faces with no mip chain.
	constexpr size_t header_size = 128;
	const auto* data = static_cast<const uint8_t*>(dds);
	if (!data || size < header_size || std::memcmp(data, "DDS ", 4) != 0 ||
		dds_word(data, 4) != 124 || dds_word(data, 76) != 32 ||
		std::memcmp(data + 84, "DXT1", 4) != 0 ||
		(dds_word(data, 112) & 0xfe00u) != 0xfe00u || dds_word(data, 28) > 1)
		return false;
	const uint32_t dimension = dds_word(data, 16);
	if (dimension < 4 || dimension > 1024 || (dimension & (dimension - 1)) ||
		dds_word(data, 12) != dimension)
		return false;
	const size_t face_bytes = dimension * dimension / 2;
	if (size != header_size + 6 * face_bytes)
		return false;

	static const char* vertex_shader =
		"attribute vec3 a_position;\n"
		"attribute vec3 a_direction;\n"
		"uniform vec3 u_forward, u_right, u_up;\n"
		"varying vec3 v_direction;\n"
		"void main() { float depth = dot(u_forward, a_position); "
		"gl_Position = vec4(dot(u_right, a_position), dot(u_up, a_position), depth, depth); "
		"v_direction = a_direction; }\n";
	static const char* fragment_shader =
		"precision mediump float;\n"
		"uniform samplerCube u_sky;\n"
		"varying vec3 v_direction;\n"
		"void main() { gl_FragColor = vec4(textureCube(u_sky, v_direction).rgb * 0.8, 1.0); }\n";
	if (!m_program.initialize(vertex_shader, fragment_shader, error, error_capacity))
		return false;
	m_position = m_program.attribute_location("a_position");
	m_direction = m_program.attribute_location("a_direction");
	m_forward = m_program.uniform_location("u_forward");
	m_right = m_program.uniform_location("u_right");
	m_up = m_program.uniform_location("u_up");
	m_cube = m_program.uniform_location("u_sky");
	if (m_position < 0 || m_direction < 0 || m_forward < 0 || m_right < 0 || m_up < 0 || m_cube < 0)
	{
		release();
		return false;
	}

	for (int i = 0; i < 16 && glGetError() != GL_NO_ERROR; ++i)
	{
	}
	glGenTextures(1, &m_texture);
	glGenBuffers(1, &m_vertices);
	if (!m_texture || !m_vertices)
	{
		release();
		return false;
	}
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_CUBE_MAP, m_texture);
	// DDS and GL use the same face order (+X, -X, +Y, -Y, +Z, -Z).
	for (unsigned face = 0; face < 6; ++face)
		glCompressedTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0,
			GL_COMPRESSED_RGB_S3TC_DXT1_EXT, dimension, dimension, 0,
			static_cast<GLsizei>(face_bytes), data + header_size + face * face_bytes);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

	// The R1 half-box stretches the side faces above the horizon using separate cube coordinates.
	struct SkyVertex
	{
		GLfloat position[3];
		GLfloat direction[3];
	};
	static const SkyVertex corners[] = {
		{{-1, -1, -1}, {-1, -1.01f, -1}}, {{1, -1, -1}, {1, -1.01f, -1}},
		{{-1, -1,  1}, {-1, -1.01f,  1}}, {{1, -1,  1}, {1, -1.01f,  1}},
		{{-1,  1, -1}, {-1,  1,     -1}}, {{1,  1, -1}, {1,  1,     -1}},
		{{-1,  1,  1}, {-1,  1,      1}}, {{1,  1,  1}, {1,  1,      1}},
		{{-1,  0, -1}, {-1, -1,     -1}}, {{1,  0, -1}, {1, -1,     -1}},
		{{-1,  0,  1}, {-1, -1,      1}}, {{1,  0,  1}, {1, -1,      1}}
	};
	static const uint8_t indices[] = {
		0, 2, 3, 3, 1, 0, 4, 5, 7, 7, 6, 4,
		0, 1, 9, 9, 8, 0, 8, 9, 5, 5, 4, 8,
		1, 3, 11, 11, 9, 1, 9, 11, 7, 7, 5, 9,
		3, 2, 11, 2, 10, 11, 11, 10, 6, 6, 7, 11,
		2, 0, 8, 8, 10, 2, 10, 8, 4, 4, 6, 10
	};
	SkyVertex triangles[sizeof(indices)] = {};
	for (size_t i = 0; i < sizeof(indices); ++i)
		triangles[i] = corners[indices[i]];
	glBindBuffer(GL_ARRAY_BUFFER, m_vertices);
	glBufferData(GL_ARRAY_BUFFER, sizeof(triangles), triangles, GL_STATIC_DRAW);
	m_vertex_count = static_cast<GLsizei>(sizeof(indices));
	if (glGetError() != GL_NO_ERROR)
	{
		release();
		return false;
	}
	return true;
}

bool VitaGLSky::draw(float yaw, float pitch, float projection_scale) const
{
	if (!m_texture)
		return false;
	const float cy = cosf(yaw), sy = sinf(yaw);
	const float cp = cosf(pitch), sp = sinf(pitch);
	// Match the world projection so the sky stays aligned while aiming.
	const float fov_scale = vita_player_fov_scale * projection_scale;
	const float horizontal = 4.53f * fov_scale;
	const float vertical = 8.f * fov_scale;
	m_program.use();
	glUniform3f(m_forward, sp * sy, -cp, sp * cy);
	glUniform3f(m_right, cy * horizontal, 0.f, -sy * horizontal);
	glUniform3f(m_up, cp * sy * vertical, sp * vertical, cp * cy * vertical);
	glUniform1i(m_cube, 0);

	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glDisable(GL_BLEND);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_CUBE_MAP, m_texture);
	glBindBuffer(GL_ARRAY_BUFFER, m_vertices);
	glVertexAttribPointer(static_cast<GLuint>(m_position), 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), nullptr);
	glEnableVertexAttribArray(static_cast<GLuint>(m_position));
	glVertexAttribPointer(static_cast<GLuint>(m_direction), 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat),
		reinterpret_cast<const void*>(3 * sizeof(GLfloat)));
	glEnableVertexAttribArray(static_cast<GLuint>(m_direction));
	glDrawArrays(GL_TRIANGLES, 0, m_vertex_count);
	glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
	glEnable(GL_BLEND);
	glDepthMask(GL_TRUE);
	glEnable(GL_DEPTH_TEST);
	return true;
}

void VitaGLSky::release()
{
	if (m_vertices)
		glDeleteBuffers(1, &m_vertices);
	if (m_texture)
		glDeleteTextures(1, &m_texture);
	m_vertices = 0;
	m_texture = 0;
	m_program.release();
}
