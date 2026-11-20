#pragma once

#include <vitaGL.h>

#include <cstddef>
#include <cstdint>
#include <vector>

struct VitaDDSInfo
{
	uint32_t mip_level;
	uint32_t width;
	uint32_t height;
	uint32_t fourcc;
	uint32_t minimum[4];
	uint32_t maximum[4];
	uint32_t mean[4];
};

struct VitaDDSTiming
{
	uint64_t decode_us;
	uint64_t upload_us;
};

struct VitaDecodedDDS
{
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t mip = 0;
	uint32_t fourcc = 0;
	std::vector<uint8_t> rgba;
};

bool vita_decode_dds(const void* data, size_t data_size, size_t memory_budget,
	uint32_t max_dimension, VitaDecodedDDS& image, VitaDDSTiming* timing = nullptr,
	size_t* required_bytes = nullptr);

struct VitaDDSRegion
{
	uint32_t source_dimension;
	uint32_t x;
	uint32_t y;
	uint32_t width;
	uint32_t height;
	uint32_t source_height; // Zero uses source_dimension for both axes.
};

class VitaGLTexture
{
public:
	VitaGLTexture();
	VitaGLTexture(const VitaGLTexture&) = delete;
	VitaGLTexture& operator=(const VitaGLTexture&) = delete;
	~VitaGLTexture();

	bool initialize_dds(const void* data, size_t data_size, size_t memory_budget, uint32_t max_dimension = 128, VitaDDSInfo* info = nullptr,
		VitaDDSTiming* timing = nullptr);
	bool initialize_dds_region(const void* data, size_t data_size, size_t memory_budget, const VitaDDSRegion& region);
	bool initialize_dxt5_region(const void* data, size_t data_size, size_t memory_budget, const VitaDDSRegion& region)
	{
		return initialize_dds_region(data, data_size, memory_budget, region);
	}
	bool upload_dds(const VitaDecodedDDS& image, VitaDDSTiming* timing = nullptr);
	void release();
	GLuint handle() const;
	size_t uploaded_size() const;

private:
	GLuint m_texture;
	size_t m_uploaded_size;
};
