#include "vita_trader_ui.h"
#include "vita_loading_screen.h"
#include "vita_inventory_ui.h"
#include "vita_main_menu.h"
#include "vita_save_ui.h"
#include <ctime>
#include <cstdio>
#include "vita_locale.h"
#include "vita_item_catalog.h"
#include "vita_hud_layout.h"

#include <psp2/pgf.h>
#include <psp2/sysmodule.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <sstream>
#include <vector>
#ifdef quad
#undef quad
#endif

namespace
{
constexpr int panel_width = 800, panel_height = 300, exchange_height = 360;
constexpr float choice_rows[] = {80.f / exchange_height, 127.f / exchange_height, 174.f / exchange_height};
constexpr int prompt_width = 392, prompt_height = 44;

unsigned codepoint(unsigned char byte)
{
	// The game's French string table uses Windows-1252 bytes.
	static const unsigned punctuation[] = {
		0x20ac, '?', 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
		0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, '?', 0x017d, '?',
		'?', 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
		0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, '?', 0x017e, 0x0178
	};
	return byte >= 0x80 && byte < 0xa0 ? punctuation[byte - 0x80] : byte;
}

class TextImage
{
public:
	TextImage(int width, int height) : m_width(width), m_height(height), m_alpha(width * height, 0) {}

	int line(SceFontHandle font, const std::string& text, int left, int baseline, int right)
	{
		int pen = left;
		std::istringstream words(text);
		std::string word;
		SceFontCharInfo space{};
		sceFontGetCharInfo(font, ' ', &space);
		const int gap = std::max(4, space.sfp26AdvanceH / 64);
		while (words >> word)
		{
			int width = 0;
			for (unsigned char letter : word)
			{
				SceFontCharInfo info{};
				if (sceFontGetCharInfo(font, codepoint(letter), &info) == 0)
					width += info.sfp26AdvanceH / 64;
			}
			if (pen != left && pen + gap + width > right)
			{
				pen = left;
				baseline += 25;
			}
			if (pen != left)
				pen += gap;
			for (unsigned char letter : word)
			{
				SceFontCharInfo info{};
				const auto character = codepoint(letter);
				if (sceFontGetCharInfo(font, character, &info) != 0)
					continue;
				const int x = pen + static_cast<int>(info.bitmapLeft);
				const int y = baseline - static_cast<int>(info.bitmapTop);
				if (info.bitmapWidth && info.bitmapHeight && x >= 0 && y >= 0 &&
					x + static_cast<int>(info.bitmapWidth) <= m_width &&
					y + static_cast<int>(info.bitmapHeight) <= m_height)
				{
					SceFontGlyphImage image{};
					image.pixelFormat = SCE_FONT_PIXELFORMAT_8;
					image.xPos64 = x * 64;
					image.yPos64 = y * 64;
					image.bufWidth = m_width;
					image.bufHeight = m_height;
					image.bytesPerLine = m_width;
					image.bufferPtr = reinterpret_cast<uintptr_t>(m_alpha.data());
					sceFontGetCharGlyphImage(font, character, &image);
				}
				pen += info.sfp26AdvanceH / 64;
			}
		}
		return baseline;
	}

	GLuint upload(bool alpha_only = false) const
	{
		if (std::none_of(m_alpha.begin(), m_alpha.end(), [](uint8_t alpha) { return alpha != 0; }))
			return 0;
		std::vector<uint8_t> rgba;
		if (!alpha_only)
		{
			rgba.assign(m_alpha.size() * 4, 255);
			for (size_t i = 0; i < m_alpha.size(); ++i)
				rgba[i * 4 + 3] = m_alpha[i];
		}
		GLuint texture = 0;
		glGenTextures(1, &texture);
		if (!texture)
			return 0;
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, texture);
		const GLenum format = alpha_only ? GL_RED : GL_RGBA;
		glTexImage2D(GL_TEXTURE_2D, 0, format, m_width, m_height, 0, format, GL_UNSIGNED_BYTE,
			alpha_only ? m_alpha.data() : rgba.data());
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		if (glGetError() != GL_NO_ERROR)
		{
			glDeleteTextures(1, &texture);
			return 0;
		}
		return texture;
	}

private:
	int m_width, m_height;
	std::vector<uint8_t> m_alpha;
};

SceFontHandle open_ui_font(SceFontLibHandle& library)
{
	SceFontNewLibParams params{};
	params.numFonts = 1;
	params.allocFunc = [](void*, unsigned size) { return std::malloc(size); };
	params.freeFunc = [](void*, void* data) { std::free(data); };
	unsigned error = 0;
	library = sceFontNewLib(&params, &error);
	if (error)
		return 0;
	SceFontStyle style{};
	style.fontH = 10;
	style.fontV = 10;
	style.fontLanguage = SCE_FONT_LANGUAGE_LATIN;
	const int font_index = sceFontFindOptimumFont(library, &style, &error);
	if (!error && font_index >= 0)
	{
		const auto font = sceFontOpen(library, font_index, 0, &error);
		if (!error)
			return font;
	}
	sceFontDoneLib(library);
	return 0;
}

int measure_text_width(SceFontHandle font, const char* text)
{
	int text_width = 0;
	for (const auto* letter = reinterpret_cast<const unsigned char*>(text); *letter; ++letter)
	{
		SceFontCharInfo glyph{};
		if (sceFontGetCharInfo(font, codepoint(*letter), &glyph) == 0)
			text_width += std::max(*letter == ' ' ? 4 : 0, glyph.sfp26AdvanceH / 64);
	}
	return text_width;
}

GLuint upload_centered_label(SceFontHandle font, const char* text, int width = 400)
{
	const int text_width = measure_text_width(font, text);
	TextImage image(width, 32);
	image.line(font, text, std::max(12, (width - text_width) / 2), 24, width - 12);
	return image.upload(true);
}

GLuint upload_exchange_page(SceFontHandle font, const char* speaker, const std::string& line,
	const std::string* alternative, bool last)
{
	TextImage image(panel_width, exchange_height);
	image.line(font, speaker, 24, 33, 774);
	if (alternative)
	{
		if (image.line(font, line, 28, 104, 774) != 104 || image.line(font, *alternative, 28, 151, 774) != 151)
			return 0;
	}
	else if (image.line(font, line, 24, 62, 776) > 305)
		return 0;
	image.line(font, alternative ? vita_text("Haut / Bas : choisir     Carr\xe9 : valider","Up / Down: select     Square: confirm") :
		last ? vita_text("Carr\xe9 : terminer","Square: finish") : vita_text("Carr\xe9 : continuer","Square: continue"), 24, 333, 584);
	image.line(font, vita_text("Rond : fermer","Circle: close"), 610, 333, 780);
	return image.upload(true);
}
}

