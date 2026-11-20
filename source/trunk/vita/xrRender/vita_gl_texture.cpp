#include "vita_gl_texture.h"

#include <psp2/kernel/processmgr.h>

#include <cstring>
#include <limits>
#include <vector>

namespace
{
	uint32_t read_u32(const uint8_t* bytes)
	{
		return static_cast<uint32_t>(bytes[0]) |
			(static_cast<uint32_t>(bytes[1]) << 8) |
			(static_cast<uint32_t>(bytes[2]) << 16) |
			(static_cast<uint32_t>(bytes[3]) << 24);
	}

	bool level_size(uint32_t width, uint32_t height, size_t block_bytes, size_t& result)
	{
		const size_t blocks_wide = (width + 3u) / 4u;
		const size_t blocks_high = (height + 3u) / 4u;
		if (!blocks_wide || !blocks_high ||
			blocks_wide > (std::numeric_limits<size_t>::max)() / blocks_high ||
			blocks_wide * blocks_high > (std::numeric_limits<size_t>::max)() / block_bytes)
			return false;
		result = blocks_wide * blocks_high * block_bytes;
		return true;
	}

	uint8_t expand5(uint32_t value) { return static_cast<uint8_t>((value << 3) | (value >> 2)); }
	uint8_t expand6(uint32_t value) { return static_cast<uint8_t>((value << 2) | (value >> 4)); }

	void decode_color_block(const uint8_t* block, bool allow_transparency, uint8_t colors[4][4])
	{
		const uint16_t c0 = static_cast<uint16_t>(block[0] | (block[1] << 8));
		const uint16_t c1 = static_cast<uint16_t>(block[2] | (block[3] << 8));
		const uint8_t r0 = expand5((c0 >> 11) & 31), g0 = expand6((c0 >> 5) & 63), b0 = expand5(c0 & 31);
		const uint8_t r1 = expand5((c1 >> 11) & 31), g1 = expand6((c1 >> 5) & 63), b1 = expand5(c1 & 31);
		colors[0][0] = r0; colors[0][1] = g0; colors[0][2] = b0; colors[0][3] = 255;
		colors[1][0] = r1; colors[1][1] = g1; colors[1][2] = b1; colors[1][3] = 255;
		if (c0 > c1 || !allow_transparency)
		{
			for (int channel = 0; channel < 3; ++channel)
			{
				colors[2][channel] = static_cast<uint8_t>((2 * colors[0][channel] + colors[1][channel]) / 3);
				colors[3][channel] = static_cast<uint8_t>((colors[0][channel] + 2 * colors[1][channel]) / 3);
			}
			colors[2][3] = colors[3][3] = 255;
		}
		else
		{
			for (int channel = 0; channel < 3; ++channel)
				colors[2][channel] = static_cast<uint8_t>((colors[0][channel] + colors[1][channel]) / 2);
			colors[2][3] = 255;
			colors[3][0] = colors[3][1] = colors[3][2] = 0;
			colors[3][3] = 0;
		}
	}

