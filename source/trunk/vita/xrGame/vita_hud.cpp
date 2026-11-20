#include <cctype>
#include "vita_hud.h"
#include "vita_hud_layout.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

bool read_vita_hud_map(const std::string& config, std::string& texture, float (&bounds)[4], const char* level)
{
	std::istringstream input(config);
	bool in_cordon = false, has_bounds = false;
	for (std::string line; std::getline(input, line); )
	{
		const size_t comment = line.find(';');
		if (comment != std::string::npos)
			line.resize(comment);
		const size_t first = line.find_first_not_of(" \t\r");
		if (first == std::string::npos)
			continue;
		if (line[first] == '[')
		{
			if (in_cordon)
				break;
			const std::string section="["+std::string(level)+"]";
            const auto candidate=line.substr(first,section.size());
            in_cordon=candidate.size()==section.size()&&std::equal(candidate.begin(),candidate.end(),section.begin(),
                [](char a,char b){return std::tolower(static_cast<unsigned char>(a))==std::tolower(static_cast<unsigned char>(b));});
		}
		else if (in_cordon)
		{
			const size_t equal = line.find('=', first);
			if (equal == std::string::npos)
				continue;
			const auto key = line.substr(first, equal - first);
			const auto name_end = key.find_last_not_of(" \t");
			if (name_end == std::string::npos)
				continue;
			const auto value = line.substr(equal + 1);
			if (key.compare(0, name_end + 1, "texture") == 0)
			{
				const auto start = value.find_first_not_of(" \t\r");
				if (start != std::string::npos)
					texture = value.substr(start, value.find_last_not_of(" \t\r") + 1 - start);
			}
			else if (key.compare(0, name_end + 1, "bound_rect") == 0)
				has_bounds = std::sscanf(value.c_str(), " %f , %f , %f , %f",
					&bounds[0], &bounds[1], &bounds[2], &bounds[3]) == 4;
		}
	}
	return has_bounds && !texture.empty() && std::all_of(bounds, bounds + 4, [](float value) {
		return std::isfinite(value);
	}) && bounds[2] > bounds[0] && bounds[3] > bounds[1];
}

bool VitaHud::initialize()
{
	static const char* vertex =
		"attribute vec2 a_position; uniform vec4 u_rect; uniform vec2 u_rotation;"
		"uniform vec3 u_uv_x, u_uv_y; varying vec2 v_uv;"
		"void main() {"
		"vec2 p = (a_position - vec2(0.5)) * u_rect.zw;"
		"vec2 r = vec2(p.x*u_rotation.x-p.y*u_rotation.y,p.x*u_rotation.y+p.y*u_rotation.x);"
		"vec2 xy = (u_rect.xy + u_rect.zw * 0.5 + r) / vec2(960.0, 544.0);"
		"vec3 uv = vec3(a_position, 1.0); v_uv = vec2(dot(u_uv_x, uv),dot(u_uv_y, uv));"
		"gl_Position = vec4(2.0*xy.x-1.0, 1.0-2.0*xy.y, 0.0, 1.0); }";
	static const char* fragment =
		"precision mediump float; varying vec2 v_uv; uniform sampler2D u_texture; uniform vec4 u_color;"
		"void main(){ gl_FragColor = texture2D(u_texture, v_uv) * u_color; }";
	if (!m_program.initialize(vertex, fragment))
		return false;
	m_position = m_program.attribute_location("a_position");
	m_texture = m_program.uniform_location("u_texture");
	m_rect = m_program.uniform_location("u_rect");
	m_uv_x = m_program.uniform_location("u_uv_x");
	m_uv_y = m_program.uniform_location("u_uv_y");
	m_rotation = m_program.uniform_location("u_rotation");
	m_color = m_program.uniform_location("u_color");
	if (m_position < 0 || m_texture < 0 || m_rect < 0 || m_uv_x < 0 || m_uv_y < 0 || m_rotation < 0 || m_color < 0)
		return false;

	static const GLfloat quad[] = {0.f, 0.f, 0.f, 1.f, 1.f, 0.f, 1.f, 1.f};
	glGenBuffers(1, &m_quad);
	if (!m_quad)
		return false;
	glBindBuffer(GL_ARRAY_BUFFER, m_quad);
	glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
	return glGetError() == GL_NO_ERROR;
}