bool VitaTraderUI::initialize(const std::string& intro, const std::string (&replies)[2], int& pgf_status, const VitaWolfLines* wolf_lines, const VitaScoutLines* scout_lines, const VitaFlashLines* flash_lines)
{
	if(!m_canvas.initialize())return false;
	// PGF imports are resolved only after the system module is available.
	pgf_status = sceSysmoduleIsLoaded(SCE_SYSMODULE_PGF);
	if (pgf_status < 0)
		pgf_status = sceSysmoduleLoadModule(SCE_SYSMODULE_PGF);
	if (pgf_status < 0)
		return false;

	static const char* vertex =
		"attribute vec2 a_position; uniform vec4 u_rect; varying vec2 v_uv;"
		"void main() { v_uv = a_position; vec2 p = (u_rect.xy + a_position * u_rect.zw) / vec2(960.0, 544.0);"
		"gl_Position = vec4(2.0*p.x-1.0, 1.0-2.0*p.y, 0.0, 1.0); }";
	static const char* fragment =
		"precision mediump float; varying vec2 v_uv; uniform sampler2D u_texture; uniform vec3 u_highlight;"
		"uniform float u_alpha_only;"
		"void main() { vec4 sample_color = texture2D(u_texture, v_uv);"
		"float text = mix(sample_color.a, sample_color.r, u_alpha_only);"
		"if (u_highlight.z > 0.5 && v_uv.y > 0.82 && v_uv.x < 0.75) text = 0.0;"
		"vec3 bg = vec3(0.065, 0.073, 0.061);"
		"if (v_uv.y >= u_highlight.x && v_uv.y <= u_highlight.y && v_uv.x > 0.014 && v_uv.x < 0.986)"
		"bg = mix(vec3(0.32, 0.24, 0.10), vec3(0.19, 0.26, 0.15), u_highlight.z);"
		"if (v_uv.y < 0.009 || v_uv.x < 0.003 || v_uv.x > 0.997 || v_uv.y > 0.991)"
		"bg = vec3(0.37, 0.40, 0.27);"
		"gl_FragColor = vec4(mix(bg, vec3(0.95, 0.93, 0.82), text), mix(0.91, 1.0, text)); }";
	if (!m_program.initialize(vertex, fragment))
		return false;
	m_rect = m_program.uniform_location("u_rect");
	m_highlight = m_program.uniform_location("u_highlight");
	m_texture = m_program.uniform_location("u_texture");
	m_position = m_program.attribute_location("a_position");
	m_alpha_only = m_program.uniform_location("u_alpha_only");
	if (m_rect < 0 || m_highlight < 0 || m_texture < 0 || m_position < 0 || m_alpha_only < 0)
		return false;

	SceFontLibHandle library{};
	const SceFontHandle font = open_ui_font(library);
	if (!font)
		return false;

	TextImage panel(panel_width, panel_height);
	panel.line(font, vita_text("Sidorovitch","Sidorovich"), 24, 33, 770);
	if (panel.line(font, intro, 24, 69, 770) > 119)
	{
		sceFontClose(font);
		sceFontDoneLib(library);
		return false;
	}
	const int row0_end = panel.line(font, replies[0], 28, 153, 760) + 12;
	const int row1_end = panel.line(font, replies[1], 28, row0_end + 29, 760) + 12;
	m_rows[0] = 130.f / panel_height;
	m_rows[1] = static_cast<float>(row0_end) / panel_height;
	m_rows[2] = static_cast<float>(row1_end) / panel_height;
	if (row1_end >= 250)
	{
		sceFontClose(font);
		sceFontDoneLib(library);
		return false;
	}
	panel.line(font, vita_text("Haut / Bas : choisir     Carr\xe9 : valider","Up / Down: select     Square: confirm"), 24, 273, 584);
	panel.line(font, vita_text("Rond : fermer","Circle: close"), 610, 273, 780);
	TextImage prompt(prompt_width, prompt_height);
	prompt.line(font, vita_text("Carr\xe9 : parler \xe0 Sidorovitch","Square: talk to Sidorovich"), 19, 28, 374);
	TextImage locked(prompt_width, prompt_height);
	locked.line(font, vita_text("Porte verrouill\xe9" "e","Door locked"), 19, 28, 374);
	TextImage door_open(prompt_width, prompt_height);
	door_open.line(font, vita_text("Carr\xe9 : ouvrir la porte","Square: open the door"), 19, 28, 374);
	for (unsigned errors = 0; errors < 16 && glGetError() != GL_NO_ERROR; ++errors)
	{
	}
    TextImage merchant(panel_width,panel_height);
    merchant.line(font,vita_text("Sidorovitch","Sidorovich"),24,33,770);
    merchant.line(font,vita_text("Que veux-tu ?","What do you want?"),24,69,770);
    merchant.line(font,vita_text("Commercer.","Let's trade."),28,153,760);
    merchant.line(font,vita_text("Au revoir.","Goodbye."),28,row0_end+29,760);
    m_trade_dialog=merchant.upload(true);
    m_actor_name=upload_centered_label(font,vita_text("Le Tatou\xe9","Marked One"),240);
    m_continue=upload_centered_label(font,vita_text(vita_text("Continuer","Continue"),"Continue"),700);
    m_leave=upload_centered_label(font,vita_text(vita_text("Quitter","Quit"),"Leave"),700);
    const char* names[]={vita_text("Sidorovitch","Sidorovich"),vita_text(vita_text("Le Loup","Wolf"),"Wolf"),"Petruha",vita_text("Allegro","Nimble"),vita_text("Bandit","Bandit"),"Stalker"};
    for(unsigned i=0;i<6;++i)m_target_names[i]=upload_centered_label(font,names[i],500);
    m_prompt = prompt.upload();
	m_dialog = panel.upload();
	static const GLfloat quad[] = {0.f, 0.f, 0.f, 1.f, 1.f, 0.f, 1.f, 1.f};
	glGenBuffers(1, &m_quad);
	if (m_quad)
	{
		glBindBuffer(GL_ARRAY_BUFFER, m_quad);
		glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
	}
	const bool success = ready() && m_quad && glGetError() == GL_NO_ERROR;
	if (success)
	{
		m_door_locked = locked.upload();
		m_door_open = door_open.upload();
		const char* const titles[] = {vita_text("Parler \xe0 Sidorovitch","Talk to Sidorovich"), vita_text("Sortir du bunker","Leave the bunker"), vita_text("Parler au Loup","Talk to Wolf"), vita_text("Rencontrer les hommes du Loup","Meet Wolf's men"), vita_text("Aider Allegro","Help Nimble"), vita_text("Parler \xe0 Allegro","Talk to Nimble"), vita_text("Rapporter la cl\xe9 USB","Return the USB drive"), vita_text("Mission termin\xe9" "e","Mission completed"), vita_text("\xc9" "liminer le dernier bandit","Eliminate the last bandit")};
		static_assert(sizeof(titles) / sizeof(titles[0]) == static_cast<unsigned>(VitaHudObjective::Count) - 1);
		for (unsigned i = 0; i < m_objectives.size(); ++i)
		{
			TextImage title(prompt_width, prompt_height);
			title.line(font, titles[i], 19, 28, 374);
			m_objectives[i] = title.upload(true);
		}
		if (wolf_lines)
		{
			TextImage wolf_prompt(prompt_width, prompt_height);
			wolf_prompt.line(font, vita_text("Carr\xe9 : parler au Loup","Square: talk to Wolf"), 19, 28, 374);
			m_wolf_prompt = wolf_prompt.upload(true);
			for (unsigned page = 0; page < m_wolf_pages.size() && m_wolf_prompt; ++page)
			{
				const bool choose = page == vita_wolf_choice_page;
				const bool last = page >= vita_wolf_accept_page;
				m_wolf_pages[page] = upload_exchange_page(font, !last && page % 2 ? vita_text("Vous","You") : vita_text("Le Loup","Wolf"),
					(*wolf_lines)[last ? page + 1 : page], choose ? &(*wolf_lines)[8] : nullptr, last);
				if (!m_wolf_pages[page])
					break;
			}
		}
		if (scout_lines)
		{
			TextImage scout_prompt(prompt_width, prompt_height);
			scout_prompt.line(font, vita_text("Carr\xe9 : parler \xe0 Petruha","Square: talk to Petruha"), 19, 28, 374);
			m_scout_prompt = scout_prompt.upload(true);
			for (unsigned page = 0; page < m_scout_pages.size() && m_scout_prompt; ++page)
			{
				const bool choose = page == vita_scout_choice_page;
				const bool last = page == vita_scout_decline_page;
				m_scout_pages[page] = upload_exchange_page(font, page % 2 ? vita_text("Vous","You") : "Petruha",
					(*scout_lines)[last ? 5 : page], choose ? &(*scout_lines)[4] : nullptr, last);
				if (!m_scout_pages[page])
					break;
			}
		}
		if (flash_lines)
		{
			TextImage allegro_prompt(prompt_width, prompt_height);
			allegro_prompt.line(font, vita_text("Carr\xe9 : parler \xe0 Allegro","Square: talk to Allegro"), 19, 28, 374);
			m_allegro_prompt = allegro_prompt.upload(true);
			TextImage flash_prompt(prompt_width, prompt_height);
			flash_prompt.line(font, vita_text("Carr\xe9 : remettre la cl\xe9 USB","Square: return USB drive"), 19, 28, 374);
			m_flash_prompt = flash_prompt.upload(true);
			for (unsigned page = 0; page < m_allegro_pages.size(); ++page)
				m_allegro_pages[page] = upload_exchange_page(font, page == 1 ? vita_text("Vous","You") : "Allegro",
					flash_lines->allegro[page], nullptr, page + 1 == m_allegro_pages.size());
			m_flash_pages[0] = upload_exchange_page(font, vita_text("Vous","You"), flash_lines->trader[0], nullptr, false);
			m_flash_pages[1] = upload_exchange_page(font, vita_text("Sidorovitch","Sidorovich"), flash_lines->trader[1] +
				"  R\xe9" "compense : " + std::to_string(VitaInventory::flash_reward) + " RU", nullptr, true);
		}
	}
	sceFontClose(font);
	sceFontDoneLib(library);
	return success;
}

void VitaTraderUI::draw(bool open, unsigned choice, bool confirmed, VitaDoorPrompt door) const
{
	m_face=0;m_history=0;
	const GLuint texture = open ? (confirmed?m_trade_dialog:m_dialog) :
		door == VitaDoorPrompt::Locked ? m_door_locked : door == VitaDoorPrompt::Open ? m_door_open : m_prompt;
	draw_panel(texture, open ? panel_height : 0, open ? m_rows[choice] : -1.f, open ? m_rows[choice + 1] : -1.f,
		false, confirmed);
}

void VitaTraderUI::draw_wolf(bool open, unsigned page, unsigned choice) const
{
	m_face=1;
	const bool choose = open && page == vita_wolf_choice_page;
    m_history=choose?m_wolf_pages[page-1]:0;
	draw_panel(open ? m_wolf_pages[page] : m_wolf_prompt, open ? exchange_height : 0,
		choose ? choice_rows[choice] : -1.f, choose ? choice_rows[choice + 1] : -1.f, false, true);
}

void VitaTraderUI::draw_scout(bool open, unsigned page, unsigned choice) const
{
	m_face=2;
	const bool choose = open && page == vita_scout_choice_page;
    m_history=choose?m_scout_pages[page-1]:0;
	draw_panel(open ? m_scout_pages[page] : m_scout_prompt, open ? exchange_height : 0,
		choose ? choice_rows[choice] : -1.f, choose ? choice_rows[choice + 1] : -1.f, false, true);
}

bool VitaTraderUI::flash_ready() const
{
	const auto loaded = [](GLuint texture) { return texture != 0; };
	return m_allegro_prompt && m_flash_prompt && std::all_of(m_allegro_pages.begin(), m_allegro_pages.end(), loaded) &&
		std::all_of(m_flash_pages.begin(), m_flash_pages.end(), loaded);
}

void VitaTraderUI::draw_flash(bool allegro, bool open, unsigned page) const
{
	m_face=allegro?3:0;m_history=0;
	const GLuint texture = allegro ? (open ? m_allegro_pages[page] : m_allegro_prompt) :
		(open ? m_flash_pages[page] : m_flash_prompt);
	draw_panel(texture, open ? exchange_height : 0, -1.f, -1.f, false, true);
}

void VitaTraderUI::draw_objective(VitaHudObjective objective) const
{
	if (objective != VitaHudObjective::None)
		draw_panel(m_objectives[static_cast<unsigned>(objective) - 1], 0, -1.f, -1.f, false, true, 22.f);
}

void VitaTraderUI::draw_panel(GLuint texture, int height, float top, float bottom, bool confirmed, bool alpha_only, float prompt_y) const
{
	if (!texture)
		return;
    if(!height&&prompt_y>400&&!show_prompts)return;
    if(height&&talk_art.handle())
    {
        constexpr float uv[]={0,0,1,1},white[]={1,1,1,1},gold[]={.94f,.64f,.18f,1},green[]={.5f,.85f,.4f,1};
        m_canvas.begin();
        const auto q=[&](GLuint id,float x,float y,float w,float h,const float(&source)[4],const float(&color)[4],float mask=0){
            m_canvas.quad(id,{x/960,y/544,w/960,h/544},source,color,mask);};
        const auto panel=[&](float y,float h,bool questions){
            q(talk_art.handle(),205,y,562,questions?64:50,
                {0,questions?580.f/1024:369.f/1024,600.f/1024,questions?98.f/1024:75.f/1024},white);
            q(talk_art.handle(),205,y+(questions?64:50),562,h-(questions?84:70),{2.f/1024,444.f/1024,600.f/1024,96.f/1024},white);
            q(talk_art.handle(),205,y+h-20,562,20,{2.f/1024,540.f/1024,600.f/1024,40.f/1024},white);
        };
        panel(28,310,false);panel(340,180,true);
        const VitaProfileLayout actor_card{14,24,180},npc_card{774,24,180};
        const auto profile_quad=[&](GLuint id,const std::array<float,4>&r,const float(&u)[4],const float(&c)[4],float mask=0){
            q(id,r[0],r[1],r[2],r[3],u,c,mask);};
        profile_quad(profiles,actor_card.frame(),{0,0,210.f/1024,150.f/1024},white);
        profile_quad(profiles,npc_card.frame(),{813.f/1024,0,210.f/1024,150.f/1024},white);
        profile_quad(m_campaign_active&&campaign_road&&road_face.handle()?road_face.handle():m_campaign_active&&campaign_face.handle()?campaign_face.handle():faces[m_face].handle(),npc_card.image(true),uv,white);
        profile_quad(actor_portrait,actor_card.image(false),uv,white);
        profile_quad(m_actor_name,actor_card.name(false),uv,gold,1);
        if(m_campaign_active)profile_quad(m_campaign_name,npc_card.name(true),uv,gold,1);
        else profile_quad(m_target_names[m_face],npc_card.name(true),{.25f,0,.5f,1},gold,1);
        const bool choices=top>=0;
        if(choices){
            // Original conversation history above, answers below; selected answer is green.
            q(m_history?m_history:texture,226,57,520,m_history?210.6f:78.f,{0,0,1,m_history?.9f:.40f},white,alpha_only?1:0);
            // Both answer rows remain visible: source range contains the two choices.
            q(texture,226,405,520,height*.34f*.65f,{0,height==panel_height?.43f:.22f,1,.34f},white,alpha_only?1:0);
            q(texture,226,405+(top-(height==panel_height?.43f:.22f))*height*.65f,520,(bottom-top)*height*.65f,
                {0,top,1,bottom-top},green,alpha_only?1:0);
        }else{
            q(texture,226,57,520,height*.9f*.65f,{0,0,1,.9f},white,alpha_only?1:0);
            q(m_continue,228,413,520,28,uv,gold,1);
        }
        q(m_leave,230,486,520,24,uv,white,1);
        m_canvas.end();return;
    }
	glDisable(GL_DEPTH_TEST);
	m_program.use();
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, texture);
	glUniform1i(m_texture, 0);
	glUniform1f(m_alpha_only, alpha_only ? 1.f : 0.f);
	glUniform3f(m_highlight, top, bottom, confirmed ? 1.f : 0.f);
	if (height)
		glUniform4f(m_rect, 80.f, 522.f - height, panel_width, static_cast<float>(height));
	else
		glUniform4f(m_rect, 284.f, prompt_y, prompt_width, prompt_height);
	glBindBuffer(GL_ARRAY_BUFFER, m_quad);
	glVertexAttribPointer(m_position, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
	glEnableVertexAttribArray(m_position);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glEnable(GL_DEPTH_TEST);
}

