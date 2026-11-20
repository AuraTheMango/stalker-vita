#pragma once

#include "vita_gl_device.h"
#include "vita_gl_texture.h"
#include "vita_ui_canvas.h"
#include <array>

enum class VitaSaveMessage { None, Saved, SaveFailed, LoadFailed, Unsafe, ConfirmNew, Count };

class VitaMainMenu
{
public:
	~VitaMainMenu();
	bool initialize();
	void draw(VitaGLDevice& graphics, unsigned choice, bool can_continue, bool load_error,
		bool paused = false, bool dead = false, VitaSaveMessage message = VitaSaveMessage::None) const;
	void release();

	VitaGLTexture background;
	VitaGLTexture atlas;

private:
	VitaUiCanvas m_canvas;
	std::array<GLuint, 5> m_options{};
	std::array<GLuint, 6> m_pause_options{};
	std::array<GLuint, static_cast<unsigned>(VitaSaveMessage::Count) - 1> m_messages{};
	GLuint m_pause_hint = 0, m_dead = 0;
	GLuint m_hint = 0, m_error = 0;
};
