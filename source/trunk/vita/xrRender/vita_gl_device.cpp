#include "vita_gl_device.h"

VitaGLDevice::VitaGLDevice() : m_initialized(false), m_width(0), m_height(0)
{
}

bool VitaGLDevice::initialize(int width, int height, int legacy_pool_size, unsigned msaa)
{
	if (m_initialized || width <= 0 || height <= 0 || legacy_pool_size < 0)
		return false;

	// vitaGL returns GL_TRUE when it had to fall back to a smaller resolution;
	// GL_FALSE means the requested resolution was accepted unchanged.
	(void)vglInitExtended(legacy_pool_size, width, height, 0, msaa==4?SCE_GXM_MULTISAMPLE_4X:msaa==2?SCE_GXM_MULTISAMPLE_2X:SCE_GXM_MULTISAMPLE_NONE);
	m_msaa=msaa;

	m_width = width;
	m_height = height;
	m_initialized = true;
	glViewport(0, 0, m_width, m_height);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	return true;
}

void VitaGLDevice::begin_frame(float red, float green, float blue, float alpha)
{
	if (!m_initialized)
		return;

	glViewport(0, 0, m_width, m_height);
	glClearColor(red, green, blue, alpha);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void VitaGLDevice::present(bool has_commondialog)
{
	if (m_initialized)
	{
		vglSwapBuffers(has_commondialog ? GL_TRUE : GL_FALSE);
		if(m_pending_width){m_width=m_pending_width;m_height=m_pending_height;m_pending_width=m_pending_height=0;}
	}
}

bool VitaGLDevice::initialized() const
{
	return m_initialized;
}

bool VitaGLDevice::set_resolution(int width,int height)
{
	if(width==m_width&&height==m_height)return true;
	if(!vglSwapResolution(width,height))return false;
	m_pending_width=width;m_pending_height=height;return true;
}