void VitaTraderUI::draw_target(unsigned name)const
{
 if(name>=m_target_names.size())return;
 constexpr float uv[]={0,0,1,1},green[]={.55f,1,.4f,1};
 m_canvas.begin();m_canvas.quad(m_target_names[name],{.34f,.55f,.32f,.045f},uv,green,1);m_canvas.end();
}

void VitaTraderUI::release()
{
 road_face.release();campaign_face.release();glDeleteTextures(m_zone_prompts.size(),m_zone_prompts.data());m_zone_prompts.fill(0);
 if(m_zone_locked)glDeleteTextures(1,&m_zone_locked);m_zone_locked=0;
 for(GLuint*t:{&m_campaign_dialog,&m_campaign_name,&m_campaign_objective,&m_campaign_prompt}){if(*t)glDeleteTextures(1,t);*t=0;}

    m_canvas.release();talk_art.release();for(auto& face:faces)face.release();
    glDeleteTextures(m_target_names.size(),m_target_names.data());m_target_names.fill(0);
    for(GLuint* t:{&m_trade_dialog,&m_continue,&m_leave,&m_actor_name}){if(*t)glDeleteTextures(1,t);*t=0;}
    if (m_quad) glDeleteBuffers(1, &m_quad);
	if (m_prompt) glDeleteTextures(1, &m_prompt);
	if (m_dialog) glDeleteTextures(1, &m_dialog);
	if (m_door_locked) glDeleteTextures(1, &m_door_locked);
	if (m_door_open) glDeleteTextures(1, &m_door_open);
	if (m_wolf_prompt) glDeleteTextures(1, &m_wolf_prompt);
	glDeleteTextures(static_cast<GLsizei>(m_wolf_pages.size()), m_wolf_pages.data());
	m_wolf_pages.fill(0);
	glDeleteTextures(static_cast<GLsizei>(m_objectives.size()), m_objectives.data());
	m_objectives.fill(0);
	m_wolf_prompt = 0;
	if (m_scout_prompt) glDeleteTextures(1, &m_scout_prompt);
	glDeleteTextures(static_cast<GLsizei>(m_scout_pages.size()), m_scout_pages.data());
	m_scout_pages.fill(0);
	m_scout_prompt = 0;
	if (m_allegro_prompt) glDeleteTextures(1, &m_allegro_prompt);
	if (m_flash_prompt) glDeleteTextures(1, &m_flash_prompt);
	glDeleteTextures(static_cast<GLsizei>(m_allegro_pages.size()), m_allegro_pages.data());
	glDeleteTextures(static_cast<GLsizei>(m_flash_pages.size()), m_flash_pages.data());
	m_allegro_pages.fill(0);
	m_flash_pages.fill(0);
	m_allegro_prompt = m_flash_prompt = 0;
	m_door_locked = m_door_open = m_prompt = m_dialog = m_quad = 0;
	m_program.release();
}


bool VitaUiCanvas::initialize()
{
	static const char* vertex =
		"attribute vec2 a_position; uniform vec4 u_rect, u_uv; varying vec2 v_uv;"
		"void main() { vec2 xy = u_rect.xy + a_position * u_rect.zw;"
		"v_uv = u_uv.xy + a_position * u_uv.zw;"
		"gl_Position = vec4(2.0 * xy.x - 1.0, 1.0 - 2.0 * xy.y, 0.0, 1.0); }";
	static const char* fragment =
		"precision mediump float; varying vec2 v_uv; uniform sampler2D u_texture;"
		"uniform vec4 u_color; uniform float u_mask;"
		"void main() { vec4 texel = texture2D(u_texture, v_uv);"
		"gl_FragColor = mix(texel, vec4(1.0, 1.0, 1.0, texel.r), u_mask) * u_color; }";
	if (!m_program.initialize(vertex, fragment))
		return false;
	m_texture = m_program.uniform_location("u_texture");
	m_position = m_program.attribute_location("a_position");
	m_rect = m_program.uniform_location("u_rect");
	m_uv = m_program.uniform_location("u_uv");
	m_mask = m_program.uniform_location("u_mask");
	m_color = m_program.uniform_location("u_color");
	if (m_position < 0 || m_rect < 0 || m_uv < 0 || m_mask < 0 || m_color < 0 || m_texture < 0)
		return false;

	static const GLfloat quad[] = {0.f, 0.f, 0.f, 1.f, 1.f, 0.f, 1.f, 1.f};
	glGenBuffers(1, &m_quad);
	if (!m_quad)
		return false;
	glBindBuffer(GL_ARRAY_BUFFER, m_quad);
	glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
	if (glGetError() != GL_NO_ERROR)
	{
		release();
		return false;
	}

	return true;
}

void VitaUiCanvas::quad(GLuint texture, const float (&destination)[4], const float (&source)[4],
	const float (&color)[4], float mask) const
{
	if (!texture)
		return;
	glBindTexture(GL_TEXTURE_2D, texture);
	glUniform4fv(m_rect, 1, destination);
	glUniform4fv(m_uv, 1, source);
	glUniform4fv(m_color, 1, color);
	glUniform1f(m_mask, mask);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void VitaUiCanvas::begin() const
{
	glDisable(GL_DEPTH_TEST);
	m_program.use();
	glActiveTexture(GL_TEXTURE0);
	glUniform1i(m_texture, 0);
	glBindBuffer(GL_ARRAY_BUFFER, m_quad);
	glVertexAttribPointer(m_position, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), nullptr);
	glEnableVertexAttribArray(m_position);
}

void VitaUiCanvas::end() const
{
	glBindTexture(GL_TEXTURE_2D, 0);
	glEnable(GL_DEPTH_TEST);
}

void VitaUiCanvas::release()
{
	if (m_quad)
		glDeleteBuffers(1, &m_quad);
	m_quad = 0;
	m_program.release();
}

VitaLoadingScreen::~VitaLoadingScreen()
{
	release();
}

bool VitaLoadingScreen::initialize()
{
	m_ready = m_canvas.initialize();
	if (!m_ready)
		return false;

	int pgf = sceSysmoduleIsLoaded(SCE_SYSMODULE_PGF);
	if (pgf < 0)
		pgf = sceSysmoduleLoadModule(SCE_SYSMODULE_PGF);
	if (pgf >= 0)
	{
		SceFontLibHandle library{};
		const SceFontHandle font = open_ui_font(library);
		if (font)
		{
            const char* const labels[] = {vita_text("Lecture des archives","Reading archives"),vita_text("Chargement du Cordon","Loading Cordon"),
                vita_text("Pr\xe9paration du terrain","Preparing terrain"),vita_text("Chargement des personnages","Loading characters"),
                vita_text("Chargement des textures","Loading textures"),vita_text("Entr\xe9" "e dans le Cordon","Entering Cordon")};
			static_assert(sizeof(labels) / sizeof(labels[0]) == static_cast<unsigned>(Stage::Ready) + 1);
			for (size_t i = 0; i < m_labels.size(); ++i)
				m_labels[i] = upload_centered_label(font, labels[i]);
			m_game_title = upload_centered_label(font, "S.T.A.L.K.E.R.");
			sceFontClose(font);
			sceFontDoneLib(library);
		}
	}
	return true;
}

void VitaLoadingScreen::draw(VitaGLDevice& graphics, Stage stage, float progress) const
{
	graphics.begin_frame(0.055f, 0.06f, 0.045f);
	const float fill = std::max(0.f, std::min(1.f, progress));
	const bool has_atlas = m_ready && atlas.handle();
	if (!has_atlas)
	{
		// Keep a visible progress indicator before the archives are mounted and if the artwork is missing.
		glEnable(GL_SCISSOR_TEST);
		glScissor(355, 7, 250, 18);
		glClearColor(0.16f, 0.17f, 0.12f, 1.f);
		glClear(GL_COLOR_BUFFER_BIT);
		glScissor(356, 8, static_cast<GLsizei>(248 * fill), 16);
		glClearColor(0.70f, 0.52f, 0.28f, 1.f);
		glClear(GL_COLOR_BUFFER_BIT);
		glDisable(GL_SCISSOR_TEST);
	}
	if (m_ready)
	{
		m_canvas.begin();
		constexpr float white[] = {1.f, 1.f, 1.f, 1.f};
		constexpr float text[] = {0.79f, 0.72f, 0.60f, 1.f};
		constexpr float uv[] = {0.f, 0.f, 1.f, 1.f};
		// The retail atlas uses a 1024 x 768 canvas in the upper part of the 1024 x 1024 texture.
		m_canvas.quad(atlas.handle(), {0.f, 0.f, 1.f, 1.f},
			{0.5f / 1024, 0.5f / 1024, 1023.f / 1024, 767.f / 1024}, white, 0.f);
		m_canvas.quad(cordon.handle(), {257.f / 1024, 369.f / 768, 512.f / 1024, 256.f / 768}, uv, white, 0.f);
		m_canvas.quad(atlas.handle(), {379.f / 1024, 726.f / 768, 268.f / 1024 * fill, 37.f / 768},
			{0.5f / 1024, 768.5f / 1024, 267.f / 1024 * fill, 36.f / 1024}, white, 0.f);
		if (!has_atlas)
			m_canvas.quad(m_game_title, {0.17f, 0.36f, 0.66f, 0.095f}, uv, text, 1.f);
		m_canvas.quad(m_labels[static_cast<unsigned>(stage)], {0.29f, 645.f / 768, 0.42f, 32.f / 544}, uv, text, 1.f);
		m_canvas.end();
	}
	graphics.present();
}

void VitaLoadingScreen::release()
{
	atlas.release();
	cordon.release();
	glDeleteTextures(static_cast<GLsizei>(m_labels.size()), m_labels.data());
	m_labels.fill(0);
	if (m_game_title)
		glDeleteTextures(1, &m_game_title);
	m_game_title = 0;
	m_ready = false;
	m_canvas.release();
}


VitaInventoryUI::~VitaInventoryUI()
{
	release();
}