	bool decode_dxt_level(const uint8_t* source, uint32_t width, uint32_t height,
		uint32_t output_width, uint32_t output_height, uint32_t format,
		std::vector<uint8_t>& output)
	{
		if (!width || !height || !output_width || !output_height ||
			static_cast<size_t>(output_width) * output_height > (std::numeric_limits<size_t>::max)() / 4)
			return false;
		output.assign(static_cast<size_t>(output_width) * output_height * 4, 0);
		const size_t blocks_wide = (width + 3u) / 4u;
		const size_t blocks_high = (height + 3u) / 4u;
		const size_t block_bytes = format == 0x31545844u ? 8 : 16;
		for (size_t by = 0; by < blocks_high; ++by)
		for (size_t bx = 0; bx < blocks_wide; ++bx)
		{
			const uint8_t* block = source + (by * blocks_wide + bx) * block_bytes;
			const uint8_t* color_block = block + (format == 0x31545844u ? 0 : 8);
			uint8_t colors[4][4];
			decode_color_block(color_block, format == 0x31545844u, colors);
			const uint32_t color_bits = read_u32(color_block + 4);
			uint64_t alpha_bits = 0;
			uint8_t alpha_table[8] = {};
			if (format == 0x33545844u)
			{
				for (int i = 0; i < 8; ++i) alpha_bits |= static_cast<uint64_t>(block[i]) << (i * 8);
			}
			else if (format == 0x35545844u)
			{
				alpha_table[0] = block[0]; alpha_table[1] = block[1];
				if (alpha_table[0] > alpha_table[1])
					for (int i = 1; i <= 6; ++i) alpha_table[i + 1] = static_cast<uint8_t>(((7 - i) * alpha_table[0] + i * alpha_table[1]) / 7);
				else
				{
					for (int i = 1; i <= 4; ++i) alpha_table[i + 1] = static_cast<uint8_t>(((5 - i) * alpha_table[0] + i * alpha_table[1]) / 5);
					alpha_table[6] = 0; alpha_table[7] = 255;
				}
				for (int i = 0; i < 6; ++i) alpha_bits |= static_cast<uint64_t>(block[2 + i]) << (i * 8);
			}
			for (uint32_t py = 0; py < 4; ++py)
			for (uint32_t px = 0; px < 4; ++px)
			{
				const uint32_t x = static_cast<uint32_t>(bx * 4 + px), y = static_cast<uint32_t>(by * 4 + py);
				if (x >= width || y >= height) continue;
				const uint32_t pixel = py * 4 + px;
				const uint32_t color_index = (color_bits >> (pixel * 2)) & 3u;
				const uint32_t dst_x = static_cast<uint32_t>((static_cast<uint64_t>(x) * output_width) / width);
				const uint32_t dst_y = static_cast<uint32_t>((static_cast<uint64_t>(y) * output_height) / height);
				uint8_t* dst = &output[(static_cast<size_t>(dst_y) * output_width + dst_x) * 4];
				for (int channel = 0; channel < 4; ++channel) dst[channel] = colors[color_index][channel];
				if (format == 0x33545844u) dst[3] = static_cast<uint8_t>(((block[pixel / 2] >> ((pixel & 1) * 4)) & 15) * 17);
				else if (format == 0x35545844u) dst[3] = alpha_table[(alpha_bits >> (pixel * 3)) & 7];
			}
		}
		return true;
	}
}

VitaGLTexture::VitaGLTexture() : m_texture(0), m_uploaded_size(0)
{
}

VitaGLTexture::~VitaGLTexture()
{
	release();
}

