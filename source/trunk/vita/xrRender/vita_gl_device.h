#pragma once

#include <vitaGL.h>

class VitaGLDevice
{
public:
	VitaGLDevice();

	bool initialize(int width = 960, int height = 544, int legacy_pool_size = 0, unsigned msaa = 0);
	void begin_frame(float red, float green, float blue, float alpha = 1.0f);
	void present(bool has_commondialog = false);
	bool initialized() const;
	bool set_resolution(int width,int height);
	int width()const{return m_width;} int height()const{return m_height;}
	unsigned msaa()const{return m_msaa;}

private:
	bool m_initialized;
	int m_width;
	int m_height;
	int m_pending_width=0,m_pending_height=0;
	unsigned m_msaa=0;
};
