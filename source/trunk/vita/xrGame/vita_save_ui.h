#pragma once
#include "vita_main_menu.h"
#include "vita_save.h"

class VitaSaveUI
{
public:
 ~VitaSaveUI(){release();}
 bool initialize(bool saving);
 bool refresh(const std::array<VitaSaveEntry,vita_save_slots>& entries);
 void draw(VitaGLDevice& graphics,const VitaMainMenu& menu,unsigned selected,unsigned confirmation)const;
 void release();
 VitaGLTexture artwork,common;
 std::array<VitaGLTexture,vita_zone_count> previews;
 const std::array<VitaSaveEntry,vita_save_slots>& entries()const{return m_entries;}
 unsigned refreshed_slots()const{return m_refreshed_slots;}
private:
 VitaUiCanvas m_canvas;
 bool m_saving=false,m_initialized=false;
 std::array<GLuint,2> m_titles{},m_actions{};
 std::array<VitaSaveEntry,vita_save_slots> m_entries{};
 std::array<bool,vita_save_slots> m_entry_ready{};
 unsigned m_refreshed_slots=0;
 GLuint m_title=0,m_hint=0,m_empty=0,m_overwrite=0,m_delete=0,m_invalid=0;
 std::array<GLuint,3> m_buttons{};
 std::array<GLuint,vita_save_slots> m_rows{},m_details{},m_slot_names{};
};