bool VitaInventoryUI::initialize()
{
	if (!m_canvas.initialize())
		return false;
	VitaDecodedDDS white;
	white.width = white.height = 1;
	white.rgba = {255, 255, 255, 255};
	if (!m_white.upload_dds(white))
		return false;

	int pgf = sceSysmoduleIsLoaded(SCE_SYSMODULE_PGF);
	if (pgf < 0)
		pgf = sceSysmoduleLoadModule(SCE_SYSMODULE_PGF);
	if (pgf < 0)
		return false;
	SceFontLibHandle library{};
	const auto font = open_ui_font(library);
	if (!font)
		return false;

	// Rasterize the inventory text at 1.5 times the display size for compact Vita labels.
	constexpr float text_scale = 1.5f;
	TextImage headings(1440, 816);
	const auto label = [&](const char* text, float x, float y, float right) {
		headings.line(font, text, static_cast<int>(x * text_scale),
			static_cast<int>(y * text_scale), static_cast<int>(right * text_scale));
	};
	label(vita_text("Nourriture","Satiety"),22,32,140);
    label(vita_text("Saignement","Bleeding"),190,32,320);
    label(vita_text("Poids","Weight"), 28, 190, 78);
	label("kg / 50 kg", 128, 190, 292);
	label("Description", 371, 190, 650);
	label(vita_text("Combi.","Outfit"), 734, 190, 920);
	label(vita_text("Param\xe8" "tres","Parameters"), 816, 403, 932);
	m_headers = headings.upload(true);
	TextImage footer(1440, 42);
	footer.line(font, vita_text("Carr\xe9 : utiliser / \xe9quiper   Triangle : jeter","Square: use / equip   Triangle: drop"), 18, 29, 700);
	footer.line(font, vita_text("Directions : choisir","Directions: select"), 730, 29, 1160);
	footer.line(font, vita_text("Rond : fermer","Circle: close"), 1230, 29, 1420);
	m_footer = footer.upload(true);

	TextImage empty(408, 57);
	empty.line(font, vita_text("Sac vide","Empty backpack"), 6, 27, 400);
	m_empty = empty.upload(true);

	for (size_t i = 0; i < m_descriptions.size(); ++i)
	{
		m_name_widths[i]=std::max(232,measure_text_width(font,item_names[i].c_str())+4);
        TextImage name(int(m_name_widths[i]),40);
		name.line(font, item_names[i], 0, 28, std::max(228, measure_text_width(font, item_names[i].c_str())));
		m_names[i] = name.upload(true);
		TextImage desc(432, 768);
		const int last_line = desc.line(font, item_details[i], 0, 24, 424);
		m_description_heights[i] = (last_line + 8.f) / text_scale;
		m_descriptions[i] = desc.upload(true);
	}
	TextImage statistics(432, 76);
	statistics.line(font, vita_text("Pr\xe9" "cision","Accuracy"), 0, 18, 210);
	statistics.line(font, vita_text("Prise en main","Handling"), 216, 18, 430);
	statistics.line(font, vita_text("D\xe9" "g\xe2ts","Damage"), 0, 54, 210);
	statistics.line(font, vita_text("Cadence de tir","Rate of fire"), 216, 54, 430);
	m_statistics = statistics.upload(true);
	TextImage condition(80, 32);
	condition.line(font, vita_text("\xe9tat","condition"), 0, 24, 79);
	m_condition = condition.upload(true);
	TextImage outfit(324, 132), protections(84, 132);
	const char* const risks[] = {vita_text("Br\xfb" "lure","Burn"), vita_text("D\xe9" "charge \xe9" "lec.","Electric shock"), "Impact",
		vita_text("Entailles","Rupture"), vita_text("Br\xfb" "lure chim.","Chemical burn"), "Explosion", vita_text("Perforation","Bullet protection")};
	for (unsigned i = 0; i < 7; ++i)
	{
		outfit.line(font, risks[i], 0, 18 + 18 * i, 320);
		protections.line(font, "+10%", 0, 18 + 18 * i, 82);
	}
	m_outfit_labels = outfit.upload(true);
	m_outfit_values = protections.upload(true);
	TextImage kg(48, 32), ru(48, 32), dot(12, 24);
	kg.line(font, "kg", 0, 24, 47);
	ru.line(font, "RU", 0, 24, 47);
	dot.line(font, ",", 0, 20, 11);
	m_kg = kg.upload(true);
	m_ru = ru.upload(true);
	m_dot = dot.upload(true);
    TextImage percent(24,24);percent.line(font,"%",0,20,23);m_percent=percent.upload(true);
	for (size_t i = 0; i < m_digits.size(); ++i)
	{
		TextImage digit(20, 24);
		digit.line(font, std::to_string(i), 2, 20, 20);
		m_digits[i] = digit.upload(true);
	}
	TextImage pm(104, 29);
	pm.line(font, "PM", 4, 21, 101);
	m_pm = pm.upload(true);
    const char* weapon_names[]={"PM","BM16","AK-74"};
    for(unsigned i=0;i<3;++i){TextImage text(104,29);text.line(font,weapon_names[i],4,21,101);m_weapon_names[i]=text.upload(true);}
    TextImage slash(20,24);slash.line(font,"/",2,20,19);m_ammo_slash=slash.upload(true);
    for(unsigned i=0;i<vita_item_count;++i)if(VitaInventory::ammunition(static_cast<VitaInventoryItem>(i))){
        TextImage label(256,32);label.line(font,vita_text(vita_item_art[i].french,vita_item_art[i].english),2,24,254);
        m_ammo_labels[i]=label.upload(true);
    }
	TextImage reload(162, 29);
	reload.line(font, vita_text("Rechargement","Reloading"), 4, 21, 157);
	m_reload = reload.upload(true);
	TextImage death(298, 75);
	death.line(font, vita_text("Vous \xeates mort","You are dead"), 15, 27, 282);
	death.line(font, vita_text("Start : menu de partie","Start: game menu"), 15, 59, 282);
	m_death = death.upload(true);

    const char* columns[]={"Qt\xe9", "Prix", "Panier"};
    for(unsigned i=0;i<3;++i){TextImage text(96,32);text.line(font,columns[i],0,24,95);m_exchange_columns[i]=text.upload(true);}
    const char* exchange[]={vita_text("Vous","You"), vita_text("Sidorovitch","Sidorovich"), vita_text("Fouille","Search"), vita_text("Carr\xe9 : proposer   Croix : retirer   Triangle : \xe9" "changer   Rond : annuler","Square: offer   X: withdraw   Triangle: trade   Circle: cancel"),
        vita_text("Carr\xe9 : prendre   Triangle : tout prendre   Rond : fermer","Square: take   Triangle: take all   Circle: close"),
        vita_text("Directions : objet / panneau   L / R : quantit\xe9","Directions: item / panel   L / R: quantity"), vita_text("Poids / 60 kg","Weight / 60 kg"), vita_text("Solde du panier (RU)","Trade balance (RU)")};
    for(unsigned i=0;i<8;++i){TextImage text(i==3||i==4||i==5?1400:420,40);
        text.line(font,exchange[i],2,28,i==3||i==4||i==5?1398:418);m_exchange_labels[i]=text.upload(true);}
    const char* results[]={"", vita_text("Op\xe9ration effectu\xe9" "e","Transaction completed"), vita_text("Aucun objet propos\xe9","No item offered"), vita_text("Fonds insuffisants","Not enough money"),
        vita_text("Le marchand manque d'argent","Trader cannot afford this"), vita_text("Sac trop lourd ou pile pleine","Too heavy or stack full"), vita_text("Stock indisponible","Out of stock"), vita_text("\xc9" "change impossible","Cannot trade")};
    for(unsigned i=0;i<8;++i){TextImage text(900,40);text.line(font,results[i],2,28,895);m_exchange_results[i]=text.upload(true);}
    const char* hints[]={vita_text("Carr\xe9 : fouiller","Square: search"), vita_text("Carr\xe9 : ramasser","Square: pick up"), "Triangle : commercer avec Sidorovitch",
        vita_text("Carr\xe9 : utiliser / \xe9quiper   Triangle : jeter","Square: use / equip   Triangle: drop")};
    for(unsigned i=0;i<4;++i){TextImage text(900,40);text.line(font,hints[i],2,28,895);m_hints[i]=text.upload(true);}

	sceFontClose(font);
	sceFontDoneLib(library);
	return std::all_of(m_exchange_labels.begin(),m_exchange_labels.end(),[](GLuint v){return v!=0;}) &&
        std::all_of(m_exchange_columns.begin(),m_exchange_columns.end(),[](GLuint v){return v!=0;}) &&
        std::all_of(m_exchange_results.begin()+1,m_exchange_results.end(),[](GLuint v){return v!=0;}) &&
        std::all_of(m_hints.begin(),m_hints.end(),[](GLuint v){return v!=0;}) &&
        m_percent && m_headers && m_footer && m_empty && m_statistics && m_outfit_labels && m_outfit_values &&
		m_kg && m_ru && m_dot && m_condition &&
		std::all_of(m_digits.begin(), m_digits.end(), [](GLuint texture) { return texture != 0; }) &&
		std::all_of(m_descriptions.begin(), m_descriptions.end(), [](GLuint texture) { return texture != 0; }) &&
		std::all_of(m_names.begin(), m_names.end(), [](GLuint texture) { return texture != 0; });
}