void VitaHud::sprite(GLuint texture, const Rect& rect, const float (&uv_x)[3], const float (&uv_y)[3],
	float cosine, float sine, const float (&color)[4]) const
{
	if (!texture)
		return;
	glBindTexture(GL_TEXTURE_2D, texture);
	const auto placed=vita_hud_rect(rect.x,rect.y,rect.width,rect.height,m_group_scale,m_anchor_x,m_anchor_y);
 glUniform4f(m_rect,placed[0],placed[1],placed[2],placed[3]);
	glUniform3fv(m_uv_x, 1, uv_x);
	glUniform3fv(m_uv_y, 1, uv_y);
	glUniform2f(m_rotation, cosine, sine);
	glUniform4fv(m_color, 1, color);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void VitaHud::atlas_sprite(const Rect& screen, const Rect& source) const
{
	constexpr float white[] = {1.f, 1.f, 1.f, 1.f};
	sprite(atlas.handle(), screen, {source.width / atlas_width, 0.f, source.x / atlas_width},
		{0.f, source.height / atlas_height, source.y / atlas_height}, 1.f, 0.f, white);
}

void VitaHud::draw_status(const VitaPlayerMovement& movement, unsigned health) const
{
	constexpr float white[] = {1.f, 1.f, 1.f, 1.f};
	m_anchor_x=8;m_anchor_y=536;
 atlas_sprite({8.f, 452.f, 88.f, 84.f}, {349.f, 65.f, 125.f, 125.f});
	// The posture atlas starts at source Y=352.
	float x = 0.f, y = 76.f;
	if (movement.stance() == VitaPlayerStance::Crouching) { x = 65.f; y = 2.f; }
	else if (movement.stance() == VitaPlayerStance::LowCrouch) y = 1.f;
	else if (movement.sprinting()) y = 151.f;
	sprite(stance.handle(), {27.f, 467.f, 46.f, 50.f}, {65.f / stance_width, 0.f, x / stance_width},
		{0.f, 75.f / stance_height, y / stance_height}, 1.f, 0.f, white);

	const float endurance = movement.endurance();
	if (endurance > 0.f)
		atlas_sprite({30.f, 515.f, 40.f * endurance, 5.f}, {347.f, 190.f, 57.f * endurance, 7.f});
	m_anchor_x=952;m_anchor_y=536;
 // Armor retains its preview level until equipment can affect protection.
	atlas_sprite({840.f, 437.f, 112.f, 24.f}, {185.f, 0.f, 164.f, 38.f});
	atlas_sprite({840.f, 461.f, 112.f, 23.f}, {185.f, 38.f, 164.f, 34.f});
	atlas_sprite({845.f, 441.f, 13.f, 13.f}, {228.f, 204.f, 19.f, 18.f});
	atlas_sprite({845.f, 468.f, 13.f, 13.f}, {209.f, 204.f, 19.f, 18.f});
	atlas_sprite({862.f, 451.f, 77.f, 7.f}, {347.f, 197.f, 110.f, 10.f});
	if (health)
	{
		const float fraction = static_cast<float>(std::min(health, 100u)) / 100.f;
		atlas_sprite({862.f, 465.f, 77.f * fraction, 7.f}, {509.f, 50.f, 110.f * fraction, 10.f});
	}
}

void VitaHud::draw(const float eye[3], float yaw, bool show_map, const float* objective, const VitaPlayerMovement& movement, unsigned health, bool show_crosshair, int relation) const
{
	if (!m_quad)
		return;
	glDisable(GL_DEPTH_TEST);
	m_program.use();
	glActiveTexture(GL_TEXTURE0);
	glUniform1i(m_texture, 0);
	glBindBuffer(GL_ARRAY_BUFFER, m_quad);
	glVertexAttribPointer(m_position, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
	glEnableVertexAttribArray(m_position);

	constexpr float full_x[] = {1.f, 0.f, 0.f}, full_y[] = {0.f, 1.f, 0.f};
	constexpr float white[] = {1.f, 1.f, 1.f, 1.f};
	float reticle[4];vita_reticle_color(relation,reticle);
	m_group_scale=1.f;
 if (show_crosshair)
		sprite(cursor.handle(), {466.f, 258.f, 28.f, 28.f}, full_x, full_y, 1.f, 0.f, reticle);
	if (show_map && map.handle())
	{
		m_group_scale=hud_scale;m_anchor_x=12;m_anchor_y=34;
  constexpr Rect box{20.f, 48.f, 116.f, 114.f};
		constexpr float world_width = 88.f;
		constexpr float world_height = world_width * box.height / box.width;
		const float width = map_bounds[2] - map_bounds[0], height = map_bounds[3] - map_bounds[1];
		const float cy = cosf(yaw), sy = sinf(yaw);
		const float center_u = (eye[0] - map_bounds[0]) / width;
		const float center_v = (map_bounds[3] - eye[2]) / height;
		const float ux = world_width * cy / width, uy = -world_height * sy / width;
		const float vx = world_width * sy / height, vy = world_height * cy / height;
		sprite(map.handle(), box, {ux, uy, center_u - (ux + uy) * 0.5f},
			{vx, vy, center_v - (vx + vy) * 0.5f}, 1.f, 0.f, white);
		atlas_sprite({12.f, 34.f, 132.f, 137.f}, {0.f, 0.f, 185.f, 192.f});
		sprite(compass.handle(), {110.f, 137.f, 22.f, 22.f}, full_x, full_y, cy, -sy, white);

		constexpr float cx = box.x + box.width * 0.5f, cz = box.y + box.height * 0.5f;
		constexpr float center_x[] = {0.03f, 0.f, 0.487f}, center_y[] = {0.f, 0.03f, 0.487f};
		if (show_crosshair)
		sprite(cursor.handle(), {cx - 2.f, cz - 2.f, 4.f, 4.f}, center_x, center_y, 1.f, 0.f, white);
		if (objective)
		{
			// Same heading-up transform as the map, in reverse, for world markers.
			const float dx = objective[0] - eye[0], dz = objective[2] - eye[2];
			constexpr float pixels_per_meter = box.width / world_width;
			const float x = (cy * dx - sy * dz) * pixels_per_meter;
			const float y = -(sy * dx + cy * dz) * pixels_per_meter;
			const float edge = std::max(1.f, std::max(std::fabs(x) / (box.width * 0.5f - 8.f),
				std::fabs(y) / (box.height * 0.5f - 8.f)));
			constexpr float amber[] = {1.f, 0.84f, 0.30f, 1.f};
			if (show_crosshair)
		sprite(cursor.handle(), {cx + x / edge - 10.f, cz + y / edge - 10.f, 20.f, 20.f},
				full_x, full_y, 1.f, 0.f, amber);
		}
	}
	m_group_scale=hud_scale;
 draw_status(movement, health);
 m_group_scale=1.f;
	glEnable(GL_DEPTH_TEST);
}

void VitaHud::draw_pda(const float eye[3], float yaw, const float* objective) const
{
	if (!m_quad || !map.handle())
		return;
	glDisable(GL_DEPTH_TEST);
	m_program.use();
	glActiveTexture(GL_TEXTURE0);
	glUniform1i(m_texture, 0);
	glBindBuffer(GL_ARRAY_BUFFER, m_quad);
	glVertexAttribPointer(m_position, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
	glEnableVertexAttribArray(m_position);
	constexpr float full_x[] = {1.f, 0.f, 0.f}, full_y[] = {0.f, 1.f, 0.f};
	constexpr float white[] = {1.f, 1.f, 1.f, 1.f};
	constexpr float amber[] = {1.f, 0.84f, 0.30f, 1.f};
	constexpr Rect frame{15.f, 67.f, 930.f, 464.f};
	sprite(pda_frame.handle(), frame, {1020.f / 1024, 0.f, 0.f}, {0.f, 702.f / 1024, 0.f}, 1.f, 0.f, white);
	const float width = map_bounds[2] - map_bounds[0], height = map_bounds[3] - map_bounds[1];
	const float scale = std::min(746.f / width, 367.f / height);
	const Rect box{480.f - width * scale * 0.5f, 287.f - height * scale * 0.5f, width * scale, height * scale};
	sprite(map.handle(), box, full_x, full_y, 1.f, 0.f, white);
	const auto plot = [&](const float* position, GLuint texture, float angle, const float (&color)[4]) {
		const float x = box.x + std::max(0.f, std::min(box.width, (position[0] - map_bounds[0]) * scale));
		const float y = box.y + std::max(0.f, std::min(box.height, (map_bounds[3] - position[2]) * scale));
		sprite(texture, {x - 10.f, y - 10.f, 20.f, 20.f}, full_x, full_y, cosf(angle), -sinf(angle), color);
	};
	if (objective)
		plot(objective, cursor.handle(), 0.f, amber);
	plot(eye, compass.handle(), yaw, white);
	glEnable(GL_DEPTH_TEST);
}

void VitaHud::release()
{
	if (m_quad)
		glDeleteBuffers(1, &m_quad);
	m_quad = 0;
	cursor.release();
	atlas.release();
	stance.release();
	map.release();
	pda_frame.release();
	compass.release();
	m_program.release();
}
