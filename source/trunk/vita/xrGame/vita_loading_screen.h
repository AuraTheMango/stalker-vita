#pragma once

#include "vita_gl_device.h"
#include "vita_ui_canvas.h"
#include "vita_gl_texture.h"

#include <array>

class VitaLoadingScreen
{
public:
	enum class Stage { Archives, Geometry, Collision, Actors, Materials, Ready };
	~VitaLoadingScreen();
	bool initialize();
	void draw(VitaGLDevice& graphics, Stage stage, float progress) const;
	void release();

	VitaGLTexture atlas;
	VitaGLTexture cordon;

private:
	VitaUiCanvas m_canvas;
	std::array<GLuint, 6> m_labels{};
	GLuint m_game_title = 0;
	bool m_ready = false;
};