void VitaInventoryUI::draw(const VitaInventory& inventory, unsigned health) const
{
	constexpr float uv[] = {0.f, 0.f, 1.f, 1.f};
	constexpr float white[] = {1.f, 1.f, 1.f, 1.f};
	constexpr float caption[] = {0.94f, 0.85f, 0.71f, 1.f};
	constexpr float orange[] = {0.96f, 0.64f, 0.12f, 1.f};
	constexpr float muted[] = {0.55f, 0.52f, 0.48f, 1.f};
	constexpr float selected[] = {0.80f, 0.58f, 0.22f, 0.9f};
	constexpr float grid[] = {0.50f, 0.51f, 0.45f, 0.23f};
	constexpr float green[] = {0.07f, 0.90f, 0.08f, 1.f};
	constexpr float cyan[] = {0.14f, 0.68f, 0.64f, 1.f};
	constexpr float dim_cyan[] = {0.14f, 0.68f, 0.64f, 0.25f};
	m_canvas.begin();
	const auto draw = [&](GLuint texture, float x, float y, float width, float height,
		const float (&source)[4], const float (&color)[4], float mask = 0.f) {
		m_canvas.quad(texture, {x / 960.f, y / 544.f, width / 960.f, height / 544.f}, source, color, mask);
	};
	const auto solid = [&](float x, float y, float width, float height, const float (&color)[4]) {
		draw(m_white.handle(), x, y, width, height, uv, color);
	};
	const auto cells = [&](float x, float y, unsigned columns, unsigned rows, float width, float height) {
		for (unsigned column = 0; column <= columns; ++column)
			solid(x + column * width, y, 0.65f, rows * height, grid);
		for (unsigned row = 0; row <= rows; ++row)
			solid(x, y + row * height, columns * width, 0.65f, grid);
	};
	// inventory_new_16.xml positions are mapped from the original 1024 x 768 canvas.
	draw(panel.handle(), 7.5f, 172.f, 938.f, 342.f, {0.f, 0.f, 1005.f / 1008.f, 483.f / 484.f}, white);
	draw(slots.handle(), 119.f, 57.f, 723.f, 122.f, {0.f, 1.f / 176.f, 1.f, 172.f / 176.f}, white);
	cells(31.f, 203.f, 9, 10, 29.f, 29.f);
	cells(141.f, 84.f, 3, 2, 36.5f, 36.8f);
	cells(281.f, 84.f, 6, 2, 36.5f, 36.8f);
	draw(m_headers, 0.f, 0.f, 960.f, 544.f, uv, orange, 1.f);
	draw(m_footer, 0.f, 516.f, 960.f, 28.f, uv, caption, 1.f);

	const bool equipped = inventory.pistol_equipped();
	constexpr float round_weight = 0.2f / VitaInventory::fmj_box;
	const float pistol_weight = 0.73f + inventory.loaded() * round_weight;
	const float ammunition_weight = inventory.reserve() * round_weight;
	const bool owns_pistol = inventory.contains(VitaInventoryItem::Pistol);
	// The original novice jacket weighs 3 kg. The other starting item masses come from their LTX sections.
	const float total_weight = inventory.weight();
	decimal(total_weight, 1, 85.f, 177.f);

    float icon_uv[vita_item_count][4]{};
    for(unsigned i=0;i<vita_item_count;++i){
        const auto&a=vita_item_art[i];const unsigned x=a.x*50,y=a.y*50,left=x&~3u,top=y&~3u;
        const unsigned w=((x+a.columns*50+3)&~3u)-left,h=((y+a.rows*50+3)&~3u)-top;
        icon_uv[i][0]=float(x-left)/w;icon_uv[i][1]=float(y-top)/h;
        icon_uv[i][2]=float(a.columns*50)/w;icon_uv[i][3]=float(a.rows*50)/h;
    }
    std::array<bool,90> occupied{};
    if(!inventory.has_items())draw(m_empty,34,204,272,38,uv,caption,1);
    for(size_t i=0;i<icons.size();++i)
    {
        const auto item=static_cast<VitaInventoryItem>(i);
        if(!inventory.contains(item))continue;
        const bool pistol_slot=item==VitaInventoryItem::Pistol&&equipped;
        const bool primary_slot=(i==12&&inventory.primary()==1)||(i==13&&inventory.primary()==2);
        const bool outfit_slot=(i==10&&inventory.outfit()==1)||(i==11&&inventory.outfit()==2);
        const bool in_slot=pistol_slot||outfit_slot||primary_slot;
        const unsigned columns=vita_item_art[i].columns,rows=vita_item_art[i].rows;
        unsigned cell=0;
        if(!in_slot)for(;cell<90;++cell){
            if(cell%9+columns>9||cell/9+rows>10)continue;
            bool free=true;for(unsigned y=0;y<rows;++y)for(unsigned x=0;x<columns;++x)free&=!occupied[cell+y*9+x];
            if(free){for(unsigned y=0;y<rows;++y)for(unsigned x=0;x<columns;++x)occupied[cell+y*9+x]=true;break;}
        }
        if(!in_slot&&cell==90)continue;
        const float x=pistol_slot?142.f:outfit_slot?552.f:primary_slot?282.f:32.f+cell%9*29.f;
        const float y=in_slot?85.f:204.f+cell/9*29.f;
        const float width=in_slot?columns*35.f:columns*29.f-2,height=in_slot?rows*35.f:rows*29.f-2;
        if(inventory.selected()==item){solid(x,y,width,1,selected);solid(x,y+height-1,width,1,selected);
            solid(x,y,1,height,selected);solid(x+width-1,y,1,height,selected);}
        draw(icons[i].handle(),x+1,y+1,width-2,height-2,icon_uv[i],white);
        if(inventory.count(item)>1)number(inventory.count(item),x,y-4,.55f);
    }

	if (inventory.has_items())
	{
		const size_t item = static_cast<size_t>(inventory.selected());
		const bool pistol = inventory.selected() == VitaInventoryItem::Pistol;
		const bool knife = inventory.selected() == VitaInventoryItem::Knife;
        const float aspect=float(vita_item_art[item].columns)/vita_item_art[item].rows;
        const float image_width=std::min(140.f,74.f*aspect),image_height=image_width/aspect;
        draw(icons[item].handle(),509-image_width*.5f,220+(74-image_height)*.5f,image_width,image_height,icon_uv[item],white);
		const float name_scale=std::min(.667f,178.f/m_name_widths[item]);
        draw(m_names[item],368,311,m_name_widths[item]*name_scale,40*name_scale,uv,orange,1);
		const auto chosen=inventory.selected();
        const int cost=VitaInventory::base_cost(chosen)*inventory.count(chosen)/VitaInventory::lot(chosen);
		unsigned digits=1;for(int n=cost;n>=10;n/=10)++digits;
        const float scale=std::min(.667f,82.f/(12*digits+64));
        const float prices_end=number(cost,643-(12*digits+64)*scale,316,scale);
        draw(m_ru,prices_end+12*scale,316-4*scale,48*scale,32*scale,uv,orange,1);
		const float weight=inventory.item_weight_grams(chosen)/1000.f;
		decimal(weight, 2, 368.f, 336.f);
		draw(m_kg, 420.f, 334.f, 32.f, 21.3f, uv, muted, 1.f);
		if (pistol)
		{
			draw(m_condition, 510.f, 329.f, 53.3f, 21.3f, uv, muted, 1.f);
			solid(564.f, 338.f, 79.f, 4.f, green);
			draw(m_statistics, 365.f, 349.f, 288.f, 50.7f, uv, muted, 1.f);
			// ui_wpn_params.script applied to the retail PM values, including k_disp=3 for 9x18 FMJ.
			const float statistics[] = {
				30.f * log10f(1.f / (0.4f * 3.f)) + 15.f * log10f(1.f / 0.4f) +
					0.02f * 260.f * log10f(1.f / (0.4f / 10.f)),
				(1.f - (0.7f - 0.5f) * (0.7f - 0.5f) + 1.f) * 50.f,
				(100.f * 0.30f - 25.f) * (100.f / 85.f),
				((260.f - 200.f) / 720.f) * ((260.f - 200.f) / 720.f) * 100.f + 15.f
			};
			for (unsigned statistic = 0; statistic < 4; ++statistic)
				for (unsigned tick = 0; tick < 20; ++tick)
					solid(365.f + (statistic % 2) * 144.f + tick * 4.4f,
						367.f + (statistic / 2) * 24.f, 3.4f, 3.f,
						tick < statistics[statistic] * 0.2f ? cyan : dim_cyan);
		}
		const float text_y = pistol ? 400.f : 353.f;
		const float viewport = 495.f - text_y;
		m_scroll_limit = std::max(0.f, m_description_heights[item] - viewport);
		const float scroll = std::min(m_scroll, m_scroll_limit);
		draw(m_descriptions[item], 366.f, text_y, 288.f, viewport,
			{0.f, scroll * 1.5f / 768.f, 1.f, viewport * 1.5f / 768.f}, caption, 1.f);
		if (m_scroll_limit > 0.f)
		{
			const float thumb_height = std::max(12.f, viewport * viewport / m_description_heights[item]);
			solid(650.f, text_y, 1.f, viewport, grid);
			solid(649.f, text_y + (viewport - thumb_height) * scroll / m_scroll_limit,
				2.f, thumb_height, muted);
		}
	}
	else
		m_scroll_limit = 0.f;

	// npc_icon_novice_outfit and the original three-gauge frame.
	if(inventory.outfit()==1)draw(portrait.handle(), 801.f, 203.f, 74.f, 185.f, uv, white);
    else if(inventory.outfit()==2)draw(icons[11].handle(),791,248,100,100,uv,white);
	draw(gauges.handle(), 693.f, 239.f, 55.3f, 126.8f,
		{2.f / 180, 39.f / 220, 59.f / 180, 179.f / 220}, white);
	const float health_height = 88.f * std::min(100u, health) / 100.f;
	draw(gauges.handle(), 698.5f, 254.f + 88.f - health_height, 8.4f, health_height,
		{99.f / 180, (41.f + 128.f * (1.f - std::min(100u, health) / 100.f)) / 220,
			9.f / 180, (128.f * std::min(100u, health) / 100.f) / 220}, white);
	draw(gauges.handle(), 711.6f, 254.f, 8.4f, 88.f,
		{126.f / 180, 41.f / 220, 9.f / 180, 128.f / 220}, white);
	unsigned money_digits=1;for(unsigned n=inventory.money();n>=10;n/=10)++money_digits;
    const float money_scale=std::min(.75f,100.f/(12*money_digits+64));
    const float money_end=number(inventory.money(),797-(12*money_digits+64)*money_scale,379,money_scale);
    draw(m_ru,money_end+12*money_scale,379-4*money_scale,48*money_scale,32*money_scale,uv,orange,1);
	draw(m_outfit_labels, 709.f, 415.f, 216.f, 88.f, uv, caption, 1.f);
	for(unsigned i=0;i<7;++i){const float end=number(inventory.protection(),884,414+i*12,.60f);
        draw(m_percent,end+8,414+i*12,14,14,uv,green,1);}
    // Food and bleeding are numeric condition indicators, not decorative fixed values.
    number(inventory.food()/10,130,19,.70f);
    number(inventory.bleeding()/10,306,19,.70f);
	m_canvas.end();
}

void VitaInventoryUI::draw_exchange(const VitaInventory& inv,const VitaLootContainer* loot,
 const VitaTraderStock& trader,const VitaTradeBasket& basket,bool buying,unsigned selection,unsigned multiplier,VitaTradeResult result)const
{
 constexpr float uv[]={0,0,1,1},white[]={1,1,1,1},gold[]={.96f,.69f,.18f,1},dark[]={0,0,0,.7f},highlight[]={.65f,.48f,.18f,.65f};
 m_canvas.begin();
 auto q=[&](GLuint tex,float x,float y,float w,float h,const float(&color)[4],float mask=0){
  m_canvas.quad(tex,{x/960,y/544,w/960,h/544},uv,color,mask);};
 q(m_white.handle(),0,0,960,544,dark);
 m_canvas.quad(trade_art.handle(),{0,140.f/544,1,390.f/544},{0,218.f/1024,1,531.f/1024},white);
 const VitaProfileLayout actor_card{15,12,180},npc_card{765,12,180};
 const auto profile_quad=[&](GLuint id,const std::array<float,4>&r,const float(&u)[4],float mask=0){
  m_canvas.quad(id,{r[0]/960,r[1]/544,r[2]/960,r[3]/544},u,mask?gold:white,mask);};
 profile_quad(trade_art.handle(),actor_card.frame(),{0,0,210.f/1024,150.f/1024});
 profile_quad(trade_art.handle(),npc_card.frame(),{813.f/1024,0,210.f/1024,150.f/1024});
 if(!loot)profile_quad(merchant_portrait,npc_card.image(true),uv);
 profile_quad(actor_portrait.handle(),actor_card.image(false),uv);
 profile_quad(m_exchange_labels[0],actor_card.name(false),{0,0,.6f,1},1);
 profile_quad(m_exchange_labels[loot?2:1],npc_card.name(true),{0,0,.6f,1},1);
 // Money belongs to the original bag money fields, below the profile cards.
 const auto cash=[&](unsigned value,float left){unsigned digits=1;for(unsigned n=value;n>=10;n/=10)++digits;
  const float scale=std::min(.75f,92.f/(12*digits+8));
  number(value,left+(96-(12*digits+8)*scale)*.5f,153,scale);};
 cash(inv.money(),30);cash(loot?loot->cash:trader.cash,817);
 decimal(inv.weight(),1,205,50);q(m_kg,250,47,32,21,gold,1);
 if(selection<vita_item_count){
  const float label_width=std::min(280.f,m_name_widths[selection]*.667f);
  q(m_names[selection],480-label_width*.5f,25,label_width,27,gold,1);
  const float aspect=float(vita_item_art[selection].columns)/vita_item_art[selection].rows;
  const float width=std::min(120.f,60.f*aspect),height=width/aspect;
  q(icons[selection].handle(),480-width*.5f,60+(60-height)*.5f,width,height,white);
  if(!loot){number(VitaTradeBasket::price(static_cast<VitaInventoryItem>(selection),
    VitaInventory::lot(static_cast<VitaInventoryItem>(selection)),buying),410,116,.75f);q(m_ru,485,113,32,21,gold,1);}
 }
 const auto grid=[&](const VitaItemStacks& items,float x,float y,bool active,bool proposals){
  unsigned cell=0;std::array<bool,90> occupied{};
  for(unsigned i=0;i<vita_item_count;++i){
   if(!items[i])continue;
   const unsigned cols=proposals?1:vita_item_art[i].columns,rows=proposals?1:vita_item_art[i].rows;
   for(cell=0;cell<90;++cell){
    if(cell%9+cols>9)continue;
    bool free=cell/9+rows<=10;
    for(unsigned r=0;free&&r<rows;++r)for(unsigned c=0;c<cols;++c)free&=!occupied[cell+r*9+c];
    if(free)break;
   }
   if(cell>=90)break;
   for(unsigned r=0;r<rows;++r)for(unsigned c=0;c<cols;++c)occupied[cell+r*9+c]=true;
   const float xx=x+cell%9*29,yy=y+cell/9*29;
   if(active&&i==selection)q(m_white.handle(),xx-1,yy-1,cols*29,rows*29,highlight);
   q(icons[i].handle(),xx,yy,cols*29-2,rows*29-2,white);
   if(items[i]>1)number(items[i],xx,yy+rows*29-13,.5f);
  }
 };
 grid(inv.state().items,29,180,!buying,false);
 grid(loot?loot->items:trader.items,681,180,buying,false);
 if(!loot){
  grid(basket.sell,340,185,false,true);grid(basket.buy,340,352,false,true);
  q(m_exchange_labels[7],325,485,280,26,gold,1);
  const int balance=basket.balance();if(balance<0)q(m_white.handle(),608,497,7,2,gold);
  number(std::abs(balance),620,485,.75f);number(multiplier,735,116,.6f);
 }
 q(m_exchange_results[static_cast<unsigned>(result)],210,142,540,26,gold,1);
 q(m_exchange_labels[loot?4:3],15,520,930,24,white,1);
 m_canvas.end();
}

