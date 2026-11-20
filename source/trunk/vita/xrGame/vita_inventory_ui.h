#pragma once

#include "vita_gl_texture.h"
#include "vita_loot_trade.h"
#include "vita_ui_canvas.h"

#include <array>
#include <string>

class VitaInventoryUI
{
public:
	~VitaInventoryUI();
	bool initialize();
	void draw(const VitaInventory& inventory, unsigned health) const;
	void draw_exchange(const VitaInventory&,const VitaLootContainer*,const VitaTraderStock&,
        const VitaTradeBasket&,bool buying,unsigned selection,unsigned multiplier,VitaTradeResult result) const;
    void draw_hint(unsigned index) const;
    void scroll_description(float pixels);
	void reset_scroll() { m_scroll = 0.f; }
	void draw_weapon_ammo(const VitaInventory& inventory, bool reloading,float hud_scale=1.f) const;
	void draw_damage(float strength, bool dead) const;
    void draw_scope()const;
    VitaGLTexture scope;
	void release();

	GLuint hud_art=0;
 GLuint merchant_portrait=0;
    VitaGLTexture actor_portrait;
    VitaGLTexture trade_art;
    VitaGLTexture panel;
	VitaGLTexture slots;
	VitaGLTexture portrait;
	VitaGLTexture gauges;
	std::array<VitaGLTexture, static_cast<size_t>(VitaInventoryItem::Count)> icons;
	std::array<std::string, static_cast<size_t>(VitaInventoryItem::Count)> item_names{
		"PMm", "Munitions 9x18 mm", "Couteau", "Cl\xe9 USB d'Allegro", "Trousse de soins", "Bandage", "Pain", "Saucisson", "Conserve", "Boisson", "Veste novice", "Tenue de bandit"};
	std::array<std::string, static_cast<size_t>(VitaInventoryItem::Count)> item_details{
		"Pistolet semi-automatique utilisant des cartouches de 9x18 mm.",
		"Munitions pour le PM. Une bo\xee" "te contient 20 cartouches.",
		"Couteau de chasse.", "Remise par Allegro. Rapportez-la \xe0 Sidorovitch.",
        "Soigne 60 points de vie et r\xe9" "duit le saignement.",
        "Soigne 10 points de vie et arr\xea" "te le saignement.",
        "Nourrit et soigne 5 points de vie.", "Nourrit et soigne 10 points de vie.",
        "Nourrit et soigne 15 points de vie.", "Restaure l'endurance.",
        "Veste de novice. Protection balistique de base : 10%.",
        "Tenue de bandit. Protection balistique de base : 15%."};

private:
	float number(int value, float x, float y, float scale = 1.f) const;
	void decimal(float value, unsigned precision, float x, float y) const;
	VitaUiCanvas m_canvas;
	VitaGLTexture m_white;
	std::array<GLuint, static_cast<size_t>(VitaInventoryItem::Count)> m_descriptions{}, m_names{};
	std::array<float, static_cast<size_t>(VitaInventoryItem::Count)> m_description_heights{};
    std::array<float,vita_item_count> m_name_widths{};
	std::array<GLuint, 10> m_digits{};
	std::array<GLuint,3> m_weapon_names{};
    std::array<GLuint,vita_item_count> m_ammo_labels{};
 GLuint m_ammo_slash=0;
 GLuint m_pm = 0, m_reload = 0, m_death = 0;
	GLuint m_percent=0;
    GLuint m_headers = 0, m_footer = 0, m_empty = 0, m_statistics = 0;
	GLuint m_outfit_labels = 0, m_outfit_values = 0, m_kg = 0, m_ru = 0, m_dot = 0, m_condition = 0;
	std::array<GLuint, 3> m_exchange_columns{};
    std::array<GLuint, 8> m_exchange_labels{};
    std::array<GLuint, 8> m_exchange_results{};
    std::array<GLuint, 4> m_hints{};
    float m_scroll = 0.f;
	mutable float m_scroll_limit = 0.f;
};
