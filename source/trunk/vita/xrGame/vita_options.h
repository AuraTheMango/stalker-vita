
#pragma once
#include "vita_settings.h"
#include "vita_main_menu.h"
class VitaOptionsMenu
{
public:
	~VitaOptionsMenu(){release();}
	bool initialize(const VitaSettings& settings);
	bool refresh(const VitaSettings& settings);
    unsigned tab=0;
    unsigned rows()const{return tab==0?3:tab==1?5:tab==2?3:2;}
    unsigned setting_row(unsigned choice)const{return tab==0?5+choice:tab==1?choice:tab==2?(choice==2?12:8+choice):10+choice;}
    VitaGLTexture frame, background, tabs;
	void draw(VitaGLDevice& graphics,const VitaMainMenu& menu,unsigned choice,bool present=false)const;
	void release();
private:
	VitaUiCanvas m_canvas;
	std::array<GLuint,13> m_labels{};
    std::array<GLuint,4> m_tabs{};
    GLuint m_controls=0;
	GLuint m_white=0;
	GLuint m_title=0,m_hint=0,m_restart=0;
};