void VitaInventoryUI::draw_hint(unsigned index)const
{
    if(index>=m_hints.size())return;
    constexpr float uv[]={0,0,1,1},white[]={1,1,1,1},dark[]={0,0,0,.8f};
    m_canvas.begin();m_canvas.quad(m_white.handle(),{.15f,.83f,.70f,.06f},uv,dark);
    m_canvas.quad(m_hints[index],{.17f,.835f,.66f,.05f},uv,white,1);m_canvas.end();
}

void VitaInventoryUI::scroll_description(float pixels)
{
	m_scroll = std::clamp(m_scroll + pixels, 0.f, m_scroll_limit);
}

float VitaInventoryUI::number(int value, float x, float y, float scale) const
{
	constexpr float uv[] = {0.f, 0.f, 1.f, 1.f};
	constexpr float text[] = {0.95f, 0.80f, 0.46f, 1.f};
	value = std::max(0, value);
	int divisor = 1;
	while (value / divisor >= 10)
		divisor *= 10;
	for (; divisor; divisor /= 10)
	{
		m_canvas.quad(m_digits[(value / divisor) % 10],
			{x / 960.f, y / 544.f, 20.f * scale / 960, 24.f * scale / 544}, uv, text, 1.f);
		x += 12.f * scale;
	}
	return x;
}

void VitaInventoryUI::decimal(float value, unsigned precision, float x, float y) const
{
	constexpr float uv[] = {0.f, 0.f, 1.f, 1.f};
	constexpr float text[] = {0.95f, 0.80f, 0.46f, 1.f};
	constexpr float scale = 0.667f;
	const int factor = precision == 1 ? 10 : 100;
	const int rounded = static_cast<int>(lroundf(value * factor));
	x = number(rounded / factor, x, y, scale);
	m_canvas.quad(m_dot, {x / 960, y / 544, 16.f * scale / 960, 24.f * scale / 544}, uv, text, 1.f);
    // Keep the decimal separator visible at the Vita's native resolution.
    m_canvas.quad(m_white.handle(),{(x+2.f)/960,(y+17.f*scale)/544,2.f/960,3.f/544},uv,text);
	x += 10.f * scale;
	for (int place = factor / 10; place; place /= 10)
	{
		number((rounded / place) % 10, x, y, scale);
		x += 12.f * scale;
	}
}

void VitaInventoryUI::draw_weapon_ammo(const VitaInventory& inventory, bool reloading,float hud_scale) const
{
    if(!inventory.firearm())return;
    (void)reloading; // Keep the original counter visible while the magazine animation plays.
    constexpr float uv[]={0,0,1,1},white[]={1,1,1,1},gold[]={238.f/255,155.f/255,23.f/255,1},caption[]={1,1,1,.65f};
    m_canvas.begin();
    const auto q=[&](GLuint id,float x,float y,float w,float h,const float(&u)[4],const float(&color)[4],float mask=0){
        const auto r=vita_hud_rect(x,y,w,h,hud_scale,952,536);
        m_canvas.quad(id,{r[0]/960,r[1]/544,r[2]/960,r[3]/544},u,color,mask);
    };
    // ui_hud_frame_patron: original atlas crop and 157:65 proportions.
    q(hud_art,840,489.63f,112,46.37f,{185.f/620,72.f/224,157.f/620,65.f/224},white);
    const auto ammo=vita_item_index(inventory.current_ammo());
    q(m_ammo_labels[ammo],846,490,101,15,uv,caption,1);
    const auto&art=vita_item_art[ammo];
    const float aspect=float(art.columns)/art.rows,icon_w=std::min(40.f,24.f*aspect),icon_h=icon_w/aspect;
    q(icons[ammo].handle(),845,508+(24-icon_h)*.5f,icon_w,icon_h,uv,white);
    const std::string counter=std::to_string(inventory.loaded())+"/"+std::to_string(inventory.reserve());
    const float scale=std::min(.85f,61.f/(12.f*counter.size()+8.f));
    float x=948-(12.f*counter.size()+8.f)*scale;
    for(char digit:counter){
        q(digit=='/'?m_ammo_slash:m_digits[digit-'0'],x,510,20*scale,24*scale,uv,gold,1);
        x+=12*scale;
    }
    m_canvas.end();
}

void VitaInventoryUI::draw_scope()const
{
 if(!scope.handle())return;
 constexpr float uv[]={0,0,1,1},white[]={1,1,1,1},black[]={0,0,0,1};
 m_canvas.begin();
 m_canvas.quad(m_white.handle(),{0,0,208.f/960,1},uv,black);
 m_canvas.quad(m_white.handle(),{752.f/960,0,208.f/960,1},uv,black);
 m_canvas.quad(scope.handle(),{208.f/960,0,544.f/960,1},uv,white);
 m_canvas.end();
}

void VitaInventoryUI::draw_damage(float strength, bool dead) const
{
	constexpr float uv[] = {0.f, 0.f, 1.f, 1.f};
	const float red[] = {0.47f, 0.025f, 0.018f, dead ? 0.65f : strength * 0.40f};
	m_canvas.begin();
	m_canvas.quad(m_white.handle(), {0.f, 0.f, 1.f, 1.f}, uv, red);
	if (dead)
	{
		constexpr float bg[] = {0.07f, 0.033f, 0.027f, 0.94f};
		constexpr float text[] = {1.f, 0.86f, 0.67f, 1.f};
		constexpr float panel[] = {331.f / 960, 236.f / 544, 298.f / 960, 75.f / 544};
		m_canvas.quad(m_white.handle(), panel, uv, bg);
		m_canvas.quad(m_death, panel, uv, text, 1.f);
	}
	m_canvas.end();
}

void VitaInventoryUI::release()
{
	glDeleteTextures(m_ammo_labels.size(),m_ammo_labels.data());m_ammo_labels.fill(0);
 if(m_ammo_slash)glDeleteTextures(1,&m_ammo_slash);m_ammo_slash=0;
 scope.release();
	actor_portrait.release();
	trade_art.release();
	panel.release();
	slots.release();
	portrait.release();
	gauges.release();
	for (auto& icon : icons)
		icon.release();
    glDeleteTextures(m_weapon_names.size(),m_weapon_names.data());m_weapon_names.fill(0);
    glDeleteTextures(m_exchange_columns.size(),m_exchange_columns.data());m_exchange_columns.fill(0);
    glDeleteTextures(m_exchange_labels.size(),m_exchange_labels.data());m_exchange_labels.fill(0);
    glDeleteTextures(m_exchange_results.size(),m_exchange_results.data());m_exchange_results.fill(0);
    glDeleteTextures(m_hints.size(),m_hints.data());m_hints.fill(0);
    m_white.release();
    m_canvas.release();
	for (GLuint texture : m_descriptions)
		if (texture)
			glDeleteTextures(1, &texture);
	m_descriptions.fill(0);
	glDeleteTextures(static_cast<GLsizei>(m_names.size()), m_names.data());
	m_names.fill(0);
	glDeleteTextures(static_cast<GLsizei>(m_digits.size()), m_digits.data());
	m_digits.fill(0);
	for (GLuint texture : {m_headers, m_footer, m_empty, m_statistics, m_outfit_labels, m_outfit_values,
		m_kg, m_ru, m_dot, m_condition, m_pm, m_reload, m_death, m_percent})
		if (texture)
			glDeleteTextures(1, &texture);
	m_headers = m_footer = m_empty = m_statistics = m_outfit_labels = m_outfit_values = 0;
	m_kg = m_ru = m_dot = m_condition = m_pm = m_reload = m_death = m_percent = 0;
}


VitaMainMenu::~VitaMainMenu()
{
	release();
}

bool VitaMainMenu::initialize()
{
	if (!m_canvas.initialize())
		return false;
	int pgf = sceSysmoduleIsLoaded(SCE_SYSMODULE_PGF);
	if (pgf < 0)
		pgf = sceSysmoduleLoadModule(SCE_SYSMODULE_PGF);
	if (pgf < 0)
		return false;
	SceFontLibHandle library{};
	const SceFontHandle font = open_ui_font(library);
	if (!font)
		return false;
	const char* const options[] = {vita_text("Continuer","Continue"), vita_text("Nouvelle partie","New game"), vita_text("Charger","Load"), "Options", vita_text("Quitter","Quit")};
	for (unsigned i = 0; i < m_options.size(); ++i)
		m_options[i] = upload_centered_label(font, options[i], 192);
	const char* const pause_options[] = {vita_text("Reprendre","Resume"), vita_text("Sauvegarder","Save"), vita_text("Charger","Load"), vita_text("Nouvelle partie","New game"), "Options", vita_text("Quitter","Quit")};
	for (unsigned i = 0; i < m_pause_options.size(); ++i)
		m_pause_options[i] = upload_centered_label(font, pause_options[i], 192);
	const char* const messages[] = {vita_text("Partie sauvegard\xe9" "e","Game saved"), vita_text("\xc9" "chec de la sauvegarde","Save failed"),
		vita_text("Sauvegarde absente ou invalide","Missing or invalid save"), vita_text("Reprenez avant de sauvegarder","Resume before saving"), vita_text("Croix : confirmer","X: confirm")};
	static_assert(sizeof(messages) / sizeof(messages[0]) == static_cast<unsigned>(VitaSaveMessage::Count) - 1);
	for (unsigned i = 0; i < m_messages.size(); ++i)
		m_messages[i] = upload_centered_label(font, messages[i], 440);
	m_pause_hint = upload_centered_label(font, vita_text("Haut / Bas : choisir     Croix : valider     Rond : retour","Up / Down: select   X: confirm   Circle: back"), 720);
	m_dead = upload_centered_label(font, vita_text("Vous \xea" "tes mort","You are dead"), 400);
	m_hint = upload_centered_label(font, vita_text("Haut / Bas : choisir     Croix : valider","Up / Down: select   X: confirm"), 440);
	m_error = upload_centered_label(font, vita_text("Sauvegarde absente ou invalide","Missing or invalid save"), 400);
	sceFontClose(font);
	sceFontDoneLib(library);
	const auto loaded = [](GLuint texture) { return texture != 0; };
	return m_hint && m_error && m_pause_hint && m_dead &&
		std::all_of(m_options.begin(), m_options.end(), loaded) &&
		std::all_of(m_pause_options.begin(), m_pause_options.end(), loaded) &&
		std::all_of(m_messages.begin(), m_messages.end(), loaded);
}

