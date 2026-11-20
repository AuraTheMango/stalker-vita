#pragma once

#include "vita_gl_program.h"
#include "vita_gl_texture.h"
#include "vita_ui_canvas.h"

#include <array>
#include <string>

enum class VitaDoorPrompt { None, Locked, Open };
enum class VitaHudObjective { None, TalkToTrader, LeaveBunker, TalkToWolf, MeetScouts, HelpAllegro, TalkToAllegro, ReturnFlash, FlashDelivered, LastBandit, Count };

struct VitaFlashLines
{
	std::array<std::string, 3> allegro;
	std::array<std::string, 2> trader;
};

using VitaScoutLines = std::array<std::string, 6>;
constexpr unsigned vita_scout_choice_page = 3;
constexpr unsigned vita_scout_decline_page = 4;

using VitaWolfLines = std::array<std::string, 11>;
constexpr unsigned vita_wolf_choice_page = 7;
constexpr unsigned vita_wolf_accept_page = 8;
constexpr unsigned vita_wolf_decline_page = 9;

class VitaTraderUI
{
public:
	bool initialize(const std::string& intro, const std::string (&replies)[2], int& pgf_status, const VitaWolfLines* wolf_lines, const VitaScoutLines* scout_lines, const VitaFlashLines* flash_lines);
	void draw(bool open, unsigned choice, bool confirmed, VitaDoorPrompt door = VitaDoorPrompt::None) const;
	bool wolf_ready() const { return m_wolf_prompt && m_wolf_pages.back(); }
	void draw_wolf(bool open, unsigned page, unsigned choice) const;
	bool scout_ready() const { return m_scout_prompt && m_scout_pages.back(); }
	void draw_scout(bool open, unsigned page, unsigned choice) const;
	bool flash_ready() const;
	void draw_flash(bool allegro, bool open, unsigned page) const;
	void draw_objective(VitaHudObjective objective) const;
    void draw_target(unsigned name)const;
    bool set_campaign_page(const std::string&name,const std::string&text);
    bool set_campaign_objective(const char*text);
    void draw_campaign(bool open)const;
    void draw_campaign_objective()const;
    void draw_campaign_target()const;
    void draw_zone_exit(unsigned exit,bool allowed)const;
    VitaGLTexture campaign_face,road_face;
    bool campaign_road=false;

    bool show_prompts=false;
    VitaGLTexture talk_art;
    std::array<VitaGLTexture,4> faces;
    GLuint profiles=0,actor_portrait=0;
	void release();
	bool ready() const { return m_prompt && m_dialog; }

private:
	void draw_panel(GLuint texture, int height, float top, float bottom, bool confirmed, bool alpha_only, float prompt_y = 456.f) const;
	VitaUiCanvas m_canvas;
    mutable bool m_campaign_active=false;
    std::array<GLuint,4> m_zone_prompts{};
    GLuint m_zone_locked=0;
    GLuint m_campaign_dialog=0,m_campaign_name=0,m_campaign_objective=0,m_campaign_prompt=0;
    GLuint m_trade_dialog=0,m_continue=0,m_leave=0,m_actor_name=0;
    std::array<GLuint,6> m_target_names{};
    mutable unsigned m_face=0;
    mutable GLuint m_history=0;
    VitaGLProgram m_program;
	GLuint m_quad = 0, m_prompt = 0, m_dialog = 0;
	GLuint m_door_locked = 0, m_door_open = 0;
	GLuint m_wolf_prompt = 0, m_scout_prompt = 0;
	GLuint m_allegro_prompt = 0, m_flash_prompt = 0;
	std::array<GLuint, 3> m_allegro_pages{};
	std::array<GLuint, 2> m_flash_pages{};
	std::array<GLuint, vita_scout_decline_page + 1> m_scout_pages{};
	std::array<GLuint, static_cast<unsigned>(VitaHudObjective::Count) - 1> m_objectives{};
	std::array<GLuint, vita_wolf_decline_page + 1> m_wolf_pages{};
	GLint m_alpha_only = -1;
	GLint m_position = -1, m_rect = -1, m_texture = -1, m_highlight = -1;
	float m_rows[3] = {};
};
