#pragma once

#include "vita_gl_program.h"
#include "vita_gl_texture.h"
#include "vita_player_movement.h"

#include <string>

bool read_vita_hud_map(const std::string& config, std::string& texture, float (&bounds)[4], const char* level="l01_escape");

inline void vita_reticle_color(int relation,float(&color)[4]){
 color[0]=relation<0?1.f:relation>0?.55f:1.f;color[1]=relation<0?.12f:1.f;color[2]=relation?.30f:1.f;color[3]=1.f;
}

class VitaHud
{
public:
	bool initialize();
	void draw(const float eye[3], float yaw, bool show_map, const float* objective, const VitaPlayerMovement& movement, unsigned health, bool show_crosshair, int relation) const;
	void draw_pda(const float eye[3], float yaw, const float* objective) const;
	void release();

	static constexpr unsigned atlas_width = 620, atlas_height = 224;
	static constexpr unsigned stance_width = 132, stance_height = 228;

	VitaGLTexture cursor, atlas, map, compass, stance, pda_frame;
 float hud_scale=1.f;
	float map_bounds[4] = {}; // min X, min Z, max X, max Z

private:
	mutable float m_group_scale=1.f,m_anchor_x=0,m_anchor_y=0;
 struct Rect { float x, y, width, height; };
	void sprite(GLuint texture, const Rect& rect, const float (&uv_x)[3], const float (&uv_y)[3],
		float cosine, float sine, const float (&color)[4]) const;

	void atlas_sprite(const Rect& screen, const Rect& source) const;
	void draw_status(const VitaPlayerMovement& movement, unsigned health) const;

	VitaGLProgram m_program;
	GLuint m_quad = 0;
	GLint m_position = -1, m_texture = -1, m_rect = -1;
	GLint m_uv_x = -1, m_uv_y = -1, m_rotation = -1, m_color = -1;
};