void VitaMainMenu::draw(VitaGLDevice& graphics, unsigned choice, bool can_continue, bool load_error,
	bool paused, bool dead, VitaSaveMessage message) const
{
	graphics.begin_frame(0.035f, 0.04f, 0.035f);
	m_canvas.begin();
	constexpr float white[] = {1.f, 1.f, 1.f, 1.f};
	constexpr float selected[] = {0.98f, 0.89f, 0.69f, 1.f};
	constexpr float regular[] = {0.12f, 0.10f, 0.075f, 1.f};
	constexpr float disabled[] = {0.43f, 0.40f, 0.36f, 1.f};
	constexpr float uv[] = {0.f, 0.f, 1.f, 1.f};
	constexpr float artwork[] = {0.5f / 1024, 0.5f / 1024, 1023.f / 1024, 767.f / 1024};
	m_canvas.quad(background.handle(), {0.f, 0.f, 1.f, 1.f}, artwork, white);
	m_canvas.quad(atlas.handle(), {0.f, 0.f, 1.f, 1.f}, artwork, white);
	const float row_spacing = paused ? 27.f : 34.f;
	m_canvas.quad(atlas.handle(), {558.f / 960, (315.f + row_spacing * choice) / 544, 367.f / 960, 52.f / 544},
		{7.f / 1024, 951.f / 1024, 392.f / 1024, 73.f / 1024}, white);
	const GLuint* options = paused ? m_pause_options.data() : m_options.data();
	const size_t count = paused ? m_pause_options.size() : m_options.size();
	for (size_t i = 0; i < count; ++i)
	{
		const bool unavailable = paused ? dead && i < 2 : !can_continue && (i == 0 || i == 2);
		const auto& color = unavailable ? disabled : i == choice || !atlas.handle() ? selected : regular;
		m_canvas.quad(options[i], {630.f / 960, (325.f + row_spacing * i) / 544, 192.f / 960, 32.f / 544},
			uv, color, 1.f);
	}
	if (paused)
	{
		m_canvas.quad(m_pause_hint, {8.f / 960, 506.f / 544, 720.f / 960, 32.f / 544}, uv, selected, 1.f);
		if (dead)
			m_canvas.quad(m_dead, {10.f / 960, 426.f / 544, 400.f / 960, 32.f / 544}, uv, selected, 1.f);
		if (message != VitaSaveMessage::None)
			m_canvas.quad(m_messages[static_cast<unsigned>(message) - 1],
				{10.f / 960, 462.f / 544, 440.f / 960, 32.f / 544}, uv, selected, 1.f);
	}
	else
	{
		m_canvas.quad(m_hint, {8.f / 960, 506.f / 544, 440.f / 960, 32.f / 544}, uv, selected, 1.f);
		if (load_error)
			m_canvas.quad(m_error, {10.f / 960, 462.f / 544, 400.f / 960, 32.f / 544}, uv, selected, 1.f);
	}
	m_canvas.end();
	if (!paused)
		graphics.present();
}

void VitaMainMenu::release()
{
	background.release();
	atlas.release();
	glDeleteTextures(static_cast<GLsizei>(m_options.size()), m_options.data());
	m_options.fill(0);
	glDeleteTextures(static_cast<GLsizei>(m_pause_options.size()), m_pause_options.data());
	m_pause_options.fill(0);
	glDeleteTextures(static_cast<GLsizei>(m_messages.size()), m_messages.data());
	m_messages.fill(0);
	if (m_pause_hint) glDeleteTextures(1, &m_pause_hint);
	if (m_dead) glDeleteTextures(1, &m_dead);
	m_pause_hint = m_dead = 0;
	if (m_hint) glDeleteTextures(1, &m_hint);
	if (m_error) glDeleteTextures(1, &m_error);
	m_hint = m_error = 0;
	m_canvas.release();
}

#include "vita_options.h"

bool VitaOptionsMenu::initialize(const VitaSettings& settings)
{
 if(!m_canvas.initialize())return false;
 const unsigned char white[]={255,255,255,255};
 glGenTextures(1,&m_white);glBindTexture(GL_TEXTURE_2D,m_white);
 glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,white);
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
 return m_white&&refresh(settings);
}
bool VitaOptionsMenu::refresh(const VitaSettings& settings)
{
 SceFontLibHandle library{};const auto font=open_ui_font(library);if(!font)return false;
 glDeleteTextures(m_labels.size(),m_labels.data());m_labels.fill(0);
 glDeleteTextures(m_tabs.size(),m_tabs.data());m_tabs.fill(0);
 for(GLuint* t:{&m_title,&m_hint,&m_restart,&m_controls}){if(*t)glDeleteTextures(1,t);*t=0;}
 m_title=upload_centered_label(font,"Options",600);
 m_hint=upload_centered_label(font,vita_text("L / R : cat\xe9gorie    Directions : choisir / modifier    Rond : retour",
 "L / R: category    Directions: select / change    Circle: back"),900);
 m_restart=upload_centered_label(font,vita_text("Langue et MSAA : relancer pour appliquer.",
 "Language and MSAA: restart to apply."),850);
 const char*categories[]={vita_text("Graphismes","Graphics"),"Audio",vita_text("Jeu","Game"),vita_text("Commandes","Controls")};
 for(unsigned i=0;i<4;++i)m_tabs[i]=upload_centered_label(font,categories[i],220);
 const char*names[]={vita_text("Volume g\xe9n\xe9ral","Master volume"),vita_text("Musique / radio","Music / radio"),
 vita_text("Effets","Effects"),vita_text("Voix","Voices"),vita_text("Ambiance","Ambience")};
 char text[160];
 for(unsigned i=0;i<5;++i){snprintf(text,sizeof(text),"%s : %u %%",names[i],settings.volume[i]);m_labels[i]=upload_centered_label(font,text,680);}
 snprintf(text,sizeof(text),"%s : %d x %d",vita_text("R\xe9solution","Resolution"),settings.width(),settings.height());m_labels[5]=upload_centered_label(font,text,680);
 const char*aa[]={vita_text("Sans","Off"),"FXAA","MSAA 2x","MSAA 4x"};
 snprintf(text,sizeof(text),"%s : %s",vita_text("Anticr\xe9nelage","Anti-aliasing"),aa[settings.aa]);m_labels[6]=upload_centered_label(font,text,680);
 snprintf(text,sizeof(text),"%s : %s",vita_text("Profondeur de champ","Depth of field"),settings.dof?vita_text("Oui","On"):vita_text("Non","Off"));m_labels[7]=upload_centered_label(font,text,680);
 snprintf(text,sizeof(text),"%s : %s",vita_text("Langue des textes","Text language"),settings.language?"English":"Fran\xe7" "ais");m_labels[8]=upload_centered_label(font,text,680);
 snprintf(text,sizeof(text),"%s : %s",vita_text("Indications des touches","Interaction hints"),settings.hints?vita_text("Oui","On"):vita_text("Non","Off"));m_labels[9]=upload_centered_label(font,text,680);
 snprintf(text,sizeof(text),"%s : %s",vita_text("Inverser l'axe vertical","Invert vertical look"),settings.invert_y?vita_text("Oui","On"):vita_text("Non","Off"));m_labels[10]=upload_centered_label(font,text,680);
 snprintf(text,sizeof(text),"%s : %u %%",vita_text("Sensibilit\xe9","Sensitivity"),settings.sensitivity);m_labels[11]=upload_centered_label(font,text,680);
 snprintf(text,sizeof(text),"%s : %u %%",vita_text("Taille du HUD","HUD size"),settings.hud_scale);m_labels[12]=upload_centered_label(font,text,680);
 TextImage chart(1200,420);
 const char*fr[]={"Stick gauche : se d\xe9placer","Stick droit : regarder","X : sauter","Carr\xe9 : utiliser / parler","Carr\xe9 maintenu : recharger","L : viser    R : tirer / lancer","Triangle : changer d'arme",
 "Rond : accroupi / maintenu : bas","Dos tactile / L3 : sprint","Select : PDA","Select maintenu : inventaire","Start : pause / maintenu : objectif",
 "Inventaire : directions, Carr\xe9 utiliser","Commerce : Carr\xe9 proposer, X retirer"};
 const char*en[]={"Left stick: move","Right stick: look","X: jump","Square: use / talk","Hold Square: reload","L: aim    R: fire / throw","Triangle: cycle weapon",
 "Circle: crouch / hold: low crouch","Rear touch / L3: sprint","Select: PDA","Hold Select: inventory","Start: pause / hold: objective",
 "Inventory: directions, Square use","Trade: Square offer, X withdraw"};
 for(unsigned i=0;i<14;++i)chart.line(font,vita_language?en[i]:fr[i],i<7?12:610,28+(i%7)*52,i<7?596:1190);
 m_controls=chart.upload(true);
 sceFontClose(font);sceFontDoneLib(library);
 return m_controls&&m_title&&m_hint&&m_restart&&std::all_of(m_labels.begin(),m_labels.end(),[](GLuint t){return t!=0;})&&
 std::all_of(m_tabs.begin(),m_tabs.end(),[](GLuint t){return t!=0;});
}
void VitaOptionsMenu::draw(VitaGLDevice& graphics,const VitaMainMenu& menu,unsigned choice,bool present)const
{
 graphics.begin_frame(.035f,.04f,.035f);m_canvas.begin();
 const float white[]={1,1,1,1},gold[]={.98f,.89f,.69f,1},dark[]={.025f,.028f,.025f,.94f},uv[]={0,0,1,1};
 m_canvas.quad(background.handle()?background.handle():menu.background.handle(),uv,{0,0,1,.75f},white);
 m_canvas.quad(frame.handle()?frame.handle():m_white,{.17f,.09f,.79f,.86f},uv,frame.handle()?white:dark);
 m_canvas.quad(m_white,{.21f,.235f,.72f,.59f},uv,dark);
 m_canvas.quad(m_title,{.26f,.105f,.56f,.055f},uv,gold,1);
 for(unsigned i=0;i<4;++i){
  const float u[]={0,(i==tab?202.f:334.f)/1024,114.f/1024,44.f/1024};
  m_canvas.quad(tabs.handle()?tabs.handle():m_white,{.215f+i*.175f,.17f,.174f,.055f},tabs.handle()?u:uv,i==tab?gold:white);
  m_canvas.quad(m_tabs[i],{.215f+i*.175f,.176f,.174f,.045f},uv,i==tab?gold:white,1);
 }
 for(unsigned i=0;i<rows();++i){
  if(i==choice){const float shade[]={.30f,.25f,.13f,.8f};m_canvas.quad(m_white,{.23f,(140.f+i*40)/544,.67f,36.f/544},uv,shade);}
  m_canvas.quad(m_labels[setting_row(i)],{.23f,(142.f+i*40)/544,.67f,32.f/544},uv,i==choice?gold:white,1);
 }
 if(tab==3)m_canvas.quad(m_controls,{.23f,.435f,.67f,.38f},uv,white,1);
 m_canvas.quad(m_restart,{.22f,.84f,.70f,.045f},uv,gold,1);
 m_canvas.quad(m_hint,{.055f,.94f,.89f,.05f},uv,white,1);
 m_canvas.end();if(present)graphics.present();
}
void VitaOptionsMenu::release()
{
 glDeleteTextures(m_labels.size(),m_labels.data());m_labels.fill(0);
 glDeleteTextures(m_tabs.size(),m_tabs.data());m_tabs.fill(0);
 for(GLuint* t:{&m_white,&m_title,&m_hint,&m_restart,&m_controls}){if(*t)glDeleteTextures(1,t);*t=0;}
 frame.release();background.release();tabs.release();m_canvas.release();
}