bool vita_decode_dds(const void* data, size_t data_size, size_t memory_budget,
	uint32_t max_dimension, VitaDecodedDDS& image, VitaDDSTiming* timing, size_t* required_bytes)
{
	image.rgba.clear();
	if (required_bytes)
		*required_bytes = 0;
	constexpr size_t dds_header_size = 128;
	if (!data || data_size < dds_header_size || !max_dimension || max_dimension > 4096)
		return false;
	const uint8_t* bytes = static_cast<const uint8_t*>(data);
	if (read_u32(bytes) != 0x20534444u || read_u32(bytes + 4) != 124u ||
		read_u32(bytes + 76) != 32u || (read_u32(bytes + 80) & 0x4u) == 0)
		return false;

	const uint32_t height = read_u32(bytes + 12);
	const uint32_t width = read_u32(bytes + 16);
	const uint32_t mip_count = (read_u32(bytes + 8) & 0x20000u) && read_u32(bytes + 28)
		? read_u32(bytes + 28) : 1u;
	const uint32_t fourcc = read_u32(bytes + 84);
	size_t block_bytes = 0;
	if (fourcc == 0x31545844u) // DXT1
	{
		block_bytes = 8;
	}
	else if (fourcc == 0x33545844u) // DXT3
	{
		block_bytes = 16;
	}
	else if (fourcc == 0x35545844u) // DXT5
	{
		block_bytes = 16;
	}
	else
		return false;
	if (!width || !height || width > 4096 || height > 4096 || mip_count > 16)
		return false;

	size_t total_size = 0;
	uint32_t mip_width = width;
	uint32_t mip_height = height;
	for (uint32_t level = 0; level < mip_count; ++level)
	{
		size_t current_size = 0;
		if (!level_size(mip_width, mip_height, block_bytes, current_size) ||
			current_size > data_size - dds_header_size - total_size)
			return false;
		total_size += current_size;
		if (mip_width > 1) mip_width >>= 1;
		if (mip_height > 1) mip_height >>= 1;
	}
	if (total_size == 0)
		return false;
	// Select a small existing mip level so the CPU decode buffer and GPU texture
	// remain bounded. The original DDS data stays compressed in the archive.
	uint32_t selected_width = width, selected_height = height;
	size_t selected_offset = dds_header_size;
	uint32_t selected_mip = 0;
	for (; selected_mip < mip_count; ++selected_mip)
	{
		size_t current_size = 0;
		if (!level_size(selected_width, selected_height, block_bytes, current_size)) return false;
		if ((selected_width <= max_dimension && selected_height <= max_dimension) || selected_mip + 1 == mip_count)
		{
			break;
		}
		selected_offset += current_size;
		if (selected_width > 1) selected_width >>= 1;
		if (selected_height > 1) selected_height >>= 1;
	}
	uint32_t output_width = selected_width, output_height = selected_height;
	if (output_width > max_dimension || output_height > max_dimension)
	{
		if (output_width >= output_height)
		{
			output_height = (output_height * max_dimension) / output_width;
			output_width = max_dimension;
		}
		else
		{
			output_width = (output_width * max_dimension) / output_height;
			output_height = max_dimension;
		}
		if (!output_width) output_width = 1;
		if (!output_height) output_height = 1;
	}
	const size_t rgba_total = static_cast<size_t>(output_width) * output_height * 4;
	if (required_bytes)
		*required_bytes = rgba_total;
	if (rgba_total == 0 || rgba_total > memory_budget ||
		rgba_total > static_cast<size_t>((std::numeric_limits<GLsizei>::max)()))
		return false;

	const uint64_t start = timing ? sceKernelGetProcessTimeWide() : 0;
	const bool decoded = decode_dxt_level(bytes + selected_offset, selected_width, selected_height,
		output_width, output_height, fourcc, image.rgba);
	if (timing)
		timing->decode_us = sceKernelGetProcessTimeWide() - start;
	if (!decoded)
		return false;
	image.width = output_width;
	image.height = output_height;
	image.mip = selected_mip;
	image.fourcc = fourcc;
	return true;
}

bool VitaGLTexture::upload_dds(const VitaDecodedDDS& image, VitaDDSTiming* timing)
{
	const uint64_t upload_start = timing ? sceKernelGetProcessTimeWide() : 0;
	release();
	glGenTextures(1, &m_texture);
	if (!m_texture)
		return false;
	for (int error_count = 0; error_count < 16 && glGetError() != GL_NO_ERROR; ++error_count)
	{
	}
	glBindTexture(GL_TEXTURE_2D, m_texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
		static_cast<GLsizei>(image.width), static_cast<GLsizei>(image.height), 0,
		GL_RGBA, GL_UNSIGNED_BYTE, image.rgba.data());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	const GLenum upload_error = glGetError();
	if (timing)
		timing->upload_us = sceKernelGetProcessTimeWide() - upload_start;
	if (upload_error != GL_NO_ERROR)
	{
		release();
		return false;
	}
	m_uploaded_size = image.rgba.size();
	return true;
}