bool VitaSaveUI::initialize(bool saving)
{
 m_saving=saving;if(!m_canvas.initialize())return false;
 SceFontLibHandle library{};const auto font=open_ui_font(library);if(!font)return false;
 m_title=upload_centered_label(font,saving?vita_text("Sauvegarder la partie","Save game"):vita_text("Charger une partie","Load game"),500);
 m_hint=upload_centered_label(font,vita_text("Directions : choisir    Croix : valider    Carr\xe9 : supprimer    Rond : retour",
 "Directions: choose    X: confirm    Square: delete    Circle: back"),1000);
 m_overwrite=upload_centered_label(font,vita_text("Remplacer ? Croix : confirmer / Rond : annuler","Overwrite? X: confirm / Circle: cancel"),650);
 m_delete=upload_centered_label(font,vita_text("Supprimer ? Croix : confirmer / Rond : annuler","Delete? X: confirm / Circle: cancel"),650);
 m_invalid=upload_centered_label(font,vita_text("Sauvegarde illisible","Invalid save"),500);
 m_empty=upload_centered_label(font,vita_text("Emplacement vide","Empty slot"),500);
 const char*buttons[]={saving?vita_text("Sauvegarder","Save"):vita_text("Charger","Load"),vita_text("Supprimer","Delete"),vita_text("Annuler","Cancel")};
 for(unsigned i=0;i<3;++i)m_buttons[i]=upload_centered_label(font,buttons[i],200);
 sceFontClose(font);sceFontDoneLib(library);
 return m_title&&m_hint&&m_empty&&m_overwrite&&m_delete&&m_invalid&&m_buttons[0]&&m_buttons[1]&&m_buttons[2];
}
bool VitaSaveUI::refresh(const std::array<VitaSaveEntry,vita_save_slots>& entries)
{
 SceFontLibHandle library{};const auto font=open_ui_font(library);if(!font)return false;
 glDeleteTextures(m_rows.size(),m_rows.data());m_rows.fill(0);
 glDeleteTextures(m_details.size(),m_details.data());m_details.fill(0);
 glDeleteTextures(m_slot_names.size(),m_slot_names.data());m_slot_names.fill(0);
 for(unsigned i=0;i<entries.size();++i){
  const auto&e=entries[i];char text[200],date[40]="";
  if(e.valid){const time_t stamp=time_t(e.saved_at);const auto*calendar=std::localtime(&stamp);
   if(calendar)std::strftime(date,sizeof(date),"%d/%m/%Y %H:%M",calendar);}
  std::snprintf(text,sizeof(text),"%s %02u   %s",i<vita_manual_slots?vita_text("Manuelle","Manual"):vita_text("Auto","Auto"),
   i<vita_manual_slots?i+1:i-vita_manual_slots+1,e.valid?date:e.exists?vita_text("Illisible","Invalid"):vita_text("Vide","Empty"));
  char slot_name[40];std::snprintf(slot_name,sizeof(slot_name),"%s %02u",i<vita_manual_slots?vita_text("Manuelle","Manual"):vita_text("Auto","Auto"),
   i<vita_manual_slots?i+1:i-vita_manual_slots+1);
  m_slot_names[i]=upload_centered_label(font,slot_name,257);
  TextImage row(640,32);row.line(font,text,2,24,635);m_rows[i]=row.upload(true);
  const char*objective=e.story?(e.zone==0?vita_text("Rejoindre la D\xe9" "charge","Reach the Garbage"):
   e.zone==1?(e.story>=3?vita_text("Rejoindre Agroprom","Reach Agroprom"):e.story>=2?vita_text("Parler \xe0 Seriy","Talk to Seriy"):vita_text("Aider Seriy","Help Seriy")):
   e.story>=5?vita_text("Trouver la planque de Strelok","Find Strelok\'s stash"):e.story>=4?vita_text("Parler \xe0 la Taupe","Talk to Mole"):vita_text("Aider la Taupe","Help Mole")):e.flash==2?vita_text("Retour chez Sidorovitch termin\xe9","Returned to Sidorovich"):
   e.flags&SaveFarmCleared?vita_text("Ramener la cl\xe9 USB","Return the flash drive"):
   e.flags&SaveScoutMet?vita_text("Aider Allegro","Help Nimble"):
   e.flags&SaveWolfMission?vita_text("Rencontrer les hommes du Loup","Meet Wolf's men"):
   e.flags&SaveDoorUnlocked?vita_text("Parler au Loup","Talk to Wolf"):vita_text("Parler \xe0 Sidorovitch","Talk to Sidorovich");
  TextImage details(340,150);
  details.line(font,e.valid?vita_text(vita_campaign_zones[e.zone].french,vita_campaign_zones[e.zone].english):e.exists?vita_text("Sauvegarde illisible","Invalid save"):vita_text("Emplacement vide","Empty slot"),6,28,328);
  if(e.valid){
   details.line(font,objective,6,60,328);
   std::snprintf(text,sizeof(text),"%s %u h %02u min",vita_text("Dur\xe9" "e :","Played:"),e.play_seconds/3600,(e.play_seconds/60)%60);
   details.line(font,text,6,120,328);
  }
  m_details[i]=details.upload(true);
 }
 sceFontClose(font);sceFontDoneLib(library);
 return std::all_of(m_slot_names.begin(),m_slot_names.end(),[](GLuint t){return t!=0;})&&
  std::all_of(m_rows.begin(),m_rows.end(),[](GLuint t){return t!=0;})&&
  std::all_of(m_details.begin(),m_details.end(),[](GLuint t){return t!=0;});
}
void VitaSaveUI::draw(VitaGLDevice& graphics,const VitaMainMenu& menu,unsigned selected,unsigned confirmation)const
{
 graphics.begin_frame(.035f,.04f,.035f);m_canvas.begin();
 const float uv[]={0,0,1,1},white[]={1,1,1,1},gold[]={.94f,.79f,.52f,1},green[]={.55f,1,.45f,1};
 // ui_mm_save_dlg / ui_mm_load_dlg: original 1024x768 canvas.
 auto q=[&](GLuint texture,float x,float y,float w,float h,const float(&src)[4],const float(&color)[4],float mask=0){
  m_canvas.quad(texture,{x/1024,y/768,w/1024,h/768},src,color,mask);};
 const GLuint background=artwork.handle()?artwork.handle():menu.background.handle();
 q(menu.background.handle(),0,0,1024,768,{0,0,1,.75f},white);
 q(background,0,0,1024,768,{0,0,1,.75f},white);
 q(common.handle(),415,168,560,460,{164.f/1024,232.f/1024,566.f/1024,461.f/1024},white);
 q(m_title,480,178,440,30,uv,gold,1);
 // The original list frame is a nine-slice, never a stretched corner.
 const float list_y=m_saving?264:389,list_h=m_saving?307:200;
 const float xs[]={480,488,921,929},ys[]={list_y,list_y+8,list_y+list_h-8,list_y+list_h};
 const int tx[]={117,149,181},ty[]={45,77,109};
 for(unsigned y=0;y<3;++y)for(unsigned x=0;x<3;++x)
  q(common.handle(),xs[x],ys[y],xs[x+1]-xs[x],ys[y+1]-ys[y],
   {tx[x]/1024.f,ty[y]/1024.f,32.f/1024,32.f/1024},white);
 if(!m_saving){
  q(artwork.handle(),462,223,497,171,{0,.75f,497.f/1024,171.f/1024},white);
  // Original picture rectangle: a fixed Cordon overview, not a fabricated save screenshot.
  if(preview.handle())q(preview.handle(),520,257,128,96,uv,white);
  q(m_details[selected],701,235,244,132,uv,gold,1);
 }
 const unsigned rows=m_saving?vita_manual_slots:vita_save_slots;
 const float step=m_saving?38.f:20.f;
 for(unsigned i=0;i<rows;++i)
  q(m_rows[i],490,list_y+9+i*step,420,25,uv,i==selected?green:gold,1);
 if(m_saving)q(m_slot_names[selected],480,223,257,23,uv,gold,1);
 for(unsigned i=0;i<3;++i){
  q(common.handle(),480+i*156,595,157,48,{0,470.f/1024,157.f/1024,48.f/1024},white);
  q(m_buttons[i],484+i*156,602,146,28,uv,gold,1);
 }
 q(confirmation==1?m_overwrite:confirmation==2?m_delete:confirmation==3?m_invalid:m_hint,
  confirmation?320:15,666,confirmation?650:994,38,uv,gold,1);
 m_canvas.end();graphics.present();
}
void VitaSaveUI::release()
{
 glDeleteTextures(m_rows.size(),m_rows.data());m_rows.fill(0);
 glDeleteTextures(m_details.size(),m_details.data());m_details.fill(0);
 glDeleteTextures(m_slot_names.size(),m_slot_names.data());m_slot_names.fill(0);
 glDeleteTextures(m_buttons.size(),m_buttons.data());m_buttons.fill(0);
 for(GLuint*t:{&m_title,&m_hint,&m_empty,&m_overwrite,&m_delete,&m_invalid}){if(*t)glDeleteTextures(1,t);*t=0;}
 artwork.release();common.release();preview.release();m_canvas.release();
}

bool VitaTraderUI::set_campaign_page(const std::string&name,const std::string&text)
{
 SceFontLibHandle library{};const auto font=open_ui_font(library);if(!font)return false;
 for(GLuint*t:{&m_campaign_dialog,&m_campaign_name,&m_campaign_prompt}){if(*t)glDeleteTextures(1,t);*t=0;}
 if(!m_zone_prompts[0]){
  for(unsigned i=0;i<4;++i){
   const auto&zone=vita_campaign_zones[vita_zone_exits[i].to];
   const std::string prompt=std::string(vita_text("Carr\xe9 : aller vers ","Square: travel to "))+vita_text(zone.french,zone.english);
   TextImage label(prompt_width,prompt_height);label.line(font,prompt,12,28,380);m_zone_prompts[i]=label.upload(true);
  }
  TextImage locked(prompt_width,prompt_height);locked.line(font,vita_text("Terminez votre objectif actuel.","Complete your current objective."),12,28,380);
  m_zone_locked=locked.upload(true);
 }
 TextImage body(800,360);body.line(font,text,16,32,780);m_campaign_dialog=body.upload(true);
 m_campaign_name=upload_centered_label(font,name.c_str(),250);
 TextImage prompt(prompt_width,prompt_height);prompt.line(font,vita_text("Carr\xe9 : parler","Square: talk"),19,28,374);
 m_campaign_prompt=prompt.upload(true);
 sceFontClose(font);sceFontDoneLib(library);return m_campaign_dialog&&m_campaign_name&&m_campaign_prompt;
}
bool VitaTraderUI::set_campaign_objective(const char*text)
{
 SceFontLibHandle library{};const auto font=open_ui_font(library);if(!font)return false;
 if(m_campaign_objective)glDeleteTextures(1,&m_campaign_objective);
 TextImage title(prompt_width,prompt_height);title.line(font,text,19,28,374);m_campaign_objective=title.upload(true);
 sceFontClose(font);sceFontDoneLib(library);return m_campaign_objective!=0;
}
void VitaTraderUI::draw_campaign(bool open)const
{
 m_campaign_active=true;m_face=0;
 draw_panel(open?m_campaign_dialog:m_campaign_prompt,open?360:0,-1,-1,false,true);
 m_campaign_active=false;
}
void VitaTraderUI::draw_campaign_objective()const{
 draw_panel(m_campaign_objective,0,-1,-1,false,true,24.f);
}

void VitaTraderUI::draw_zone_exit(unsigned exit,bool allowed)const{
 if(exit<m_zone_prompts.size())draw_panel(allowed?m_zone_prompts[exit]:m_zone_locked,0,-1,-1,false,true,398.f);
}

void VitaTraderUI::draw_campaign_target()const{
 const float uv[]={0,0,1,1},green[]={.55f,1,.3f,1};
 m_canvas.begin();m_canvas.quad(m_campaign_name,{.41f,.55f,.18f,.045f},uv,green,1);m_canvas.end();
}