bool VitaGLTexture::initialize_dds(const void* data, size_t data_size, size_t memory_budget,
	uint32_t max_dimension, VitaDDSInfo* info, VitaDDSTiming* timing)
{
	release();
	VitaDecodedDDS image;
	if (!vita_decode_dds(data, data_size, memory_budget, max_dimension, image, timing) || !upload_dds(image, timing))
		return false;
	const auto& rgba = image.rgba;
	if (info)
	{
		*info = VitaDDSInfo{image.mip, image.width, image.height, image.fourcc,
			{255, 255, 255, 255}, {}, {}};
		uint64_t sums[4] = {};
		for (size_t pixel = 0; pixel < rgba.size(); pixel += 4)
		{
			for (size_t channel = 0; channel < 4; ++channel)
			{
				const uint32_t value = rgba[pixel + channel];
				if (value < info->minimum[channel]) info->minimum[channel] = value;
				if (value > info->maximum[channel]) info->maximum[channel] = value;
				sums[channel] += value;
			}
		}
		for (size_t channel = 0; channel < 4; ++channel)
			info->mean[channel] = static_cast<uint32_t>(sums[channel] / (rgba.size() / 4));
	}
	return true;
}

bool VitaGLTexture::initialize_dds_region(const void* data, size_t data_size, size_t memory_budget,
	const VitaDDSRegion& region)
{
	constexpr size_t header_size = 128;
	const uint32_t source_height = region.source_height ? region.source_height : region.source_dimension;
	if (!data || data_size < header_size || !region.source_dimension || region.source_dimension > 4096 ||
		!region.width || !region.height || source_height > 4096 ||
		((region.source_dimension | source_height | region.x | region.y | region.width | region.height) & 3u))
		return false;
	const uint8_t* bytes = static_cast<const uint8_t*>(data);
	const uint32_t format = read_u32(bytes + 84);
	const size_t block_size = format == 0x31545844u ? 8 : 16;
	const uint32_t width = read_u32(bytes + 16);
	const uint32_t height = read_u32(bytes + 12);
	if (read_u32(bytes) != 0x20534444u || read_u32(bytes + 4) != 124u ||
		read_u32(bytes + 76) != 32u || (read_u32(bytes + 80) & 0x4u) == 0 ||
		(format != 0x31545844u && format != 0x33545844u && format != 0x35545844u) || width != region.source_dimension || height != source_height ||
		region.x >= width || region.y >= height || region.width > width - region.x || region.height > height - region.y)
		return false;
	size_t source_size = 0;
	size_t region_size = 0;
	if (!level_size(width, height, block_size, source_size) || source_size > data_size - header_size ||
		!level_size(region.width, region.height, block_size, region_size) ||
		static_cast<size_t>(region.width) * region.height * 4 > memory_budget)
		return false;

	// Reuse the normal decoder by building a single-mip DDS from whole compressed blocks.
	std::vector<uint8_t> cropped(header_size + region_size);
	memcpy(cropped.data(), bytes, header_size);
	const auto write_u32 = [&cropped](size_t offset, uint32_t value)
	{
		for (size_t byte = 0; byte < 4; ++byte)
			cropped[offset + byte] = static_cast<uint8_t>(value >> (8 * byte));
	};
	write_u32(12, region.height);
	write_u32(16, region.width);
	write_u32(20, static_cast<uint32_t>(region_size));
	write_u32(28, 1);
	const size_t row_bytes = (region.width / 4) * block_size;
	const size_t source_stride = (width / 4) * block_size;
	const uint8_t* first_row = bytes + header_size + (region.y / 4) * source_stride + (region.x / 4) * block_size;
	for (size_t row = 0; row < region.height / 4; ++row)
		memcpy(cropped.data() + header_size + row * row_bytes, first_row + row * source_stride, row_bytes);
	return initialize_dds(cropped.data(), cropped.size(), memory_budget, region.width > region.height ? region.width : region.height);
}

void VitaGLTexture::release()
{
	if (m_texture)
		glDeleteTextures(1, &m_texture);
	m_texture = 0;
	m_uploaded_size = 0;
}

GLuint VitaGLTexture::handle() const { return m_texture; }
size_t VitaGLTexture::uploaded_size() const { return m_uploaded_size; }
