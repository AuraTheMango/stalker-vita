#include "vita_texture_policy.h"
#include "vita_campaign_dialogue.h"
#include "vita_gl_device.h"
#include "vita_gl_debug_renderer.h"
#include "vita_gl_r1_buffers.h"
#include "vita_gl_texture.h"
#include "vita_gl_sky.h"
#include "vita_cform_ground.h"
#include "vita_assault_path.h"
#include "vita_assault_advance.h"
#include "vita_trader_ui.h"
#include "vita_hud.h"
#include "vita_loading_screen.h"
#include "vita_inventory_ui.h"
#include "vita_grenades.h"
#include "vita_item_catalog.h"
#include "vita_save.h"
#include "vita_zone_confirmation.h"
#include "vita_save_ui.h"
#include "vita_main_menu.h"
#include "vita_button_gesture.h"
#include "vita_player_jump.h"
#include "vita_guard_combat.h"
#include "vita_npc_battle.h"
#include "vita_options.h"
#include "vita_locale.h"
#include "vita_postprocess.h"
#include "vita_game_audio.h"
#include "vita_trader_model.h"
#include "xrCore.h"
#include "stream_reader.h"

#include <psp2/ctrl.h>
#include <psp2/appmgr.h>
#include <psp2/touch.h>
#include <psp2/gxm.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <limits>
#include <stdio.h>
#include <stdint.h>
#include <memory>
#include <string>
#include <string.h>
#include <strings.h>
#include <utility>
#include <vector>

typedef void DUMMY_STUFF(const void*, const u32&, void*);
extern XRCORE_API DUMMY_STUFF* g_temporary_stuff;

static unsigned g_zone=0;
static std::string zone_file(const char* name){return std::string(vita_campaign_zones[g_zone].directory)+"\\"+name;}
static SceUID g_boot_log = -1;
static char g_boot_log_buffer[512u * 1024u];
static std::atomic<bool> g_boot_log_buffering{false};
static std::atomic<size_t> g_boot_log_used{0};
static std::atomic<unsigned> g_boot_log_dropped{0};

struct PresentTiming
{
	SceUInt64 end_scene_us = 0;
	SceUInt64 queue_us = 0;
	SceUInt64 sema_us = 0;
};

struct VSyncRunStats
{
	unsigned frames = 0;
	unsigned long_frames = 0;
	SceUInt64 total_us = 0;
	SceUInt64 peak_us = 0;
	SceUInt64 queue_total_us = 0;
	SceUInt64 queue_peak_us = 0;
};

static PresentTiming g_present_timing;
static std::atomic<SceUID> g_measured_present_thread{-1};

template <typename Call>
static int measure_present_call(SceUInt64 PresentTiming::*elapsed, const Call& call)
{
	const SceUID thread = g_measured_present_thread.load();
	if (thread < 0 || thread != sceKernelGetThreadId())
		return call();

	const SceUInt64 start = sceKernelGetProcessTimeWide();
	const int result = call();
	g_present_timing.*elapsed += sceKernelGetProcessTimeWide() - start;
	return result;
}

extern "C" int __real_sceGxmEndScene(SceGxmContext* context,
	const SceGxmNotification* vertex, const SceGxmNotification* fragment);
extern "C" int __real_sceGxmDisplayQueueAddEntry(SceGxmSyncObject* old_buffer,
	SceGxmSyncObject* new_buffer, const void* callback_data);
extern "C" int __real_sceKernelWaitSema(SceUID semaphore, int signal, SceUInt* timeout);

extern "C" int __wrap_sceGxmEndScene(SceGxmContext* context,
	const SceGxmNotification* vertex, const SceGxmNotification* fragment)
{
	return measure_present_call(&PresentTiming::end_scene_us,
		[&] { return __real_sceGxmEndScene(context, vertex, fragment); });
}

extern "C" int __wrap_sceGxmDisplayQueueAddEntry(SceGxmSyncObject* old_buffer,
	SceGxmSyncObject* new_buffer, const void* callback_data)
{
	return measure_present_call(&PresentTiming::queue_us,
		[&] { return __real_sceGxmDisplayQueueAddEntry(old_buffer, new_buffer, callback_data); });
}

extern "C" int __wrap_sceKernelWaitSema(SceUID semaphore, int signal, SceUInt* timeout)
{
	return measure_present_call(&PresentTiming::sema_us,
		[&] { return __real_sceKernelWaitSema(semaphore, signal, timeout); });
}

struct ArchiveDecodeKey
{
	u8 alphabet_back[256];
	u32 table_seed;
	u32 table_iterations;
	u32 encrypt_seed;
	bool initialized;
};

static ArchiveDecodeKey g_archive_decode_keys[2] = {
	{{0}, 20091958, 2048, 20031955, false},
	{{0}, 6011979, 1024, 24031979, false}
};

static void write_boot_log(SceUID file, const char* message, bool newline = false)
{
	if (file < 0)
		return;

	const size_t length = strlen(message);
	if (!g_boot_log_buffering.load())
	{
		sceIoWrite(file, message, length);
		if (newline)
			sceIoWrite(file, "\n", 1);
		return;
	}

	// The renderer and the DDS worker can log through the same callback.
	const size_t bytes = length + static_cast<size_t>(newline);
	size_t offset = g_boot_log_used.load();
	do
	{
		if (bytes > sizeof(g_boot_log_buffer) - offset)
		{
			++g_boot_log_dropped;
			return;
		}
	}
	while (!g_boot_log_used.compare_exchange_weak(offset, offset + bytes));
	memcpy(g_boot_log_buffer + offset, message, length);
	if (newline)
		g_boot_log_buffer[offset + length] = '\n';
}

static void flush_boot_log(SceUID file)
{
	// Producers have stopped, so every reserved range is ready to write.
	g_boot_log_buffering.store(false);
	if (file < 0)
		return;

	const size_t used = g_boot_log_used.load();
	size_t offset = 0;
	while (offset < used)
	{
		const SceSSize written = sceIoWrite(file, g_boot_log_buffer + offset, used - offset);
		if (written <= 0)
			break;
		offset += static_cast<size_t>(written);
	}
	char line[160];
	snprintf(line, sizeof(line), "debug_player_boot_log_buffer bytes=%u written=%u dropped=%u\n",
		static_cast<unsigned>(used), static_cast<unsigned>(offset), g_boot_log_dropped.load());
	write_boot_log(file, line);
}

static void boot_log_callback(LPCSTR message)
{
	write_boot_log(g_boot_log, message, true);
}

static u32 archive_random(u32& seed, u32 range)
{
	seed = 0x08088405u * seed + 1u;
	return static_cast<u32>((static_cast<u64>(seed) * range) >> 32);
}

static void initialize_archive_decode_key(ArchiveDecodeKey& key)
{
	if (key.initialized)
		return;

	u8 alphabet[256];
	for (u32 i = 0; i < 256; ++i)
		alphabet[i] = static_cast<u8>(i);

	u32 seed = key.table_seed;
	for (u32 i = 0; i < key.table_iterations; ++i)
	{
		u32 j = archive_random(seed, 256);
		u32 k = archive_random(seed, 256);
		while (j == k)
			k = archive_random(seed, 256);
		u8 value = alphabet[j];
		alphabet[j] = alphabet[k];
		alphabet[k] = value;
	}

	for (u32 i = 0; i < 256; ++i)
		key.alphabet_back[alphabet[i]] = static_cast<u8>(i);
	key.initialized = true;
}

static u32 archive_decoded_size(const ArchiveDecodeKey& key, const u8* source)
{
	u32 seed = key.encrypt_seed;
	u8 decoded[4];
	for (u32 i = 0; i < 4; ++i)
	{
		const u8 mask = static_cast<u8>(archive_random(seed, 256));
		decoded[i] = key.alphabet_back[source[i] ^ mask];
	}
	return static_cast<u32>(decoded[0]) |
		(static_cast<u32>(decoded[1]) << 8) |
		(static_cast<u32>(decoded[2]) << 16) |
		(static_cast<u32>(decoded[3]) << 24);
}

static void decode_archive_data(const void* source, const u32& source_size, void* destination)
{
	if (!source || !destination || source_size == 0)
		return;

	const u8* input = static_cast<const u8*>(source);
	u8* output = static_cast<u8*>(destination);
	int selected = -1;
	u32 decoded_sizes[2] = {0, 0};
	const u32 max_index_size = 64u * 1024u * 1024u;

	for (u32 i = 0; i < 2; ++i)
	{
		initialize_archive_decode_key(g_archive_decode_keys[i]);
		if (source_size >= 4)
			decoded_sizes[i] = archive_decoded_size(g_archive_decode_keys[i], input);
		if (decoded_sizes[i] > 0 && decoded_sizes[i] <= max_index_size && selected < 0)
			selected = static_cast<int>(i);
	}

	Log("VITA decoder Russian candidate size", decoded_sizes[0]);
	Log("VITA decoder international candidate size", decoded_sizes[1]);
	if (selected < 0)
	{
		write_boot_log(g_boot_log, "archive_decoder_no_valid_key\n");
		return;
	}

	write_boot_log(g_boot_log, selected == 0
		? "archive_decoder_russian_key\n"
		: "archive_decoder_international_key\n");
	ArchiveDecodeKey& key = g_archive_decode_keys[selected];
	u32 seed = key.encrypt_seed;
	for (u32 i = 0; i < source_size; ++i)
	{
		const u8 encoded = input[i];
		const u8 mask = static_cast<u8>(archive_random(seed, 256));
		output[i] = key.alphabet_back[encoded ^ mask];
	}
}

static bool dump_shader_sources_if_requested()
{
	static const char* request_path = "ux0:data/stalker/dump_shaders.flag";
	SceIoStat request_stat;
	if (sceIoGetstat(request_path, &request_stat) < 0)
		return false;
	sceIoRemove(request_path);

	static const char* output_root = "ux0:data/stalker/_appdata_/shaders";
	sceIoMkdir(output_root, 0777);
	xr_vector<LPSTR>* files = FS.file_list_open("$fs_root$", FS_ListFiles);
	if (!files)
	{
		write_boot_log(g_boot_log, "shader_dump_root_list_failed\n");
		return true;
	}

	u32 dumped_count = 0;
	u32 matching_count = 0;
	for (xr_vector<LPSTR>::iterator it = files->begin(); it != files->end(); ++it)
	{
		const char* shader_path = 0;
		for (const char* path_part = *it; *path_part; ++path_part)
		{
			if (strncasecmp(path_part, "shaders", 7) == 0 &&
				(path_part[7] == '\\' || path_part[7] == '/') &&
				strncasecmp(path_part + 8, "r1", 2) == 0 &&
				(path_part[10] == '\\' || path_part[10] == '/'))
			{
				shader_path = path_part;
				break;
			}
		}
		if (!shader_path)
			continue;

		const char* extension = strrchr(*it, '.');
		if (!extension || (strcasecmp(extension, ".vs") != 0 &&
			strcasecmp(extension, ".ps") != 0 && strcasecmp(extension, ".h") != 0))
			continue;
		++matching_count;

		IReader* source = FS.r_open("$fs_root$", *it);
		if (!source)
			continue;

		char output_name[1024];
		strncpy(output_name, shader_path, sizeof(output_name) - 1);
		output_name[sizeof(output_name) - 1] = '\0';
		for (char* ch = output_name; *ch; ++ch)
			if (*ch == '\\' || *ch == '/')
				*ch = '_';

		char output_path[1536];
		const int output_length = snprintf(output_path, sizeof(output_path), "%s/%s", output_root, output_name);
		if (output_length < 0 || static_cast<size_t>(output_length) >= sizeof(output_path))
		{
			FS.r_close(source);
			continue;
		}
		const SceUID output_file = sceIoOpen(output_path,
			SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
		if (output_file >= 0)
		{
			const u8* data = static_cast<const u8*>(source->pointer());
			u32 remaining = static_cast<u32>(source->length());
			bool write_ok = true;
			while (remaining > 0)
			{
				const int written = sceIoWrite(output_file, data, remaining);
				if (written <= 0)
				{
					write_ok = false;
					break;
				}
				data += written;
				remaining -= static_cast<u32>(written);
			}
			sceIoClose(output_file);
			if (write_ok)
				++dumped_count;
		}
		FS.r_close(source);
	}
	FS.file_list_close(files);

	char summary[112];
	snprintf(summary, sizeof(summary), "shader_dump_complete matched=%u written=%u\n", matching_count, dumped_count);
	write_boot_log(g_boot_log, summary);
	return true;
}

struct VitaSlideWindow
{
	u32 offset;
	u16 triangles;
	u16 vertices;
};

static_assert(sizeof(VitaSlideWindow) == 8, "R1 slide window record size");

struct VitaTreeWindows
{
	VitaSlideWindow normal;
	VitaSlideWindow coarse;
};

static bool load_escape_level_geometry(
	VitaGLR1StaticBuffers& buffers, std::vector<VitaTreeWindows>& tree_windows, GLint position_attribute, GLint texcoord_attribute,
	GLint lightmap_texcoord_attribute)
{
	CStreamReader* level_geometry = FS.rs_open("$game_levels$", zone_file("level.geom").c_str());
	if (!level_geometry)
	{
		const auto suffix_storage=zone_file("level.geom");
        const char* level_suffix=suffix_storage.c_str();
	const size_t suffix_length = strlen(level_suffix);
		xr_vector<LPSTR>* files = FS.file_list_open("$fs_root$", FS_ListFiles);
		if (files)
		{
			for (xr_vector<LPSTR>::iterator it = files->begin(); it != files->end(); ++it)
			{
				for (const char* path_part = *it; *path_part; ++path_part)
				{
					if (strncasecmp(path_part, level_suffix, suffix_length) != 0)
						continue;
					if (path_part != *it && path_part[-1] != '\\' && path_part[-1] != '/')
						continue;

					char path_log[1100];
					const int path_log_length = snprintf(path_log, sizeof(path_log), "debug_level_geom_path=%s\n", *it);
					if (path_log_length > 0 && static_cast<size_t>(path_log_length) < sizeof(path_log))
						write_boot_log(g_boot_log, path_log);
					level_geometry = FS.rs_open("$fs_root$", *it);
					if (level_geometry)
						break;
				}
				if (level_geometry)
					break;
			}
			FS.file_list_close(files);
		}
		if (!level_geometry)
		{
			write_boot_log(g_boot_log, "debug_level_geom_missing\n");
			return false;
		}
	}

	// R1's fsL_VB and fsL_IB chunk IDs are 9 and 10.
	CStreamReader* vertex_chunk = level_geometry->open_chunk(9);
	CStreamReader* index_chunk = level_geometry->open_chunk(10);
	bool loaded = false;
	if (vertex_chunk && index_chunk && position_attribute >= 0 && texcoord_attribute >= 0 &&
		lightmap_texcoord_attribute >= 0)
	{
		const VitaGLSemanticBinding bindings[] = {
			{0, 0, static_cast<GLuint>(position_attribute)},
			{5, 0, static_cast<GLuint>(texcoord_attribute)},
			{5, 1, static_cast<GLuint>(lightmap_texcoord_attribute)}
		};
		loaded = buffers.load_chunks(vertex_chunk, index_chunk, bindings, sizeof(bindings) / sizeof(bindings[0]));
	}
	if (vertex_chunk)
		FS.r_close(vertex_chunk);
	if (index_chunk)
		FS.r_close(index_chunk);
	if (CStreamReader* windows = loaded ? level_geometry->open_chunk(11) : 0) // fsL_SWIS
	{
		const size_t item_header_size = 5 * sizeof(u32);
		if (windows->length() >= sizeof(u32))
		{
			const u32 item_count = windows->r_u32();
			if (item_count <= (windows->length() - windows->tell()) / item_header_size)
			{
				for (u32 i = 0; i < item_count && windows->length() - windows->tell() >= item_header_size; ++i)
				{
					windows->advance(4 * sizeof(u32));
					const u32 count = windows->r_u32();
					if (count > (windows->length() - windows->tell()) / sizeof(VitaSlideWindow))
						break;
					VitaTreeWindows tree = {};
					if (count)
					{
						const u32 detail_index = count / 2;
						windows->advance(static_cast<int>(detail_index * sizeof(VitaSlideWindow)));
						windows->r(&tree.normal, sizeof(tree.normal));
						if (detail_index + 1 < count)
						{
							windows->advance(static_cast<int>((count - detail_index - 2) * sizeof(VitaSlideWindow)));
							windows->r(&tree.coarse, sizeof(tree.coarse));
						}
					}
					tree_windows.push_back(tree);
				}
			}
		}
		FS.r_close(windows);
	}
	FS.r_close(level_geometry);

	if (loaded)
	{
		write_boot_log(g_boot_log, "debug_level_geom_loaded\n");
		return buffers.vertex_buffer_count() > 0 && buffers.index_buffer_count() > 0;
	}
	write_boot_log(g_boot_log, "debug_level_geom_load_failed\n");
	return false;
}

struct VitaDebugVisualRange
{
	u32 visual_id;
	bool focus_sector;
	bool bunker_passage;
	bool village_cellar;
	u16 shader_id;
	u32 vertex_buffer;
	u32 first_vertex;
	u32 vertex_count;
	u32 index_buffer;
	u32 first_index;
	u32 index_count;
	u32 coarse_first_index;
	u32 coarse_index_count;
	u32 coarse_vertex_count;
	float bounds_center[3];
	float bounds_minimum[3];
	float bounds_maximum[3];
	float bounds_radius;
	bool has_bounds;
	bool has_tree_transform;
	float tree_transform[16];
};

struct VitaLodVertex
{
	float position[3];
	float uv[2];
};

struct VitaLodGroup
{
	u32 visual_id;
	u16 shader_id;
	VitaLodVertex faces[8][4];
	std::vector<u32> children;
};

struct VitaLevelVisualNode
{
	u32 visual_id;
	u8 type;
	u16 shader_id;
	std::vector<u32> children;
};

struct VitaVisibleVisual
{
	size_t index;
	float score;
	u32 screen_cell;
};

class VitaViewFrustum
{
public:
	VitaViewFrustum(const float center[3], float scale, float yaw, float pitch, float projection)
	{
		const float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch);
		const float x[] = {cy * scale, 0.f, -sy * scale};
		const float y[] = {cp * sy * scale, sp * scale, cp * cy * scale};
		const float depth[] = {sp * sy * scale, -cp * scale, sp * cy * scale};
		for (size_t axis = 0; axis < 3; ++axis)
		{
			m_planes[0][axis] = depth[axis];
			m_planes[1][axis] = depth[axis] + x[axis] * (4.53f * projection);
			m_planes[2][axis] = depth[axis] - x[axis] * (4.53f * projection);
			m_planes[3][axis] = depth[axis] + y[axis] * (8.f * projection);
			m_planes[4][axis] = depth[axis] - y[axis] * (8.f * projection);
		}
		for (size_t i = 0; i < 5; ++i)
		{
			auto& plane = m_planes[i];
			plane[3] = (i == 0 ? 7.9f : 8.f) -
				(plane[0] * center[0] + plane[1] * center[1] + plane[2] * center[2]);
		}
	}

	bool intersects(const float minimum[3], const float maximum[3]) const
	{
		for (const auto& plane : m_planes)
		{
			const float x = plane[0] >= 0.f ? maximum[0] : minimum[0];
			const float y = plane[1] >= 0.f ? maximum[1] : minimum[1];
			const float z = plane[2] >= 0.f ? maximum[2] : minimum[2];
			if (plane[0] * x + plane[1] * y + plane[2] * z + plane[3] < -0.01f)
				return false;
		}
		return true;
	}

private:
	float m_planes[5][4];
};

static float distance_to_visual_squared(const VitaDebugVisualRange& visual, const float eye[3])
{
	float distance = 0.f;
	for (size_t axis = 0; axis < 3; ++axis)
	{
		const float delta = eye[axis] - std::clamp(eye[axis], visual.bounds_minimum[axis], visual.bounds_maximum[axis]);
		distance += delta * delta;
	}
	return distance;
}

class VitaVisualSpatialIndex
{
public:
	void build(const std::vector<VitaDebugVisualRange>& visuals)
	{
		for (size_t i = 0; i < visuals.size(); ++i)
			if (visuals[i].has_bounds)
				m_indices.push_back(i);
		if (!m_indices.empty())
			build_cell(visuals, 0, m_indices.size());
	}

	void query(const float center[3], float scale, float yaw, float pitch, float projection,
		std::vector<size_t>& candidates) const
	{
		query(VitaViewFrustum(center, scale, yaw, pitch, projection), candidates);
	}

	void query(const VitaViewFrustum& frustum, std::vector<size_t>& candidates) const
	{
		candidates.clear();
		if (!m_cells.empty())
			query_cell(0, frustum, candidates);
	}

private:
	struct Cell
	{
		float minimum[3];
		float maximum[3];
		size_t first;
		size_t count;
		size_t right;
	};

	void build_cell(const std::vector<VitaDebugVisualRange>& visuals, size_t first, size_t count)
	{
		const size_t cell_index = m_cells.size();
		Cell cell{};
		cell.first = first;
		cell.count = count;
		const VitaDebugVisualRange& seed = visuals[m_indices[first]];
		std::copy(seed.bounds_minimum, seed.bounds_minimum + 3, cell.minimum);
		std::copy(seed.bounds_maximum, seed.bounds_maximum + 3, cell.maximum);
		for (size_t i = first + 1; i < first + count; ++i)
		{
			const VitaDebugVisualRange& visual = visuals[m_indices[i]];
			for (size_t axis = 0; axis < 3; ++axis)
			{
				cell.minimum[axis] = std::min(cell.minimum[axis], visual.bounds_minimum[axis]);
				cell.maximum[axis] = std::max(cell.maximum[axis], visual.bounds_maximum[axis]);
			}
		}
		m_cells.push_back(cell);
		if (count <= 16)
			return;
		const size_t axis = cell.maximum[0] - cell.minimum[0] >= cell.maximum[2] - cell.minimum[2] ? 0 : 2;
		const size_t left_count = count / 2;
		std::nth_element(m_indices.begin() + first, m_indices.begin() + first + left_count,
			m_indices.begin() + first + count, [&](size_t lhs, size_t rhs)
			{
				const float a = visuals[lhs].bounds_center[axis];
				const float b = visuals[rhs].bounds_center[axis];
				return a == b ? lhs < rhs : a < b;
			});
		build_cell(visuals, first, left_count);
		m_cells[cell_index].right = m_cells.size();
		build_cell(visuals, first + left_count, count - left_count);
	}

	void query_cell(size_t index, const VitaViewFrustum& frustum, std::vector<size_t>& candidates) const
	{
		const Cell& cell = m_cells[index];
		if (!frustum.intersects(cell.minimum, cell.maximum))
			return;
		if (cell.right)
		{
			query_cell(index + 1, frustum, candidates);
			query_cell(cell.right, frustum, candidates);
		}
		else
			candidates.insert(candidates.end(), m_indices.begin() + cell.first,
				m_indices.begin() + cell.first + cell.count);
	}

	std::vector<size_t> m_indices;
	std::vector<Cell> m_cells;
};

static float player_stick_axis(u8 value)
{
	const float axis = (static_cast<float>(value) - 127.5f) / 127.5f;
	constexpr float dead_zone = 0.18f;
	const float magnitude = fabsf(axis);
	return magnitude > dead_zone ? copysignf((magnitude - dead_zone) / (1.f - dead_zone), axis) : 0.f;
}

static void compute_player_view_center(const float eye[3], float scale, float yaw, float pitch, float center[3])
{
	// The orbit projection places the eye eight view units behind the center.
	const float distance = 8.f / scale;
	const float horizontal_distance = distance * sinf(pitch);
	center[0] = eye[0] + horizontal_distance * sinf(yaw);
	center[1] = eye[1] - distance * cosf(pitch);
	center[2] = eye[2] + horizontal_distance * cosf(yaw);
}

static bool score_visual_for_view(
	const VitaDebugVisualRange& visual, const float center[3], float scale,
	float yaw, float pitch, float projection_scale, float& score, u32& screen_cell, float* screen_area = nullptr)
{
	if (!visual.has_bounds || !(scale > 0.f)) return false;
	const float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch);
	float clip_min_x = (std::numeric_limits<float>::max)();
	float clip_min_y = (std::numeric_limits<float>::max)();
	float clip_max_x = -(std::numeric_limits<float>::max)();
	float clip_max_y = -(std::numeric_limits<float>::max)();
	float nearest_depth = (std::numeric_limits<float>::max)();
	bool has_front_corner = false;
	float corner_view_x[8];
	float corner_view_y[8];
	float corner_camera_z[8];
	static const u32 near_plane_edges[12][2] = {
		{0, 1}, {2, 3}, {4, 5}, {6, 7},
		{0, 2}, {1, 3}, {4, 6}, {5, 7},
		{0, 4}, {1, 5}, {2, 6}, {3, 7}
	};
	for (u32 corner = 0; corner < 8; ++corner)
	{
		const float x = (corner & 1 ? visual.bounds_maximum[0] : visual.bounds_minimum[0]) - center[0];
		const float y = (corner & 2 ? visual.bounds_maximum[1] : visual.bounds_minimum[1]) - center[1];
		const float z = (corner & 4 ? visual.bounds_maximum[2] : visual.bounds_minimum[2]) - center[2];
		corner_view_x[corner] = (cy * x - sy * z) * scale;
		corner_view_y[corner] = (cp * sy * x + sp * y + cp * cy * z) * scale;
		const float view_z = (-sp * sy * x + cp * y - sp * cy * z) * scale;
		corner_camera_z[corner] = 8.f - view_z;
		const float camera_z = corner_camera_z[corner];
		if (camera_z <= 0.1f)
			continue;
		has_front_corner = true;
		if (camera_z < nearest_depth) nearest_depth = camera_z;
		const float clip_x = corner_view_x[corner] * (4.53f * projection_scale) / camera_z;
		const float clip_y = corner_view_y[corner] * (8.f * projection_scale) / camera_z;
		if (clip_x < clip_min_x) clip_min_x = clip_x;
		if (clip_x > clip_max_x) clip_max_x = clip_x;
		if (clip_y < clip_min_y) clip_min_y = clip_y;
		if (clip_y > clip_max_y) clip_max_y = clip_y;
	}
	if (!has_front_corner)
		return false;
	for (u32 edge = 0; edge < 12; ++edge)
	{
		const u32 a = near_plane_edges[edge][0];
		const u32 b = near_plane_edges[edge][1];
		const float depth_a = corner_camera_z[a];
		const float depth_b = corner_camera_z[b];
		if ((depth_a <= 0.1f) == (depth_b <= 0.1f))
			continue;
		const float t = (0.1f - depth_a) / (depth_b - depth_a);
		const float view_x = corner_view_x[a] + (corner_view_x[b] - corner_view_x[a]) * t;
		const float view_y = corner_view_y[a] + (corner_view_y[b] - corner_view_y[a]) * t;
		const float clip_x = view_x * (4.53f * projection_scale) / 0.1f;
		const float clip_y = view_y * (8.f * projection_scale) / 0.1f;
		if (clip_x < clip_min_x) clip_min_x = clip_x;
		if (clip_x > clip_max_x) clip_max_x = clip_x;
		if (clip_y < clip_min_y) clip_min_y = clip_y;
		if (clip_y > clip_max_y) clip_max_y = clip_y;
	}
	if (clip_max_x < -1.f || clip_min_x > 1.f || clip_max_y < -1.f || clip_min_y > 1.f)
		return false;
	if (screen_area)
	{
		const float width = std::min(clip_max_x, 1.f) - std::max(clip_min_x, -1.f);
		const float height = std::min(clip_max_y, 1.f) - std::max(clip_min_y, -1.f);
		*screen_area = width * height * 0.25f;
	}
	const float clip_center_x = (clip_min_x + clip_max_x) * 0.5f;
	const float clip_center_y = (clip_min_y + clip_max_y) * 0.5f;
	int tile_x = static_cast<int>((clip_center_x + 1.f) * 2.f);
	int tile_y = static_cast<int>((clip_center_y + 1.f) * 2.f);
	if (tile_x < 0) tile_x = 0;
	if (tile_x > 3) tile_x = 3;
	if (tile_y < 0) tile_y = 0;
	if (tile_y > 3) tile_y = 3;
	screen_cell = static_cast<u32>(tile_y * 4 + tile_x);
	score = clip_center_x * clip_center_x + clip_center_y * clip_center_y + nearest_depth * (projection_scale == 1.f ? 0.001f : 0.08f);
	return true;
}

static bool load_escape_level_shader_names(
	CStreamReader* level, std::vector<std::string>& shader_names,
	std::vector<std::string>& texture_names, std::vector<std::string>& lightmap_names)
{
	CStreamReader* shader_chunk = level ? level->open_chunk(2) : 0; // fsL_SHADERS
	if (!shader_chunk || shader_chunk->length() - shader_chunk->tell() < sizeof(u32))
	{
		if (shader_chunk)
			FS.r_close(shader_chunk);
		write_boot_log(g_boot_log, "debug_level_shaders_chunk_missing\n");
		return false;
	}
	const u32 count = shader_chunk->r_u32();
	if (count > 4096)
	{
		FS.r_close(shader_chunk);
		write_boot_log(g_boot_log, "debug_level_shaders_bad_count\n");
		return false;
	}
	shader_names.reserve(count);
	for (u32 i = 0; i < count; ++i)
	{
		std::string name;
		bool terminated = false;
		while (shader_chunk->tell() < shader_chunk->length())
		{
			const u8 ch = shader_chunk->r_u8();
			if (ch == 0)
			{
				terminated = true;
				break;
			}
			if (name.size() < 511)
				name.push_back(static_cast<char>(ch));
		}
		if (!terminated)
			break;
		shader_names.push_back(name);
		std::string texture_name;
		std::string lightmap_name;
		const size_t texture_list = name.find('/');
		if (texture_list != std::string::npos)
		{
			const size_t texture_start = texture_list + 1;
			const size_t texture_end = name.find(',', texture_start);
			texture_name = name.substr(texture_start, texture_end == std::string::npos
				? std::string::npos : texture_end - texture_start);
			if (texture_end != std::string::npos)
			{
				const size_t lightmap_start = texture_end + 1;
				const size_t lightmap_end = name.find(',', lightmap_start);
				lightmap_name = name.substr(lightmap_start, lightmap_end == std::string::npos
					? std::string::npos : lightmap_end - lightmap_start);
			}
		}
		texture_names.push_back(texture_name);
		lightmap_names.push_back(lightmap_name);
		char line[640];
		const int line_size = snprintf(line, sizeof(line), "debug_level_shader[%u]=%s\n", i, name.c_str());
		if (line_size > 0 && static_cast<size_t>(line_size) < sizeof(line))
			write_boot_log(g_boot_log, line);
	}
	FS.r_close(shader_chunk);
	char summary[96];
	snprintf(summary, sizeof(summary), "debug_level_shader_names=%u\n", static_cast<unsigned>(shader_names.size()));
	write_boot_log(g_boot_log, summary);
	return !shader_names.empty();
}

struct MobileTextureProfile
{
	unsigned frame;
};

static const CLocatorAPI::file* resolve_level_dds(const std::string& texture_name, string_path& resolved_name)
{
	string_path root_texture_name;
	strconcat(sizeof(root_texture_name), root_texture_name,
		(std::string("gamedata\\levels\\")+vita_campaign_zones[g_zone].directory+"\\").c_str(), texture_name.c_str());
	const CLocatorAPI::file* texture_file =
		FS.exist(resolved_name, "$fs_root$", root_texture_name, ".dds");
	if (!texture_file)
	{
		strconcat(sizeof(root_texture_name), root_texture_name,
			"gamedata\\textures\\", texture_name.c_str());
		texture_file = FS.exist(resolved_name, "$fs_root$", root_texture_name, ".dds");
	}
	return texture_file;
}

enum class BaseTextureStatus : u8
{
	Unrequested, Pending, Budget, NotFound, ReadFailed, DecodeFailed, UploadFailed, LoadFailed, NoTexture, NoUV, Ready
};

static const char* base_texture_status_name(BaseTextureStatus status)
{
	switch (status)
	{
	case BaseTextureStatus::Unrequested: return "unrequested";
	case BaseTextureStatus::Pending: return "pending";
	case BaseTextureStatus::Budget: return "budget";
	case BaseTextureStatus::NotFound: return "not_found";
	case BaseTextureStatus::ReadFailed: return "read_failed";
	case BaseTextureStatus::DecodeFailed: return "decode_failed";
	case BaseTextureStatus::UploadFailed: return "upload_failed";
	case BaseTextureStatus::LoadFailed: return "sync_load_failed";
	case BaseTextureStatus::NoTexture: return "no_base_texture";
	case BaseTextureStatus::NoUV: return "no_base_uv";
	case BaseTextureStatus::Ready: return "ready";
	}
	return "unknown";
}

// Single in-flight request. Only the render thread touches GL and the texture cache.
struct MobileDDSJob
{
	enum State { Idle, Pending, Ready };
	std::atomic<State> state{Idle};
	std::atomic<bool> stopping{false};
	SceUID wake = -1;
	u16 shader = 0;
	unsigned frame = 0;
	const std::string* name = nullptr;
	size_t budget = 0;
	u32 max_dimension = 128;
	bool found = false;
	bool decoded = false;
	bool opened = false;
	size_t required_bytes = 0;
	SceUInt64 lookup_us = 0;
	SceUInt64 read_us = 0;
	VitaDDSTiming timing{};
	VitaDecodedDDS image;
};

static int mobile_dds_thread(SceSize, void* args)
{
	MobileDDSJob& job = **static_cast<MobileDDSJob**>(args);
	while (sceKernelWaitSema(job.wake, 1, nullptr) >= 0)
	{
		if (job.stopping.load())
			break;
		if (job.state.load() != MobileDDSJob::Pending)
			continue;

		string_path resolved_name;
		const SceUInt64 start = sceKernelGetProcessTimeWide();
		job.found = resolve_level_dds(*job.name, resolved_name) != nullptr;
		const SceUInt64 read_start = sceKernelGetProcessTimeWide();
		IReader* source = job.found ? FS.r_open(resolved_name) : nullptr;
		job.read_us = sceKernelGetProcessTimeWide() - read_start;
		job.lookup_us = read_start - start;
		job.timing = {};
		job.decoded = false;
		job.opened = source != nullptr;
		job.required_bytes = 0;
		if (source)
		{
			job.decoded = vita_decode_dds(source->pointer(), source->length(), job.budget,
				job.max_dimension, job.image, &job.timing, &job.required_bytes);
			FS.r_close(source);
		}
		job.state.store(MobileDDSJob::Ready);
	}
	return 0;
}

static void load_cordon_sky(VitaGLSky& sky)
{
	string_path resolved_name;
	IReader* source = resolve_level_dds("sky\\sky_3_cube", resolved_name) ? FS.r_open(resolved_name) : nullptr;
	char error[1024] = {};
	const bool ready = source && sky.initialize(source->pointer(), source->length(), error, sizeof(error));
	char line[160];
	snprintf(line, sizeof(line), "debug_cordon_sky ready=%u source_bytes=%u weather=default_weather_07 geometry=half_box\n",
		static_cast<unsigned>(ready), source ? static_cast<unsigned>(source->length()) : 0);
	if (source)
		FS.r_close(source);
	write_boot_log(g_boot_log, line);
	if (error[0])
	{
		write_boot_log(g_boot_log, error);
		write_boot_log(g_boot_log, "\n");
	}
}

static bool load_level_dds_texture(
	const std::string& texture_name, VitaGLTexture& texture, size_t budget, bool& source_found, u32 max_dimension = 128,
	const char* terrain_slot = nullptr, const VitaDDSRegion* region = nullptr, const MobileTextureProfile* profile = nullptr)
{
	const SceUInt64 start = profile ? sceKernelGetProcessTimeWide() : 0;
	source_found = false;
	if (texture_name.empty())
		return false;
	string_path resolved_name;
	const CLocatorAPI::file* texture_file = resolve_level_dds(texture_name, resolved_name);
	if (!texture_file)
	{
		static bool probe_written = false;
		if (!probe_written)
		{
			probe_written = true;
			char line[1100];
			const int line_size = snprintf(line, sizeof(line), "debug_dds_resolved_candidate=%s\n", resolved_name);
			if (line_size > 0 && static_cast<size_t>(line_size) < sizeof(line))
				write_boot_log(g_boot_log, line);
			xr_vector<LPSTR>* texture_files = FS.file_list_open("$fs_root$", FS_ListFiles);
			if (texture_files)
			{
				u32 matches = 0;
				for (xr_vector<LPSTR>::iterator it = texture_files->begin(); it != texture_files->end(); ++it)
				{
					if (strstr(*it, "gamedata\\textures\\glow\\") != *it)
						continue;
					if (matches < 8)
					{
						snprintf(line, sizeof(line), "debug_game_texture_sample=%s\n", *it);
						write_boot_log(g_boot_log, line);
					}
					++matches;
				}
				snprintf(line, sizeof(line), "debug_game_texture_root_matches=%u\n", matches);
				write_boot_log(g_boot_log, line);
				FS.file_list_close(texture_files);
			}
			else
				write_boot_log(g_boot_log, "debug_fs_root_texture_list_failed\n");
		}
		return false;
	}
	source_found = true;
	const SceUInt64 read_start = profile ? sceKernelGetProcessTimeWide() : 0;
	IReader* source = FS.r_open(resolved_name);
	const SceUInt64 read_end = profile ? sceKernelGetProcessTimeWide() : 0;
	VitaDDSInfo info{};
	VitaDDSTiming timing{};
	bool loaded = false;
	if (source)
	{
		loaded = region
			? texture.initialize_dds_region(source->pointer(), source->length(), budget, *region)
			: texture.initialize_dds(source->pointer(), source->length(), budget, max_dimension,
				terrain_slot ? &info : nullptr, profile ? &timing : nullptr);
		FS.r_close(source);
	}
	if (profile)
	{
		const SceUInt64 total = sceKernelGetProcessTimeWide() - start;
		char line[768];
		snprintf(line, sizeof(line),
			"debug_player_dds frame=%u texture=%s loaded=%u lookup_us=%llu read_us=%llu decode_us=%llu upload_us=%llu total_us=%llu\n",
			profile->frame, texture_name.c_str(), static_cast<unsigned>(loaded),
			static_cast<unsigned long long>(read_start - start), static_cast<unsigned long long>(read_end - read_start),
			static_cast<unsigned long long>(timing.decode_us), static_cast<unsigned long long>(timing.upload_us),
			static_cast<unsigned long long>(total));
		write_boot_log(g_boot_log, line);
	}
	if (terrain_slot)
	{
		char line[1280];
		snprintf(line, sizeof(line),
			"debug_terrain_dds slot=%s loaded=%u path=%s mip=%u size=%ux%u fourcc=%08x "
			"rgba_min=%u,%u,%u,%u rgba_max=%u,%u,%u,%u rgba_mean=%u,%u,%u,%u\n",
			terrain_slot, static_cast<unsigned>(loaded), resolved_name, info.mip_level, info.width, info.height,
			info.fourcc, info.minimum[0], info.minimum[1], info.minimum[2], info.minimum[3],
			info.maximum[0], info.maximum[1], info.maximum[2], info.maximum[3],
			info.mean[0], info.mean[1], info.mean[2], info.mean[3]);
		write_boot_log(g_boot_log, line);
	}
	return loaded;
}

static bool load_cordon_hud(VitaHud& hud)
{
	if (!hud.initialize())
		return false;

	bool source_found = false;
	load_level_dds_texture("ui\\cursor", hud.cursor, 64u * 64u * 4u, source_found, 64);
	const VitaDDSRegion atlas_region{1024, 0, 0, VitaHud::atlas_width, VitaHud::atlas_height};
	load_level_dds_texture("ui\\ui_hud", hud.atlas, VitaHud::atlas_width * VitaHud::atlas_height * 4u,
		source_found, VitaHud::atlas_width, nullptr, &atlas_region);
	const VitaDDSRegion stance_region{1024, 0, 352, VitaHud::stance_width, VitaHud::stance_height};
	load_level_dds_texture("ui\\ui_hud", hud.stance, VitaHud::stance_width * VitaHud::stance_height * 4u,
		source_found, VitaHud::stance_height, nullptr, &stance_region);
	load_level_dds_texture("ui\\ui_hud_map_arrow", hud.compass, 32u * 32u * 4u, source_found, 32);

	string_path path;
	const auto* asset = FS.exist(path, "$fs_root$", "gamedata\\config\\game_maps_single.ltx");
	IReader* reader = asset && asset->size_real <= 128u * 1024u ? FS.r_open(path) : nullptr;
	if (reader)
	{
		const std::string config(static_cast<const char*>(reader->pointer()), reader->length());
		FS.r_close(reader);
		std::string texture;
		if (read_vita_hud_map(config, texture, hud.map_bounds,vita_campaign_zones[g_zone].directory))
			load_level_dds_texture(texture, hud.map, 1024u * 1024u, source_found, 512);
	}
	char line[176];
	snprintf(line, sizeof(line), "debug_hud cursor=%u frame=%u map=%u compass=%u stance=%u bounds=%.1f,%.1f,%.1f,%.1f\n",
		static_cast<unsigned>(hud.cursor.handle() != 0), static_cast<unsigned>(hud.atlas.handle() != 0),
		static_cast<unsigned>(hud.map.handle() != 0), static_cast<unsigned>(hud.compass.handle() != 0),
		static_cast<unsigned>(hud.stance.handle() != 0), hud.map_bounds[0], hud.map_bounds[1], hud.map_bounds[2], hud.map_bounds[3]);
	write_boot_log(g_boot_log, line);
	return hud.cursor.handle() || hud.map.handle() || hud.atlas.handle() || hud.stance.handle();
}

static std::vector<u8> load_passable_materials()
{
	// gamemtl.xr version 1 stores the material ID and passable flag in each material chunk.
	string_path path;
	const CLocatorAPI::file* file = FS.exist(path, "$game_data$", "gamemtl.xr");
	if (!file)
		file = FS.exist(path, "$fs_root$", "gamedata\\gamemtl.xr");
	IReader* reader = file ? FS.r_open(path) : nullptr;
	std::vector<u8> passable;
	unsigned marked = 0;
	if (reader)
	{
		if (reader->find_chunk(0x1000) >= sizeof(u16) && reader->r_u16() == 1)
		{
			IReader* materials = reader->open_chunk(0x1002);
			if (materials)
			{
				passable.resize(VitaCformGround::material_count, 0);
				u32 chunk;
				for (IReader* item = materials->open_chunk_iterator(chunk); item;
					item = materials->open_chunk_iterator(chunk, item))
				{
					if (item->find_chunk(0x1000) < sizeof(u32))
						continue;
					const u32 id = item->r_u32();
					constexpr u32 passable_flag = 1u << 7;
					constexpr u32 dynamic_flag = 1u << 8;
					if (id < passable.size() && item->find_chunk(0x1001) >= sizeof(u32) &&
						(item->r_u32() & (passable_flag | dynamic_flag)) == passable_flag && !passable[id])
					{
						passable[id] = 1;
						++marked;
					}
				}
				materials->close();
			}
		}
		FS.r_close(reader);
	}
	char line[128];
	snprintf(line, sizeof(line), "debug_player_collision_materials ready=%u passable_ids=%u\n",
		static_cast<unsigned>(!passable.empty()), marked);
	write_boot_log(g_boot_log, line);
	return passable;
}

struct CordonAssaultPaths
{
	VitaPatrolPoint muster[3]{};
	VitaPatrolPoint regroup{};
	VitaPatrolPoint farm_bounds{};
	VitaPatrolPoint guards[6]{};
	VitaPatrolPoint allegro{}, allegro_look{};
	bool allegro_found = false;
	std::array<bool, 6> guards_found{};
	std::vector<VitaPatrolPoint> approach;
	bool ready = false;
};

static bool load_scout_waypoints(CordonAssaultPaths& assault)
{
 if(g_zone){
  const auto&zone=vita_campaign_zones[g_zone];
  for(unsigned i=0;i<3;++i)assault.muster[i]=zone.allies[i];
  for(unsigned i=0;i<6;++i){assault.guards[i]=zone.enemies[i];assault.guards_found[i]=true;}
  assault.regroup=assault.farm_bounds=zone.allies[0];assault.approach.push_back(zone.allies[0]);assault.ready=true;
  return true;
 }

	string_path path;
	const auto* asset = FS.exist(path, "$game_levels$", "l01_escape\\level.game");
	if (!asset)
		asset = FS.exist(path, "$fs_root$", "gamedata\\levels\\l01_escape\\level.game");
	IReader* file = asset && asset->size_real <= 1024u * 1024u ? FS.r_open(path) : nullptr;
	if (!file)
		return false;

	struct Marker { const char* name; VitaPatrolPoint& position; bool found; };
	Marker markers[] = {
		{"esc_assault_escape_btr_path1", assault.muster[0], false},
		{"esc_assault_walk_wait1", assault.muster[1], false},
		{"esc_assault_walk_wait2", assault.muster[2], false},
		{"esc_assault_walk_syn2", assault.regroup, false},
		{"esc_assault_kamp_center_task", assault.farm_bounds, false},
		{"esc_fabrika_bandit_day_walk1", assault.guards[0], false},
		{"esc_fabrika_bandit_day_walk2", assault.guards[1], false},
		{"esc_fabrika_bandit_day_walk3", assault.guards[2], false},
		{"esc_fabrika_bandit_day_walk4", assault.guards[3], false},
		{"esc_fabrika_bandit_day_alarm_walk6", assault.guards[4], false},
		{"esc_fabrika_bandit_day_night_sniper_walk", assault.guards[5], false},
		{"esc_lager_prisoner_walk", assault.allegro, false},
		{"esc_lager_prisoner_look", assault.allegro_look, false}
	};
	IReader* paths = file->open_chunk(0x1000); // WAY_PATROLPATH_CHUNK
	if (paths)
	{
		u32 id;
		for (IReader* record = paths->open_chunk_iterator(id); record; record = paths->open_chunk_iterator(id, record))
		{
			const size_t name_bytes = record->find_chunk(5); // WAYOBJECT_CHUNK_NAME
			if (name_bytes == 0)
				continue;
			const auto* name = static_cast<const char*>(record->pointer());
			const auto matches = [&](const char* expected) {
				return name_bytes == strlen(expected) + 1 && memcmp(name, expected, name_bytes) == 0;
			};
			if (matches("esc_assault_escape_factory_way1"))
			{
				const size_t points_size = record->find_chunk(2);
				const void* points = record->pointer();
				const size_t links_size = record->find_chunk(3);
				read_vita_patrol(points, points_size, record->pointer(), links_size, assault.approach);
				continue;
			}
			for (Marker& marker : markers)
			{
				if (marker.found || !matches(marker.name))
					continue;
				if (record->find_chunk(2) >= sizeof(u16) + 3 * sizeof(float) && record->r_u16() > 0)
				{
					record->r(marker.position.data(), 3 * sizeof(float));
					marker.found = std::all_of(marker.position.begin(), marker.position.end(), [](float value) {
						return std::isfinite(value) && fabsf(value) < 2000.f;
					});
				}
				break;
			}
		}
		paths->close();
	}
	FS.r_close(file);
	for (size_t i = 0; i < assault.guards_found.size(); ++i)
		assault.guards_found[i] = markers[5 + i].found;
	assault.allegro_found = markers[11].found && markers[12].found;
	assault.ready = markers[3].found && markers[4].found && assault.approach.size() > 1;
	char line[194];
	snprintf(line, sizeof(line), "debug_scout_waypoints leader=%u scout1=%u scout2=%u assault=%u approach_points=%u guards=%u\\n",
		static_cast<unsigned>(markers[0].found), static_cast<unsigned>(markers[1].found),
		static_cast<unsigned>(markers[2].found), static_cast<unsigned>(assault.ready),
		static_cast<unsigned>(assault.approach.size()),
		static_cast<unsigned>(std::count(assault.guards_found.begin(), assault.guards_found.end(), true)));
	write_boot_log(g_boot_log, line);
	return markers[0].found && markers[1].found && markers[2].found;
}

struct ScoutRouteSample
{
	VitaPatrolPoint position;
	float metres;
};

static std::vector<ScoutRouteSample> build_scout_route(const VitaCformGround& ground,
	const VitaPatrolPoint& start, const CordonAssaultPaths& paths, bool regroup, unsigned& missing_ground, unsigned member=0)
{
	std::vector<VitaPatrolPoint> waypoints{start};
	if (regroup)
		waypoints.push_back(paths.regroup);
	waypoints.insert(waypoints.end(), paths.approach.begin(), paths.approach.end());
    if(g_zone)waypoints.resize(1); // New encounters navigate from each actual spawn, without the Cordon regroup segment.
	std::vector<ScoutRouteSample> samples{{start, 0.f}};
	float length = 0.f;
	for (size_t i = 1; i < waypoints.size(); ++i)
	{
		const auto& from = waypoints[i - 1];
		const auto& to = waypoints[i];
		const float span = hypotf(to[0] - from[0], to[2] - from[2]);
		if (span < 0.01f)
			continue;
		const unsigned steps = static_cast<unsigned>(ceilf(span / 0.35f));
		for (unsigned step = 1; step <= steps; ++step)
		{
			const float t = static_cast<float>(step) / steps;
			VitaPatrolPoint position;
			for (unsigned axis = 0; axis < 3; ++axis)
				position[axis] = from[axis] + (to[axis] - from[axis]) * t;
			const float previous_y = samples.back().position[1];
			if (!ground.height_at(position[0], position[2], previous_y, 0.5f, 1.f, position[1]))
				++missing_ground;
			samples.push_back({position, length + span * t});
		}
		length += span;
	}
 // Retail patrol signals switch the PC AI into attack before the last marker.
 // The Vita encounter must explicitly reach a collision-safe firing position.
 std::vector<VitaPatrolPoint> advance;
 VitaAssaultAdvanceStats advance_stats;
 const bool advance_ready=find_vita_assault_advance(ground,samples.back().position,paths.guards,
  paths.guards_found,13.5f-member*1.5f,advance,advance_stats);
 for(const auto& position:advance)
 {
  const auto& previous=samples.back();
  const float distance=std::hypot(position[0]-previous.position[0],position[2]-previous.position[2]);
  if(distance>.01f)samples.push_back({position,previous.metres+distance});
 }
 char line[144];
 snprintf(line,sizeof(line),"debug_scout_advance member=%u ready=%u points=%u visited=%u\n",
  member,unsigned(advance_ready),unsigned(advance.size()),advance_stats.visited);write_boot_log(g_boot_log,line);

	return samples;
}

static bool load_escape_ground(VitaCformGround& ground, float center_x, float center_z, const float* destination)
{
	string_path path;
	const CLocatorAPI::file* file = FS.exist(path, "$game_levels$", zone_file("level.cform").c_str());
	if (!file)
		file = FS.exist(path, "$fs_root$", (std::string("gamedata\\levels\\")+zone_file("level.cform")).c_str());
	if (!file)
	{
		write_boot_log(g_boot_log, "debug_player_cform ready=0 reason=missing\n");
		return false;
	}
	const bool compressed = file->size_real != file->size_compressed;
	constexpr size_t max_buffered_cform_bytes = 32u * 1024u * 1024u;
	const size_t bytes = file->size_real;
	if (bytes > VitaCformGround::max_file_bytes ||
		(compressed && (bytes > max_buffered_cform_bytes || file->size_compressed > max_buffered_cform_bytes)))
	{
		write_boot_log(g_boot_log, "debug_player_cform ready=0 reason=size_limit\n");
		return false;
	}

	const SceUInt64 start = sceKernelGetProcessTimeWide();
	const auto passable_materials = load_passable_materials();
	bool loaded = false;
	if (!compressed)
	{
		CStreamReader* reader = FS.rs_open(nullptr, path);
		if (reader)
		{
			loaded = ground.load([](void* source, void* output, size_t count)
			{
				auto& stream = *static_cast<CStreamReader*>(source);
				if (count > stream.elapsed())
					return false;
				const u32 offset = stream.tell();
				stream.r(output, static_cast<u32>(count));
				return stream.tell() - offset == count;
			}, reader, bytes, center_x, center_z, passable_materials, destination, true);
			FS.r_close(reader);
		}
	}
	else
	{
		IReader* reader = FS.r_open(path);
		if (reader)
		{
			loaded = ground.load([](void* source, void* output, size_t count)
			{
				auto& input = *static_cast<IReader*>(source);
				if (count > input.elapsed())
					return false;
				input.r(output, static_cast<u32>(count));
				return true;
			}, reader, bytes, center_x, center_z, passable_materials, destination, true);
			FS.r_close(reader);
		}
	}
	char line[192];
	snprintf(line, sizeof(line), "debug_player_cform ready=%u reason=%s bytes=%u walkable_faces=%u wall_faces=%u retained_bytes=%u load_us=%llu\n",
		static_cast<unsigned>(loaded), loaded ? "ok" : "read_invalid_or_limit", static_cast<unsigned>(bytes),
		static_cast<unsigned>(ground.face_count()), static_cast<unsigned>(ground.wall_count()), static_cast<unsigned>(ground.memory_bytes()),
		static_cast<unsigned long long>(sceKernelGetProcessTimeWide() - start));
	write_boot_log(g_boot_log, line);
	return loaded;
}

static CStreamReader* open_escape_level_file()
{
	write_boot_log(g_boot_log, "debug_level_file_open_begin\n");
	CStreamReader* level = FS.rs_open("$game_levels$", zone_file("level").c_str());
	write_boot_log(g_boot_log, level ? "debug_level_file_alias_opened\n" : "debug_level_file_alias_missed\n");
	if (level)
		return level;
	const auto suffix_storage=zone_file("level");
    const char* suffix=suffix_storage.c_str();
	const size_t suffix_length = strlen(suffix);
	xr_vector<LPSTR>* files = FS.file_list_open("$fs_root$", FS_ListFiles);
	write_boot_log(g_boot_log, files ? "debug_level_file_scan_begin\n" : "debug_level_file_scan_unavailable\n");
	if (!files)
		return 0;
	for (xr_vector<LPSTR>::iterator it = files->begin(); it != files->end() && !level; ++it)
	{
		for (const char* part = *it; *part; ++part)
		{
			if (strncasecmp(part, suffix, suffix_length) == 0 &&
				(part == *it || part[-1] == '\\' || part[-1] == '/') &&
				(part[suffix_length] == '\0' || part[suffix_length] == '.'))
			{
				level = FS.rs_open("$fs_root$", *it);
				break;
			}
		}
	}
	FS.file_list_close(files);
	write_boot_log(g_boot_log, level ? "debug_level_file_scan_opened\n" : "debug_level_file_scan_missed\n");
	return level;
}

static bool load_escape_level_visuals(
	std::vector<VitaDebugVisualRange>& visuals, const std::vector<VitaTreeWindows>& tree_windows,
	std::vector<VitaLodGroup>& lod_groups, std::vector<std::string>& shader_names,
	std::vector<std::string>& texture_names,
	std::vector<std::string>& lightmap_names)
{
	CStreamReader* level = open_escape_level_file();
	if (!level)
	{
		write_boot_log(g_boot_log, "debug_level_visuals_file_missing\n");
		return false;
	}
	write_boot_log(g_boot_log, "debug_level_visuals_file_opened\n");
	load_escape_level_shader_names(level, shader_names, texture_names, lightmap_names);
	CStreamReader* visual_chunk = level->open_chunk(3); // fsL_VISUALS
	if (!visual_chunk)
	{
		FS.r_close(level);
		write_boot_log(g_boot_log, "debug_level_visuals_chunk_missing\n");
		return false;
	}
	write_boot_log(g_boot_log, "debug_level_visuals_chunk_opened\n");
	std::vector<u8> visual_data;
	std::vector<VitaLevelVisualNode> visual_nodes;
	u32 progressive_windows = 0;
	u32 progressive_indices_removed = 0;
	u32 tree_transforms = 0;
	u32 tree_progressive_windows = 0;
	while (visual_chunk->tell() <= visual_chunk->length() &&
		visual_chunk->length() - visual_chunk->tell() >= 8)
	{
		const u32 visual_id = visual_chunk->r_u32();
		const u32 visual_size = visual_chunk->r_u32();
		if (visual_size > visual_chunk->length() - visual_chunk->tell())
		{
			write_boot_log(g_boot_log, "debug_level_visuals_bad_chunk_size\n");
			break;
		}
		if (visual_size == 0)
		{
			write_boot_log(g_boot_log, "debug_level_visuals_empty_chunk\n");
			break;
		}
		if (visual_size < 8)
		{
			visual_chunk->advance(static_cast<int>(visual_size));
			continue;
		}
		visual_data.resize(visual_size);
		visual_chunk->r(visual_data.data(), visual_size);
		size_t offset = 0;
		VitaLevelVisualNode node = {};
		node.visual_id = visual_id;
		VitaDebugVisualRange range = {};
		const u8* lod_payload = nullptr;
		bool found_container = false;
		VitaSlideWindow window = {};
		VitaSlideWindow coarse = {};
		u32 tree_window_id = (std::numeric_limits<u32>::max)();
		while (offset <= visual_data.size() && visual_data.size() - offset >= 8)
		{
			u32 chunk_type;
			u32 chunk_size;
			memcpy(&chunk_type, visual_data.data() + offset, sizeof(chunk_type));
			memcpy(&chunk_size, visual_data.data() + offset + sizeof(chunk_type), sizeof(chunk_size));
			offset += 8;
			if (chunk_size > visual_data.size() - offset)
				break;
			if ((chunk_type & 0x7fffffffu) == 1 && chunk_size >= 4) // OGF_HEADER
			{
				node.type = visual_data[offset + 1];
				memcpy(&node.shader_id, visual_data.data() + offset + 2, sizeof(node.shader_id));
			}
			else if ((chunk_type & 0x7fffffffu) == 21 && chunk_size >= 6 * sizeof(u32)) // OGF_GCONTAINER
			{
				memcpy(&range.vertex_buffer, visual_data.data() + offset, sizeof(u32));
				memcpy(&range.first_vertex, visual_data.data() + offset + 4, sizeof(u32));
				memcpy(&range.vertex_count, visual_data.data() + offset + 8, sizeof(u32));
				memcpy(&range.index_buffer, visual_data.data() + offset + 12, sizeof(u32));
				memcpy(&range.first_index, visual_data.data() + offset + 16, sizeof(u32));
				memcpy(&range.index_count, visual_data.data() + offset + 20, sizeof(u32));
				found_container = true;
			}
			else if (chunk_type == 6 && chunk_size >= 28) // OGF_SWIDATA
			{
				// The first window is the highest-detail range used by FProgressive.
				u32 window_count;
				memcpy(&window_count, visual_data.data() + offset + 16, sizeof(window_count));
				if (window_count && window_count <= (chunk_size - 20) / 8)
				{
					const u8* windows = visual_data.data() + offset + 20;
					memcpy(&window, windows, sizeof(window));
					if (window_count > 1)
						memcpy(&coarse, windows + (window_count - 1) * sizeof(coarse), sizeof(coarse));
				}
			}
			else if (chunk_type == 12 && chunk_size >= sizeof(range.tree_transform)) // OGF_TREEDEF2
			{
				memcpy(range.tree_transform, visual_data.data() + offset, sizeof(range.tree_transform));
				range.has_tree_transform = std::all_of(range.tree_transform, range.tree_transform + 16,
					[](float value) { return std::isfinite(value); });
			}
			else if (chunk_type == 20 && chunk_size >= sizeof(tree_window_id)) // OGF_SWICONTAINER
				memcpy(&tree_window_id, visual_data.data() + offset, sizeof(tree_window_id));
			else if (chunk_type == 11 && chunk_size == 8 * 4 * 28) // OGF_LODDEF2
				lod_payload = visual_data.data() + offset;
			else if ((chunk_type & 0x7fffffffu) == 10 && chunk_size >= sizeof(u32)) // OGF_CHILDREN_L
			{
				u32 child_count = 0;
				memcpy(&child_count, visual_data.data() + offset, sizeof(child_count));
				if (child_count <= (chunk_size - sizeof(u32)) / sizeof(u32))
				{
					node.children.resize(child_count);
					if (child_count)
						memcpy(node.children.data(), visual_data.data() + offset + sizeof(u32),
							child_count * sizeof(u32));
				}
			}
			offset += chunk_size;
		}
		if (found_container && range.vertex_count && range.index_count >= 3)
		{
			const bool tree = node.type == 7 || node.type == 11;
			range.has_tree_transform = tree && range.has_tree_transform;
			if (range.has_tree_transform) ++tree_transforms;
			if (node.type == 11 && tree_window_id < tree_windows.size())
			{
				window = tree_windows[tree_window_id].normal;
				coarse = tree_windows[tree_window_id].coarse;
			}
			const auto valid_window = [&range](const VitaSlideWindow& candidate)
			{
				return candidate.triangles && candidate.vertices && candidate.vertices <= range.vertex_count &&
					candidate.offset <= range.index_count && 3u * candidate.triangles <= range.index_count - candidate.offset &&
					candidate.offset <= (std::numeric_limits<u32>::max)() - range.first_index;
			};
			const u32 window_index_count = 3u * window.triangles;
			if ((node.type == 2 || node.type == 11) && valid_window(window))
			{
				++progressive_windows;
				if (tree) ++tree_progressive_windows;
				progressive_indices_removed += range.index_count - window_index_count;
				if (valid_window(coarse) && 3u * coarse.triangles < window_index_count)
				{
					range.coarse_first_index = range.first_index + coarse.offset;
					range.coarse_index_count = 3u * coarse.triangles;
					range.coarse_vertex_count = coarse.vertices;
				}
				range.first_index += window.offset;
				range.index_count = window_index_count;
				range.vertex_count = window.vertices;
			}
			range.visual_id = visual_id;
			range.shader_id = node.shader_id;
			visuals.push_back(range);
		}
		if (node.type == 6 && lod_payload && !node.children.empty()) // MT_LOD
		{
			VitaLodGroup group{};
			group.visual_id = visual_id;
			group.shader_id = node.shader_id;
			group.children = node.children;
			bool valid = true;
			for (auto& face : group.faces)
				for (VitaLodVertex& vertex : face)
				{
					memcpy(vertex.position, lod_payload, sizeof(vertex.position));
					memcpy(vertex.uv, lod_payload + sizeof(vertex.position), sizeof(vertex.uv));
					lod_payload += 28; // Position, UV, packed lighting, and padding.
					for (float coordinate : vertex.position) valid &= std::isfinite(coordinate);
					for (float coordinate : vertex.uv) valid &= std::isfinite(coordinate);
				}
			if (valid)
				lod_groups.push_back(std::move(group));
		}
		visual_nodes.push_back(std::move(node));
	}
	char progressive_log[112];
	snprintf(progressive_log, sizeof(progressive_log),
		"debug_visual_progressive_windows=%u indices_removed=%u\n",
		progressive_windows, progressive_indices_removed);
	write_boot_log(g_boot_log, progressive_log);
	snprintf(progressive_log, sizeof(progressive_log), "debug_visual_tree_transforms=%u progressive_windows=%u\n",
		tree_transforms, tree_progressive_windows);
	write_boot_log(g_boot_log, progressive_log);
	FS.r_close(visual_chunk);
	CStreamReader* sectors_chunk = level->open_chunk(8); // fsL_SECTORS
	std::vector<u32> sector_roots;
	if (sectors_chunk)
	{
		for (u32 sector_id = 0; ; ++sector_id)
		{
			CStreamReader* sector = sectors_chunk->open_chunk(sector_id);
			if (!sector) break;
			CStreamReader* root_chunk = sector->open_chunk(2); // fsP_Root
			if (root_chunk && root_chunk->tell() <= root_chunk->length() &&
				root_chunk->length() - root_chunk->tell() >= sizeof(u32))
				sector_roots.push_back(root_chunk->r_u32());
			else
				sector_roots.push_back((std::numeric_limits<u32>::max)());
			if (root_chunk) FS.r_close(root_chunk);
			FS.r_close(sector);
		}
		FS.r_close(sectors_chunk);
	}
	if (!sector_roots.empty() && !visual_nodes.empty())
	{
		u32 maximum_visual_id = 0;
		for (size_t i = 0; i < visual_nodes.size(); ++i)
			if (visual_nodes[i].visual_id > maximum_visual_id)
				maximum_visual_id = visual_nodes[i].visual_id;
		if (maximum_visual_id <= 1048576u)
		{
			std::vector<int> node_by_id(static_cast<size_t>(maximum_visual_id) + 1, -1);
			for (size_t i = 0; i < visual_nodes.size(); ++i)
				node_by_id[visual_nodes[i].visual_id] = static_cast<int>(i);
			std::vector<u8> has_geometry(node_by_id.size(), 0);
			for (size_t i = 0; i < visuals.size(); ++i)
				if (visuals[i].visual_id < has_geometry.size())
					has_geometry[visuals[i].visual_id] = 1;
			std::vector<u8> best_sector_nodes(visual_nodes.size(), 0);
			std::vector<u8> largest_sector_nodes(visual_nodes.size(), 0);
			std::vector<u8> bunker_passage_nodes(visual_nodes.size(), 0);
			std::vector<u8> village_cellar_nodes(visual_nodes.size(), 0);
			u32 largest_sector_mesh_count = 0;
			u32 best_sector_mesh_count = 0;
			u32 best_sector_id = 0;
			bool best_sector_is_local = false;
			u32 reachable_node_count = 0;
			char sector_count_log[512];
			size_t sector_count_log_length = static_cast<size_t>(snprintf(
				sector_count_log, sizeof(sector_count_log), "debug_level_sector_mesh_counts="));
			for (size_t sector_index = 0; sector_index < sector_roots.size(); ++sector_index)
			{
				const u32 root_id = sector_roots[sector_index];
				if (root_id >= node_by_id.size() || node_by_id[root_id] < 0) continue;
				std::vector<u8> sector_nodes(visual_nodes.size(), 0);
				std::vector<size_t> pending_nodes(1, static_cast<size_t>(node_by_id[root_id]));
				while (!pending_nodes.empty())
				{
					const size_t node_index = pending_nodes.back();
					pending_nodes.pop_back();
					if (sector_nodes[node_index]) continue;
					sector_nodes[node_index] = 1;
					const std::vector<u32>& children = visual_nodes[node_index].children;
					for (size_t child = 0; child < children.size(); ++child)
						if (children[child] < node_by_id.size() && node_by_id[children[child]] >= 0)
							pending_nodes.push_back(static_cast<size_t>(node_by_id[children[child]]));
				}
				u32 sector_mesh_count = 0;
				for (size_t node_index = 0; node_index < sector_nodes.size(); ++node_index)
				{
					if (!sector_nodes[node_index]) continue;
					++reachable_node_count;
					// Cordon sector 2 connects to sector 23 through the two stair sectors.
					if (sector_index == 0 || sector_index == 1)
						bunker_passage_nodes[node_index] = 1;
					// The accessible village cellar is a distinct indoor sector.
					if (sector_index == 3)
						village_cellar_nodes[node_index] = 1;
					const u32 visual_id = visual_nodes[node_index].visual_id;
					if (visual_id < has_geometry.size() && has_geometry[visual_id])
						++sector_mesh_count;
				}
				if (sector_count_log_length < sizeof(sector_count_log))
				{
					const int appended = snprintf(sector_count_log + sector_count_log_length,
						sizeof(sector_count_log) - sector_count_log_length, "%s%u:%u",
						sector_index ? "," : "", static_cast<unsigned>(sector_index), sector_mesh_count);
					if (appended > 0 && static_cast<size_t>(appended) < sizeof(sector_count_log) - sector_count_log_length)
						sector_count_log_length += static_cast<size_t>(appended);
					else
						sector_count_log_length = sizeof(sector_count_log);
				}
				const bool sector_is_local = sector_mesh_count > 0 && sector_mesh_count < 128;
				if (sector_mesh_count > largest_sector_mesh_count)
				{
					largest_sector_mesh_count = sector_mesh_count;
					largest_sector_nodes = sector_nodes;
				}
				const bool better_sector = sector_mesh_count > 0 && (sector_is_local
					? (!best_sector_is_local || sector_mesh_count > best_sector_mesh_count)
					: (!best_sector_is_local &&
						(best_sector_mesh_count == 0 || sector_mesh_count < best_sector_mesh_count)));
				if (better_sector)
				{
					best_sector_mesh_count = sector_mesh_count;
					best_sector_id = static_cast<u32>(sector_index);
					best_sector_is_local = sector_is_local;
					best_sector_nodes.swap(sector_nodes);
				}
			}
			if (sector_count_log_length < sizeof(sector_count_log) - 1)
			{
				sector_count_log[sector_count_log_length++] = '\n';
				sector_count_log[sector_count_log_length] = '\0';
				write_boot_log(g_boot_log, sector_count_log);
			}
			const size_t all_visual_count = visuals.size();
			unsigned passage_meshes = 0, cellar_meshes = 0;
			std::vector<VitaDebugVisualRange> reachable_visuals;
			reachable_visuals.reserve(visuals.size());
			for (size_t i = 0; i < visuals.size(); ++i)
			{
				VitaDebugVisualRange visual = visuals[i];
				if (visual.visual_id >= node_by_id.size() || node_by_id[visual.visual_id] < 0)
					continue;
				const size_t node_index = static_cast<size_t>(node_by_id[visual.visual_id]);
				visual.focus_sector = best_sector_nodes[node_index] != 0;
				visual.bunker_passage = bunker_passage_nodes[node_index] != 0;
				visual.village_cellar = village_cellar_nodes[node_index] != 0;
				cellar_meshes += visual.village_cellar;
				if (visual.bunker_passage)
					++passage_meshes;
				if (g_zone!=0 || visual.focus_sector || visual.bunker_passage || visual.village_cellar || largest_sector_nodes[node_index])
					reachable_visuals.push_back(visual);
			}
			if (!reachable_visuals.empty())
				visuals.swap(reachable_visuals);
			else
				write_boot_log(g_boot_log, "debug_level_sector_visual_match_empty_using_all\n");
			char root_summary[128];
			snprintf(root_summary, sizeof(root_summary),
				"debug_level_sector_roots=%u sector_node_visits=%u selected_sector=%u sector_meshes=%u ranges=%u of %u\n",
				static_cast<unsigned>(sector_roots.size()), reachable_node_count, best_sector_id,
				best_sector_mesh_count, static_cast<unsigned>(visuals.size()),
				static_cast<unsigned>(all_visual_count));
			write_boot_log(g_boot_log, root_summary);
			snprintf(root_summary, sizeof(root_summary), "debug_bunker_passage sectors=0,1 meshes=%u\n", passage_meshes);
			write_boot_log(g_boot_log, root_summary);
			snprintf(root_summary, sizeof(root_summary), "debug_village_cellar sector=3 meshes=%u\n", cellar_meshes);
			write_boot_log(g_boot_log, root_summary);
		}
		else
			write_boot_log(g_boot_log, "debug_level_visual_id_limit_exceeded\n");
	}
	else
		write_boot_log(g_boot_log, sector_roots.empty()
			? "debug_level_sector_roots_missing\n" : "debug_level_visual_nodes_missing\n");
	FS.r_close(level);
	u32 mapped_shader_ids = 0;
	u32 nonzero_shader_ids = 0;
	for (size_t i = 0; i < visuals.size(); ++i)
	{
		if (visuals[i].shader_id < shader_names.size())
			++mapped_shader_ids;
		if (visuals[i].shader_id != 0)
			++nonzero_shader_ids;
	}
	char summary[96];
	snprintf(summary, sizeof(summary), "debug_level_visual_ranges=%u\n", static_cast<unsigned>(visuals.size()));
	write_boot_log(g_boot_log, summary);
	snprintf(summary, sizeof(summary), "debug_level_visual_shader_ids_mapped=%u\n", mapped_shader_ids);
	write_boot_log(g_boot_log, summary);
	snprintf(summary, sizeof(summary), "debug_level_visual_shader_ids_nonzero=%u\n", nonzero_shader_ids);
	write_boot_log(g_boot_log, summary);
	return !visuals.empty();
}

static void compute_level_bounds(VitaGLR1StaticBuffers& buffers, float center[3], float& scale)
{
	float minimum[3] = {0.f, 0.f, 0.f};
	float maximum[3] = {0.f, 0.f, 0.f};
	bool has_bounds = false;
	for (size_t i = 0; i < buffers.vertex_buffer_count(); ++i)
	{
		VitaGLVertexBuffer* vertices = buffers.vertex_buffer(i);
		float current_min[3], current_max[3];
		if (!vertices || !vertices->position_bounds(current_min, current_max))
			continue;
		if (!has_bounds)
		{
			memcpy(minimum, current_min, sizeof(minimum));
			memcpy(maximum, current_max, sizeof(maximum));
			has_bounds = true;
		}
		else for (u32 axis = 0; axis < 3; ++axis)
		{
			if (current_min[axis] < minimum[axis]) minimum[axis] = current_min[axis];
			if (current_max[axis] > maximum[axis]) maximum[axis] = current_max[axis];
		}
	}
	center[0] = (minimum[0] + maximum[0]) * 0.5f;
	center[1] = (minimum[1] + maximum[1]) * 0.5f;
	center[2] = (minimum[2] + maximum[2]) * 0.5f;
	float extent = maximum[0] - minimum[0];
	if (maximum[1] - minimum[1] > extent) extent = maximum[1] - minimum[1];
	if (maximum[2] - minimum[2] > extent) extent = maximum[2] - minimum[2];
	scale = has_bounds && extent > 0.f ? 1.6f / extent : 0.f;
}

static void compute_visual_bounds(
	const std::vector<VitaDebugVisualRange>& visuals, float center[3], float& scale)
{
	bool use_focus = false;
	for (size_t i = 0; i < visuals.size(); ++i)
		if (visuals[i].focus_sector && visuals[i].has_bounds) use_focus = true;
	float minimum[3] = {0.f, 0.f, 0.f};
	float maximum[3] = {0.f, 0.f, 0.f};
	bool has_bounds = false;
	for (size_t i = 0; i < visuals.size(); ++i)
	{
		const VitaDebugVisualRange& visual = visuals[i];
		if (!visual.has_bounds || (use_focus && !visual.focus_sector)) continue;
		if (!has_bounds)
		{
			memcpy(minimum, visual.bounds_minimum, sizeof(minimum));
			memcpy(maximum, visual.bounds_maximum, sizeof(maximum));
			has_bounds = true;
		}
		else for (size_t axis = 0; axis < 3; ++axis)
		{
			if (visual.bounds_minimum[axis] < minimum[axis]) minimum[axis] = visual.bounds_minimum[axis];
			if (visual.bounds_maximum[axis] > maximum[axis]) maximum[axis] = visual.bounds_maximum[axis];
		}
	}
	if (!has_bounds) return;
	for (size_t axis = 0; axis < 3; ++axis)
		center[axis] = (minimum[axis] + maximum[axis]) * 0.5f;
	float extent = maximum[0] - minimum[0];
	if (maximum[1] - minimum[1] > extent) extent = maximum[1] - minimum[1];
	if (maximum[2] - minimum[2] > extent) extent = maximum[2] - minimum[2];
	if (extent > 0.f) scale = 1.6f / extent;
}

static bool load_escape_actor_spawn(float position[3])
{
	CStreamReader* spawn = FS.rs_open("$game_levels$", "l01_escape\\level.spawn");
	if (!spawn)
	{
		write_boot_log(g_boot_log, "debug_actor_spawn_file_missing\n");
		return false;
	}
	std::vector<u8> packet;
	bool found = false;
	while (spawn->tell() <= spawn->length() && spawn->length() - spawn->tell() >= 8)
	{
		const u32 chunk_id = spawn->r_u32();
		const u32 packet_size = spawn->r_u32();
		if (packet_size > spawn->length() - spawn->tell()) break;
		if (packet_size > 65536) break;
		if (packet_size < 2)
		{
			spawn->advance(static_cast<int>(packet_size));
			continue;
		}
		packet.resize(packet_size);
		spawn->r(packet.data(), packet_size);
		u16 message_id = 0;
		memcpy(&message_id, packet.data(), sizeof(message_id));
		if (message_id != 1) continue; // M_SPAWN
		size_t cursor = 2;
		const char* section = reinterpret_cast<const char*>(packet.data() + cursor);
		const void* section_end = memchr(section, 0, packet.size() - cursor);
		if (!section_end) continue;
		cursor += static_cast<const char*>(section_end) - section + 1;
		if (cursor >= packet.size()) continue;
		const char* replacement = reinterpret_cast<const char*>(packet.data() + cursor);
		const void* replacement_end = memchr(replacement, 0, packet.size() - cursor);
		if (!replacement_end) continue;
		cursor += static_cast<const char*>(replacement_end) - replacement + 1;
		if (strncasecmp(section, "actor", 5) != 0 ||
			packet.size() - cursor < 2 + 3 * sizeof(float)) continue;
		cursor += 2; // game ID and respawn point
		memcpy(position, packet.data() + cursor, 3 * sizeof(float));
		if (position[0] != position[0] || position[1] != position[1] || position[2] != position[2] ||
			fabsf(position[0]) > 1000000.f || fabsf(position[1]) > 1000000.f ||
			fabsf(position[2]) > 1000000.f) continue;
		char spawn_log[128];
		snprintf(spawn_log, sizeof(spawn_log),
			"debug_actor_spawn_chunk=%u position=%.2f,%.2f,%.2f\n",
			chunk_id, position[0], position[1], position[2]);
		write_boot_log(g_boot_log, spawn_log);
		found = true;
		break;
	}
	FS.r_close(spawn);
	if (!found) write_boot_log(g_boot_log, "debug_actor_spawn_not_found\n");
	return found;
}

struct GameSpawnPose
{
	float position[3] = {};
	float angles[3] = {};
	bool found = false;
};

static bool load_game_actor_spawn(float position[3], float trader_position[3], float trader_angles[3],
	bool& trader_found, GameSpawnPose& door_spawn, GameSpawnPose& wolf_spawn)
{
	CStreamReader* spawn = FS.rs_open("$game_spawn$", "all.spawn");
	if (!spawn) spawn = FS.rs_open("$game_spawn$", "game.spawn");
	if (!spawn)
	{
		xr_vector<LPSTR>* files = FS.file_list_open("$fs_root$", FS_ListFiles);
		if (files)
		{
			for (xr_vector<LPSTR>::iterator it = files->begin(); it != files->end(); ++it)
			{
				const char* name = strrchr(*it, '\\');
				const char* slash = strrchr(*it, '/');
				if (slash && (!name || slash > name)) name = slash;
				name = name ? name + 1 : *it;
				if (strcasecmp(name, "all.spawn") != 0 && strcasecmp(name, "game.spawn") != 0)
					continue;
				char path_log[1100];
				const int length = snprintf(path_log, sizeof(path_log), "debug_game_spawn_path=%s\n", *it);
				if (length > 0 && static_cast<size_t>(length) < sizeof(path_log))
					write_boot_log(g_boot_log, path_log);
				spawn = FS.rs_open("$fs_root$", *it);
				if (spawn) break;
			}
			FS.file_list_close(files);
		}
	}
	if (!spawn)
	{
		write_boot_log(g_boot_log, "debug_game_spawn_file_missing\n");
		return false;
	}
	CStreamReader* graph = spawn->open_chunk(1);
	CStreamReader* vertices = graph ? graph->open_chunk(1) : 0;
	bool found = false;
	u32 actor_vertex = 0;
	if (vertices)
	{
		for (u32 vertex_id = 0; vertex_id < 100000; ++vertex_id)
		{
			CStreamReader* vertex = vertices->open_chunk(vertex_id);
			if (!vertex) break;
			CStreamReader* data = vertex->open_chunk(1);
			CStreamReader* packet_chunk = data ? data->open_chunk(0) : nullptr;
			if (packet_chunk && packet_chunk->elapsed() >= sizeof(u16))
			{
				const u16 packet_size = packet_chunk->r_u16();
				if (packet_size >= 2 && packet_size <= packet_chunk->elapsed())
				{
					std::vector<u8> packet(packet_size);
					packet_chunk->r(packet.data(), packet_size);
					u16 message_id = 0;
					memcpy(&message_id, packet.data(), sizeof(message_id));
					if (message_id == 1) // M_SPAWN
					{
						const char* section = reinterpret_cast<const char*>(packet.data() + 2);
						const char* end = reinterpret_cast<const char*>(packet.data() + packet.size());
						const char* section_end = static_cast<const char*>(memchr(section, 0, end - section));
						if (section_end && section_end + 1 < end)
						{
							const char* name = section_end + 1;
							const char* name_end = static_cast<const char*>(memchr(name, 0, end - name));
							const bool actor = !found && strcmp(section, "actor") == 0;
							const bool trader = !trader_found && strcmp(section, "m_trader") == 0 &&
								name_end && strcmp(name, "escape_trader") == 0;
							const bool door = !door_spawn.found && strcmp(section, "physic_object") == 0 &&
								name_end && strcmp(name, "trader_door") == 0;
							const bool wolf = !wolf_spawn.found && strcmp(section, "stalker") == 0 &&
								name_end && strcmp(name, "esc_wolf") == 0;
							if (name_end && (actor || trader || door || wolf) &&
								static_cast<size_t>(end - (name_end + 1)) >= 2 + (actor ? 3 : 6) * sizeof(float))
							{
								float coordinates[3];
								memcpy(coordinates, name_end + 3, sizeof(coordinates));
								if (std::all_of(coordinates, coordinates + 3,
									[](float value) { return std::isfinite(value) && fabsf(value) < 1000000.f; }))
								{
									auto& object = wolf ? wolf_spawn : door_spawn;
									std::copy(coordinates, coordinates + 3, actor ? position : trader ? trader_position : object.position);
									char line[160];
									snprintf(line, sizeof(line), "debug_game_%s_spawn_vertex=%u position=%.2f,%.2f,%.2f\n",
										actor ? "actor" : trader ? "trader" : wolf ? "wolf" : "trader_door", vertex_id, coordinates[0], coordinates[1], coordinates[2]);
									write_boot_log(g_boot_log, line);
									if (actor)
									{
										found = true;
										actor_vertex = vertex_id;
									}
									else
									{
										float* angles = trader ? trader_angles : object.angles;
										bool& pose_found = trader ? trader_found : object.found;
										memcpy(angles, name_end + 3 + sizeof(coordinates), sizeof(coordinates));
										pose_found = std::all_of(angles, angles + 3,
											[](float value) { return std::isfinite(value); });
									}
								}
							}
						}
					}
				}
			}
			if (packet_chunk) FS.r_close(packet_chunk);
			if (data) FS.r_close(data);
			FS.r_close(vertex);
			if (found && ((trader_found && door_spawn.found && wolf_spawn.found) || vertex_id - actor_vertex >= 128))
				break;
		}
		FS.r_close(vertices);
	}
	if (graph) FS.r_close(graph);
	FS.r_close(spawn);
	if (!found) write_boot_log(g_boot_log, "debug_game_actor_spawn_not_found\n");
	return found;
}

static bool upload_bunker_mesh(const VitaGLDebugRenderer& renderer, const std::vector<VitaTraderVertex>& source_vertices,
	const std::vector<u16>& source_indices, VitaGLVertexBuffer& vertices, VitaGLIndexBuffer& indices)
{
	const VitaD3D9VertexElement declaration[] = {
		{0, 0, 2, 0, 0, 0}, {0, 12, 1, 0, 5, 0}, {0xff, 0, 17, 0, 0, 0}
	};
	const VitaGLSemanticBinding bindings[] = {
		{0, 0, static_cast<GLuint>(renderer.position_attribute_location())},
		{5, 0, static_cast<GLuint>(renderer.texcoord_attribute_location())}
	};
	if (!vertices.initialize(source_vertices.data(), source_vertices.size() * sizeof(VitaTraderVertex),
		declaration, sizeof(declaration), bindings, 2) ||
		!indices.initialize(source_indices.data(), source_indices.size()))
	{
		vertices.release();
		indices.release();
		return false;
	}
	vertices.discard_position_data();
	indices.discard_cpu_copy();
	return true;
}

static bool load_trader_model(const VitaGLDebugRenderer& renderer, VitaGLVertexBuffer& vertices,
	VitaGLIndexBuffer& indices, VitaGLTexture& texture, float target[3], bool& seated)
{
	string_path path;
	const CLocatorAPI::file* asset = FS.exist(path, "$fs_root$", "gamedata\\meshes\\actors\\trader\\trader.ogf");
	if (!asset || asset->size_real > 2u * 1024u * 1024u)
		return false;
	IReader* reader = FS.r_open(path);
	if (!reader)
		return false;

	std::vector<VitaTraderVertex> source_vertices;
	std::vector<float> animation_positions;
	std::vector<u16> source_indices;
	std::string texture_name;
	const auto* motion_asset = FS.exist(path, "$fs_root$", "gamedata\\meshes\\actors\\trader_animation.omf");
	IReader* motion = motion_asset && motion_asset->size_real <= 4u * 1024u * 1024u ? FS.r_open(path) : nullptr;
	const int motion_length = motion ? motion->length() : 0;
	const int length = reader->length();
	const bool parsed = length > 0 && read_vita_trader_model(reader->pointer(), static_cast<size_t>(length),
		motion_length > 0 ? motion->pointer() : nullptr, motion_length > 0 ? static_cast<size_t>(motion_length) : 0,
		source_vertices, source_indices, texture_name, target, animation_positions);
	if (motion) FS.r_close(motion);
	FS.r_close(reader);
	if (!parsed)
		return false;

	if (!upload_bunker_mesh(renderer, source_vertices, source_indices, vertices, indices))
		return false;
	seated = !animation_positions.empty();
	if (seated)
		vertices.upload_position_frames(static_cast<GLuint>(renderer.position_attribute_location()),
			animation_positions.data(), animation_positions.size() / (3 * vertices.vertex_count()));

	bool source_found = false;
	load_level_dds_texture(texture_name, texture, 512u * 512u * 4u, source_found, 512);
	return true;
}

static void read_stalker_clips(std::vector<u8>& params, std::vector<u8>& keys, std::vector<u8>* walk, std::vector<u8>* unarmed, std::array<VitaNpcMotions, 3>& npc_motions)
{
	string_path path;
	const auto* asset = FS.exist(path, "$fs_root$", "gamedata\\meshes\\actors\\stalker_animation.omf");
	if (!asset || asset->size_real != asset->size_compressed)
		return;
	CStreamReader* motion = FS.rs_open(nullptr, path);
	if (!motion)
		return;
	const auto read_chunk = [](CStreamReader* chunk, std::vector<u8>& bytes, size_t limit)
	{
		if (!chunk)
			return;
		if (chunk->length() <= limit)
		{
			bytes.resize(chunk->length());
			if (!bytes.empty())
				chunk->r(bytes.data(), static_cast<u32>(bytes.size()));
		}
		FS.r_close(chunk);
	};
	read_chunk(motion->open_chunk(15), params, 256u * 1024u); // OGF_S_SMPARAMS
	CStreamReader* animations = motion->open_chunk(14); // OGF_S_MOTIONS
	if (animations)
	{
		// The retail Cordon resources store norm_idle_1_ in this chunk.
		read_chunk(animations->open_chunk(77), keys, 128u * 1024u);
		if (walk)
			read_chunk(animations->open_chunk(127), *walk, 128u * 1024u); // norm_walk_fwd_1
		if (unarmed) read_chunk(animations->open_chunk(1414), *unarmed, 128u * 1024u); // norm_idle_0
		const unsigned slots[3][6] = {{262,263,1340,1342,1349,78},{269,270,1350,1352,1359,78},{259,260,1330,1332,1339,78}};
		for (unsigned actor=0;actor<3;++actor)
			for (unsigned clip=0;clip<6;++clip)
				read_chunk(animations->open_chunk(slots[actor][clip]), npc_motions[actor][clip], 256u * 1024u);
		FS.r_close(animations);
	}
	FS.r_close(motion);
}

struct StalkerVisual
{
	VitaGLVertexBuffer vertices;
	VitaGLIndexBuffer indices;
	VitaGLTexture texture;
	VitaStalkerWeaponPose weapon_pose;
	VitaNpcRanges animations;
    VitaGLIndexBuffer addon_indices[2];
    bool body_visible=true;
};


static bool load_npc_weapon(const VitaGLDebugRenderer& renderer, const char* name, StalkerVisual (&parts)[2])
{
	string_path path;
	const auto* asset = FS.exist(path, "$fs_root$", name);
	IReader* reader = asset && asset->size_real <= 512u * 1024u ? FS.r_open(path) : nullptr;
	if (!reader) return false;
	std::vector<VitaStalkerMesh> meshes;
	const bool parsed = read_vita_weapon_world(reader->pointer(), reader->length(), meshes);
	FS.r_close(reader);
	if (!parsed || meshes.size() > 2) return false;
	for (size_t i = 0; i < meshes.size(); ++i)
	{
		auto& mesh = meshes[i];
		if (!upload_bunker_mesh(renderer, mesh.vertices, mesh.indices, parts[i].vertices, parts[i].indices)) return false;
		bool found = false;
		if (!load_level_dds_texture(mesh.texture, parts[i].texture, 256u * 256u * 4u, found, 256)) return false;
	}
	return true;
}

template <size_t Count>
static bool load_weapon_hud(const VitaGLDebugRenderer& renderer, const char* name, bool pistol,
	StalkerVisual (&parts)[Count], VitaWeaponClips& clips,unsigned kind=0,bool addons=false)
{
	string_path path;
	const auto* asset = FS.exist(path, "$fs_root$", name);
	IReader* reader = asset && asset->size_real <= 1024u * 1024u ? FS.r_open(path) : nullptr;
	if (!reader)
		return false;
	std::vector<VitaWeaponHudMesh> meshes;
	const bool parsed = read_vita_weapon_hud(reader->pointer(), reader->length(), pistol, meshes, clips,kind,addons);
	FS.r_close(reader);
	if (!parsed || meshes.size() > Count)
		return false;
	bool animation_uploaded = true;
	for (size_t i = 0; i < meshes.size(); ++i)
	{
		auto& mesh = meshes[i];
		auto& part = parts[i];
		if (!upload_bunker_mesh(renderer, mesh.vertices, mesh.indices, part.vertices, part.indices))
			return false;
        if(addons){
            std::vector<u16> body,addon[2];
            for(size_t tri=0;tri<mesh.indices.size()/3;++tri){
                const unsigned flags=mesh.triangle_addons[tri];
                if(flags&8)continue;
                auto& indices=(flags&1)?addon[0]:(flags&4)?addon[1]:body;
                indices.insert(indices.end(),mesh.indices.begin()+tri*3,mesh.indices.begin()+tri*3+3);
            }
            part.body_visible=!body.empty();
            if(!body.empty()){part.indices.release();if(!part.indices.initialize(body.data(),body.size()))return false;part.indices.discard_cpu_copy();}
            for(unsigned a=0;a<2;++a)if(!addon[a].empty()){
                if(!part.addon_indices[a].initialize(addon[a].data(),addon[a].size()))return false;
                part.addon_indices[a].discard_cpu_copy();
            }
        }
		const size_t frames = mesh.positions.size() / (3 * mesh.vertices.size());
		if (frames > 1 && !part.vertices.upload_position_frames(static_cast<GLuint>(renderer.position_attribute_location()),
			mesh.positions.data(), frames))
			animation_uploaded = false;
		std::vector<float>().swap(mesh.positions);
		std::transform(mesh.texture.begin(), mesh.texture.end(), mesh.texture.begin(), [](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
		bool source_found = false;
		if (!load_level_dds_texture(mesh.texture, part.texture, 256u * 256u * 4u, source_found, 256))
			return false;
	}
	if (!animation_uploaded){clips={};write_boot_log(g_boot_log,"debug_weapon_hud animation_fallback=1\n");}
	return true;
}

static float guard_ray_distance(const float origin[3], const float direction[3], const Fvector& feet, float range)
{
	const float minimum[] = {feet.x - 0.30f, feet.y + 0.07f, feet.z - 0.30f};
	const float maximum[] = {feet.x + 0.30f, feet.y + 1.76f, feet.z + 0.30f};
	float near = 0.035f, far = range;
	for (unsigned axis = 0; axis < 3; ++axis)
	{
		if (fabsf(direction[axis]) < 1e-6f)
		{
			if (origin[axis] < minimum[axis] || origin[axis] > maximum[axis])
				return range;
			continue;
		}
		const float a = (minimum[axis] - origin[axis]) / direction[axis];
		const float b = (maximum[axis] - origin[axis]) / direction[axis];
		near = std::max(near, std::min(a, b));
		far = std::min(far, std::max(a, b));
		if (near > far)
			return range;
	}
	return near;
}

static bool load_stalker_model(const VitaGLDebugRenderer& renderer, const char* model_path, StalkerVisual (&visuals)[2],
	const std::vector<u8>& params, const std::vector<u8>& keys, float target[3], bool& posed, bool load_body = true, const std::vector<u8>* walk = nullptr, const VitaNpcMotions* motions = nullptr)
{
	string_path path;
	const auto* asset = FS.exist(path, "$fs_root$", model_path);
	if (!asset || asset->size_real > 2u * 1024u * 1024u)
		return false;
	IReader* reader = FS.r_open(path);
	if (!reader)
		return false;

	VitaStalkerMesh parts[2];
	const int length = reader->length();
	const bool parsed = length > 0 && read_vita_stalker_model(reader->pointer(), static_cast<size_t>(length),
		params, keys, parts, target, posed, walk, &visuals[0].weapon_pose);
	if (parsed && posed && motions && walk)
	{
		const bool animated = read_vita_npc_animations(reader->pointer(), length, params, keys, *walk, *motions,
			parts, visuals[0].weapon_pose, visuals[0].animations);
		char line[100]; snprintf(line,sizeof(line),"debug_npc_animation ready=%u frames=%u\n",unsigned(animated),
			unsigned(visuals[0].weapon_pose.walk.size()));write_boot_log(g_boot_log,line);
	}
	FS.r_close(reader);
	if (!parsed) return false;

	bool animation_uploaded=true;
	for (unsigned i = 0; i < 2; ++i)
	{
		auto& part = parts[i];
		auto& visual = visuals[i];
		if (!upload_bunker_mesh(renderer, part.vertices, part.indices, visual.vertices, visual.indices))
			return false;
		if (!part.walk_positions.empty())
		{
			// Frame zero is the existing standing pose used at the rendezvous.
			std::vector<float> frames;
			frames.reserve(3 * part.vertices.size() + part.walk_positions.size());
			for (const auto& vertex : part.vertices)
				frames.insert(frames.end(), vertex.position, vertex.position + 3);
			frames.insert(frames.end(), part.walk_positions.begin(), part.walk_positions.end());
			animation_uploaded = visual.vertices.upload_position_frames(static_cast<GLuint>(renderer.position_attribute_location()),
				frames.data(), frames.size() / (3 * part.vertices.size())) && animation_uploaded;
		}
		if (i != 0 || load_body)
		{
			bool source_found = false;
			load_level_dds_texture(part.texture, visual.texture, 512u * 512u * 4u, source_found, 512);
		}
	}
	if(!animation_uploaded)
	{
		visuals[0].animations={};visuals[0].weapon_pose.walk.clear();
		write_boot_log(g_boot_log,"debug_npc_animation gpu_fallback=1\n");
	}
	return true;
}

static bool load_bunker_door(const VitaGLDebugRenderer& renderer, VitaGLVertexBuffer& vertices,
	VitaGLIndexBuffer& indices, VitaGLTexture& texture, float minimum[3], float maximum[3])
{
	string_path path;
	const auto* asset = FS.exist(path, "$fs_root$", "gamedata\\meshes\\physics\\door\\door_trader.ogf");
	if (!asset || asset->size_real > 512u * 1024u)
		return false;
	IReader* reader = FS.r_open(path);
	if (!reader)
		return false;
	std::vector<VitaTraderVertex> source_vertices;
	std::vector<u16> source_indices;
	std::string texture_name;
	const int length = reader->length();
	const bool parsed = length > 0 && read_vita_trader_door(reader->pointer(), static_cast<size_t>(length),
		source_vertices, source_indices, texture_name);
	FS.r_close(reader);
	if (!parsed || !upload_bunker_mesh(renderer, source_vertices, source_indices, vertices, indices) ||
		!vertices.position_bounds(minimum, maximum))
		return false;
	bool source_found = false;
	load_level_dds_texture(texture_name, texture, 512u * 512u * 4u, source_found, 512);
	return true;
}

constexpr float bunker_door_open_angle = -1.9198622f;

static VitaCformGround::DoorPanel bunker_door_panel(const Fmatrix& matrix, const float minimum[3], const float maximum[3])
{
	const float perimeter[4][2] = {
		{minimum[0], minimum[2]}, {maximum[0], minimum[2]},
		{maximum[0], maximum[2]}, {minimum[0], maximum[2]}
	};
	VitaCformGround::DoorPanel panel{};
	panel.bottom = matrix.c.y + minimum[1];
	panel.top = matrix.c.y + maximum[1];
	for (unsigned i = 0; i < 4; ++i)
	{
		Fvector corner;
		corner.set(perimeter[i][0], minimum[1], perimeter[i][1]);
		matrix.transform_tiny(corner);
		panel.corners[i][0] = corner.x;
		panel.corners[i][1] = corner.z;
	}
	return panel;
}

// The original Cordon stairs include a taller lip at the exterior threshold.
static bool can_stand_at(const VitaCformGround& ground, float x, float z, float y, float height)
{
	constexpr float offsets[][2] = {{0.f, 0.f}, {0.24f, 0.f}, {-0.24f, 0.f}, {0.f, 0.24f}, {0.f, -0.24f}};
	for (const auto& offset : offsets)
	{
		float ceiling;
		if (ground.height_at(x + offset[0], z + offset[1], y + height, 1.8f - height, 0.f, ceiling))
			return false;
	}
	return true;
}

static bool at_bunker_stairs(float x, float z)
{
	return x > -257.f && x < -251.25f && z > -135.5f && z < -125.8f;
}

static bool player_ground_height(const VitaCformGround& ground, float x, float z, float old_x, float old_z,
	bool bunker_stairs, float& y)
{
	const float rise = bunker_stairs ? 0.65f : 0.45f;
	return ground.support_at(x,z,y,rise,.75f,y);

}

static bool is_looking_at(const float eye[3], float yaw, float pitch, const Fvector& target)
{
	constexpr float focus_cosine = 0.9063078f; // 25 degrees from the centre of the view.
	const float x = target.x - eye[0];
	const float y = target.y - eye[1];
	const float z = target.z - eye[2];
	const float forward = sinf(pitch) * (sinf(yaw) * x + cosf(yaw) * z) - cosf(pitch) * y;
	return forward > focus_cosine * sqrtf(x * x + y * y + z * z);
}

static bool load_escape_dialog_xml(std::string& xml)
{
	string_path path;
	const CLocatorAPI::file* asset = FS.exist(path, "$fs_root$",
		vita_text("gamedata\\config\\text\\fra\\stable_dialogs_escape.xml","gamedata\\config\\text\\eng\\stable_dialogs_escape.xml"));
	if (!asset || asset->size_real > 1024u * 1024u)
		return false;
	IReader* reader = FS.r_open(path);
	if (!reader)
		return false;
	xml.assign(static_cast<const char*>(reader->pointer()), reader->length());
	FS.r_close(reader);
	return true;
}

static bool read_wolf_lines(const std::string& xml, VitaWolfLines& lines)
{
	const char* keys[] = {
		"11", "111", "1111", "11111", "111111", "1111111", "11111111",
		"111111111", "111111112", "1111111111", "111111121"
	};
	for (size_t i = 0; i < lines.size(); ++i)
	{
		const std::string key = std::string("escape_lager_volk_talk_") + keys[i];
		if (!read_dialog_line(xml, key.c_str(), lines[i]))
			return false;
	}
	return true;
}

static bool read_scout_lines(const std::string& xml, VitaScoutLines& lines)
{
	const char* const keys[] = {
		"31", "311", "3111", "31111", "31112", "311121"
	};
	for (size_t i = 0; i < lines.size(); ++i)
	{
		const std::string key = std::string("escape_factory_assault_start_") + keys[i];
		if (!read_dialog_line(xml, key.c_str(), lines[i]))
			return false;
	}
	return true;
}

static bool read_flash_lines(const std::string& xml, VitaFlashLines& lines)
{
	return read_dialog_line(xml, "escape_shustryi_start_11", lines.allegro[0]) &&
		read_dialog_line(xml, "escape_shustryi_start_111", lines.allegro[1]) &&
		read_dialog_line(xml, "escape_shustryi_start_1111", lines.allegro[2]) &&
		read_dialog_line(xml, "escape_trader_start_dialog_521211", lines.trader[0]) &&
		read_dialog_line(xml, "escape_trader_start_dialog_5212111", lines.trader[1]);
}

static std::string read_strelok_information()
{
 string_path path;
 const auto file=std::string("gamedata\\config\\text\\")+vita_text("fra\\","eng\\")+"stable_storyline_info_agroprom.xml";
 const auto*asset=FS.exist(path,"$fs_root$",file.c_str());
 IReader*r=asset&&asset->size_real<1024u*1024u?FS.r_open(path):nullptr;
 std::string text;
 if(r){const std::string xml(static_cast<const char*>(r->pointer()),r->length());
  read_dialog_line(xml,"storyline_gunslinger_flash_info",text,8192);FS.r_close(r);}
 size_t offset=0;while((offset=text.find("\\n",offset))!=std::string::npos)text.replace(offset,2," ");
 return text.empty()?vita_text("Informations de Strelok. Prochain objectif : trouver Spectre.",
  "Strelok's information. Next objective: find Ghost."):text;
}

static void load_inventory_text(VitaInventoryUI& ui)
{
	const char* const files[] = {"string_table_enc_weapons.xml", "string_table_general.xml", "string_table_items.xml", "string_table_enc_equipment.xml", "string_table_outfit.xml", "string_table_quest_items.xml"};
	unsigned names_loaded = 0, details_loaded = 0;
    for(unsigned i=0;i<vita_item_count;++i){
        ui.item_names[i]=vita_text(vita_item_art[i].french,vita_item_art[i].english);
        if(vita_language||ui.item_details[i].empty())ui.item_details[i]=ui.item_names[i];
    }

	for (const char* file : files)
	{
		string_path path;
		const std::string resource = std::string("gamedata\\config\\text\\")+vita_text("fra\\","eng\\")+file;
		const auto* asset = FS.exist(path, "$fs_root$", resource.c_str());
		IReader* source = asset && asset->size_real <= 1024u * 1024u ? FS.r_open(path) : nullptr;
		if (!source)
			continue;
		const std::string xml(static_cast<const char*>(source->pointer()), source->length());
		FS.r_close(source);
		for (size_t i = 0; i < ui.item_names.size(); ++i)
		{
			std::string text;
			if (read_dialog_line(xml, vita_item_art[i].name_key, text))
			{
				ui.item_names[i] = text;
				++names_loaded;
			}
			if (read_dialog_line(xml, vita_item_art[i].description_key, text))
			{
				size_t offset = 0;
				while ((offset = text.find("\\n", offset)) != std::string::npos)
					text.replace(offset, 2, " ");
				ui.item_details[i] = text;
				++details_loaded;
			}
		}
	}
	ui.item_details[23]=read_strelok_information();
	char line[96];
	snprintf(line, sizeof(line), "debug_inventory_text names=%u descriptions=%u source=fra\n",
		names_loaded, details_loaded);
	write_boot_log(g_boot_log, line);
}

static void load_inventory_art(VitaInventoryUI& ui)
{
    bool source_found = false;
    load_level_dds_texture("ui\\ui_inventory2",ui.trade_art,1024u*1024u*4,source_found,1024);
    load_level_dds_texture("wpn\\wpn_crosshair",ui.scope,512u*512u*4,source_found,512);
    const VitaDDSRegion actor{1024,692,216,168,108,0};
    load_level_dds_texture("ui\\ui_iconstotal",ui.actor_portrait,168*108*4,source_found,1024,nullptr,&actor);
    const VitaDDSRegion bag{1024, 0, 0, 1008, 484, 0};
	const VitaDDSRegion slots{1024, 0, 748, 1024, 176, 0};
	const bool bag_ready = load_level_dds_texture("ui\\ui_inventory", ui.panel,
		bag.width * bag.height * 4u, source_found, 1024, nullptr, &bag);
	const bool slots_ready = load_level_dds_texture("ui\\ui_inventory2", ui.slots,
		slots.width * slots.height * 4u, source_found, 1024, nullptr, &slots);
	const VitaDDSRegion portrait{2048, 0, 704, 128, 320, 1024};
	const bool portrait_ready = load_level_dds_texture("ui\\ui_icons_npc", ui.portrait,
		portrait.width * portrait.height * 4u, source_found, 1024, nullptr, &portrait);
	const VitaDDSRegion gauges{1024, 208, 0, 180, 220, 0};
	const bool gauges_ready = load_level_dds_texture("ui\\ui_inventory2", ui.gauges,
		gauges.width * gauges.height * 4u, source_found, 1024, nullptr, &gauges);
	string_path path;
	IReader* source = resolve_level_dds("ui\\ui_icon_equipment", path) ? FS.r_open(path) : nullptr;
	unsigned icons_loaded = 0;
	if (source)
	{
		// The original equipment atlas uses a 50-pixel grid. Include whole DXT5 blocks.
		for (size_t i = 0; i < ui.icons.size(); ++i)
		{
            const auto& item=vita_item_art[i];
            const unsigned x=item.x*50,y=item.y*50,left=x&~3u,top=y&~3u;
            const VitaDDSRegion region{1024,left,top,((x+item.columns*50+3)&~3u)-left,
                ((y+item.rows*50+3)&~3u)-top,2048};
			icons_loaded += ui.icons[i].initialize_dxt5_region(source->pointer(), source->length(),
				region.width * region.height * 4u, region);
		}
		FS.r_close(source);
	}
	char line[108];
	snprintf(line, sizeof(line), "debug_inventory_art bag=%u slots=%u icons=%u portrait=%u gauges=%u\n",
		static_cast<unsigned>(bag_ready), static_cast<unsigned>(slots_ready), icons_loaded,
		static_cast<unsigned>(portrait_ready), static_cast<unsigned>(gauges_ready));
	write_boot_log(g_boot_log, line);
}

static void apply_audio_settings(VitaGameAudio& audio,const VitaSettings& settings)
{
	const auto& v=settings.volume;audio.set_levels(v[0],v[1],v[2],v[3],v[4]);
}
static void edit_options(VitaGLDevice& graphics,VitaMainMenu& menu,VitaGameAudio& audio,VitaSettings& settings)
{
	VitaOptionsMenu options;
	if(!options.initialize(settings)){write_boot_log(g_boot_log,"debug_options ready=0\n");return;}
	bool found=false;
    const VitaDDSRegion frame{1024,164,232,568,464,0};
    load_level_dds_texture("ui\\ui_common",options.frame,frame.width*frame.height*4,found,1024,nullptr,&frame);
    load_level_dds_texture("ui\\ui_common",options.tabs,1024u*1024u*4,found,1024);
    load_level_dds_texture("ui\\ui_static_mm_back_03",options.background,1024u*1024u*4,found,1024);
    unsigned choice=0;
    SceCtrlData pad{};sceCtrlPeekBufferPositive(0,&pad,1);u32 previous=pad.buttons;
	for(;;)
	{
		sceCtrlPeekBufferPositive(0,&pad,1);const u32 pressed=pad.buttons&~previous;previous=pad.buttons;
        if(pressed&SCE_CTRL_CIRCLE)break;
        if(pressed&(SCE_CTRL_LTRIGGER|SCE_CTRL_RTRIGGER)){
            options.tab=(options.tab+((pressed&SCE_CTRL_LTRIGGER)?3:1))%4;choice=0;}
        if(pressed&(SCE_CTRL_UP|SCE_CTRL_DOWN))choice=(choice+((pressed&SCE_CTRL_UP)?options.rows()-1:1))%options.rows();
        if(pressed&(SCE_CTRL_LEFT|SCE_CTRL_RIGHT|SCE_CTRL_CROSS)){
            settings.change(options.setting_row(choice),(pressed&SCE_CTRL_LEFT)?-1:1);
            apply_audio_settings(audio,settings);options.refresh(settings);}
		options.draw(graphics,menu,choice,true);
	}
	const bool saved=write_vita_settings("ux0:data/stalker",settings);
	write_boot_log(g_boot_log,saved?"debug_options saved=1\n":"debug_options saved=0\n");
	if(graphics.set_resolution(settings.width(),settings.height()))graphics.present();
	char line[128];snprintf(line,sizeof(line),"debug_options resolution=%dx%d aa=%u dof=%u active_msaa=%u hud_scale=%u\n",
		settings.width(),settings.height(),settings.aa,settings.dof,graphics.msaa(),settings.hud_scale);write_boot_log(g_boot_log,line);
	do{sceCtrlPeekBufferPositive(0,&pad,1);sceKernelDelayThread(10000);}while(pad.buttons&(SCE_CTRL_CIRCLE|SCE_CTRL_CROSS|SCE_CTRL_SQUARE));
}



static std::vector<std::string> read_campaign_pages(unsigned zone,bool cleared,bool road=false)
{
 if(zone>2)return {};
 const char* files[]={"escape","garbage","agroprom"};
 const std::string path_name=std::string("gamedata\\config\\text\\")+(vita_language?"eng":"fra")+"\\stable_dialogs_"+files[zone]+".xml";
 string_path path;const auto*asset=FS.exist(path,"$fs_root$",path_name.c_str());
 std::string xml;
 if(asset&&asset->size_real<1024u*1024u){IReader*r=FS.r_open(path);if(r){xml.assign(static_cast<const char*>(r->pointer()),r->length());FS.r_close(r);}}
 return road?read_vita_road_pages(xml):read_vita_campaign_pages(xml,zone,cleared);
}
static bool relaunch_vita_state(VitaGLDevice& graphics,const VitaSaveState& state,bool new_game=false)
{
 for(const char*asset:{"level","level.geom","level.cform"}){
  string_path path;const auto local=std::string(vita_campaign_zones[state.zone].directory)+"\\"+asset;
  if(!FS.exist(path,"$game_levels$",local.c_str())&&!FS.exist(path,"$fs_root$",(std::string("gamedata\\levels\\")+local).c_str())){
   write_boot_log(g_boot_log,"debug_zone preflight_missing=1\n");return false;
  }
 }
 sceIoMkdir("ux0:data/stalker/transition",0777);
 if(!new_game&&!write_vita_save("ux0:data/stalker/transition",state))return false;
 VitaLoadingScreen screen;
 if(!screen.initialize())return false;
 bool found=false;
 load_level_dds_texture("ui\\ui_load",screen.atlas,1024u*1024u*4,found,1024);
 load_level_dds_texture(std::string("intro\\intro_")+vita_campaign_zones[state.zone].directory,
  screen.cordon,512u*256u*4,found,512);
 screen.draw(graphics,VitaLoadingScreen::Stage::Archives,.05f);glFinish();
 char arg0[]="app0:eboot.bin",arg1[]="vita-transition",arg_new[]="vita-newgame";char*args[]={arg0,new_game?arg_new:arg1,nullptr};
 write_boot_log(g_boot_log,"debug_zone transition_committed=1 relaunch=1\n");
 const int result=sceAppMgrLoadExec("app0:eboot.bin",args,nullptr);
 char log[100];snprintf(log,sizeof(log),"debug_zone relaunch_failed=0x%x\n",result);write_boot_log(g_boot_log,log);
 return false;
}

// The browser owns a bounded set of reusable UI resources. File entries are still
// checked on every opening; only changed rows need font work.
static bool prepare_vita_save_browser(VitaSaveUI& ui)
{
 const auto begin=sceKernelGetProcessTimeWide();
 if(!ui.initialize(false))return false;
 const auto initialized=sceKernelGetProcessTimeWide();bool found=false;
 if(!ui.common.handle())load_level_dds_texture("ui\\ui_common",ui.common,512u*512u*4,found,512);
 if(!ui.artwork.handle())load_level_dds_texture("ui\\ui_static_mm_back_04",ui.artwork,512u*512u*4,found,512);
 const auto artwork_loaded=sceKernelGetProcessTimeWide();
 for(unsigned zone=0;zone<vita_zone_count;++zone)if(!ui.previews[zone].handle())
  load_level_dds_texture(std::string("intro\\intro_")+vita_campaign_zones[zone].directory,
   ui.previews[zone],256u*128u*4,found,256);
 const auto previews_loaded=sceKernelGetProcessTimeWide();
 const auto entries=list_vita_saves("ux0:data/stalker");
 const auto scanned=sceKernelGetProcessTimeWide();
 const bool refreshed=ui.refresh(entries);
 char log[240];snprintf(log,sizeof(log),"debug_save_browser_cache init_us=%llu artwork_us=%llu previews_us=%llu scan_us=%llu refresh_us=%llu changed_slots=%u ready=%u\n",
  static_cast<unsigned long long>(initialized-begin),static_cast<unsigned long long>(artwork_loaded-initialized),
  static_cast<unsigned long long>(previews_loaded-artwork_loaded),static_cast<unsigned long long>(scanned-previews_loaded),
  static_cast<unsigned long long>(sceKernelGetProcessTimeWide()-scanned),ui.refreshed_slots(),unsigned(refreshed));
 write_boot_log(g_boot_log,log);
 return refreshed&&ui.common.handle()&&ui.artwork.handle();
}

// Modal original save/load dialog. Returns a slot; writes are committed by the caller.
static int browse_vita_saves(VitaGLDevice& graphics,VitaMainMenu& menu,VitaSaveUI& ui,bool saving,SceUInt64* modal_us=nullptr)
{
 const auto prepare_started=sceKernelGetProcessTimeWide();
 if(!prepare_vita_save_browser(ui)||!ui.initialize(saving))return -1;
 auto entries=ui.entries();

 char preparation[120];snprintf(preparation,sizeof(preparation),"debug_save_browser saving=%u prepare_us=%llu\n",
  unsigned(saving),static_cast<unsigned long long>(sceKernelGetProcessTimeWide()-prepare_started));write_boot_log(g_boot_log,preparation);
 struct ModalTime {
  SceUInt64*elapsed;SceUInt64 start=sceKernelGetProcessTimeWide();
  ~ModalTime(){const auto duration=sceKernelGetProcessTimeWide()-start;if(elapsed)*elapsed+=duration;
   char log[100];snprintf(log,sizeof(log),"debug_save_browser modal_us=%llu\n",static_cast<unsigned long long>(duration));write_boot_log(g_boot_log,log);}
 } modal_time{modal_us};
 unsigned selected=0,confirmation=0;
 const unsigned count=saving?vita_manual_slots:vita_save_slots;
 SceCtrlData pad{};sceCtrlPeekBufferPositive(0,&pad,1);u32 previous=pad.buttons;
 for(;;){
  sceCtrlPeekBufferPositive(0,&pad,1);const u32 pressed=pad.buttons&~previous;previous=pad.buttons;
  if(pressed&SCE_CTRL_CIRCLE){if(confirmation)confirmation=0;else return -1;}
  else if(pressed&(SCE_CTRL_UP|SCE_CTRL_DOWN)){
   selected=(selected+((pressed&SCE_CTRL_UP)?count-1:1))%count;confirmation=0;
  }else if(pressed&SCE_CTRL_SQUARE){if(entries[selected].exists)confirmation=2;}
  else if(pressed&SCE_CTRL_CROSS){
   if(confirmation==2){
    const bool removed=delete_vita_slot("ux0:data/stalker",selected);
    write_boot_log(g_boot_log,removed?"debug_save deleted=1\n":"debug_save deleted=0\n");
    entries=list_vita_saves("ux0:data/stalker");const bool refreshed=ui.refresh(entries);confirmation=removed&&refreshed?0:3;
   }else if(saving){
    if(entries[selected].exists&&confirmation!=1)confirmation=1;
    else return int(selected);
   }else if(entries[selected].valid)return int(selected);
   else confirmation=3;
  }
  ui.draw(graphics,menu,selected,confirmation);
 }
}

int main(int argc,char**argv)
{
 VitaSaveState startup_state;
 bool transition_resume=false,force_new_game=false;
 for(int i=0;i<argc;++i)if(argv&&argv[i]){
  if(strcmp(argv[i],"vita-transition")==0)transition_resume=true;
  if(strcmp(argv[i],"vita-newgame")==0)force_new_game=true;
 }

	static const char* game_data_root = "ux0:data/stalker/";
	static const char* fsgame_path = "ux0:data/stalker/fsgame.ltx";
	sceIoMkdir("ux0:data/stalker/_appdata_", 0777);
	sceIoMkdir("ux0:data/stalker/_appdata_/logs", 0777);
	g_boot_log = sceIoOpen("ux0:data/stalker/boot.log",
		SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	write_boot_log(g_boot_log, "startup\n");
	SceIoStat fsgame_stat;
	const bool has_game_data = sceIoGetstat(fsgame_path, &fsgame_stat) >= 0;
	write_boot_log(g_boot_log, has_game_data ? "fsgame_found\n" : "fsgame_missing\n");

	// Initialize xrCore first so its memory, logging, and filesystem objects exist.
	// The game's filesystem is enabled only when its Vita data directory is installed.
	write_boot_log(g_boot_log, "core_init_begin\n");
	Core._initialize("xray", boot_log_callback, FALSE, 0);
	write_boot_log(g_boot_log, "core_init_done\n");
 transition_resume=transition_resume&&read_vita_save("ux0:data/stalker/transition",startup_state);
 g_zone=transition_resume?startup_state.zone:0;
	VitaSettings settings;
	read_vita_settings("ux0:data/stalker",settings);
    vita_language=settings.language;
	VitaGLDevice graphics;
	write_boot_log(g_boot_log, "graphics_init_begin\n");
	if (!graphics.initialize(settings.width(),settings.height(),0,settings.msaa()))
	{
		write_boot_log(g_boot_log, "graphics_init_failed\n");
		if (g_boot_log >= 0)
			sceIoClose(g_boot_log);
		Core._destroy();
		return 1;
	}
	write_boot_log(g_boot_log, "graphics_init_done\n");
	VitaLoadingScreen loading;
	using LoadStage = VitaLoadingScreen::Stage;
	const bool loading_ui = loading.initialize();
	write_boot_log(g_boot_log, loading_ui ? "debug_loading_screen ready=1\n" :
		"debug_loading_screen ready=0 fallback=progress\n");
	loading.draw(graphics, LoadStage::Archives, 0.05f);
	if (has_game_data)
	{
		g_temporary_stuff = &decode_archive_data;
		write_boot_log(g_boot_log, "archive_decoder_ready\n");
		strcpy_s(Core.ApplicationPath, sizeof(Core.ApplicationPath), game_data_root);
		write_boot_log(g_boot_log, "fs_init_begin\n");
		FS._initialize(CLocatorAPI::flScanAppRoot, 0, "fsgame.ltx");
		EFS._initialize();
		write_boot_log(g_boot_log, "fs_init_done\n");
		if (dump_shader_sources_if_requested())
		{
			loading.release();
			if (g_boot_log >= 0)
				sceIoClose(g_boot_log);
			Core._destroy();
			sceKernelExitProcess(0);
			return 0;
		}
		if (loading_ui)
		{
			bool source_found = false;
			const bool atlas_loaded = load_level_dds_texture("ui\\ui_load", loading.atlas,
				1024u * 1024u * 4u, source_found, 1024);
			const bool cordon_loaded = load_level_dds_texture("intro\\intro_l01_escape", loading.cordon,
				512u * 256u * 4u, source_found, 512);
			char line[98];
			snprintf(line, sizeof(line), "debug_loading_screen atlas=%u cordon=%u\n",
				static_cast<unsigned>(atlas_loaded), static_cast<unsigned>(cordon_loaded));
			write_boot_log(g_boot_log, line);
		}
	}
	VitaSaveUI save_browser;
	bool startup_load_requested = transition_resume;
 int startup_slot=-1;
	if (has_game_data && !transition_resume && !force_new_game)
	{
		prepare_vita_save_browser(save_browser);
		VitaMainMenu main_menu;
		if (!main_menu.initialize())
		{
			write_boot_log(g_boot_log, "debug_main_menu ready=0\n");
			save_browser.release();
			loading.release();
			if (g_boot_log >= 0) sceIoClose(g_boot_log);
			Core._destroy();
			sceKernelExitProcess(1);
			return 1;
		}
		bool source_found = false;
		const bool background_ready = load_level_dds_texture("ui\\ui_static_main_menu_background",
			main_menu.background, 1024u * 1024u * 4u, source_found, 1024);
		const bool artwork_ready = load_level_dds_texture("ui\\ui_mainmenu",
			main_menu.atlas, 1024u * 1024u * 4u, source_found, 1024);
		VitaGameAudio menu_audio;
		apply_audio_settings(menu_audio,settings);
		const bool music_ready = menu_audio.initialize(true);
		if (music_ready)
			menu_audio.set_radio(0.70f);
		VitaSaveState menu_save;
		startup_slot=latest_vita_slot("ux0:data/stalker");
        bool can_continue = startup_slot>=0;
		bool load_error = !can_continue && has_vita_save("ux0:data/stalker");
		unsigned choice = can_continue ? 0 : 1;
		SceCtrlData initial_menu_pad{};
		sceCtrlPeekBufferPositive(0, &initial_menu_pad, 1);
		u32 menu_previous_buttons = initial_menu_pad.buttons;
		bool quit_menu = false;
		char menu_log[144];
		snprintf(menu_log, sizeof(menu_log), "debug_main_menu ready=1 background=%u artwork=%u music=%u save=%u\n",
			static_cast<unsigned>(background_ready), static_cast<unsigned>(artwork_ready),
			static_cast<unsigned>(music_ready && menu_audio.radio_ready()), static_cast<unsigned>(can_continue));
		write_boot_log(g_boot_log, menu_log);
		vglWaitVblankStart(GL_TRUE);
		for (;;)
		{
			SceCtrlData menu_pad{};
			if (sceCtrlPeekBufferPositive(0, &menu_pad, 1) > 0)
			{
				const u32 pressed = menu_pad.buttons & ~menu_previous_buttons;
				menu_previous_buttons = menu_pad.buttons;
				if (pressed & (SCE_CTRL_UP | SCE_CTRL_DOWN))
				{
					choice = (choice + ((pressed & SCE_CTRL_UP) ? 4 : 1)) % 5;
					load_error = false;
				}
				if ((menu_pad.buttons & (SCE_CTRL_START | SCE_CTRL_SELECT)) == (SCE_CTRL_START | SCE_CTRL_SELECT))
					quit_menu = true;
				else if (pressed & (SCE_CTRL_CROSS | SCE_CTRL_SQUARE))
				{
					if (choice == 0 || choice == 2)
					{
						if(choice==2){
                            startup_slot=browse_vita_saves(graphics,main_menu,save_browser,false);
                            sceCtrlPeekBufferPositive(0,&initial_menu_pad,1);menu_previous_buttons=initial_menu_pad.buttons;
                            if(startup_slot<0){can_continue=latest_vita_slot("ux0:data/stalker")>=0;continue;}
                        }else startup_slot=latest_vita_slot("ux0:data/stalker");
                        can_continue=startup_slot>=0;
						if (can_continue)
						{
							startup_load_requested = true;
							write_boot_log(g_boot_log, choice == 0 ? "debug_main_menu action=continue\n" :
								"debug_main_menu action=load\n");
							break;
						}
						load_error = true;
					}
					else if (choice == 1)
					{
						write_boot_log(g_boot_log, "debug_main_menu action=new_game\n");
						break;
					}
					else if(choice==3)
					{
						edit_options(graphics,main_menu,menu_audio,settings);
						sceCtrlPeekBufferPositive(0,&initial_menu_pad,1);menu_previous_buttons=initial_menu_pad.buttons;
					}
					else
						quit_menu = true;
				}
			}
			main_menu.draw(graphics, choice, can_continue, load_error);
			if (quit_menu)
				break;
		}
		menu_audio.release();
		main_menu.release();
		if (quit_menu)
		{
			write_boot_log(g_boot_log, "debug_main_menu action=quit\n");
			save_browser.release();
			loading.release();
			if (g_boot_log >= 0) sceIoClose(g_boot_log);
			Core._destroy();
			sceKernelExitProcess(0);
			return 0;
		}
		// Do not carry the confirmation button into the loaded game.
		for (;;)
		{
			SceCtrlData released{};
			if (sceCtrlPeekBufferPositive(0, &released, 1) <= 0 ||
				!(released.buttons & (SCE_CTRL_CROSS | SCE_CTRL_SQUARE)))
				break;
			sceKernelDelayThread(10000);
		}
	}
 if(startup_load_requested&&!transition_resume){
  if(startup_slot<0||!read_vita_slot("ux0:data/stalker",unsigned(startup_slot),startup_state))startup_load_requested=false;
 }
 g_zone=startup_load_requested?startup_state.zone:0;
 if(g_zone){bool found=false;loading.cordon.release();
  load_level_dds_texture(std::string("intro\\intro_")+vita_campaign_zones[g_zone].directory,loading.cordon,512u*256u*4,found,512);}
 char zone_log[128];snprintf(zone_log,sizeof(zone_log),"debug_zone loaded=%u name=%s story=%u\n",g_zone,
  vita_campaign_zones[g_zone].directory,startup_state.story);write_boot_log(g_boot_log,zone_log);
	loading.draw(graphics, LoadStage::Geometry, 0.15f);
	VitaGLDebugRenderer debug_renderer;
	char shader_error[1024] = {0};
	if (debug_renderer.initialize(shader_error, sizeof(shader_error)))
		write_boot_log(g_boot_log, "debug_shader_ready\ndebug_view_transform=explicit_rows square=untextured circle=terrain_anchor\ndebug_vertex_streams=separate\ndebug_uv_decode=r1_base_fraction_and_lightmap\ndebug_terrain_material=opaque_shared_uv\ndebug_scene_overview=wide terrain_atlas_max=512\ndebug_tree_material=r1_uv_blend\ndebug_visual_local_reserve=without_trees\ndebug_tree_lod=mid\n");
	else
	{
		write_boot_log(g_boot_log, "debug_shader_failed\n");
		if (shader_error[0])
		{
			write_boot_log(g_boot_log, shader_error);
			write_boot_log(g_boot_log, "\n");
		}
	}
	const bool foliage_shader_ready = debug_renderer.initialize_foliage(shader_error, sizeof(shader_error));
	if (foliage_shader_ready)
		write_boot_log(g_boot_log, "debug_player_foliage_shader ready=1 alpha_ref=0.5\n");
	else
	{
		write_boot_log(g_boot_log, "debug_player_foliage_shader ready=0 fallback=blend\n");
		if (shader_error[0])
		{
			write_boot_log(g_boot_log, shader_error);
			write_boot_log(g_boot_log, "\n");
		}
	}
	VitaGLSky cordon_sky;
	if (has_game_data)
		load_cordon_sky(cordon_sky);
	bool sky_draw_logged = false;
	bool sky_enabled = true, foliage_cutout_enabled = foliage_shader_ready;
	write_boot_log(g_boot_log, "debug_player_render_controls left=sky right=foliage right_stick=yaw\n");
	VitaGLR1StaticBuffers level_buffers;
	std::vector<VitaTreeWindows> tree_windows;
	const bool level_geometry_loaded = has_game_data &&
		load_escape_level_geometry(level_buffers, tree_windows, debug_renderer.position_attribute_location(),
			debug_renderer.texcoord_attribute_location(), debug_renderer.lightmap_texcoord_attribute_location());
	std::vector<VitaDebugVisualRange> level_visuals;
	std::vector<VitaLodGroup> level_lod_groups;
	std::vector<std::string> level_shader_names;
	std::vector<std::string> level_texture_names;
	std::vector<std::string> level_lightmap_names;
	write_boot_log(g_boot_log, "debug_level_visuals_load_begin\n");
	const bool level_visuals_loaded = level_geometry_loaded &&
		load_escape_level_visuals(level_visuals, tree_windows, level_lod_groups, level_shader_names, level_texture_names, level_lightmap_names);
	write_boot_log(g_boot_log, level_visuals_loaded ? "debug_level_visuals_load_done\n" : "debug_level_visuals_load_failed\n");
	float actor_spawn_position[3] = {};
	float trader_spawn_position[3] = {};
	float trader_spawn_angles[3] = {};
	GameSpawnPose door_spawn, wolf_spawn;
	bool actor_spawn_found = false, trader_spawn_found = false;
	if (has_game_data)
	{
		if(!g_zone)actor_spawn_found = load_game_actor_spawn(actor_spawn_position, trader_spawn_position, trader_spawn_angles, trader_spawn_found, door_spawn, wolf_spawn);
        else{std::copy(startup_state.eye.begin(),startup_state.eye.end(),actor_spawn_position);
            actor_spawn_position[1]=startup_state.ground;actor_spawn_found=true;}
		if (!actor_spawn_found)
			actor_spawn_found = load_escape_actor_spawn(actor_spawn_position);
	}
	float level_center[3] = {0.f, 0.f, 0.f};
	float level_scale = 0.f;
	if (level_visuals_loaded)
		compute_level_bounds(level_buffers, level_center, level_scale);
	u32 visuals_with_bounds = 0;
	u32 visuals_with_index_bounds = 0;
	for (size_t i = 0; i < level_visuals.size(); ++i)
	{
		VitaDebugVisualRange& visual = level_visuals[i];
		VitaGLVertexBuffer* vertices = level_buffers.vertex_buffer(visual.vertex_buffer);
		VitaGLIndexBuffer* indices = level_buffers.index_buffer(visual.index_buffer);
		float minimum[3], maximum[3];
		bool has_bounds = false;
		if (vertices && indices && indices->cpu_indices() &&
			visual.first_index <= indices->cpu_index_count() &&
			visual.index_count <= indices->cpu_index_count() - visual.first_index)
		{
			has_bounds = vertices->indexed_position_bounds(visual.first_vertex, visual.vertex_count,
				indices->cpu_indices() + visual.first_index, visual.index_count, minimum, maximum);
			if (has_bounds) ++visuals_with_index_bounds;
		}
		if (!has_bounds && vertices)
			has_bounds = vertices->range_position_bounds(visual.first_vertex, visual.vertex_count, minimum, maximum);
		if (vertices && has_bounds)
		{
			if (visual.has_tree_transform)
			{
				const float* transform = visual.tree_transform;
				float world_minimum[3], world_maximum[3];
				for (size_t axis = 0; axis < 3; ++axis)
				{
					world_minimum[axis] = world_maximum[axis] = transform[12 + axis];
					for (size_t local = 0; local < 3; ++local)
					{
						const float a = transform[local * 4 + axis] * minimum[local];
						const float b = transform[local * 4 + axis] * maximum[local];
						world_minimum[axis] += a < b ? a : b;
						world_maximum[axis] += a > b ? a : b;
					}
				}
				memcpy(minimum, world_minimum, sizeof(minimum));
				memcpy(maximum, world_maximum, sizeof(maximum));
			}
			float radius_squared = 0.f;
			for (size_t axis = 0; axis < 3; ++axis)
			{
				visual.bounds_minimum[axis] = minimum[axis];
				visual.bounds_maximum[axis] = maximum[axis];
				visual.bounds_center[axis] = (minimum[axis] + maximum[axis]) * 0.5f;
				const float half_extent = (maximum[axis] - minimum[axis]) * 0.5f;
				radius_squared += half_extent * half_extent;
			}
			visual.bounds_radius = sqrtf(radius_squared);
			visual.has_bounds = true;
			++visuals_with_bounds;
		}
	}
	if (level_visuals_loaded)
		compute_visual_bounds(level_visuals, level_center, level_scale);
	if (actor_spawn_found && level_visuals_loaded)
	{
		const float focus_radius_squared = 20.f * 20.f;
		float nearest_distance_squared = (std::numeric_limits<float>::max)();
		size_t nearest_visual = level_visuals.size();
		size_t local_visual_count = 0;
		for (size_t i = 0; i < level_visuals.size(); ++i)
		{
			VitaDebugVisualRange& visual = level_visuals[i];
			visual.focus_sector = false;
			if (!visual.has_bounds) continue;
			float distance_squared = 0.f;
			for (size_t axis = 0; axis < 3; ++axis)
			{
				const float delta = actor_spawn_position[axis] < visual.bounds_minimum[axis]
					? visual.bounds_minimum[axis] - actor_spawn_position[axis]
					: (actor_spawn_position[axis] > visual.bounds_maximum[axis]
						? actor_spawn_position[axis] - visual.bounds_maximum[axis] : 0.f);
				distance_squared += delta * delta;
			}
			if (distance_squared < nearest_distance_squared)
			{
				nearest_distance_squared = distance_squared;
				nearest_visual = i;
			}
			if (distance_squared <= focus_radius_squared)
			{
				visual.focus_sector = true;
				++local_visual_count;
			}
		}
		if (!local_visual_count && nearest_visual < level_visuals.size())
		{
			level_visuals[nearest_visual].focus_sector = true;
			local_visual_count = 1;
		}
		char focus_log[128];
		snprintf(focus_log, sizeof(focus_log),
			"debug_actor_local_visuals=%u nearest=%.2f\n",
			static_cast<unsigned>(local_visual_count), sqrtf(nearest_distance_squared));
		write_boot_log(g_boot_log, focus_log);
	}
	if (!actor_spawn_found && level_visuals_loaded && level_scale > 0.f)
	{
		const float focus_extent = 1.6f / level_scale;
		const float maximum_distance_squared = 4.f * focus_extent * focus_extent;
		float closest_distance_squared = (std::numeric_limits<float>::max)();
		float closest_radius = (std::numeric_limits<float>::max)();
		size_t closest_terrain = level_visuals.size();
		float closest_x = level_center[0];
		float closest_z = level_center[2];
		for (size_t i = 0; i < level_visuals.size(); ++i)
		{
			const VitaDebugVisualRange& visual = level_visuals[i];
			if (!visual.has_bounds || visual.shader_id >= level_shader_names.size() ||
				level_shader_names[visual.shader_id].find("terrain") == std::string::npos)
				continue;
			const float x = level_center[0] < visual.bounds_minimum[0] ? visual.bounds_minimum[0] :
				(level_center[0] > visual.bounds_maximum[0] ? visual.bounds_maximum[0] : level_center[0]);
			const float z = level_center[2] < visual.bounds_minimum[2] ? visual.bounds_minimum[2] :
				(level_center[2] > visual.bounds_maximum[2] ? visual.bounds_maximum[2] : level_center[2]);
			const float dx = x - level_center[0];
			const float dz = z - level_center[2];
			const float distance_squared = dx * dx + dz * dz;
			if (distance_squared < closest_distance_squared ||
				(distance_squared == closest_distance_squared && visual.bounds_radius < closest_radius))
			{
				closest_distance_squared = distance_squared;
				closest_radius = visual.bounds_radius;
				closest_terrain = i;
				closest_x = x;
				closest_z = z;
			}
		}
		if (closest_terrain < level_visuals.size() && closest_distance_squared <= maximum_distance_squared)
		{
			level_center[0] = closest_x;
			level_center[1] = level_visuals[closest_terrain].bounds_center[1];
			level_center[2] = closest_z;
			char camera_log[120];
			snprintf(camera_log, sizeof(camera_log), "debug_camera_terrain_anchor=%u distance=%.2f\n",
				level_visuals[closest_terrain].visual_id, sqrtf(closest_distance_squared));
			write_boot_log(g_boot_log, camera_log);
		}
		else
			write_boot_log(g_boot_log, "debug_camera_terrain_anchor_missing\n");
	}
	const size_t debug_index_budget_limit = 180000;
	const bool use_actor_view = actor_spawn_found && level_visuals_loaded && level_scale > 0.f;
	const float initial_view_pitch = use_actor_view ? 0.75f : 0.28f;
	const float debug_view_scale = use_actor_view ? level_scale / 3.f : level_scale;
	size_t terrain_anchor = level_visuals.size();
	if (use_actor_view)
	{
		memcpy(level_center, actor_spawn_position, sizeof(level_center));
		float closest_distance_squared = (std::numeric_limits<float>::max)();
		for (size_t i = 0; i < level_visuals.size(); ++i)
		{
			const VitaDebugVisualRange& visual = level_visuals[i];
			if (!visual.has_bounds || visual.index_count > debug_index_budget_limit / 3 ||
				visual.shader_id >= level_shader_names.size() ||
				level_shader_names[visual.shader_id].find("terrain") == std::string::npos)
				continue;
			const VitaGLVertexBuffer* vertices = level_buffers.vertex_buffer(visual.vertex_buffer);
			const VitaGLIndexBuffer* indices = level_buffers.index_buffer(visual.index_buffer);
			if (!vertices || !indices || !vertices->position_data() || !indices->cpu_indices() ||
				visual.first_index > indices->index_count() ||
				visual.index_count > indices->index_count() - visual.first_index ||
				visual.first_vertex > vertices->vertex_count() ||
				visual.vertex_count > vertices->vertex_count() - visual.first_vertex)
				continue;

			const float* positions = vertices->position_data() + visual.first_vertex * 3;
			const uint16_t* triangle = indices->cpu_indices() + visual.first_index;
			for (size_t j = 0; j + 2 < visual.index_count; j += 3, triangle += 3)
			{
				if (triangle[0] >= visual.vertex_count || triangle[1] >= visual.vertex_count ||
					triangle[2] >= visual.vertex_count)
					continue;
				const float* a = positions + triangle[0] * 3;
				const float* b = positions + triangle[1] * 3;
				const float* c = positions + triangle[2] * 3;
				const float twice_area_xz = (b[0] - a[0]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[0] - a[0]);
				// Skip thin or vertical faces when looking for a patch of ground.
				if (!(fabsf(twice_area_xz) >= 1.f))
					continue;
				const float center[3] = {
					(a[0] + b[0] + c[0]) / 3.f,
					(a[1] + b[1] + c[1]) / 3.f,
					(a[2] + b[2] + c[2]) / 3.f
				};
				const float dx = center[0] - actor_spawn_position[0];
				const float dz = center[2] - actor_spawn_position[2];
				const float distance_squared = dx * dx + dz * dz;
				if (!std::isfinite(center[1]) || !(distance_squared < closest_distance_squared))
					continue;
				closest_distance_squared = distance_squared;
				terrain_anchor = i;
				memcpy(level_center, center, sizeof(level_center));
			}
		}
		if (terrain_anchor < level_visuals.size())
		{
			char anchor_log[128];
			snprintf(anchor_log, sizeof(anchor_log),
				"debug_camera_terrain_triangle=%u distance_xz=%.2f indices=%u\n",
				level_visuals[terrain_anchor].visual_id, sqrtf(closest_distance_squared),
				level_visuals[terrain_anchor].index_count);
			write_boot_log(g_boot_log, anchor_log);
		}
		else
			write_boot_log(g_boot_log, "debug_camera_terrain_triangle_missing\n");
		char camera_log[128];
		snprintf(camera_log, sizeof(camera_log),
			"debug_camera_actor_target=%.2f,%.2f,%.2f scale=%.6f pitch=%.2f\n",
			level_center[0], level_center[1], level_center[2], debug_view_scale, initial_view_pitch);
		write_boot_log(g_boot_log, camera_log);
	}
	char bounds_log[80];
	snprintf(bounds_log, sizeof(bounds_log), "debug_visual_bounds=%u of %u\n",
		visuals_with_bounds, static_cast<unsigned>(level_visuals.size()));
	write_boot_log(g_boot_log, bounds_log);
	snprintf(bounds_log, sizeof(bounds_log), "debug_visual_index_bounds=%u\n", visuals_with_index_bounds);
	write_boot_log(g_boot_log, bounds_log);
	for (size_t i = 0; i < level_buffers.vertex_buffer_count(); ++i)
		if (VitaGLVertexBuffer* vertices = level_buffers.vertex_buffer(i))
			vertices->discard_position_data();
	for (size_t i = 0; i < level_buffers.index_buffer_count(); ++i)
		if (VitaGLIndexBuffer* indices = level_buffers.index_buffer(i))
			indices->discard_cpu_copy();
	const size_t debug_visual_limit = 192;
	std::vector<std::pair<float, size_t>> foliage_draws;
	foliage_draws.reserve(debug_visual_limit);
	std::vector<std::pair<float, size_t>> translucent_draws;
	translucent_draws.reserve(debug_visual_limit);
	std::vector<u8> barbed_wire_materials(level_shader_names.size(), 0);
	bool barbed_wire_draw_logged = false;
	std::vector<u8> foliage_materials(level_shader_names.size(), 0);
	for (size_t shader = 0; shader < foliage_materials.size(); ++shader)
	{
		const auto& name = level_shader_names[shader];
		constexpr char wire_vertex_shader[] = "def_shaders\\def_trans_v/prop\\prop_barbwire3";
		constexpr char wire_lightmap_shader[] = "def_shaders\\def_trans/prop\\prop_barbwire3,";
		barbed_wire_materials[shader] = name == wire_vertex_shader ||
			name.compare(0, sizeof(wire_lightmap_shader) - 1, wire_lightmap_shader) == 0;
		constexpr char leaf_shader[] = "flora\\leaf_wave/";
		foliage_materials[shader] = name.compare(0, sizeof(leaf_shader) - 1, leaf_shader) == 0 ||
			name == "def_shaders\\def_aref_v/trees\\trees_forestwall1";
	}
	constexpr float player_view_scale = 0.5f;
	constexpr float player_view_pitch = 1.45f;
	constexpr float player_eye_height = 1.7f;
	float player_eye[3] = {level_center[0], level_center[1] + player_eye_height, level_center[2]};
	loading.draw(graphics, LoadStage::Collision, 0.38f);
	VitaCformGround player_ground;
	CordonAssaultPaths assault_paths;
	const auto& scout_positions = assault_paths.muster;
	const bool scout_waypoints = use_actor_view && load_scout_waypoints(assault_paths);
	const bool ground_ready = use_actor_view && load_escape_ground(player_ground, player_eye[0], player_eye[2],
		scout_waypoints ? (assault_paths.ready ? assault_paths.farm_bounds.data() : scout_positions[0].data()) : nullptr);
	float start_ground_y = level_center[1];
	const bool start_grounded = ground_ready &&
		player_ground.height_at(player_eye[0], player_eye[2], level_center[1], 2.f, 2.f, start_ground_y);
	if (start_grounded)
		player_eye[1] = start_ground_y + player_eye_height;
 if(ground_ready&&startup_state.needs_landing){
  float floor=startup_state.ground;
  if(player_ground.height_at(startup_state.eye[0],startup_state.eye[2],floor,3.f,30.f,floor)){
   startup_state.ground=floor;startup_state.eye[1]=floor+1.7f;startup_state.needs_landing=false;
  }
 }
	float bunker_ground_y = actor_spawn_position[1];
	const bool bunker_grounded = ground_ready && trader_spawn_found &&
		player_ground.height_at(actor_spawn_position[0], actor_spawn_position[2], actor_spawn_position[1],
			0.45f, 0.75f, bunker_ground_y);
	const float bunker_eye[] = {actor_spawn_position[0], bunker_ground_y + player_eye_height, actor_spawn_position[2]};
	std::vector<size_t> bunker_candidates;
	std::vector<size_t> bunker_shell;
	if (bunker_grounded)
	{
		constexpr float bunker_radius = 14.f;
		for (size_t i = 0; i < level_visuals.size(); ++i)
		{
			const auto& visual = level_visuals[i];
			if (!visual.has_bounds || visual.has_tree_transform)
				continue;
			const bool close_to_spawn =
				visual.bounds_minimum[0] <= bunker_eye[0] + bunker_radius &&
				visual.bounds_maximum[0] >= bunker_eye[0] - bunker_radius &&
				visual.bounds_minimum[2] <= bunker_eye[2] + bunker_radius &&
				visual.bounds_maximum[2] >= bunker_eye[2] - bunker_radius &&
				visual.bounds_minimum[1] <= bunker_ground_y + 3.5f &&
				visual.bounds_maximum[1] >= bunker_ground_y - 2.f;
			// Visual 1430 is the last stone arch, stored in the exterior sector.
			const bool exit_arch = visual.visual_id == 1430 &&
				at_bunker_stairs(visual.bounds_center[0], visual.bounds_center[2]);
			if (close_to_spawn || visual.bunker_passage || exit_arch)
				bunker_candidates.push_back(i);
			if ((visual.bunker_passage || exit_arch) && visual.shader_id < level_texture_names.size())
			{
				const auto& texture = level_texture_names[visual.shader_id];
				if (texture == "ston\\ston_walls1_1" || texture == "grnd\\grnd_concrete_1")
					bunker_shell.push_back(i);
			}
		}
	}
	const bool bunker_ready = !bunker_candidates.empty();
	VitaGLVertexBuffer trader_vertices;
	VitaGLIndexBuffer trader_indices;
	VitaGLTexture trader_texture;
	float trader_local_target[3] = {0.f, 1.45f, 0.f};
	bool trader_seated = false;
	const bool trader_model_ready = bunker_ready && load_trader_model(debug_renderer,
		trader_vertices, trader_indices, trader_texture, trader_local_target, trader_seated);
	Fmatrix trader_transform;
	trader_transform.setXYZ(trader_spawn_angles[0], trader_spawn_angles[1], trader_spawn_angles[2]);
	trader_transform.c.set(trader_spawn_position[0], trader_spawn_position[1], trader_spawn_position[2]);
	Fvector trader_target;
	trader_target.set(trader_local_target[0], trader_local_target[1], trader_local_target[2]);
	trader_transform.transform_tiny(trader_target);
	const float target_x = trader_target.x - bunker_eye[0];
	const float target_z = trader_target.z - bunker_eye[2];
	const float bunker_yaw = atan2f(target_x, target_z);
	// Player pitch is measured from down (+Y in the view basis points up at pi/2).
	const float bunker_pitch = trader_seated
		? atan2f(hypotf(target_x, target_z), bunker_eye[1] - trader_target.y)
		: player_view_pitch;
	if (bunker_ready)
	{
		char line[192];
		snprintf(line, sizeof(line), "debug_trader_model ready=%u texture=%u vertices=%u indices=%u yaw=%.4f pose=%s target_y=%.2f\n",
			static_cast<unsigned>(trader_model_ready), static_cast<unsigned>(trader_texture.handle() != 0),
			static_cast<unsigned>(trader_vertices.vertex_count()), static_cast<unsigned>(trader_indices.index_count()),
			trader_spawn_angles[1], trader_seated ? vita_trader_idle_clip : "bind", trader_target.y);
		write_boot_log(g_boot_log, line);
		snprintf(line, sizeof(line), "debug_trader_idle frames=%u fps=%u position_bytes=%u\n",
			static_cast<unsigned>(trader_vertices.position_frame_count()), vita_trader_idle_fps,
			trader_vertices.position_frame_count() > 1
				? static_cast<unsigned>(trader_vertices.position_frame_count() * trader_vertices.vertex_count() * 3 * sizeof(float))
				: 0u);
		write_boot_log(g_boot_log, line);
	}
	VitaGLVertexBuffer door_vertices;
	VitaGLIndexBuffer door_indices;
	VitaGLTexture door_texture;
	float door_minimum[3] = {}, door_maximum[3] = {};
	const bool door_ready = bunker_ready && door_spawn.found && load_bunker_door(debug_renderer,
		door_vertices, door_indices, door_texture, door_minimum, door_maximum);
	Fmatrix door_closed_transform, door_transform;
	door_closed_transform.setXYZ(door_spawn.angles[0], door_spawn.angles[1], door_spawn.angles[2]);
	door_closed_transform.c.set(door_spawn.position[0], door_spawn.position[1], door_spawn.position[2]);
	door_transform = door_closed_transform;
	auto door_panel = bunker_door_panel(door_transform, door_minimum, door_maximum);
	Fvector door_target;
	door_target.set((door_minimum[0] + door_maximum[0]) * 0.5f, 1.4f, 0.f);
	door_closed_transform.transform_tiny(door_target);
	if (bunker_ready)
	{
		char line[128];
		snprintf(line, sizeof(line), "debug_bunker_door ready=%u texture=%u indices=%u\n",
			static_cast<unsigned>(door_ready), static_cast<unsigned>(door_texture.handle() != 0),
			static_cast<unsigned>(door_indices.index_count()));
		write_boot_log(g_boot_log, line);
	}
	write_boot_log(g_boot_log,debug_renderer.initialize_npc(shader_error,sizeof(shader_error))?
		"debug_npc_interpolation ready=1\n":"debug_npc_interpolation ready=0\n");
	StalkerVisual wolf_visuals[2];
	float wolf_local_target[3] = {0.f, 1.6f, 0.f};
	bool wolf_posed = false;
	float wolf_floor = wolf_spawn.position[1];
	const bool wolf_grounded = wolf_spawn.found && ground_ready &&
		player_ground.height_at(wolf_spawn.position[0], wolf_spawn.position[2], wolf_floor, 0.45f, 0.75f, wolf_floor);
	loading.draw(graphics, LoadStage::Actors, 0.62f);
	std::vector<u8> stalker_params, stalker_keys, scout_walk_keys, unarmed_keys;
	std::array<VitaNpcMotions, 3> npc_motions;
	if (wolf_grounded || scout_waypoints)
		read_stalker_clips(stalker_params, stalker_keys, &scout_walk_keys, &unarmed_keys, npc_motions);
	const bool wolf_model_ready = wolf_grounded && load_stalker_model(debug_renderer,
		"gamedata\\meshes\\actors\\neytral\\stalker_neytral_balon_1.ogf",
		wolf_visuals, stalker_params, stalker_keys, wolf_local_target, wolf_posed, true, &scout_walk_keys, &npc_motions[0]);
	Fmatrix wolf_spawn_transform;
	wolf_spawn_transform.setXYZ(wolf_spawn.angles[0], wolf_spawn.angles[1], wolf_spawn.angles[2]);
	wolf_spawn_transform.c.set(wolf_spawn.position[0], wolf_floor, wolf_spawn.position[2]);
	Fvector wolf_target;
	wolf_target.set(wolf_local_target[0], wolf_local_target[1], wolf_local_target[2]);
	wolf_spawn_transform.transform_tiny(wolf_target);

	StalkerVisual scout_visuals[3][2];
	bool scout_ready[3] = {};
	Fmatrix scout_transforms[3];
	for (auto& transform : scout_transforms) transform.identity();
	float scout_floor = scout_positions[0][1];
	Fvector scout_target;
	scout_target.set(scout_positions[0][0], scout_floor + 1.5f, scout_positions[0][2]);
	if (scout_waypoints && ground_ready && g_zone!=3)
	{
		const char* const models[] = {
			"gamedata\\meshes\\actors\\novice\\green_stalker_8.ogf",
			"gamedata\\meshes\\actors\\novice\\green_stalker_2.ogf",
			"gamedata\\meshes\\actors\\novice\\green_stalker_3.ogf"
		};
		for (unsigned i = 0; i < 3; ++i)
		{
			const auto& position = scout_positions[i];
			float floor = position[1];
			if (!player_ground.height_at(position[0], position[2], floor, 0.5f, 0.85f, floor))
				continue;
			float local_head[3] = {0.f, 1.5f, 0.f};
			bool posed = false;
			scout_ready[i] = load_stalker_model(debug_renderer,
                g_zone&&i==0?(g_zone==1?"gamedata\\meshes\\actors\\neytral\\stalker_neytral_hood_4.ogf":"gamedata\\meshes\\actors\\neytral\\stalker_neytral_rukzak_4.ogf"):models[i],scout_visuals[i],
				stalker_params, stalker_keys, local_head, posed, g_zone || i == 0 || !scout_visuals[0][0].texture.handle(),
				&scout_walk_keys, &npc_motions[g_zone?0:1]);
			scout_transforms[i].setXYZ(0.f, -1.5707963f, 0.f);
			scout_transforms[i].c.set(position[0], floor, position[2]);
			if (i == 0)
			{
				scout_floor = floor;
				scout_target.set(local_head[0], local_head[1], local_head[2]);
				scout_transforms[0].transform_tiny(scout_target);
			}
			char line[144];
			snprintf(line, sizeof(line), "debug_scout_model member=%u ready=%u posed=%u floor=%.2f walk_frames=%u\n",
				i, static_cast<unsigned>(scout_ready[i]), static_cast<unsigned>(posed), floor,
				static_cast<unsigned>(scout_ready[i] ? scout_visuals[i][0].vertices.position_frame_count() - 1 : 0));
			write_boot_log(g_boot_log, line);
		}
	}
	std::array<std::vector<ScoutRouteSample>, 3> scout_routes;
	StalkerVisual guard_visual[2];
	std::array<Fmatrix, 6> guard_transforms{};
	std::array<bool, 6> guards_placed{};
	bool guard_model_ready = false;
	if (assault_paths.ready && scout_waypoints && ground_ready)
	{
		unsigned missing_ground = 0;
		for (size_t i = 0; i < scout_routes.size(); ++i)
		{
			const auto& position = scout_transforms[i].c;
			if (scout_ready[i])
				scout_routes[i] = build_scout_route(player_ground, {position.x, position.y, position.z},
					assault_paths, i != 0, missing_ground, i);
		}
		for (size_t i = 0; i < guards_placed.size(); ++i)
		{
			const auto& point = assault_paths.guards[i];
			float floor = point[1];
			if (!assault_paths.guards_found[i] || !player_ground.height_at(point[0], point[2], floor, 0.55f, 0.8f, floor))
				continue;
			const auto& gate = assault_paths.approach.back();
			guard_transforms[i].setXYZ(0.f, -atan2f(gate[0] - point[0], gate[2] - point[2]), 0.f);
			guard_transforms[i].c.set(point[0], floor, point[2]);
			guards_placed[i] = true;
		}
		if (std::any_of(guards_placed.begin(), guards_placed.end(), [](bool placed) { return placed; }))
		{
			float head[3] = {};
			bool posed = false;
			guard_model_ready = load_stalker_model(debug_renderer, g_zone==2?"gamedata\\meshes\\actors\\soldier\\soldier_bandana_6.ogf":"gamedata\\meshes\\actors\\bandit\\stalker_bandit_3.ogf",
				guard_visual, stalker_params, stalker_keys, head, posed, true, &scout_walk_keys, &npc_motions[g_zone==2?0:2]);
		}
		char line[154];
		snprintf(line, sizeof(line), "debug_cordon_assault routes=%u,%u,%u missing_ground=%u guards=%u model=%u\n",
			static_cast<unsigned>(scout_routes[0].size()), static_cast<unsigned>(scout_routes[1].size()),
			static_cast<unsigned>(scout_routes[2].size()), missing_ground,
			static_cast<unsigned>(std::count(guards_placed.begin(), guards_placed.end(), true)),
			static_cast<unsigned>(guard_model_ready));
		write_boot_log(g_boot_log, line);
	}
	StalkerVisual allegro_visual[2];
	Fmatrix allegro_transform;
	float allegro_floor = assault_paths.allegro[1];
	const auto& allegro_point = assault_paths.allegro;
	const auto& allegro_look = assault_paths.allegro_look;
	float allegro_head[3] = {0.f, 1.58f, 0.f};
	bool allegro_posed = false;
	const bool allegro_grounded = assault_paths.allegro_found && ground_ready &&
		player_ground.height_at(allegro_point[0], allegro_point[2], allegro_floor, 0.5f, 0.85f, allegro_floor);
	const bool allegro_model_ready = allegro_grounded && load_stalker_model(debug_renderer, "gamedata\\meshes\\actors\\novice\\green_stalker_1.ogf",
			allegro_visual, stalker_params, unarmed_keys, allegro_head, allegro_posed);
	allegro_transform.setXYZ(0.f, -atan2f(allegro_look[0] - allegro_point[0], allegro_look[2] - allegro_point[2]), 0.f);
	allegro_transform.c.set(allegro_point[0], allegro_floor, allegro_point[2]);
	Fvector allegro_target;
	allegro_target.set(allegro_head[0], allegro_head[1], allegro_head[2]);
	allegro_transform.transform_tiny(allegro_target);
	char allegro_log[176];
	snprintf(allegro_log, sizeof(allegro_log), "debug_allegro model=%u posed=%u waypoints=%u grounded=%u x=%.2f y=%.2f z=%.2f\n",
		static_cast<unsigned>(allegro_model_ready), static_cast<unsigned>(allegro_posed),
		static_cast<unsigned>(assault_paths.allegro_found), static_cast<unsigned>(allegro_grounded),
		allegro_point[0], allegro_floor, allegro_point[2]);
	write_boot_log(g_boot_log, allegro_log);
    StalkerVisual road_visual[2];float road_head[3]={0,1.5f,0};bool road_posed=false;
    const bool road_model_ready=g_zone==1&&load_stalker_model(debug_renderer,
        "gamedata\\meshes\\actors\\novice\\green_stalker_11.ogf",road_visual,
        stalker_params,stalker_keys,road_head,road_posed,true,&scout_walk_keys,&npc_motions[2]);
	std::vector<u8>().swap(stalker_params);
	std::vector<u8>().swap(stalker_keys);
	std::vector<u8>().swap(scout_walk_keys);
	std::vector<u8>().swap(unarmed_keys);
	for(auto& set:npc_motions) for(auto& clip:set) std::vector<u8>().swap(clip);

	// Shared original world models: Wolf AKS-74U, scouts BM16, bandits PM.
	StalkerVisual npc_weapons[3][2];
	const char* const npc_weapon_paths[] = {
		"gamedata\\meshes\\weapons\\ak-74u\\ak74u.ogf",
		"gamedata\\meshes\\weapons\\bm_16\\wpn_bm-16.ogf",
		"gamedata\\meshes\\weapons\\pm\\wpn_pm.ogf"
	};
	bool npc_weapon_ready[3] = {};
	Fmatrix npc_weapon_offsets[3];
	const float npc_positions[][3] = {{-0.026f,-0.215f,0.f}, {-0.026f,-0.096f,0.045f}, {-0.021f,-0.09f,0.f}};
	for (size_t i = 0; i < 3; ++i)
	{
		npc_weapon_offsets[i].setHPB(0.f, i == 1 ? 0.5235987756f : 0.f, i == 2 ? 0.0174532925f : 0.f);
		npc_weapon_offsets[i].c.set(npc_positions[i][0], npc_positions[i][1], npc_positions[i][2]);
		npc_weapon_ready[i] = has_game_data && load_npc_weapon(debug_renderer, npc_weapon_paths[i], npc_weapons[i]);
	}
	char npc_weapon_log[192];
	snprintf(npc_weapon_log, sizeof(npc_weapon_log), "debug_npc_weapons models=%u,%u,%u poses=%u,%u,%u,%u,%u allegro_unarmed=%u\n",
		unsigned(npc_weapon_ready[0]), unsigned(npc_weapon_ready[1]), unsigned(npc_weapon_ready[2]),
		unsigned(wolf_visuals[0].weapon_pose.ready), unsigned(scout_visuals[0][0].weapon_pose.ready),
		unsigned(scout_visuals[1][0].weapon_pose.ready), unsigned(scout_visuals[2][0].weapon_pose.ready),
		unsigned(guard_visual[0].weapon_pose.ready), unsigned(allegro_posed));
	write_boot_log(g_boot_log, npc_weapon_log);
	bool npc_weapon_draw_logged[3] = {};

	StalkerVisual pm_hud[8], knife_hud[8],primary_hud[8],grenade_hud[8];
	VitaWeaponClips pm_clips{}, knife_clips{};
	const bool pm_hud_ready = has_game_data && load_weapon_hud(debug_renderer,
		"gamedata\\meshes\\weapons\\pm\\wpn_pm_hud.ogf", true, pm_hud, pm_clips,0,true);
	const bool knife_hud_ready = has_game_data && load_weapon_hud(debug_renderer,
		"gamedata\\meshes\\weapons\\knife\\wpn_knife_hud.ogf", false, knife_hud, knife_clips);
    StalkerVisual strelok_item[2];
    const bool strelok_item_ready=g_zone==3&&load_npc_weapon(debug_renderer,
      "gamedata\\meshes\\equipments\\item_datchik2.ogf",strelok_item);
    if(g_zone==3)write_boot_log(g_boot_log,strelok_item_ready?"debug_strelok item_model=1\n":"debug_strelok item_model=0\n");
    StalkerVisual grenade_world[2][2];
    bool grenade_world_ready[2]={
        has_game_data&&load_npc_weapon(debug_renderer,"gamedata\\meshes\\weapons\\grenade\\wpn_rgd_5.ogf",grenade_world[0]),
        has_game_data&&load_npc_weapon(debug_renderer,"gamedata\\meshes\\weapons\\grenade\\wpn_f_1.ogf",grenade_world[1])};
    std::array<VitaGLTexture,64> explosion_frames;
    string_path explosion_path;
    IReader* explosion_source=resolve_level_dds("pfx\\pfx_ani-explosion-02",explosion_path)?FS.r_open(explosion_path):nullptr;
    if(explosion_source){
        for(unsigned i=0;i<64;++i){
            const VitaDDSRegion region{1024,(i%8)*128,(i/8)*128,128,128,0};
            explosion_frames[i].initialize_dds_region(explosion_source->pointer(),explosion_source->length(),128*128*4,region);
        }FS.r_close(explosion_source);
    }
	char weapon_loading_log[100];
	snprintf(weapon_loading_log, sizeof(weapon_loading_log), "debug_weapon_hud pm=%u knife=%u pm_parts=%u\n",
		static_cast<unsigned>(pm_hud_ready), static_cast<unsigned>(knife_hud_ready),
		pm_hud_ready ? static_cast<unsigned>(std::size(pm_hud)) : 0);
	write_boot_log(g_boot_log, weapon_loading_log);
    VitaWeaponClips primary_clips{},grenade_clips{};
    unsigned loaded_primary=0,loaded_grenade=2;
    bool primary_hud_ready=false,grenade_hud_ready=false;
    const auto release_hud=[](auto& parts){for(auto&part:parts){
        for(auto& addon:part.addon_indices)addon.release();
        part.indices.release();part.vertices.release();part.texture.release();part.body_visible=true;}};
    const auto ensure_hud=[&](const VitaInventory& inv,bool freeze_grenade){
        if(inv.primary()&&loaded_primary!=inv.primary()){
            if(loaded_primary)glFinish();
            release_hud(primary_hud);primary_clips={};loaded_primary=inv.primary();
            primary_hud_ready=load_weapon_hud(debug_renderer,loaded_primary==1?
                "gamedata\\meshes\\weapons\\bm_16\\wpn_bm-16_hud.ogf":"gamedata\\meshes\\weapons\\ak74\\wpn_ak74_hud.ogf",
                true,primary_hud,primary_clips,loaded_primary==1?2:3,true);
            char line[100];snprintf(line,sizeof(line),"debug_player_primary kind=%u ready=%u\n",loaded_primary,unsigned(primary_hud_ready));write_boot_log(g_boot_log,line);
        }
        if(!freeze_grenade&&inv.weapon()==VitaWeapon::Grenade&&loaded_grenade!=unsigned(inv.grenade()==VitaInventoryItem::F1)){
            if(loaded_grenade<2)glFinish();
            release_hud(grenade_hud);grenade_clips={};loaded_grenade=unsigned(inv.grenade()==VitaInventoryItem::F1);
            grenade_hud_ready=load_weapon_hud(debug_renderer,loaded_grenade?
                "gamedata\\meshes\\weapons\\grenade\\wpn_f_1_hud.ogf":"gamedata\\meshes\\weapons\\grenade\\wpn_rgd_5_hud.ogf",
                false,grenade_hud,grenade_clips,4);
        }
    };
	const auto& pm_aim = pm_clips[static_cast<size_t>(VitaWeaponClip::Aim)];
	const auto& pm_reload = pm_clips[static_cast<size_t>(VitaWeaponClip::Reload)];
	const auto& pm_reload_empty = pm_clips[static_cast<size_t>(VitaWeaponClip::ReloadEmpty)];
	const auto& pm_shoot = pm_clips[static_cast<size_t>(VitaWeaponClip::Shoot)];
	const auto& pm_shoot_last = pm_clips[static_cast<size_t>(VitaWeaponClip::ShootLast)];
	const auto& knife_windup = knife_clips[static_cast<size_t>(VitaWeaponClip::KnifeWindup)];
	const auto& knife_strike = knife_clips[static_cast<size_t>(VitaWeaponClip::KnifeStrike)];
	const SceUInt64 knife_windup_us = knife_windup.duration_us();
	const SceUInt64 knife_motion_us = knife_windup_us + knife_strike.duration_us();
	char weapon_animation_log[192];
	snprintf(weapon_animation_log, sizeof(weapon_animation_log),
		"debug_weapon_animation aim=%u pm_reload=%u pm_reload_empty=%u knife_start=%u knife_end=%u fps=%u pm_shoot=%u pm_last=%u\n",
		pm_hud_ready ? pm_aim.source_frames : 0, pm_hud_ready ? pm_reload.source_frames : 0,
		pm_hud_ready ? pm_reload_empty.source_frames : 0, knife_hud_ready ? knife_windup.source_frames : 0,
		knife_hud_ready ? knife_strike.source_frames : 0, vita_weapon_animation_fps,
		pm_hud_ready ? pm_shoot.source_frames : 0, pm_hud_ready ? pm_shoot_last.source_frames : 0);
	write_boot_log(g_boot_log, weapon_animation_log);
	std::array<unsigned, 6> guard_health{2, 2, 2, 2, 2, 2};
	std::array<VitaGuardCombat, 6> guard_combat;
    VitaGarbageRoad road_encounter;
    VitaGarbageRoadState road_saved=startup_state.garbage_road;
    if(g_zone==1&&ground_ready){
        const bool ready=road_encounter.restore(road_saved,player_ground);
        write_boot_log(g_boot_log,ready?"debug_garbage_road actors=4 ready=1\n":"debug_garbage_road ready=0\n");
    }
    VitaNpcBattle battle;
	for(size_t i=0;i<battle.guards.size();++i)
	{
		const auto& c=guard_transforms[i].c;
		battle.guards[i].position=battle.guards[i].home={c.x,c.y,c.z};
		battle.guards[i].health=guards_placed[i]?guard_health[i]:0;
	}
	for(size_t i=0;i<battle.scouts.size();++i)
	{
		const auto& c=scout_transforms[i].c;
		battle.scouts[i].position=battle.scouts[i].home={c.x,c.y,c.z};
	}
	float npc_animation_seconds=0.f;
	unsigned player_health = 100;
	SceUInt64 player_hit_time = 0;
	VitaGameAudio game_audio;
	apply_audio_settings(game_audio,settings);
	VitaPostProcess postprocess;
	SceUInt64 next_ambient=0;
	unsigned ambient_variant=0;
	const bool weapon_audio_ready = has_game_data && game_audio.initialize();
	write_boot_log(g_boot_log, weapon_audio_ready ? "debug_weapon_audio ready=1\n" : "debug_weapon_audio ready=0\n");
	char game_audio_log[80];
	snprintf(game_audio_log, sizeof(game_audio_log), "debug_game_audio voices=%u bunker_radio=%u\n",
		game_audio.voice_count(), static_cast<unsigned>(weapon_audio_ready && game_audio.radio_ready()));
	write_boot_log(g_boot_log, game_audio_log);
	SceUInt64 weapon_action_time = 0, reload_started = 0, reload_deadline = 0;
    SceUInt64 grenade_throw_started=0;bool grenade_pending=false;unsigned grenade_thrown_type=0;
	const VitaWeaponClipRange* reload_clip = nullptr;
	const VitaWeaponClipRange* shoot_clip = nullptr;
	bool knife_hit_pending = false, logged_aim = false;
	constexpr float pm_aim_projection = 1.60f;
	VitaButtonGesture select_gesture, square_gesture;
	enum class SquareContext { Orbit, World, Menu };
	SquareContext previous_square_context = SquareContext::Orbit;
	VitaInventory inventory;
	VitaInventoryUI inventory_ui;
	bool inventory_open = false, pda_open = false;
    VitaLootWorld loot_world;
    VitaGrenades grenades;
    struct ExplosionFlash{std::array<float,3> position{};SceUInt64 time=0;};
    std::array<ExplosionFlash,8> explosion_flashes{};
    VitaTraderStock trader_stock;
    VitaTradeBasket trade_basket;
    bool trade_open=false,exchange_buying=true;
    int loot_open=-1;
    unsigned exchange_selection=0,exchange_multiplier=1;
    VitaTradeResult exchange_result=VitaTradeResult::Ready;
    float survival_seconds=0;
    const auto exchange_open=[&](){return trade_open||loot_open>=0;};
    VitaGLVertexBuffer loot_vertices;
    VitaGLIndexBuffer loot_indices;
	if (has_game_data)
		load_inventory_text(inventory_ui);
	const bool inventory_ready = has_game_data && inventory_ui.initialize();
	if (inventory_ready)
		load_inventory_art(inventory_ui);
	else
		inventory_ui.release();
	write_boot_log(g_boot_log, inventory_ready ? "debug_inventory ready=1 layout=pc controls=select_hold,dpad,square,circle,right_stick_scroll\n" :
		"debug_inventory ready=0\n");
	VitaHud hud;
	const bool hud_ready = use_actor_view && load_cordon_hud(hud);
	if (hud_ready)
	{
		bool frame_found = false;
		load_level_dds_texture("ui\\ui_pda", hud.pda_frame, 512u * 512u * 4u, frame_found, 512);
	}
	const bool pda_ready = hud_ready && hud.map.handle();
	write_boot_log(g_boot_log, pda_ready ? "debug_pda ready=1 controls=select_tap\n" : "debug_pda ready=0\n");
	VitaTraderUI trader_ui;
    bool talk_art_found=false;
    load_level_dds_texture("ui\\ui_numpad",trader_ui.talk_art,1024u*1024u*4,talk_art_found,1024);
    inventory_ui.hud_art=hud.atlas.handle();
    trader_ui.profiles=inventory_ui.trade_art.handle();
    trader_ui.actor_portrait=inventory_ui.actor_portrait.handle();
    const VitaDDSRegion face_regions[]={{1024,164,324,168,108,2048},{1024,328,324,168,108,2048},
        {1024,824,216,168,108,2048},{1024,0,324,168,108,2048}};
    for(unsigned i=0;i<4;++i)load_level_dds_texture("ui\\ui_npc_unique",trader_ui.faces[i],168*108*4,talk_art_found,2048,nullptr,&face_regions[i]);

	inventory_ui.merchant_portrait=trader_ui.faces[0].handle();
	std::string trader_intro, trader_replies[2];
	VitaWolfLines wolf_lines;
	VitaScoutLines scout_lines;
	VitaFlashLines flash_lines;
	bool flash_lines_ready = false;
	int trader_pgf_status = -1;
	bool trader_lines_ready = false, wolf_lines_ready = false, scout_lines_ready = false, trader_ui_ready = false;
	if (bunker_ready || g_zone)
	{
		std::string xml;
		if (load_escape_dialog_xml(xml))
		{
			trader_lines_ready = read_dialog_line(xml, "escape_trader_start_dialog_3", trader_intro) &&
				read_dialog_line(xml, "escape_trader_start_dialog_31", trader_replies[0]) &&
				read_dialog_line(xml, "escape_trader_start_dialog_32", trader_replies[1]);
			wolf_lines_ready = wolf_model_ready && read_wolf_lines(xml, wolf_lines);
			scout_lines_ready = scout_ready[0] && read_scout_lines(xml, scout_lines);
			flash_lines_ready = allegro_model_ready && read_flash_lines(xml, flash_lines);
			trader_ui_ready = trader_lines_ready && trader_ui.initialize(trader_intro, trader_replies, trader_pgf_status,
				wolf_lines_ready ? &wolf_lines : nullptr, scout_lines_ready ? &scout_lines : nullptr,
				flash_lines_ready ? &flash_lines : nullptr);
		}
	}
	const bool wolf_ui_ready = trader_ui_ready && trader_ui.wolf_ready();
	const bool scout_ui_ready = trader_ui_ready && trader_ui.scout_ready();
	const bool flash_ui_ready = trader_ui_ready && trader_ui.flash_ready();
	write_boot_log(g_boot_log, flash_ui_ready ? "debug_allegro_dialog ready=1\n" : "debug_allegro_dialog ready=0\n");
	if (scout_waypoints)
	{
		char line[96];
		snprintf(line, sizeof(line), "debug_scout_dialog french_lines=%u ui=%u\n",
			static_cast<unsigned>(scout_lines_ready), static_cast<unsigned>(scout_ui_ready));
		write_boot_log(g_boot_log, line);
	}
	if (wolf_spawn.found)
	{
		char line[168];
		snprintf(line, sizeof(line), "debug_wolf ready=%u grounded=%u posed=%u textures=%u french_lines=%u ui=%u\n",
			static_cast<unsigned>(wolf_model_ready), static_cast<unsigned>(wolf_grounded), static_cast<unsigned>(wolf_posed),
			static_cast<unsigned>((wolf_visuals[0].texture.handle() != 0) + (wolf_visuals[1].texture.handle() != 0)),
			static_cast<unsigned>(wolf_lines_ready), static_cast<unsigned>(wolf_ui_ready));
		write_boot_log(g_boot_log, line);
	}
	char bunker_log[192];
	snprintf(bunker_log, sizeof(bunker_log),
		"debug_trader_bunker ready=%u meshes=%u ground_y=%.2f french_lines=%u ui=%u pgf_status=%d\n",
		static_cast<unsigned>(bunker_ready), static_cast<unsigned>(bunker_candidates.size()), bunker_ground_y,
		static_cast<unsigned>(trader_lines_ready), static_cast<unsigned>(trader_ui_ready), trader_pgf_status);
	write_boot_log(g_boot_log, bunker_log);
	if (use_actor_view)
	{
		char line[160];
		snprintf(line, sizeof(line), "debug_player_ground_start grounded=%u reference_y=%.2f ground_y=%.2f\n",
			static_cast<unsigned>(start_grounded), level_center[1], start_ground_y);
		write_boot_log(g_boot_log, line);
		snprintf(line, sizeof(line), "debug_player_view eye=%.2f,%.2f,%.2f toggle=triangle_cross fov_x=80\n",
			player_eye[0], player_eye[1], player_eye[2]);
		write_boot_log(g_boot_log, line);
	}
	size_t reference_index_budget = 0;
	const size_t reference_count = level_visuals.size() < debug_visual_limit
		? level_visuals.size() : debug_visual_limit;
	for (size_t i = 0; i < reference_count; ++i)
	{
		const size_t reference_index = i * level_visuals.size() / reference_count;
		if (level_visuals[reference_index].index_count <=
			(std::numeric_limits<size_t>::max)() - reference_index_budget)
			reference_index_budget += level_visuals[reference_index].index_count;
	}
	if (reference_index_budget > debug_index_budget_limit)
		reference_index_budget = debug_index_budget_limit;
	// Identify nearby construction materials before choosing the initial heading.
	struct NearbyStructure
	{
		size_t index;
		float weight;
	};
	std::vector<NearbyStructure> nearby_structures;
	std::vector<u8> player_structure(level_visuals.size(), 0);
	float initial_player_yaw = 0.f;
	if (use_actor_view)
	{
		for (size_t i = 0; i < level_visuals.size(); ++i)
		{
			const VitaDebugVisualRange& visual = level_visuals[i];
			if (!visual.has_bounds || visual.has_tree_transform || visual.shader_id >= level_texture_names.size())
				continue;
			const std::string& texture = level_texture_names[visual.shader_id];
			if (texture.compare(0, 5, "wood\\") != 0 && texture.compare(0, 5, "ston\\") != 0 &&
				texture.compare(0, 6, "crete\\") != 0 && texture.compare(0, 4, "mtl\\") != 0 &&
				texture.compare(0, 5, "roof\\") != 0)
				continue;
			player_structure[i] = 1;
			const float dx = visual.bounds_center[0] - player_eye[0];
			const float dz = visual.bounds_center[2] - player_eye[2];
			const float distance_squared = dx * dx + dz * dz;
			if (!(distance_squared <= 65.f * 65.f) || visual.index_count > reference_index_budget / 2)
				continue;
			const float weight = static_cast<float>(std::min(visual.index_count, 1500u)) /
				(1.f + distance_squared / (25.f * 25.f));
			nearby_structures.push_back(NearbyStructure{i, weight});
		}
		float best_weight = 0.f;
		unsigned best_count = 0;
		constexpr unsigned heading_count = 24;
		for (unsigned heading = 0; heading < heading_count; ++heading)
		{
			const float yaw = heading * (6.2831853f / heading_count);
			float center[3];
			compute_player_view_center(player_eye, player_view_scale, yaw, player_view_pitch, center);
			float heading_weight = 0.f;
			unsigned visible_structures = 0;
			for (const NearbyStructure& structure : nearby_structures)
			{
				float score = 0.f;
				u32 cell = 0;
				if (!score_visual_for_view(level_visuals[structure.index], center, player_view_scale,
					yaw, player_view_pitch, vita_player_fov_scale, score, cell))
					continue;
				heading_weight += structure.weight / (1.f + score);
				++visible_structures;
			}
			if (heading_weight > best_weight)
			{
				best_weight = heading_weight;
				best_count = visible_structures;
				initial_player_yaw = yaw;
			}
		}
		char line[160];
		snprintf(line, sizeof(line), "debug_player_heading yaw_deg=%.0f nearby_structures=%u visible_structures=%u\n",
			initial_player_yaw * 57.29578f, static_cast<unsigned>(nearby_structures.size()), best_count);
		write_boot_log(g_boot_log, line);
	}
	std::vector<size_t> terrain_visuals;
	for (size_t i = 0; i < level_visuals.size(); ++i)
	{
		const VitaDebugVisualRange& visual = level_visuals[i];
		if (visual.has_bounds && !visual.has_tree_transform && visual.shader_id < level_shader_names.size() &&
			level_shader_names[visual.shader_id].find("terrain") != std::string::npos)
			terrain_visuals.push_back(i);
	}
	constexpr float near_terrain_radius = 55.f;
	const auto terrain_near_eye = [&](const float eye[3])
	{
		std::vector<std::pair<float, size_t>> nearby;
		const float x = eye[0], z = eye[2];
		for (const size_t index : terrain_visuals)
		{
			const VitaDebugVisualRange& visual = level_visuals[index];
			const float dx = x - std::max(visual.bounds_minimum[0], std::min(x, visual.bounds_maximum[0]));
			const float dz = z - std::max(visual.bounds_minimum[2], std::min(z, visual.bounds_maximum[2]));
			const float distance = dx * dx + dz * dz;
			if (distance <= near_terrain_radius * near_terrain_radius)
				nearby.emplace_back(distance, index);
		}
		std::sort(nearby.begin(), nearby.end());
		return nearby;
	};
	struct ViewSelection
	{
		std::vector<size_t> indices;
		size_t candidates;
		size_t index_count;
		size_t focus_visuals;
		size_t focus_indices;
		size_t terrain_visuals;
		size_t terrain_indices;
		size_t nearby_terrain;
		size_t nearby_terrain_selected;
	};
	const auto select_view = [&](const float center[3], float scale, float yaw, float pitch, float projection_scale, bool player,
		const std::vector<size_t>* spatial_candidates = nullptr, const float* moving_eye = nullptr,
		const std::vector<size_t>* nearby_shell = nullptr)
	{
		std::vector<VitaVisibleVisual> visible_cells[16];
		size_t visible_candidate_count = 0;
		const size_t candidate_count = spatial_candidates ? spatial_candidates->size() : level_visuals.size();
		for (size_t candidate = 0; candidate < candidate_count; ++candidate)
		{
			const size_t i = spatial_candidates ? (*spatial_candidates)[candidate] : candidate;
			float score = 0.f;
			u32 screen_cell = 0;
			if (score_visual_for_view(level_visuals[i], center, scale, yaw, pitch, projection_scale, score, screen_cell))
			{
				VitaVisibleVisual visible = {i, score, screen_cell};
				visible_cells[screen_cell].push_back(visible);
				++visible_candidate_count;
			}
		}
		if (visible_candidate_count == 0 && !player)
		{
			for (size_t i = 0; i < level_visuals.size(); ++i)
			{
				VitaVisibleVisual visible = {i, static_cast<float>(i), 0};
				visible_cells[0].push_back(visible);
			}
			visible_candidate_count = level_visuals.size();
		}
		for (size_t cell = 0; cell < 16; ++cell)
			std::sort(visible_cells[cell].begin(), visible_cells[cell].end(),
				[](const VitaVisibleVisual& lhs, const VitaVisibleVisual& rhs)
				{
					return lhs.score == rhs.score ? lhs.index < rhs.index : lhs.score < rhs.score;
				});
		std::vector<size_t> render_visual_indices;
		render_visual_indices.reserve(debug_visual_limit);
		std::vector<u8> selected_visuals(level_visuals.size(), 0);
		size_t selected_index_count = 0;
		if (nearby_shell)
		{
			for (const size_t index : *nearby_shell)
			{
				const size_t count = level_visuals[index].index_count;
				if (render_visual_indices.size() >= debug_visual_limit || count > reference_index_budget - selected_index_count)
					continue;
				render_visual_indices.push_back(index);
				selected_visuals[index] = 1;
				selected_index_count += count;
			}
		}
		size_t terrain_index_count = 0;
		size_t terrain_visual_count = 0;
		const size_t terrain_index_budget = reference_index_budget / 3;
		// Keep slots available for nearby structures even when terrain ranges are small.
		const size_t terrain_visual_limit = player ? debug_visual_limit / 4 : debug_visual_limit;
		const auto add_terrain = [&](size_t index)
		{
			const size_t count = level_visuals[index].index_count;
			if (selected_visuals[index] || render_visual_indices.size() >= debug_visual_limit ||
				terrain_visual_count >= terrain_visual_limit ||
				count > terrain_index_budget - terrain_index_count ||
				count > reference_index_budget - selected_index_count)
				return false;
			render_visual_indices.push_back(index);
			selected_visuals[index] = 1;
			selected_index_count += count;
			terrain_index_count += count;
			++terrain_visual_count;
			return true;
		};
		// The ground around the moving camera must stay continuous across view updates.
		// These candidates share the existing terrain quota with farther visible terrain.
		size_t nearby_terrain = 0, nearby_terrain_selected = 0;
		if (moving_eye)
		{
			const auto nearby = terrain_near_eye(moving_eye);
			nearby_terrain = nearby.size();
			for (const auto& candidate : nearby)
				if (add_terrain(candidate.second))
					++nearby_terrain_selected;
		}
		if (terrain_anchor < level_visuals.size())
			add_terrain(terrain_anchor);
		size_t terrain_cell_cursors[16] = {};
		while (terrain_visual_count < terrain_visual_limit && terrain_index_count < terrain_index_budget)
		{
			bool selected_terrain = false;
			for (size_t cell = 0; cell < 16 && terrain_visual_count < terrain_visual_limit; ++cell)
			{
				while (terrain_cell_cursors[cell] < visible_cells[cell].size())
				{
					const size_t visual_index = visible_cells[cell][terrain_cell_cursors[cell]++].index;
					const VitaDebugVisualRange& visual = level_visuals[visual_index];
					if (visual.shader_id >= level_shader_names.size() ||
						level_shader_names[visual.shader_id].find("terrain") == std::string::npos ||
						!add_terrain(visual_index))
						continue;
					selected_terrain = true;
					break;
				}
			}
			if (!selected_terrain) break;
		}
		size_t focus_index_count = 0;
		size_t focus_visual_count = 0;
		const size_t focus_index_budget = reference_index_budget / 2;
		size_t focus_cell_cursors[16] = {};
		while (render_visual_indices.size() < debug_visual_limit && focus_index_count < focus_index_budget)
		{
			bool selected_focus = false;
			for (size_t cell = 0; cell < 16 && render_visual_indices.size() < debug_visual_limit; ++cell)
			{
				while (focus_cell_cursors[cell] < visible_cells[cell].size())
				{
					const size_t visual_index = visible_cells[cell][focus_cell_cursors[cell]++].index;
					const VitaDebugVisualRange& visual = level_visuals[visual_index];
					// Trees compete for the general budget, leaving the local reserve for structures.
					if (player ? !player_structure[visual_index] : (!visual.focus_sector || visual.has_tree_transform)) continue;
					const size_t index_count = visual.index_count;
					if (selected_visuals[visual_index] || index_count > focus_index_budget - focus_index_count ||
						index_count > reference_index_budget - selected_index_count)
						continue;
					render_visual_indices.push_back(visual_index);
					selected_visuals[visual_index] = 1;
					selected_index_count += index_count;
					focus_index_count += index_count;
					++focus_visual_count;
					selected_focus = true;
					break;
				}
			}
			if (!selected_focus) break;
		}
		size_t cell_cursors[16] = {};
		while (render_visual_indices.size() < debug_visual_limit)
		{
			bool selected_any = false;
			for (size_t cell = 0; cell < 16 && render_visual_indices.size() < debug_visual_limit; ++cell)
			{
				while (cell_cursors[cell] < visible_cells[cell].size())
				{
					const size_t visual_index = visible_cells[cell][cell_cursors[cell]++].index;
					if (selected_visuals[visual_index]) continue;
					const size_t index_count = level_visuals[visual_index].index_count;
					if (index_count > reference_index_budget - selected_index_count) continue;
					render_visual_indices.push_back(visual_index);
					selected_visuals[visual_index] = 1;
					selected_index_count += index_count;
					selected_any = true;
					break;
				}
			}
			if (!selected_any) break;
		}
		if (render_visual_indices.empty() && !player)
		{
			for (size_t i = 0; i < level_visuals.size() && render_visual_indices.size() < debug_visual_limit; ++i)
			{
				const size_t index_count = level_visuals[i].index_count;
				if (index_count > reference_index_budget - selected_index_count) continue;
				render_visual_indices.push_back(i);
				selected_visuals[i] = 1;
				selected_index_count += index_count;
			}
		}
		return ViewSelection{std::move(render_visual_indices), visible_candidate_count,
			selected_index_count, focus_visual_count, focus_index_count, terrain_visual_count, terrain_index_count,
			nearby_terrain, nearby_terrain_selected};
	};
	const ViewSelection orbit_selection = select_view(level_center, debug_view_scale, 0.f, initial_view_pitch, 1.f, false);
	ViewSelection player_selection{};
	float player_center[3] = {};
	if (use_actor_view)
	{
		compute_player_view_center(player_eye, player_view_scale, initial_player_yaw, player_view_pitch, player_center);
		player_selection = select_view(player_center, player_view_scale, initial_player_yaw, player_view_pitch, vita_player_fov_scale, true);
	}
	char player_selection_log[160];
	snprintf(player_selection_log, sizeof(player_selection_log),
		"debug_player_selection meshes=%u candidates=%u indices=%u budget=%u structures=%u structure_indices=%u\n",
		static_cast<unsigned>(player_selection.indices.size()), static_cast<unsigned>(player_selection.candidates),
		static_cast<unsigned>(player_selection.index_count), static_cast<unsigned>(reference_index_budget),
		static_cast<unsigned>(player_selection.focus_visuals), static_cast<unsigned>(player_selection.focus_indices));
	write_boot_log(g_boot_log, player_selection_log);
	snprintf(player_selection_log, sizeof(player_selection_log),
		"debug_player_passes terrain=%u terrain_indices=%u terrain_limit=%u general=%u general_indices=%u\n",
		static_cast<unsigned>(player_selection.terrain_visuals), static_cast<unsigned>(player_selection.terrain_indices),
		static_cast<unsigned>(debug_visual_limit / 4),
		static_cast<unsigned>(player_selection.indices.size() - player_selection.terrain_visuals - player_selection.focus_visuals),
		static_cast<unsigned>(player_selection.index_count - player_selection.terrain_indices - player_selection.focus_indices));
	write_boot_log(g_boot_log, player_selection_log);
	// General-pass meshes follow the two reserved passes in the draw list.
	const size_t player_general_start = player_selection.terrain_visuals + player_selection.focus_visuals;
	enum class PlayerVegetation : u8 { None, Visible, LargestOnScreen };
	std::vector<PlayerVegetation> player_general_vegetation(player_selection.indices.size(), PlayerVegetation::None);
	struct VegetationCoverage
	{
		size_t draw_slot;
		float screen_area;
	};
	std::vector<VegetationCoverage> vegetation_coverage;
	size_t vegetation_indices = 0;
	for (size_t i = player_general_start; i < player_selection.indices.size(); ++i)
	{
		const VitaDebugVisualRange& visual = level_visuals[player_selection.indices[i]];
		const bool vegetation = visual.has_tree_transform ||
			(visual.shader_id < level_texture_names.size() &&
			 level_texture_names[visual.shader_id].compare(0, 6, "trees\\") == 0) ||
			(visual.shader_id < level_shader_names.size() &&
			 level_shader_names[visual.shader_id].compare(0, 6, "flora\\") == 0);
		if (!vegetation)
			continue;
		player_general_vegetation[i] = PlayerVegetation::Visible;
		vegetation_indices += visual.index_count;
		float score = 0.f;
		u32 cell = 0;
		float screen_area = 0.f;
		if (score_visual_for_view(visual, player_center, player_view_scale, initial_player_yaw,
			player_view_pitch, vita_player_fov_scale, score, cell, &screen_area) && screen_area > 0.f)
			vegetation_coverage.push_back(VegetationCoverage{i, screen_area});
	}
	std::sort(vegetation_coverage.begin(), vegetation_coverage.end(),
		[](const VegetationCoverage& lhs, const VegetationCoverage& rhs)
		{
			return lhs.screen_area == rhs.screen_area ? lhs.draw_slot < rhs.draw_slot : lhs.screen_area > rhs.screen_area;
		});
	const size_t coverage_test_count = std::min<size_t>(4, vegetation_coverage.size());
	size_t lod_test_meshes = 0;
	size_t lod_test_indices_saved = 0;
	for (size_t i = 0; i < coverage_test_count; ++i)
	{
		const size_t slot = vegetation_coverage[i].draw_slot;
		player_general_vegetation[slot] = PlayerVegetation::LargestOnScreen;
		const VitaDebugVisualRange& visual = level_visuals[player_selection.indices[slot]];
		if (visual.coarse_index_count)
		{
			++lod_test_meshes;
			lod_test_indices_saved += visual.index_count - visual.coarse_index_count;
		}
		snprintf(player_selection_log, sizeof(player_selection_log),
			"debug_player_vegetation_lod visual=%u indices=%u coarse_indices=%u bounds_pct=%.1f\n",
			visual.visual_id, visual.index_count, visual.coarse_index_count,
			vegetation_coverage[i].screen_area * 100.f);
		write_boot_log(g_boot_log, player_selection_log);
	}
	snprintf(player_selection_log, sizeof(player_selection_log),
		"debug_player_general_vegetation meshes=%u indices=%u\n",
		static_cast<unsigned>(std::count_if(player_general_vegetation.begin(), player_general_vegetation.end(),
			[](PlayerVegetation type) { return type != PlayerVegetation::None; })),
		static_cast<unsigned>(vegetation_indices));
	write_boot_log(g_boot_log, player_selection_log);
	snprintf(player_selection_log, sizeof(player_selection_log),
		"debug_player_vegetation_lod_summary targeted=%u available=%u saved_indices=%u\n",
		static_cast<unsigned>(coverage_test_count), static_cast<unsigned>(lod_test_meshes),
		static_cast<unsigned>(lod_test_indices_saved));
	write_boot_log(g_boot_log, player_selection_log);
	const std::vector<size_t>& render_visual_indices = orbit_selection.indices;
	std::vector<u8> selected_visuals(level_visuals.size(), 0);
	for (size_t index : render_visual_indices)
		selected_visuals[index] = 1;
	std::vector<size_t> material_visuals = bunker_shell;
	material_visuals.insert(material_visuals.end(), render_visual_indices.begin(), render_visual_indices.end());
	for (size_t index : player_selection.indices)
		if (!selected_visuals[index])
			material_visuals.push_back(index);
	const size_t render_visual_count = render_visual_indices.size();
	size_t tree_visual_count = 0;
	size_t tree_index_count = 0;
	for (size_t i = 0; i < render_visual_count; ++i)
	{
		const VitaDebugVisualRange& visual = level_visuals[render_visual_indices[i]];
		if (!visual.has_tree_transform) continue;
		++tree_visual_count;
		tree_index_count += visual.index_count;
	}
	char visual_limit_log[80];
	snprintf(visual_limit_log, sizeof(visual_limit_log), "debug_visuals_drawn=%u of %u visible_candidates=%u\n",
		static_cast<unsigned>(render_visual_count), static_cast<unsigned>(level_visuals.size()),
		static_cast<unsigned>(orbit_selection.candidates));
	write_boot_log(g_boot_log, visual_limit_log);
	snprintf(visual_limit_log, sizeof(visual_limit_log), "debug_visual_focus=%u indices=%u\n",
		static_cast<unsigned>(orbit_selection.focus_visuals), static_cast<unsigned>(orbit_selection.focus_indices));
	write_boot_log(g_boot_log, visual_limit_log);
	snprintf(visual_limit_log, sizeof(visual_limit_log), "debug_visual_index_budget=%u used=%u\n",
		static_cast<unsigned>(reference_index_budget), static_cast<unsigned>(orbit_selection.index_count));
	write_boot_log(g_boot_log, visual_limit_log);
	// Include terrain admitted by the general selection as well as the reserved pass.
	size_t terrain_visual_count = 0;
	size_t terrain_index_count = 0;
	for (size_t i = 0; i < render_visual_count; ++i)
	{
		const VitaDebugVisualRange& visual = level_visuals[render_visual_indices[i]];
		if (visual.shader_id < level_shader_names.size() &&
			level_shader_names[visual.shader_id].find("terrain") != std::string::npos)
		{
			++terrain_visual_count;
			terrain_index_count += visual.index_count;
		}
	}
	snprintf(visual_limit_log, sizeof(visual_limit_log), "debug_visual_terrain=%u indices=%u\n",
		static_cast<unsigned>(terrain_visual_count), static_cast<unsigned>(terrain_index_count));
	write_boot_log(g_boot_log, visual_limit_log);
	snprintf(visual_limit_log, sizeof(visual_limit_log), "debug_visual_trees=%u indices=%u\n",
		static_cast<unsigned>(tree_visual_count), static_cast<unsigned>(tree_index_count));
	write_boot_log(g_boot_log, visual_limit_log);
	snprintf(visual_limit_log, sizeof(visual_limit_log), "debug_visual_draw_calls=%u merged=%u\n",
		static_cast<unsigned>(render_visual_count), 0u);
	write_boot_log(g_boot_log, visual_limit_log);
	std::vector<std::unique_ptr<VitaGLTexture> > level_textures(level_texture_names.size());
	std::vector<GLuint> level_lightmap_handles(level_lightmap_names.size(), 0);
	std::vector<u8> texture_attempted(level_texture_names.size(), 0);
	std::vector<GLuint> base_texture_handles(level_texture_names.size(), 0);
	std::vector<BaseTextureStatus> base_texture_status(level_texture_names.size(), BaseTextureStatus::Unrequested);
	std::vector<u16> fallback_logged(level_texture_names.size(), 0);
	unsigned async_dds_budget_refusals = 0;
	unsigned texture_fallbacks_logged = 0;
	u32 base_texture_reuses = 0;
	std::vector<std::string> loaded_lightmap_names;
	std::vector<std::unique_ptr<VitaGLTexture> > loaded_lightmap_textures;
	// Decode compact DDS mip levels and load only materials used by the drawn ranges.
	const size_t texture_memory_budget = 8u * 1024u * 1024u;
	size_t texture_memory_used = 0,structure_quality_bytes=0;
	const size_t lightmap_memory_budget = 1024u * 1024u;
	size_t lightmap_memory_used = 0;
	u32 lightmap_loaded_count = 0;
	std::string first_lightmap_failure;
	u32 texture_loaded_count = 0;
	u32 texture_attempted_count = 0;
	u32 texture_missing_count = 0;
	u32 texture_invalid_count = 0;
	std::string first_texture_failure;
	const size_t terrain_shader = terrain_anchor < level_visuals.size()
		? level_visuals[terrain_anchor].shader_id : level_shader_names.size();
	if (terrain_shader < level_shader_names.size() && terrain_shader < level_texture_names.size() &&
		terrain_shader < level_lightmap_names.size())
	{
		char line[1024];
		snprintf(line, sizeof(line), "debug_terrain_material visual=%u shader_id=%u shader=%s base=%s lightmap=%s\n",
			level_visuals[terrain_anchor].visual_id, static_cast<unsigned>(terrain_shader),
			level_shader_names[terrain_shader].c_str(), level_texture_names[terrain_shader].c_str(),
			level_lightmap_names[terrain_shader].c_str());
		write_boot_log(g_boot_log, line);
	}
	const auto load_lightmap = [&](u16 shader_id, const MobileTextureProfile* profile = nullptr)
	{
		if (shader_id >= level_lightmap_names.size() || level_lightmap_names[shader_id].empty() || level_lightmap_handles[shader_id]) return;
		const std::string& name = level_lightmap_names[shader_id];
		size_t cached = 0;
		for (; cached < loaded_lightmap_names.size(); ++cached)
			if (loaded_lightmap_names[cached] == name) break;
		if (cached == loaded_lightmap_names.size())
		{
			std::unique_ptr<VitaGLTexture> lightmap(new VitaGLTexture());
			bool source_found = false;
			if (lightmap_memory_used < lightmap_memory_budget &&
				load_level_dds_texture(name, *lightmap, lightmap_memory_budget - lightmap_memory_used, source_found, 128,
					terrain_shader < level_lightmap_names.size() && name == level_lightmap_names[terrain_shader]
						? "lightmap" : nullptr, nullptr, profile))
			{
				lightmap_memory_used += lightmap->uploaded_size();
				++lightmap_loaded_count;
				loaded_lightmap_names.push_back(name);
				loaded_lightmap_textures.push_back(std::move(lightmap));
			}
			else
			{
				if (first_lightmap_failure.empty()) first_lightmap_failure = name;
				loaded_lightmap_names.push_back(name);
				loaded_lightmap_textures.push_back(std::unique_ptr<VitaGLTexture>());
			}
		}
		if (loaded_lightmap_textures[cached])
			level_lightmap_handles[shader_id] = loaded_lightmap_textures[cached]->handle();
	};
	loading.draw(graphics, LoadStage::Materials, 0.78f);
	for (size_t index : material_visuals)
		load_lightmap(level_visuals[index].shader_id);
	char lightmap_summary[128];
	snprintf(lightmap_summary, sizeof(lightmap_summary),
		"debug_lightmaps_loaded=%u unique=%u bytes=%u\n", lightmap_loaded_count,
		static_cast<unsigned>(loaded_lightmap_names.size()), static_cast<unsigned>(lightmap_memory_used));
	write_boot_log(g_boot_log, lightmap_summary);
	write_boot_log(g_boot_log, "debug_terrain_lighting=r1_fixed_morning\ndebug_dds_mip_offset=header_128\ndebug_terrain_alpha_views=circle_l_base,circle_r_lightmap\n");
	if (!first_lightmap_failure.empty())
	{
		snprintf(lightmap_summary, sizeof(lightmap_summary), "debug_lightmap_first_failure=%s\n",
			first_lightmap_failure.c_str());
		write_boot_log(g_boot_log, lightmap_summary);
	}
	const auto load_base = [&](u16 shader_id, size_t budget, const MobileTextureProfile* profile = nullptr,
		MobileDDSJob* job = nullptr, size_t decode_budget = 0, bool allow_load = true)
	{
		if (shader_id >= level_texture_names.size() || texture_attempted[shader_id] ||
			level_texture_names[shader_id].empty())
			return;
		const std::string& name = level_texture_names[shader_id];
		for (size_t i = 0; i < level_textures.size(); ++i)
		{
			if (level_textures[i] && level_texture_names[i] == name)
			{
				base_texture_handles[shader_id] = level_textures[i]->handle();
				base_texture_status[shader_id] = BaseTextureStatus::Ready;
				texture_attempted[shader_id] = 1;
				++base_texture_reuses;
				return;
			}
		}
		if (!allow_load)
			return;
		if (!job && texture_memory_used >= budget)
		{
			base_texture_status[shader_id] = BaseTextureStatus::Budget;
			return;
		}
		texture_attempted[shader_id] = 1;
		++texture_attempted_count;
		const size_t remaining_texture_bytes = budget - texture_memory_used;
        const auto max_dimension=[&](size_t available){
            return vita_level_texture_dimension(name,g_zone,available,structure_quality_bytes);
        };
		if (job)
		{
			job->shader = shader_id;
			job->frame = profile->frame;
			job->name = &name;
			job->budget = decode_budget;
			job->max_dimension = max_dimension(decode_budget);
			base_texture_status[shader_id] = BaseTextureStatus::Pending;
			job->state.store(MobileDDSJob::Pending);
			if (sceKernelSignalSema(job->wake, 1) >= 0)
				return;
			job->state.store(MobileDDSJob::Idle);
			if (remaining_texture_bytes == 0)
			{
				base_texture_status[shader_id] = BaseTextureStatus::Budget;
				texture_attempted[shader_id] = 0;
				return;
			}
		}
		std::unique_ptr<VitaGLTexture> texture(new VitaGLTexture());
		bool source_found = false;
		if (load_level_dds_texture(level_texture_names[shader_id], *texture,
			remaining_texture_bytes, source_found, max_dimension(remaining_texture_bytes),
			shader_id == terrain_shader ? "base" : nullptr, nullptr, profile))
		{
			texture_memory_used += texture->uploaded_size();
            if(vita_structure_texture(name)&&texture->uploaded_size()>128u*128u*4)structure_quality_bytes+=texture->uploaded_size();
			base_texture_handles[shader_id] = texture->handle();
			base_texture_status[shader_id] = BaseTextureStatus::Ready;
			level_textures[shader_id] = std::move(texture);
			++texture_loaded_count;
		}
		else
		{
			base_texture_status[shader_id] = source_found ? BaseTextureStatus::LoadFailed : BaseTextureStatus::NotFound;
			if (source_found) ++texture_invalid_count;
			else ++texture_missing_count;
			if (first_texture_failure.empty())
				first_texture_failure = level_texture_names[shader_id];
		}
	};
	SceUInt64 loading_update = sceKernelGetProcessTimeWide();
	for (size_t i = 0; i < material_visuals.size(); ++i)
	{
		load_base(level_visuals[material_visuals[i]].shader_id, texture_memory_budget);
		const SceUInt64 now = sceKernelGetProcessTimeWide();
		if (now - loading_update >= 150000)
		{
			const float fraction = static_cast<float>(i + 1) / material_visuals.size();
			loading.draw(graphics, LoadStage::Materials, 0.82f + 0.15f * fraction);
			loading_update = now;
		}
	}
	// Cordon material names and the shared detail scale come from the PC shaders.xr.
	struct TerrainDetail
	{
		const char* shader;
		const char* texture;
		size_t texture_slot;
	};
	const TerrainDetail terrain_materials[] = {
		{"levels\\escape_grass", "detail\\detail_grnd_grass", 0},
		{"levels\\escape_sand", "detail\\detail_grnd_earth", 1},
		{"levels\\escape_asfalt", "detail\\detail_grnd_asphalt", 2},
		{"levels\\escape_asfalt_noblend", "detail\\detail_grnd_asphalt", 2},
        {"levels\\garbage_grass","detail\\detail_grnd_grass",0},
        {"levels\\garbage_asfalt","detail\\detail_grnd_asphalt",2},
        {"levels\\agroprom_grass","detail\\detail_grnd_grass",0},
        {"levels\\agroprom_gravel","detail\\detail_grnd_earth",1},
        {"levels\\agroprom_asfalt","detail\\detail_grnd_asphalt",2}
	};
	VitaGLTexture terrain_detail_textures[3];
	bool terrain_detail_attempted[3] = {};
	std::vector<GLuint> terrain_detail_handles(level_shader_names.size(), 0);
	const u32 terrain_detail_dimension = 128;
	const size_t terrain_detail_budget = terrain_detail_dimension * terrain_detail_dimension * 4u;
	for (size_t i = 0; i < material_visuals.size(); ++i)
	{
		const u16 shader_id = level_visuals[material_visuals[i]].shader_id;
		if (shader_id >= level_shader_names.size())
			continue;
		const std::string& shader = level_shader_names[shader_id];
		for (const TerrainDetail& material : terrain_materials)
		{
			const size_t name_length = strlen(material.shader);
			if (shader.size() <= name_length || shader[name_length] != '/' ||
				shader.compare(0, name_length, material.shader) != 0)
				continue;
			VitaGLTexture& detail = terrain_detail_textures[material.texture_slot];
			if (!terrain_detail_attempted[material.texture_slot])
			{
				terrain_detail_attempted[material.texture_slot] = true;
				bool source_found = false;
				const bool loaded = load_level_dds_texture(material.texture, detail, terrain_detail_budget,
					source_found, terrain_detail_dimension);
				char line[192];
				snprintf(line, sizeof(line), "debug_terrain_detail texture=%s loaded=%u found=%u bytes=%u scale=144\n",
					material.texture, static_cast<unsigned>(loaded), static_cast<unsigned>(source_found),
					static_cast<unsigned>(detail.uploaded_size()));
				write_boot_log(g_boot_log, line);
			}
			terrain_detail_handles[shader_id] = detail.handle();
			break;
		}
	}
	char texture_summary[128];
	u32 nonempty_texture_names = 0;
	for (size_t i = 0; i < level_texture_names.size(); ++i)
		if (!level_texture_names[i].empty())
			++nonempty_texture_names;
	snprintf(texture_summary, sizeof(texture_summary), "debug_level_texture_names_nonempty=%u\n",
		nonempty_texture_names);
	write_boot_log(g_boot_log, texture_summary);
	snprintf(texture_summary, sizeof(texture_summary),
		"debug_dds_textures_attempted=%u loaded=%u missing=%u invalid=%u bytes=%u\n",
		texture_attempted_count, texture_loaded_count, texture_missing_count,
		texture_invalid_count, static_cast<unsigned>(texture_memory_used));
	write_boot_log(g_boot_log, texture_summary);
	if (!first_texture_failure.empty())
	{
		snprintf(texture_summary, sizeof(texture_summary), "debug_dds_first_failure=%s\n",
			first_texture_failure.c_str());
		write_boot_log(g_boot_log, texture_summary);
	}
	// Only prepare native LOD groups containing one of the four target meshes.
	struct PlayerLodDraw
	{
		std::unique_ptr<VitaGLVertexBuffer> vertices;
		std::unique_ptr<VitaGLVertexBuffer> original_mip_vertices;
		std::unique_ptr<VitaGLVertexBuffer> alpha_vertices;
	};
	std::vector<PlayerLodDraw> player_lod_draws;
	std::vector<size_t> player_lod_slots(player_selection.indices.size(), level_lod_groups.size());
	std::vector<u8> player_lod_first(player_selection.indices.size(), 0);
	VitaGLIndexBuffer lod_indices;
	VitaGLTexture lod_atlas;
	VitaGLTexture lod_original_mip;
	constexpr u16 lod_quad_indices[] = {0, 1, 2, 3, 2, 1};
	bool lod_atlas_attempted = false;
	bool lod_ready = false;
	constexpr float max_native_lod_area = 0.20f;
	for (const VitaLodGroup& group : level_lod_groups)
	{
		std::vector<size_t> slots;
		bool targeted = false;
		float largest_child_area = 0.f;
		for (size_t i = 0; i < player_selection.indices.size(); ++i)
		{
			if (player_lod_slots[i] != level_lod_groups.size())
				continue;
			const u32 visual = level_visuals[player_selection.indices[i]].visual_id;
			if (std::find(group.children.begin(), group.children.end(), visual) == group.children.end())
				continue;
			slots.push_back(i);
			targeted |= player_general_vegetation[i] == PlayerVegetation::LargestOnScreen;
			float score = 0.f;
			u32 cell = 0;
			float area = 0.f;
			if (score_visual_for_view(level_visuals[player_selection.indices[i]], player_center, player_view_scale,
				initial_player_yaw, player_view_pitch, vita_player_fov_scale, score, cell, &area))
				largest_child_area = std::max(largest_child_area, area);
		}
		if (!targeted || group.shader_id >= level_texture_names.size() || level_texture_names[group.shader_id] != "level_lods")
			continue;

		float center[3] = {};
		for (const auto& face : group.faces)
			for (const VitaLodVertex& vertex : face)
				for (size_t axis = 0; axis < 3; ++axis)
					center[axis] += vertex.position[axis] / 32.f;
		Fvector direction;
		direction.set(center[0] - player_eye[0], center[1] - player_eye[1], center[2] - player_eye[2]);
		size_t best_face = 0;
		float best_dot = -(std::numeric_limits<float>::max)();
		for (size_t i = 0; i < 8; ++i)
		{
			Fvector points[4];
			for (size_t v = 0; v < 4; ++v)
				points[v].set(group.faces[i][v].position[0], group.faces[i][v].position[1], group.faces[i][v].position[2]);
			Fvector normal;
			normal.set(0.f, 0.f, 0.f);
			for (size_t v = 0; v < 4; ++v)
			{
				Fvector triangle_normal;
				triangle_normal.mknormal(points[v], points[(v + 1) % 4], points[(v + 2) % 4]);
				normal.add(triangle_normal);
			}
			normal.normalize_safe();
			const float dot = -direction.dotproduct(normal);
			if (dot > best_dot)
			{
				best_dot = dot;
				best_face = i;
			}
		}
		const auto& face = group.faces[best_face];
		const VitaLodVertex quad[] = {face[3], face[0], face[2], face[1]};
		VitaDebugVisualRange quad_bounds{};
		quad_bounds.has_bounds = true;
		for (size_t axis = 0; axis < 3; ++axis)
		{
			quad_bounds.bounds_minimum[axis] = quad[0].position[axis];
			quad_bounds.bounds_maximum[axis] = quad[0].position[axis];
			for (const VitaLodVertex& vertex : quad)
			{
				quad_bounds.bounds_minimum[axis] = std::min(quad_bounds.bounds_minimum[axis], vertex.position[axis]);
				quad_bounds.bounds_maximum[axis] = std::max(quad_bounds.bounds_maximum[axis], vertex.position[axis]);
			}
		}
		float score = 0.f;
		u32 cell = 0;
		float quad_area = 0.f;
		const bool quad_visible = score_visual_for_view(quad_bounds, player_center, player_view_scale,
			initial_player_yaw, player_view_pitch, vita_player_fov_scale, score, cell, &quad_area);
		if (!quad_visible || largest_child_area > max_native_lod_area || quad_area > max_native_lod_area)
		{
			char line[160];
			snprintf(line, sizeof(line), "debug_player_native_lod_skipped group=%u child_pct=%.1f quad_pct=%.1f visible=%u limit_pct=%.0f\n",
				group.visual_id, largest_child_area * 100.f, quad_area * 100.f,
				static_cast<unsigned>(quad_visible), max_native_lod_area * 100.f);
			write_boot_log(g_boot_log, line);
			continue;
		}
		if (!lod_atlas_attempted)
		{
			lod_atlas_attempted = true;
			bool source_found = false;
			constexpr u32 atlas_dimension = 512;
			lod_ready = load_level_dds_texture("level_lods", lod_atlas, atlas_dimension * atlas_dimension * 4u,
				source_found, atlas_dimension) &&
				lod_indices.initialize(lod_quad_indices, sizeof(lod_quad_indices) / sizeof(lod_quad_indices[0]));
			char line[128];
			snprintf(line, sizeof(line), "debug_player_native_lod_atlas ready=%u found=%u bytes=%u\n",
				static_cast<unsigned>(lod_ready), static_cast<unsigned>(source_found),
				static_cast<unsigned>(lod_atlas.uploaded_size()));
			write_boot_log(g_boot_log, line);
		}
		if (!lod_ready)
			break;

		const VitaD3D9VertexElement declaration[] = {
			{0, 0, 2, 0, 0, 0}, {0, 12, 1, 0, 5, 0}, {0xff, 0, 17, 0, 0, 0}
		};
		const VitaGLSemanticBinding bindings[] = {
			{0, 0, static_cast<GLuint>(debug_renderer.position_attribute_location())},
			{5, 0, static_cast<GLuint>(debug_renderer.texcoord_attribute_location())}
		};
		std::unique_ptr<VitaGLVertexBuffer> vertices(new VitaGLVertexBuffer());
		if (!vertices->initialize(quad, sizeof(quad), declaration, sizeof(declaration), bindings, 2))
			continue;
		vertices->discard_position_data();
		for (size_t slot : slots)
			player_lod_slots[slot] = player_lod_draws.size();
		player_lod_first[slots.front()] = 1;
		std::unique_ptr<VitaGLVertexBuffer> original_mip_vertices;
		VitaLodVertex cropped_quad[4];
		if (group.visual_id == 5518 && best_face == 6)
		{
			// The 64x64 atlas cell starts at (384, 1216); keep a 4-texel border.
			constexpr VitaDDSRegion region{2048, 380, 1212, 72, 72};
			memcpy(cropped_quad, quad, sizeof(cropped_quad));
			bool in_region = true;
			for (VitaLodVertex& vertex : cropped_quad)
			{
				vertex.uv[0] = (vertex.uv[0] * region.source_dimension - region.x) / region.width;
				vertex.uv[1] = (vertex.uv[1] * region.source_dimension - region.y) / region.height;
				in_region &= vertex.uv[0] >= 0.f && vertex.uv[0] <= 1.f &&
					vertex.uv[1] >= 0.f && vertex.uv[1] <= 1.f;
			}
			bool source_found = false;
			const size_t budget = region.width * region.height * 4u;
			if (in_region && load_level_dds_texture("level_lods", lod_original_mip, budget, source_found,
				region.width, nullptr, &region))
			{
				original_mip_vertices.reset(new VitaGLVertexBuffer());
				if (original_mip_vertices->initialize(cropped_quad, sizeof(cropped_quad), declaration,
					sizeof(declaration), bindings, 2))
					original_mip_vertices->discard_position_data();
				else
					original_mip_vertices.reset();
			}
			char mip_line[160];
			snprintf(mip_line, sizeof(mip_line),
				"debug_player_native_lod_mip group=5518 facet=6 ready=%u found=%u size=72x72 bytes=%u compare=L\n",
				static_cast<unsigned>(original_mip_vertices != nullptr), static_cast<unsigned>(source_found),
				static_cast<unsigned>(lod_original_mip.uploaded_size()));
			write_boot_log(g_boot_log, mip_line);
		}
		std::unique_ptr<VitaGLVertexBuffer> alpha_vertices;
		if (original_mip_vertices)
		{
			char error[512] = {};
			if (debug_renderer.initialize_lod_alpha(error, sizeof(error)))
			{
				const VitaGLSemanticBinding alpha_bindings[] = {
					{0, 0, static_cast<GLuint>(debug_renderer.lod_position_attribute())},
					{5, 0, static_cast<GLuint>(debug_renderer.lod_uv_attribute())}
				};
				alpha_vertices.reset(new VitaGLVertexBuffer());
				if (alpha_vertices->initialize(cropped_quad, sizeof(cropped_quad), declaration, sizeof(declaration), alpha_bindings, 2))
					alpha_vertices->discard_position_data();
				else
					alpha_vertices.reset();
			}
			write_boot_log(g_boot_log, alpha_vertices ? "debug_player_lod_alpha ready=1 threshold=8\n" :
				"debug_player_lod_alpha ready=0\n");
			if (error[0])
			{
				write_boot_log(g_boot_log, error);
				write_boot_log(g_boot_log, "\n");
			}
		}
		player_lod_draws.push_back(PlayerLodDraw{std::move(vertices), std::move(original_mip_vertices), std::move(alpha_vertices)});
		char line[128];
		snprintf(line, sizeof(line), "debug_player_native_lod group=%u children=%u facet=%u child_pct=%.1f quad_pct=%.1f\n",
			group.visual_id, static_cast<unsigned>(slots.size()), static_cast<unsigned>(best_face),
			largest_child_area * 100.f, quad_area * 100.f);
		write_boot_log(g_boot_log, line);
	}
	enum class LodCaptureMode { Geometry, AlphaThreshold, Blend };
	if (use_actor_view)
		write_boot_log(g_boot_log, "debug_player_lod_capture_marker green=alpha8 cyan=blend amber=geometry blue=geometry_vsync_off magenta=terrain_coverage controls=none_geometry,L_blend,LR_alpha\n");
	float view_yaw = bunker_ready ? bunker_yaw : 0.f;
	float view_pitch = bunker_ready ? bunker_pitch : initial_view_pitch;
	float view_scale = debug_view_scale;
	bool player_view = bunker_ready || (g_zone&&start_grounded);
	bool bunker_view = bunker_ready;
	bool bunker_surface = false;
	bool door_unlocked = false, door_open_logged = false;
	SceUInt64 door_open_start = 0;
	bool bunker_selection_dirty = bunker_ready;
	bool bunker_shell_selected = false;
	bool view_toggle_held = false;
	bool trader_dialog_open = false, trader_confirmed = false;
	bool wolf_dialog_open = false, wolf_mission_accepted = false, wolf_draw_logged = false;
	bool scout_dialog_open = false, scout_met = false, scout_draw_logged = false;
	bool farm_cleared = false, allegro_dialog_open = false, flash_dialog_open = false;
	bool allegro_draw_logged = false;
	unsigned allegro_page = 0, flash_page = 0;
	unsigned scout_page = 0, scout_choice = 0;
	bool assault_with_group = false, assault_arrival_logged = false, guard_draw_logged = false;
	std::array<bool, 6> bandit_called{};
	unsigned bandit_voice_variant = 0;
	SceUInt64 next_bandit_callout = 0, next_ally_callout=0;
	bool trader_intro_played = false, radio_audible = false;
	bool farm_fight_started = false, scout_attack_called = false, scout_victory_called = false;
 unsigned campaign_story=startup_load_requested?startup_state.story:0;
 std::array<std::vector<uint8_t>,vita_zone_count> campaign_worlds=startup_load_requested?startup_state.worlds:decltype(campaign_worlds){};
 bool campaign_open=false,campaign_cleared_dialog=false,campaign_is_road=false,campaign_is_stash=false;
 unsigned campaign_page=0,last_campaign_key=~0u;
 uint64_t campaign_visible_until=0,transition_retry_after=0;
 VitaZoneConfirmation zone_confirmation;
 const bool zone_confirmation_ui_ready=trader_ui_ready&&trader_ui.prepare_zone_confirmation();
 write_boot_log(g_boot_log,zone_confirmation_ui_ready?"debug_zone_confirmation ui_ready=1\n":"debug_zone_confirmation ui_ready=0\n");

 std::vector<std::string> campaign_pages;
 auto road_pages=g_zone==1?read_campaign_pages(1,true,true):std::vector<std::string>{};
 const auto strelok_pages=paginate_vita_campaign_text(inventory_ui.item_details[23].empty()?read_strelok_information():inventory_ui.item_details[23]);
 auto north_pages=read_campaign_pages(g_zone,true);
 auto attack_pages=read_campaign_pages(g_zone,false);
 {char log[128];snprintf(log,sizeof(log),"debug_campaign zone=%u north_pages=%u attack_pages=%u\n",
     g_zone,unsigned(north_pages.size()),unsigned(attack_pages.size()));write_boot_log(g_boot_log,log);}
 const auto campaign_name=[&](){return campaign_is_stash?"PDA":campaign_is_road?vita_text("Yourik","Yurik"):g_zone==2?vita_text("La Taupe","Mole"):vita_campaign_zones[g_zone].leader;};
 if(trader_ui_ready&&g_zone==3){
  const auto begin=sceKernelGetProcessTimeWide();unsigned cached=0;
  for(const auto&page:strelok_pages)cached+=trader_ui.set_campaign_page("PDA",page);
  for(const char*caption:{vita_text("Trouver la planque de Strelok","Find Strelok's stash"),
   vita_text("Trouver les informations de Strelok","Find Strelok's information"),vita_text("Trouver Spectre","Find Ghost")})
   trader_ui.set_campaign_objective(caption);
  char line[120];snprintf(line,sizeof(line),"debug_strelok ui_prepared_pages=%u prepare_us=%llu\n",cached,static_cast<unsigned long long>(sceKernelGetProcessTimeWide()-begin));write_boot_log(g_boot_log,line);
 }
 if(trader_ui_ready&&!north_pages.empty())trader_ui.set_campaign_page(campaign_name(),north_pages[0]);
 if(g_zone==1){bool found=false;const VitaDDSRegion face{1024,825,324,165,108,2048};
  load_level_dds_texture("ui\\ui_npc_unique",trader_ui.road_face,165*108*4,found,2048,nullptr,&face);}
 if(g_zone){bool found=false;
  const VitaDDSRegion face{1024,g_zone==1?0u:164u,g_zone==1?432u:0u,168,108,2048};
  load_level_dds_texture("ui\\ui_npc_unique",trader_ui.campaign_face,168*108*4,found,2048,nullptr,&face);
 }
	float assault_elapsed = 0.f;
 std::array<float,3> scout_route_metres{};
 std::array<bool,3> scout_route_rejoin{};
	VitaHudObjective last_hud_objective = VitaHudObjective::None;
	SceUInt64 objective_visible_until = 0;
	constexpr SceUInt64 objective_duration_us = 5000000;
	write_boot_log(g_boot_log, "debug_hud_objective automatic_ms=5000 recall=start_hold\n");
	unsigned wolf_page = 0, wolf_choice = 0;
	bool trader_model_draw_logged = false, trader_was_focused = false;
	SceUInt64 trader_idle_start = 0;
	bool trader_idle_logged = false, trader_idle_loop_logged = false;
	unsigned trader_choice = 0;
	u32 previous_buttons = 0;
    SceCtrlData campaign_initial_pad{};if(sceCtrlPeekBufferPositive(0,&campaign_initial_pad,1)>0)previous_buttons=campaign_initial_pad.buttons;
	const float* starting_eye = bunker_view ? bunker_eye : player_eye;
	float moving_player_eye[3] = {starting_eye[0], starting_eye[1], starting_eye[2]};
	float ground_y = bunker_view ? bunker_ground_y : start_ground_y;
	bool grounded = bunker_view ? bunker_grounded : start_grounded;
	VitaPlayerJump jump;
	write_boot_log(g_boot_log, "debug_player_controls jump=cross interact=square_tap reload=square_hold hold_ms=450 diagnostic=triangle_start\ndebug_player_stairs support=disk radius=0.32 substep=0.12 toe=0.15 flat_fast_path=1\n");
	unsigned ground_hits = 0, ground_misses = 0, ground_blocked = 0;
	unsigned wall_contacts = 0, wall_stops = 0;
	SceUInt64 ground_peak_us = 0, wall_peak_us = 0;
	constexpr float player_move_speed = 3.f;
	constexpr float player_look_speed = 1.5f;
	const int analog_status = sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
	VitaPlayerMovement movement;
	SceTouchPanelInfo rear_panel{};
	const bool rear_sprint_ready = use_actor_view &&
		sceTouchGetPanelInfo(SCE_TOUCH_PORT_BACK, &rear_panel) >= 0 &&
		sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START) >= 0;
	const int rear_center_x = (rear_panel.minAaX + rear_panel.maxAaX) / 2;
	const int rear_center_y = (rear_panel.minAaY + rear_panel.maxAaY) / 2;
	bool rear_sprint_held = false;
	VitaPlayerStance logged_stance = VitaPlayerStance::Standing;
	bool logged_sprint = false;
	write_boot_log(g_boot_log, rear_sprint_ready ? "debug_player_controls sprint=l3_or_rear_upper_left crouch=circle\n" :
		"debug_player_controls sprint=l3 crouch=circle rear_unavailable=1\n");
	char movement_log[128];
	snprintf(movement_log, sizeof(movement_log),
		"debug_player_movement left=move right=look triangle=reset speed=%.1f analog_status=%d floor=%s walls=%s\n",
		player_move_speed, analog_status, ground_ready ? "cform" : "fixed", ground_ready ? "cform" : "off");
	write_boot_log(g_boot_log, movement_log);
	constexpr size_t extra_mobile_texture_bytes = 2u * 1024u * 1024u;
	const size_t pinned_texture_bytes = texture_memory_used;
	const size_t mobile_texture_limit = pinned_texture_bytes + extra_mobile_texture_bytes;
	std::vector<u8> pinned_textures(level_textures.size(), 0);
	std::vector<size_t> texture_keys(level_textures.size());
	for (size_t i = 0; i < level_textures.size(); ++i)
	{
		pinned_textures[i] = level_textures[i] != nullptr;
		const auto first = std::find(level_texture_names.begin(), level_texture_names.begin() + i, level_texture_names[i]);
		texture_keys[i] = first - level_texture_names.begin();
	}
	std::vector<unsigned> texture_last_selection(level_textures.size(), 0);
	std::vector<size_t> texture_blocks(level_textures.size(), 0);
	unsigned mobile_texture_evictions = 0;
	unsigned mobile_texture_retries = 0;
	unsigned mobile_texture_cancellations = 0;
	size_t mobile_texture_peak_bytes = 0;
	VitaVisualSpatialIndex mobile_spatial_index;
	if (use_actor_view)
		mobile_spatial_index.build(level_visuals);
	std::vector<size_t> mobile_candidates;
	mobile_candidates.reserve(level_visuals.size());
	ViewSelection mobile_selection{};
	// Cache a small band outside the viewport, then enforce the original budgets at submission.
	constexpr size_t mobile_lookahead_meshes = 48;
	constexpr size_t mobile_lookahead_indices = 24000;
	std::vector<u8> mobile_terrain(level_visuals.size(), 0), mobile_residency(level_visuals.size(), 0);
	for (const size_t index : terrain_visuals)
		mobile_terrain[index] = 1;
	struct MobileCandidate
	{
		size_t index;
		float priority;
		bool structure;
	};
	std::vector<MobileCandidate> mobile_ranked;
	mobile_ranked.reserve(level_visuals.size());
	std::vector<size_t> mobile_next;
	mobile_next.reserve(debug_visual_limit + mobile_lookahead_meshes);
	unsigned mobile_visible_peak = 0, mobile_indices_peak = 0;
	SceUInt64 mobile_select_peak_us = 0;
	bool mobile_cellar_active = false;

	const auto select_mobile_view = [&](const float center[3], float scale, float yaw, float pitch,
		const float eye[3], bool interior, bool shell)
	{
		const VitaViewFrustum frustum(center, scale, yaw, pitch, vita_player_fov_scale * 0.74f);
		const VitaViewFrustum viewport(center, scale, yaw, pitch, vita_player_fov_scale);
		if (interior)
			mobile_candidates = bunker_candidates;
		else
			mobile_spatial_index.query(frustum, mobile_candidates);
		mobile_ranked.clear();
		for (size_t index : mobile_candidates)
		{
			const auto& visual = level_visuals[index];
			if (!visual.has_bounds || !frustum.intersects(visual.bounds_minimum, visual.bounds_maximum))
				continue;
			const float distance = distance_to_visual_squared(visual, eye);
			const bool foliage = visual.shader_id < foliage_materials.size() && foliage_materials[visual.shader_id];
			const bool structure = !mobile_terrain[index] && !visual.has_tree_transform && !foliage &&
				((player_structure[index] && distance < 55.f * 55.f) || distance < 18.f * 18.f);
			const bool onscreen = viewport.intersects(visual.bounds_minimum, visual.bounds_maximum);
			// Prefer nearby meshes and retain them through small changes in distance.
			const float keep_bias = mobile_residency[index] ? 0.85f : 1.f;
			mobile_ranked.push_back({index, distance * keep_bias + (onscreen ? 0.f : 3600.f), structure});
		}
		std::sort(mobile_ranked.begin(), mobile_ranked.end(), [](const MobileCandidate& a, const MobileCandidate& b) {
			return a.priority == b.priority ? a.index < b.index : a.priority < b.priority;
		});

		mobile_next.clear();
		const size_t index_limit = reference_index_budget + mobile_lookahead_indices;
		const size_t mesh_limit = debug_visual_limit + mobile_lookahead_meshes;
		ViewSelection result{};
		result.candidates = mobile_ranked.size();
		const auto add = [&](size_t index)
		{
			const size_t count = level_visuals[index].index_count;
			if (mobile_residency[index] == 2 || mobile_next.size() >= mesh_limit ||
				count > index_limit - result.index_count)
				return false;
			mobile_next.push_back(index);
			mobile_residency[index] = 2;
			result.index_count += count;
			return true;
		};
		if (shell)
			for (size_t index : bunker_shell)
				add(index);
		// Reserve the cellar's visible surfaces around the entrance and while inside.
		// The cached and submitted geometry still shares the level's normal budgets.
		bool cellar_active = false;
		if (!interior)
		{
			constexpr float cellar_preload_distance = 6.f;
			const size_t cellar_index_limit = reference_index_budget / 2;
			size_t cellar_indices = 0;
			for (const auto& candidate : mobile_ranked)
			{
				const auto& visual = level_visuals[candidate.index];
				if (!visual.village_cellar ||
					distance_to_visual_squared(visual, eye) > cellar_preload_distance * cellar_preload_distance ||
					visual.index_count > cellar_index_limit - cellar_indices)
					continue;
				if (add(candidate.index))
				{
					cellar_indices += visual.index_count;
					cellar_active = true;
				}
			}
		}
		if (cellar_active != mobile_cellar_active)
		{
			mobile_cellar_active = cellar_active;
			write_boot_log(g_boot_log, cellar_active ? "debug_village_cellar priority=1\n" : "debug_village_cellar priority=0\n");
		}
		const auto add_terrain = [&](size_t index)
		{
			const size_t count = level_visuals[index].index_count;
			if (result.terrain_visuals >= debug_visual_limit / 4 ||
				count > reference_index_budget / 3 - result.terrain_indices || !add(index))
				return false;
			++result.terrain_visuals;
			result.terrain_indices += count;
			return true;
		};
		if (!interior)
		{
			const auto nearby = terrain_near_eye(eye);
			result.nearby_terrain = nearby.size();
			for (const auto& terrain : nearby)
				result.nearby_terrain_selected += add_terrain(terrain.second);
		}
		for (const auto& candidate : mobile_ranked)
			if (mobile_terrain[candidate.index])
				add_terrain(candidate.index);
		const size_t structure_mesh_limit = debug_visual_limit / 2;
		const size_t structure_index_limit = reference_index_budget / 2;
		for (const auto& candidate : mobile_ranked)
		{
			const size_t count = level_visuals[candidate.index].index_count;
			if (result.focus_visuals == structure_mesh_limit)
				break;
			if (candidate.structure && count <= structure_index_limit - result.focus_indices && add(candidate.index))
			{
				++result.focus_visuals;
				result.focus_indices += count;
			}
		}
		for (const auto& candidate : mobile_ranked)
			if (mobile_next.size() == mesh_limit)
				break;
			else
				add(candidate.index);

		for (const size_t index : mobile_selection.indices)
			if (mobile_residency[index] == 1)
				mobile_residency[index] = 0;
		for (const size_t index : mobile_next)
			mobile_residency[index] = 1;
		result.indices.swap(mobile_next);
		mobile_next.swap(mobile_selection.indices);
		mobile_selection = std::move(result);
	};
	std::vector<size_t> terrain_coverage_draws;
	bool terrain_coverage = false, terrain_coverage_held = false, terrain_coverage_refresh = false;
	unsigned terrain_coverage_selection = 0;
	constexpr u32 terrain_coverage_buttons = SCE_CTRL_LTRIGGER | SCE_CTRL_UP;
	write_boot_log(g_boot_log, "debug_player_near_terrain radius_m=55 quota=existing_terrain_limits\n");
	write_boot_log(g_boot_log, "debug_player_terrain_coverage controls=L_up radius_m=55 extra_index_limit=300000\n");
	unsigned mobile_texture_batches = 0;
	unsigned async_dds_completed = 0;
	SceUInt64 async_dds_peak_upload_us = 0;
	SceUInt64 mobile_texture_total_us = 0;
	SceUInt64 mobile_texture_peak_us = 0;
	bool mobile_active = false;
	size_t next_mobile_material = 0;
	float selection_x = player_eye[0], selection_y = player_eye[1], selection_z = player_eye[2];
	float selection_yaw = initial_player_yaw, selection_pitch = player_view_pitch;
	SceUInt64 selection_time = 0;
	unsigned selection_updates = 0;
	// Every shader that shares a DDS also shares its visibility and last-use stamp.
	const auto can_evict_texture = [&](size_t owner)
	{
		return level_textures[owner] && !pinned_textures[owner] &&
			texture_last_selection[texture_keys[owner]] != selection_updates;
	};
	const auto reclaimable_texture_bytes = [&]()
	{
		size_t available = mobile_texture_limit - texture_memory_used;
		for (size_t owner = 0; owner < level_textures.size(); ++owner)
			if (can_evict_texture(owner))
				available += level_textures[owner]->uploaded_size();
		return available;
	};
	const auto reserve_mobile_texture = [&](size_t bytes, unsigned frame)
	{
		if (bytes > reclaimable_texture_bytes())
			return false;
		while (bytes > mobile_texture_limit - texture_memory_used)
		{
			size_t oldest = level_textures.size();
			for (size_t owner = 0; owner < level_textures.size(); ++owner)
			{
				if (can_evict_texture(owner) && (oldest == level_textures.size() ||
					texture_last_selection[texture_keys[owner]] < texture_last_selection[texture_keys[oldest]]))
					oldest = owner;
			}
			const GLuint handle = level_textures[oldest]->handle();
			const size_t reclaimed = level_textures[oldest]->uploaded_size();
			for (size_t shader = 0; shader < base_texture_handles.size(); ++shader)
			{
				if (base_texture_handles[shader] != handle)
					continue;
				base_texture_handles[shader] = 0;
				base_texture_status[shader] = BaseTextureStatus::Unrequested;
				texture_attempted[shader] = 0;
			}
			level_textures[oldest]->release();
			level_textures[oldest].reset();
			texture_memory_used -= reclaimed;
            if(vita_structure_texture(level_texture_names[oldest])&&reclaimed>128u*128u*4)
                structure_quality_bytes-=std::min(structure_quality_bytes,reclaimed);
			++mobile_texture_evictions;
			if (mobile_texture_evictions <= 16 || mobile_texture_evictions % 32 == 0)
			{
				char line[384];
				snprintf(line, sizeof(line), "debug_player_texture_evict frame=%u texture=%s bytes=%u count=%u\n",
					frame, level_texture_names[oldest].c_str(), static_cast<unsigned>(reclaimed), mobile_texture_evictions);
				write_boot_log(g_boot_log, line);
			}
		}
		return true;
	};
	write_boot_log(g_boot_log, "debug_player_dynamic_selection interval_ms=100 move_m=1 angle_rad=0.07 extra_texture_bytes=2097152\n");

	write_boot_log(g_boot_log, "debug_player_frame_profile version=7 modal_browser_separate=1 long_frame_us=50000 dds_toggle=LR_down boot_log=buffered present=gxm_calls vsync_default=0 vsync_toggle=orbit_start\n");
	g_boot_log_buffering.store(true);
	MobileDDSJob mobile_dds;
	SceUID mobile_dds_worker = -1;
	if (use_actor_view)
	{
		mobile_dds.wake = sceKernelCreateSema("stalker_dds_wake", 0, 0, 1, nullptr);
		if (mobile_dds.wake >= 0)
		{
			mobile_dds_worker = sceKernelCreateThread("stalker_dds", mobile_dds_thread,
				0x10000100, 0x10000, 0, 0, nullptr);
			if (mobile_dds_worker >= 0)
			{
				MobileDDSJob* job = &mobile_dds;
				if (sceKernelStartThread(mobile_dds_worker, sizeof(job), &job) < 0)
				{
					sceKernelDeleteThread(mobile_dds_worker);
					mobile_dds_worker = -1;
				}
			}
		}
	}
	write_boot_log(g_boot_log, mobile_dds_worker >= 0 ? "debug_player_dds_worker ready=1\n" :
		"debug_player_dds_worker ready=0\n");
	constexpr const char* save_directory = "ux0:data/stalker";
	bool save_menu_open = false, death_menu_shown = false;
	bool pause_resources_active = false, quit_from_menu = false;
	VitaMainMenu pause_menu;
	VitaGameAudio pause_music;
 bool pause_resources_ready=false,pause_music_ready=false;
 const auto prepare_pause_resources=[&](){
  if(pause_resources_ready)return true;
  const auto started=sceKernelGetProcessTimeWide();
  const bool ui_ready=pause_menu.initialize();bool found=false,background_ready=false,artwork_ready=false;
  if(ui_ready){
   background_ready=load_level_dds_texture("ui\\ui_static_main_menu_background",pause_menu.background,512u*512u*4,found,512);
   artwork_ready=load_level_dds_texture("ui\\ui_mainmenu",pause_menu.atlas,512u*512u*4,found,512);
  }
  apply_audio_settings(pause_music,settings);pause_music_ready=ui_ready&&pause_music.initialize(true);
  pause_music.set_radio(0.f);
  pause_resources_ready=ui_ready;
  char log[180];snprintf(log,sizeof(log),"debug_pause_menu prepared=%u background=%u artwork=%u music=%u prepare_us=%llu texture_limit=512\n",
   unsigned(ui_ready),unsigned(background_ready),unsigned(artwork_ready),unsigned(pause_music_ready),static_cast<unsigned long long>(sceKernelGetProcessTimeWide()-started));
  write_boot_log(g_boot_log,log);return ui_ready;
 };

	unsigned save_choice = 0;
	VitaSaveMessage save_message = VitaSaveMessage::None;
	SceUInt64 pause_started = 0;
	VitaButtonGesture start_gesture;
	const auto initial_guard_transforms = guard_transforms;
    float played_seconds=0,auto_played=0,auto_retry_after=10;
    unsigned auto_flags=0;

	const auto capture_save = [&]()
	{
		VitaSaveState state;
		state.inventory = inventory.state();
        state.zone=g_zone;state.story=campaign_story;state.worlds=campaign_worlds;
        state.garbage_road=g_zone==1&&road_encounter.ready?road_encounter.state():road_saved;
        state.play_seconds=static_cast<unsigned>(played_seconds);
        state.survival_seconds=survival_seconds;
        state.world_details=true;
        state.route_metres=scout_route_metres;
        for(unsigned i=0;i<3;++i)if(scout_route_rejoin[i])state.route_rejoin|=1u<<i;
        for(unsigned i=0;i<6;++i)if(bandit_called[i])state.voice_flags|=1u<<i;
        if(scout_attack_called)state.voice_flags|=64;
        if(scout_victory_called)state.voice_flags|=128;
        const auto clock=pause_started?pause_started:sceKernelGetProcessTimeWide();
        const auto delay=[&](uint64_t deadline){return unsigned(deadline>clock?std::min(uint64_t(60000),(deadline-clock+999)/1000):0);};
        state.voice_delay_ms={delay(next_bandit_callout),delay(next_ally_callout)};
        for(unsigned i=0;i<9;++i){
            const auto&a=i<6?battle.guards[i]:battle.scouts[i-6];auto&out=state.agents[i];
            out.home=a.home;
            const auto&transform=i<6?guard_transforms[i]:scout_transforms[i-6];
            out.yaw=-atan2f(transform.k.x,transform.k.z);
            out.shots=a.shots;out.target=a.target;
            out.flags=(a.walking?1u:0u)|(a.engaged?2u:0u);
            out.next_shot_ms=delay(a.next_shot);out.reload_ms=delay(a.reload_until);
            out.shot_age_ms=a.shot_time&&clock>=a.shot_time?unsigned(std::min(uint64_t(60000),(clock-a.shot_time)/1000)):60000;
        }
        state.loot=loot_world;state.trader=trader_stock;
		state.stance = static_cast<unsigned>(movement.stance());
		state.endurance = movement.endurance();
		std::copy(moving_player_eye, moving_player_eye + 3, state.eye.begin());
		state.ground = ground_y;
		state.yaw = view_yaw;
		state.pitch = view_pitch;
		state.health = player_health;
		state.flags = (bunker_view ? SaveBunker : 0) | (bunker_surface ? SaveSurface : 0) |
			(door_unlocked ? SaveDoorUnlocked : 0) | (door_open_logged ? SaveDoorOpen : 0) |
			(trader_confirmed ? SaveTraderConfirmed : 0) | (wolf_mission_accepted ? SaveWolfMission : 0) |
			(scout_met ? SaveScoutMet : 0) | (farm_cleared ? SaveFarmCleared : 0) |
			(assault_with_group ? SaveGroup : 0) | (farm_fight_started ? SaveFightStarted : 0) |
			(assault_arrival_logged ? SaveScoutsArrived : 0) | (trader_intro_played ? SaveTraderIntro : 0);
		for (size_t i = 0; i < state.guards.size(); ++i)
			state.guards[i] = guards_placed[i] ? guard_health[i] : 0;
		for(size_t i=0;i<state.guard_positions.size();++i)
		{
			state.guard_positions[i]=battle.guards[i].position;
		}
		for(size_t i=0;i<state.scout_health.size();++i)state.scout_health[i]=battle.scouts[i].health;
		state.npc_positions=true;
		state.assault_elapsed = assault_elapsed;
		for (size_t i = 0; i < state.scouts.size(); ++i)
			for (size_t element = 0; element < 16; ++element)
				state.scouts[i][element] = scout_transforms[i].m[element / 4][element % 4];
		return state;
	};
    const auto seed_supplies=[&](){
        if(g_zone)return;
        const bool placed=ground_ready&&loot_world.place_supplies(player_ground,
            {bunker_eye[0],bunker_ground_y,bunker_eye[2]},bunker_yaw);
        write_boot_log(g_boot_log,placed?"debug_loot supplies=1\n":"debug_loot supplies=0\n");
    };
    seed_supplies();
    const std::vector<VitaTraderVertex> loot_quad={
        {{-.22f,0,0},{0,1}},{{.22f,0,0},{1,1}},{{.22f,.44f,0},{1,0}},{{-.22f,.44f,0},{0,0}}};
    const bool loot_visual_ready=inventory_ready&&upload_bunker_mesh(debug_renderer,loot_quad,{0,1,2,0,2,3},loot_vertices,loot_indices);
	VitaSaveState fresh_game = capture_save();fresh_game.story=0;fresh_game.worlds={};fresh_game.garbage_road={};
	const auto apply_save = [&](const VitaSaveState& state)
	{
		float floor = state.ground;
		VitaInventory restored_inventory;
		VitaPlayerMovement restored_movement;
		if (state.zone!=g_zone || !ground_ready || !state.loot.valid() || !state.trader.valid() || !restored_inventory.restore(state.inventory) ||
			!restored_movement.restore(state.stance, state.endurance) ||
			!player_ground.support_at(state.eye[0], state.eye[2], state.ground, 0.25f, 0.25f, floor,false) ||
			fabsf(floor - state.ground) > 0.1f ||
			(!g_zone && (state.flags & SaveScoutMet) && !scout_ui_ready) ||
			(!g_zone && state.inventory.flash && !flash_ui_ready))
			return false;
		for (size_t i = 0; i < state.guards.size(); ++i)
			if (state.guards[i] && !guards_placed[i])
				return false;
		if(state.npc_positions)
			for(size_t i=0;i<state.guard_positions.size();++i)
				if(state.guards[i])
				{
					const auto& pos=state.guard_positions[i];float floor=pos[1];
					if(!player_ground.height_at(pos[0],pos[2],pos[1],.25f,.25f,floor)||fabsf(floor-pos[1])>.1f)return false;
				}
		VitaGarbageRoad restored_road;
        if(g_zone==1&&!restored_road.restore(state.garbage_road,player_ground))return false;
        // Commit only after all checks pass.
        road_saved=state.garbage_road;if(g_zone==1)road_encounter=restored_road;
        zone_confirmation.reset();campaign_story=state.story;campaign_worlds=state.worlds;campaign_open=false;campaign_is_road=false;campaign_is_stash=false;trader_ui.campaign_information=false;last_campaign_key=~0u;
		inventory = restored_inventory;
        loot_world=state.loot;trader_stock=state.trader;seed_supplies();
        trade_open=false;loot_open=-1;trade_basket={};survival_seconds=state.survival_seconds;
        played_seconds=state.play_seconds;auto_played=played_seconds;auto_retry_after=played_seconds+15;auto_flags=state.flags|(state.inventory.flash<<12)|(state.story<<16);
        grenades={};grenade_pending=false;grenade_throw_started=0;explosion_flashes={};
		movement = restored_movement;
		std::copy(state.eye.begin(), state.eye.end(), moving_player_eye);
		ground_y = state.ground;
		view_yaw = state.yaw;
		view_pitch = state.pitch;
		player_health = state.health;
		guard_health = state.guards;
		guard_transforms = initial_guard_transforms;
		guard_combat = {};
		for (size_t i = 0; i < state.scouts.size(); ++i)
			for (size_t element = 0; element < 16; ++element)
				scout_transforms[i].m[element / 4][element % 4] = state.scouts[i][element];
		battle={};
		for(size_t i=0;i<battle.guards.size();++i)
		{
			auto&a=battle.guards[i];const auto& home=initial_guard_transforms[i].c;
			if(state.npc_positions)
			{
				const auto&pos=state.guard_positions[i];
				guard_transforms[i].c.set(pos[0],pos[1],pos[2]);
			}
			const auto& c=guard_transforms[i].c;
			a.position={c.x,c.y,c.z};a.home={home.x,home.y,home.z};a.health=guard_health[i];
		}
		for(size_t i=0;i<battle.scouts.size();++i)
		{
			auto&a=battle.scouts[i];const auto&c=scout_transforms[i].c;
			a.position=a.home={c.x,c.y,c.z};a.health=state.scout_health[i];
		}
  for(size_t i=0;i<scout_routes.size();++i)
  {
   const auto& route=scout_routes[i];const auto& c=scout_transforms[i].c;
   if(route.empty()){scout_route_metres[i]=0.f;continue;}
   const auto closest=std::min_element(route.begin(),route.end(),[&](const ScoutRouteSample&a,const ScoutRouteSample&b){
    return std::hypot(a.position[0]-c.x,a.position[2]-c.z)<std::hypot(b.position[0]-c.x,b.position[2]-c.z);
   });
   scout_route_metres[i]=closest->metres;scout_route_rejoin[i]=false;
  }
        if(state.world_details){
            scout_route_metres=state.route_metres;
            const auto clock=pause_started?pause_started:sceKernelGetProcessTimeWide();
            for(unsigned i=0;i<3;++i)scout_route_rejoin[i]=(state.route_rejoin&(1u<<i))!=0;
            for(unsigned i=0;i<9;++i){
                auto&a=i<6?battle.guards[i]:battle.scouts[i-6];const auto&in=state.agents[i];
                a.home=in.home;a.yaw=in.yaw;a.shots=in.shots;a.target=in.target;
                a.walking=in.flags&1;a.engaged=in.flags&2;a.last_update=clock;
                a.next_shot=in.next_shot_ms?clock+uint64_t(in.next_shot_ms)*1000:0;
                a.reload_until=in.reload_ms?clock+uint64_t(in.reload_ms)*1000:0;
                const uint64_t age=uint64_t(in.shot_age_ms)*1000;
                a.shot_time=in.shot_age_ms<60000&&clock>age?clock-age:0;
                if(i<6){guard_transforms[i].setXYZ(0,a.yaw,0);
                    guard_transforms[i].c.set(a.position[0],a.position[1],a.position[2]);}
            }
        }
		assault_elapsed = state.assault_elapsed;
		bunker_view = state.flags & SaveBunker;
		bunker_surface = state.flags & SaveSurface;
		door_unlocked = state.flags & SaveDoorUnlocked;
		door_open_logged = state.flags & SaveDoorOpen;
		trader_confirmed = state.flags & SaveTraderConfirmed;
		wolf_mission_accepted = state.flags & SaveWolfMission;
		scout_met = state.flags & SaveScoutMet;
		farm_cleared = state.flags & SaveFarmCleared;
		assault_with_group = state.flags & SaveGroup;
		farm_fight_started = state.flags & SaveFightStarted;
		assault_arrival_logged = state.flags & SaveScoutsArrived;
		trader_intro_played = state.flags & SaveTraderIntro;
		Fmatrix swing;
		swing.setXYZ(0.f, door_open_logged ? bunker_door_open_angle : 0.f, 0.f);
		door_transform.mul_43(door_closed_transform, swing);
		door_panel = bunker_door_panel(door_transform, door_minimum, door_maximum);
		player_view = true;
		grounded = true;
		jump.reset();
		trader_dialog_open = wolf_dialog_open = scout_dialog_open = allegro_dialog_open = flash_dialog_open = false;
		inventory_open = pda_open = false;
		reload_deadline = reload_started = weapon_action_time = door_open_start = player_hit_time = 0;
		reload_clip = shoot_clip = nullptr;
		knife_hit_pending = false;
		select_gesture = square_gesture = VitaButtonGesture{};
		for(unsigned i=0;i<6;++i)bandit_called[i]=state.voice_flags&(1u<<i);
        scout_attack_called=state.voice_flags&64;scout_victory_called=state.voice_flags&128;
        const auto voice_clock=pause_started?pause_started:sceKernelGetProcessTimeWide();
        next_bandit_callout=state.voice_delay_ms[0]?voice_clock+uint64_t(state.voice_delay_ms[0])*1000:0;
        next_ally_callout=state.voice_delay_ms[1]?voice_clock+uint64_t(state.voice_delay_ms[1])*1000:0;
		bunker_selection_dirty = true;
		terrain_coverage = false;
		terrain_coverage_refresh = true;
		last_hud_objective = VitaHudObjective::None;
		death_menu_shown = false;
		game_audio.stop_speech();
		write_boot_log(g_boot_log, "debug_save applied=1\n");
		return true;
	};
	VitaSaveState startup_save=startup_state;
	if (startup_load_requested)
	{
		const bool loaded = apply_save(startup_save);
        if(loaded&&transition_resume){
            const unsigned slot=next_vita_auto_slot(save_directory);
            const bool saved=write_vita_slot(save_directory,slot,capture_save());
            write_boot_log(g_boot_log,saved?"debug_zone arrival_autosave=1\n":"debug_zone arrival_autosave=0\n");
        }
		write_boot_log(g_boot_log, loaded ? "debug_save startup_loaded=1\n" : "debug_save startup_loaded=0\n");
		if (!loaded)
		{
			save_menu_open = true;
			save_message = VitaSaveMessage::LoadFailed;
		}
	}
	write_boot_log(g_boot_log, trader_ui_ready ? "debug_save menu_ready=1 style=main_menu\n" : "debug_save menu_ready=0\n");
	write_boot_log(g_boot_log, "debug_save controls=start_tap_menu start_hold_objective files=save_a.bin,save_b.bin\n");
	loading.draw(graphics, LoadStage::Ready, .95f);
    prepare_pause_resources();
    prepare_vita_save_browser(save_browser);
	loading.draw(graphics, LoadStage::Ready, 1.f);
	loading.release();
	write_boot_log(g_boot_log, "debug_loading_screen completed=1\n");
	bool vsync_enabled = false;
	bool start_held = false;
	VSyncRunStats vsync_stats[2];
	vglWaitVblankStart(GL_FALSE);
	bool dds_paused = false;
	bool dds_toggle_held = false;
	constexpr u32 dds_toggle_buttons = SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER | SCE_CTRL_DOWN;
	unsigned frame_number = 0;
	unsigned long_frames = 0;
	SceUInt64 peak_frame_us = 0;
	SceUInt64 peak_interval_us = 0;
	int measured_render_mode = -1;
	unsigned render_sample_frames = 0, render_sample_over_budget = 0;
	SceUInt64 render_sample_us = 0;
	SceUInt64 previous_frame = sceKernelGetProcessTimeWide();
	SceUInt64 previous_present_end = previous_frame;
	const SceUID render_thread = sceKernelGetThreadId();
	for (;;)
	{
		const SceUInt64 now = sceKernelGetProcessTimeWide();
		const SceUInt64 interval_us = now - previous_frame;
		const SceUInt64 gap_us = now - previous_present_end;
		const float frame_seconds = std::min(static_cast<float>(interval_us) * 0.000001f, 0.1f);
		previous_frame = now;
		const MobileTextureProfile texture_profile{++frame_number};
		SceUInt64 frame_selection_us = 0;
		SceUInt64 frame_load_us = 0;
        SceUInt64 frame_modal_us=0;
		SceCtrlData pad{};
		const bool has_input = sceCtrlPeekBufferPositive(0, &pad, 1) > 0;
		if (!has_input)
			pad.buttons = 0;
		// Some input paths expose the shoulders as L1/R1 instead of the trigger bits.
		if (pad.buttons & SCE_CTRL_L1) pad.buttons |= SCE_CTRL_LTRIGGER;
		if (pad.buttons & SCE_CTRL_R1) pad.buttons |= SCE_CTRL_RTRIGGER;
		const bool quit_requested = (pad.buttons & (SCE_CTRL_START | SCE_CTRL_SELECT)) == (SCE_CTRL_START | SCE_CTRL_SELECT);
		const u32 pressed = pad.buttons & ~previous_buttons;
		const u32 released = previous_buttons & ~pad.buttons;
		previous_buttons = pad.buttons;
		bool rear_sprint = false;
		if (rear_sprint_ready)
		{
			SceTouchData touch{};
			if (sceTouchPeek(SCE_TOUCH_PORT_BACK, &touch, 1) > 0)
			{
				for (unsigned index = 0; index < touch.reportNum && index < SCE_TOUCH_MAX_REPORT; ++index)
				{
					const auto& finger = touch.report[index];
					rear_sprint |= finger.x < rear_center_x && finger.y < rear_center_y;
				}
			}
		}
		const bool sprint_pressed = (pressed & SCE_CTRL_L3) || (rear_sprint && !rear_sprint_held);
		rear_sprint_held = rear_sprint;
        const bool zone_dialog_active=zone_confirmation.active();
        const unsigned zone_exit_choice=zone_dialog_active?zone_confirmation.exit():0;
        bool zone_transition_confirmed=false;
		const bool save_dialog_active = trader_dialog_open || wolf_dialog_open || scout_dialog_open ||
			allegro_dialog_open || flash_dialog_open || campaign_open || exchange_open() || zone_dialog_active;
		const bool start_chord = quit_requested ||
			(pad.buttons & (SCE_CTRL_TRIANGLE | SCE_CTRL_START)) == (SCE_CTRL_TRIANGLE | SCE_CTRL_START);
		const bool save_start_down = (pad.buttons & SCE_CTRL_START) != 0;
		const auto start_action = start_gesture.update(save_start_down,
			has_input && player_view && !save_dialog_active && !start_chord && !inventory_open && !pda_open, now);
		bool save_consumed_input = save_menu_open;
		if (!player_health && !death_menu_shown && trader_ui_ready)
		{
			save_menu_open = death_menu_shown = true;
			save_choice = 2;
			save_message = VitaSaveMessage::None;
			pause_started = now;
		}
		if (start_action == VitaButtonAction::Tap && trader_ui_ready)
		{
			save_menu_open = !save_menu_open;
			save_consumed_input = true;
			save_message = VitaSaveMessage::None;
			save_choice = player_health ? 0 : 2;
			if (save_menu_open) pause_started = now;
		}
		if (save_menu_open)
		{
			save_consumed_input = true;
			if (!pause_started) pause_started = now;
			if (pressed & (SCE_CTRL_UP | SCE_CTRL_DOWN))
			{
				save_choice = (save_choice + ((pressed & SCE_CTRL_UP) ? 5 : 1)) % 6;
				save_message = VitaSaveMessage::None;
			}
			if (pressed & SCE_CTRL_CIRCLE)
				save_menu_open = false;
			else if (pressed & (SCE_CTRL_CROSS | SCE_CTRL_SQUARE))
			{
				if (save_choice == 0 && player_health)
					save_menu_open = false;
				else if (save_choice == 1 && player_health)
				{
					if (!grounded || jump.active() || (door_open_start && !door_open_logged)||grenade_pending||std::any_of(grenades.bodies.begin(),grenades.bodies.end(),[](const VitaGrenadeBody&g){return g.active;}))
						save_message = VitaSaveMessage::Unsafe;
					else
					{
						const int slot=browse_vita_saves(graphics,pause_menu,save_browser,true,&frame_modal_us);
                        previous_frame=now+frame_modal_us;previous_present_end+=frame_modal_us;
                        sceCtrlPeekBufferPositive(0,&pad,1);previous_buttons=pad.buttons;
                        if(slot<0)continue;
                        const auto write_started=sceKernelGetProcessTimeWide();
                        const auto captured=capture_save();
                        const auto captured_at=sceKernelGetProcessTimeWide();
                        const bool saved = write_vita_slot(save_directory,unsigned(slot),captured);
                        char timing[160];snprintf(timing,sizeof(timing),"debug_save_io operation=write capture_us=%llu io_us=%llu written=%u\n",
                            static_cast<unsigned long long>(captured_at-write_started),static_cast<unsigned long long>(sceKernelGetProcessTimeWide()-captured_at),unsigned(saved));write_boot_log(g_boot_log,timing);
						save_message = saved ? VitaSaveMessage::Saved : VitaSaveMessage::SaveFailed;
						write_boot_log(g_boot_log, saved ? "debug_save written=1\n" : "debug_save written=0\n");
					}
				}
				else if (save_choice == 2)
				{
					VitaSaveState state;
					const int slot=browse_vita_saves(graphics,pause_menu,save_browser,false,&frame_modal_us);
                    previous_frame=now+frame_modal_us;previous_present_end+=frame_modal_us;
                        sceCtrlPeekBufferPositive(0,&pad,1);previous_buttons=pad.buttons;
                    if(slot<0)continue;
                    const auto read_started=sceKernelGetProcessTimeWide();
                    const bool read=read_vita_slot(save_directory,unsigned(slot),state);
                    char read_timing[120];snprintf(read_timing,sizeof(read_timing),"debug_save_io operation=read io_us=%llu loaded=%u\n",
                        static_cast<unsigned long long>(sceKernelGetProcessTimeWide()-read_started),unsigned(read));write_boot_log(g_boot_log,read_timing);
                    if(read&&state.zone!=g_zone){relaunch_vita_state(graphics,state);save_message=VitaSaveMessage::LoadFailed;continue;}
                    const bool loaded = read && apply_save(state);
					save_message = loaded ? VitaSaveMessage::None : VitaSaveMessage::LoadFailed;
					save_menu_open = !loaded;
					write_boot_log(g_boot_log, loaded ? "debug_save loaded=1\n" : "debug_save loaded=0\n");
				}
				else if (save_choice == 3)
				{
					if (save_message != VitaSaveMessage::ConfirmNew)
						save_message = VitaSaveMessage::ConfirmNew;
					else
					{
						if(g_zone){VitaSaveState fresh;
                            relaunch_vita_state(graphics,fresh,true);save_message=VitaSaveMessage::LoadFailed;continue;}
                        const bool reset = apply_save(fresh_game);
						save_menu_open = !reset;
						save_message = reset ? VitaSaveMessage::None : VitaSaveMessage::LoadFailed;
						write_boot_log(g_boot_log, reset ? "debug_save new_game=1\n" : "debug_save new_game=0\n");
					}
				}
				else if(save_choice==4)
				{
					edit_options(graphics,pause_menu,pause_music,settings);
					postprocess.release();
					apply_audio_settings(game_audio,settings);
				}
				else if (save_choice == 5)
				{
					quit_from_menu = true;
					write_boot_log(g_boot_log, "debug_pause_menu action=quit\n");
				}
			}
		}
        if(save_menu_open&&!pause_resources_active){
            const auto started=sceKernelGetProcessTimeWide();
            pause_resources_active=true;
            const bool ready=prepare_pause_resources();
            game_audio.stop_speech();game_audio.set_ambience(false);
            apply_audio_settings(pause_music,settings);
            if(pause_music_ready)pause_music.set_radio(.70f);
            char log[120];snprintf(log,sizeof(log),"debug_pause_menu opened=%u open_us=%llu cached=%u\n",
                unsigned(ready),static_cast<unsigned long long>(sceKernelGetProcessTimeWide()-started),unsigned(pause_resources_ready));write_boot_log(g_boot_log,log);
            if(!ready)save_menu_open=false;
        }
        if(!save_menu_open&&pause_resources_active){
            pause_music.set_radio(0.f);pause_resources_active=false;
            write_boot_log(g_boot_log,"debug_pause_menu closed=1 cached=1\n");
        }
		if (!save_menu_open && pause_started && !zone_dialog_active)
		{
			const SceUInt64 paused = now - pause_started;
			if (door_open_start) door_open_start += paused;
			if (reload_started) reload_started += paused;
			if (reload_deadline) reload_deadline += paused;
			if (weapon_action_time) weapon_action_time += paused;
            if(grenade_throw_started)grenade_throw_started+=paused;
			if (player_hit_time) player_hit_time += paused;
			for (auto& guard : guard_combat) guard.pause(paused);
			battle.pause(paused);road_encounter.battle.pause(paused);
            if(next_bandit_callout)next_bandit_callout+=paused;
            if(next_ally_callout)next_ally_callout+=paused;
			pause_started = 0;
		}
		// Autosaves run only at safe points, with no directory scan during ordinary frames.
        if(!save_menu_open&&player_view&&player_health&&!save_dialog_active&&!inventory_open&&!pda_open)
            played_seconds+=frame_seconds;
        const unsigned checkpoint_flags=(bunker_view?SaveBunker:0)|(bunker_surface?SaveSurface:0)|
            (door_unlocked?SaveDoorUnlocked:0)|(door_open_logged?SaveDoorOpen:0)|
            (trader_confirmed?SaveTraderConfirmed:0)|(wolf_mission_accepted?SaveWolfMission:0)|
            (scout_met?SaveScoutMet:0)|(farm_cleared?SaveFarmCleared:0)|
            (assault_with_group?SaveGroup:0)|(farm_fight_started?SaveFightStarted:0)|
            (assault_arrival_logged?SaveScoutsArrived:0)|(trader_intro_played?SaveTraderIntro:0)|(unsigned(inventory.flash_status())<<12)|(campaign_story<<16)|((g_zone==1&&road_encounter.ready&&road_encounter.cleared())?(1u<<20):0)|((road_encounter.flags&4)?(1u<<21):0);
        const unsigned milestones=SaveSurface|SaveDoorUnlocked|SaveWolfMission|SaveScoutMet|SaveFarmCleared|SaveTraderConfirmed|(3u<<12)|(7u<<16)|(3u<<20);
        if(played_seconds>=auto_retry_after&&(!auto_played||played_seconds-auto_played>=180||
            ((checkpoint_flags^auto_flags)&milestones))&&
            player_view&&player_health>=35&&!save_menu_open&&!save_dialog_active&&!inventory_open&&!pda_open&&
            grounded&&!jump.active()&&!inventory.bleeding()&&!reload_deadline&&!grenade_pending&&
            (!door_open_start||door_open_logged)&&(!farm_fight_started||farm_cleared)&&
            std::none_of(grenades.bodies.begin(),grenades.bodies.end(),[](const VitaGrenadeBody&g){return g.active;})){
            const unsigned slot=next_vita_auto_slot(save_directory);
            const auto captured=capture_save();const auto write_started=sceKernelGetProcessTimeWide();
            const bool saved=write_vita_slot(save_directory,slot,captured);
            char timing[120];snprintf(timing,sizeof(timing),"debug_save_io operation=auto io_us=%llu written=%u\n",
                static_cast<unsigned long long>(sceKernelGetProcessTimeWide()-write_started),unsigned(saved));write_boot_log(g_boot_log,timing);
            auto_retry_after=played_seconds+(saved?15:30);
            if(saved){auto_played=played_seconds;auto_flags=checkpoint_flags;}
            char log[128];snprintf(log,sizeof(log),"debug_autosave slot=%u written=%u played=%u\n",
                slot-vita_manual_slots+1,unsigned(saved),unsigned(played_seconds));write_boot_log(g_boot_log,log);
        }
        if (save_consumed_input)
		{
			pad.buttons &= SCE_CTRL_START | SCE_CTRL_SELECT;
			movement.cancel_input();
		}
		const bool trader_nearby = bunker_view && player_view && trader_ui_ready && grounded &&
			fabsf(ground_y - trader_spawn_position[1]) < 1.f &&
			hypotf(moving_player_eye[0] - trader_spawn_position[0], moving_player_eye[2] - trader_spawn_position[2]) < 3.f;
		const bool trader_focused = trader_nearby && is_looking_at(moving_player_eye, view_yaw, view_pitch, trader_target);
		if (trader_focused != trader_was_focused)
		{
			write_boot_log(g_boot_log, trader_focused ? "debug_trader_focus active=1\n" : "debug_trader_focus active=0\n");
			trader_was_focused = trader_focused;
		}
		const bool diagnostic_toggle = (pad.buttons & (SCE_CTRL_TRIANGLE | SCE_CTRL_START)) ==
			(SCE_CTRL_TRIANGLE | SCE_CTRL_START);
		const bool door_focused = door_ready && trader_ui_ready && bunker_view && !bunker_surface && !door_open_logged && grounded &&
			!door_open_start && fabsf(ground_y - door_spawn.position[1]) < 1.f &&
			hypotf(moving_player_eye[0] - door_target.x, moving_player_eye[2] - door_target.z) < 2.25f &&
			is_looking_at(moving_player_eye, view_yaw, view_pitch, door_target);
		const bool wolf_visible = wolf_model_ready && player_view && (!bunker_view || bunker_surface) &&
			hypotf(moving_player_eye[0] - wolf_target.x, moving_player_eye[2] - wolf_target.z) < 55.f;
		const bool wolf_focused = wolf_visible && wolf_ui_ready && door_open_logged && grounded &&
			fabsf(ground_y - wolf_floor) < 1.f &&
			hypotf(moving_player_eye[0] - wolf_target.x, moving_player_eye[2] - wolf_target.z) < 3.f &&
			is_looking_at(moving_player_eye, view_yaw, view_pitch, wolf_target);
		const float scout_distance = hypotf(moving_player_eye[0] - scout_target.x, moving_player_eye[2] - scout_target.z);
		const bool scouts_visible = g_zone!=3 && wolf_mission_accepted && scout_waypoints && player_view && (!bunker_view || bunker_surface);
		const bool scout_focused = scouts_visible && scout_ui_ready && !scout_met && grounded && scout_distance < 3.f &&
			fabsf(ground_y - scout_floor) < 1.f && is_looking_at(moving_player_eye, view_yaw, view_pitch, scout_target);
 Fvector campaign_target;
        if(g_zone){campaign_target=scout_transforms[0].c;campaign_target.y+=1.45f;}else campaign_target=trader_target;
        const float campaign_distance=hypotf(campaign_target.x-moving_player_eye[0],campaign_target.z-moving_player_eye[2]);
        const bool campaign_focused=g_zone!=3&&trader_ui_ready&&grounded&&player_view&&player_health&&
            (g_zone?(scout_ready[0]&&campaign_distance<3.2f):(inventory.flash_status()==VitaFlashStatus::Delivered&&!campaign_story&&!north_pages.empty()&&trader_focused))&&
            is_looking_at(moving_player_eye,view_yaw,view_pitch,campaign_target);
		const auto flash_status = inventory.flash_status();
		const float allegro_dx = allegro_target.x - moving_player_eye[0], allegro_dy = allegro_target.y - moving_player_eye[1];
		const float allegro_dz = allegro_target.z - moving_player_eye[2];
		const float allegro_distance = sqrtf(allegro_dx * allegro_dx + allegro_dy * allegro_dy + allegro_dz * allegro_dz);
		bool allegro_focused = player_health && flash_ui_ready && scouts_visible && farm_cleared &&
			flash_status == VitaFlashStatus::Find && grounded && fabsf(ground_y - allegro_floor) < 1.f &&
			allegro_distance > 0.15f && allegro_distance <= 3.f &&
			is_looking_at(moving_player_eye, view_yaw, view_pitch, allegro_target);
		if (allegro_focused)
		{
			const float ray[] = {allegro_dx / allegro_distance, allegro_dy / allegro_distance, allegro_dz / allegro_distance};
			allegro_focused = !player_ground.blocks_ray(moving_player_eye, ray, allegro_distance - 0.1f);
		}
		const bool flash_focused = player_health && flash_ui_ready && trader_focused && flash_status == VitaFlashStatus::Carrying;
        int focused_loot=-1;
        float loot_distance=2.6f;
        if(inventory_ready&&player_view&&player_health&&!inventory_open&&!pda_open)
            for(size_t i=0;i<loot_world.capacity;++i)
            {
                const auto&c=loot_world.containers[i];if(!c.kind||(c.kind!=1&&c.empty()))continue;
                const float dx=c.position[0]-moving_player_eye[0],dz=c.position[2]-moving_player_eye[2];
                const float dy=c.position[1]+.22f-moving_player_eye[1];
                const float distance=std::sqrt(dx*dx+dy*dy+dz*dz);
                if(distance>=loot_distance||distance<.1f)continue;
                Fvector target;target.set(c.position[0],c.position[1]+.22f,c.position[2]);
                const float ray[]={dx/distance,dy/distance,dz/distance};
                if(is_looking_at(moving_player_eye,view_yaw,view_pitch,target)&&
                    !player_ground.blocks_ray(moving_player_eye,ray,std::max(.01f,distance-.25f)))
                    {loot_distance=distance;focused_loot=i;}
            }
        Fvector road_target;const auto&road_position=road_encounter.battle.scouts[0].position;
        road_target.set(road_position[0],road_position[1]+1.45f,road_position[2]);
        const bool road_focused=g_zone==1&&road_encounter.ready&&road_encounter.cleared()&&!road_pages.empty()&&
            player_health&&player_view&&grounded&&std::hypot(moving_player_eye[0]-road_target.x,moving_player_eye[2]-road_target.z)<3.f&&
            is_looking_at(moving_player_eye,view_yaw,view_pitch,road_target);
		const bool any_dialog = zone_dialog_active || scout_dialog_open || wolf_dialog_open || trader_dialog_open || allegro_dialog_open || flash_dialog_open || campaign_open || exchange_open();
		const auto select_action = select_gesture.update((pad.buttons & SCE_CTRL_SELECT) != 0,
			has_input && player_view && player_health && !any_dialog && !quit_requested && !save_consumed_input, now);
		const bool toggle_inventory = inventory_ready && select_action == VitaButtonAction::Hold;
		const bool toggle_pda = pda_ready && select_action == VitaButtonAction::Tap;
		const bool menu_consumed_input = zone_dialog_active || exchange_open() || inventory_open || pda_open || toggle_inventory || toggle_pda || save_consumed_input;
		const SquareContext square_context = menu_consumed_input || any_dialog ? SquareContext::Menu :
			player_view ? SquareContext::World : SquareContext::Orbit;
		const bool square_down = (pad.buttons & SCE_CTRL_SQUARE) != 0;
		const bool same_square_context = square_context == previous_square_context || (pressed & SCE_CTRL_SQUARE);
		const auto square_action = square_gesture.update(square_down,
			has_input && player_health && !quit_requested && !diagnostic_toggle && !pda_open && !save_consumed_input &&
				!toggle_inventory && !toggle_pda && same_square_context, now);
		previous_square_context = square_context;
		const bool interact_pressed = square_action == VitaButtonAction::Tap;
		const bool reload_held = square_context == SquareContext::World && square_action == VitaButtonAction::Hold;

        const std::array<float,3> stash_feet={moving_player_eye[0],ground_y,moving_player_eye[2]};
        const bool stash_context=g_zone==3&&player_view&&player_health&&grounded&&!jump.active()&&!any_dialog&&!menu_consumed_input;
        int stash_action=-1;
        if(stash_context){
         Fvector flash_target;flash_target.set(vita_strelok_flash[0],vita_strelok_flash[1],vita_strelok_flash[2]);
         if(campaign_story<7&&vita_stash_near(stash_feet,vita_strelok_flash,2.4f,2.f)&&
             is_looking_at(moving_player_eye,view_yaw,view_pitch,flash_target)){
          const float dx=flash_target.x-moving_player_eye[0],dy=flash_target.y-moving_player_eye[1],dz=flash_target.z-moving_player_eye[2];
          const float length=std::sqrt(dx*dx+dy*dy+dz*dz);
          const float ray[]={dx/std::max(length,.01f),dy/std::max(length,.01f),dz/std::max(length,.01f)};
          if(!player_ground.blocks_ray(moving_player_eye,ray,std::max(0.f,length-.15f)))stash_action=2;
         }
         if(stash_action<0&&vita_stash_near(stash_feet,vita_strelok_ladder_bottom,2.8f,5.5f))stash_action=0;
         else if(stash_action<0&&vita_stash_near(stash_feet,vita_strelok_ladder_top,1.65f,1.2f))stash_action=1;
        }
		bool dialog_consumed_input = menu_consumed_input;
        if(zone_dialog_active){
            dialog_consumed_input=true;movement.cancel_input();
            const bool released=(pad.buttons&(SCE_CTRL_CROSS|SCE_CTRL_CIRCLE|SCE_CTRL_SQUARE|SCE_CTRL_START))==0;
            const auto action=zone_confirmation.update((pressed&SCE_CTRL_CROSS)!=0,(pressed&SCE_CTRL_CIRCLE)!=0,released);
            zone_transition_confirmed=action==VitaZoneConfirmation::Action::Confirm;
            if(action==VitaZoneConfirmation::Action::Cancel)write_boot_log(g_boot_log,"debug_zone_confirmation cancelled=1\n");
            if(zone_transition_confirmed)write_boot_log(g_boot_log,"debug_zone_confirmation confirmed=1\n");
        }
        else if (save_consumed_input)
            dialog_consumed_input = true;
        else if(stash_action>=0&&interact_pressed&&!diagnostic_toggle){
            dialog_consumed_input=true;movement.cancel_input();
            if(stash_action==2){
                if(inventory.receive_strelok_flash()){
                    campaign_story=7;campaign_page=0;campaign_is_stash=true;trader_ui.campaign_information=true;campaign_is_road=false;trader_ui.campaign_road=false;
                    campaign_pages=strelok_pages;
                    campaign_open=!campaign_pages.empty()&&trader_ui.set_campaign_page("PDA",campaign_pages[0]);
                    write_boot_log(g_boot_log,"debug_strelok flash_taken=1 next=find_ghost\n");
                }
            }else{
                // Contextual ladder traversal. Both landings must support the player with headroom.
                const auto&landing=stash_action==0?vita_strelok_ladder_top:vita_strelok_ladder_bottom;
                float floor=landing[1];const float up[]={0,1,0};
                if(ground_ready&&player_ground.height_at(landing[0],landing[2],floor,.25f,.25f,floor)){
                    const float base[]={landing[0],floor+.05f,landing[2]};
                    if(!player_ground.blocks_ray(base,up,movement.body_height())){
                        moving_player_eye[0]=landing[0];moving_player_eye[2]=landing[2];
                        ground_y=floor;moving_player_eye[1]=floor+movement.eye_height();grounded=true;
                        write_boot_log(g_boot_log,stash_action==0?"debug_strelok ladder_up=1\n":"debug_strelok ladder_down=1\n");
                    }
                }
            }
        }
        else if(campaign_open){
            dialog_consumed_input=true;movement.cancel_input();
            if(pressed&SCE_CTRL_CIRCLE){campaign_open=false;}
            else if(interact_pressed||(pressed&SCE_CTRL_CROSS)){
                if(++campaign_page>=campaign_pages.size()){
                    campaign_open=false;
                    if(campaign_is_stash){}
                    else if(campaign_is_road)road_encounter.flags|=4;
                    else if(!g_zone)campaign_story=std::max(campaign_story,1u);
                    else if(!campaign_is_stash&&campaign_cleared_dialog)campaign_story=std::max(campaign_story,g_zone==1?3u:5u);
                    write_boot_log(g_boot_log,"debug_campaign dialogue_completed=1\n");
                }else trader_ui.set_campaign_page(campaign_name(),campaign_pages[campaign_page]);
            }
        }
        else if(road_focused&&!any_dialog&&!menu_consumed_input&&!diagnostic_toggle&&interact_pressed){
            dialog_consumed_input=true;movement.cancel_input();campaign_page=0;campaign_is_stash=false;trader_ui.campaign_information=false;campaign_is_road=true;trader_ui.campaign_road=true;
            campaign_pages=road_pages;campaign_open=trader_ui.set_campaign_page(campaign_name(),campaign_pages[0]);
            write_boot_log(g_boot_log,"debug_garbage_road dialogue_opened=1\n");
        }
        else if(campaign_focused&&!any_dialog&&!menu_consumed_input&&!diagnostic_toggle&&interact_pressed){
            dialog_consumed_input=true;movement.cancel_input();campaign_page=0;campaign_is_stash=false;trader_ui.campaign_information=false;campaign_is_road=false;trader_ui.campaign_road=false;
            campaign_cleared_dialog=!g_zone||farm_cleared;
            campaign_pages=campaign_cleared_dialog?north_pages:attack_pages;
            if(!campaign_pages.empty())campaign_open=trader_ui.set_campaign_page(campaign_name(),campaign_pages[0]);
            write_boot_log(g_boot_log,campaign_open?"debug_campaign dialogue_opened=1\n":"debug_campaign dialogue_open_failed=1\n");
            if(!campaign_open&&!g_zone){trader_dialog_open=true;trader_confirmed=inventory.state().equipment!=0;trader_choice=0;}
        }
        else if(exchange_open())
        {
            dialog_consumed_input=true;movement.cancel_input();
            if(pressed&SCE_CTRL_CIRCLE){trade_open=false;loot_open=-1;trade_basket={};write_boot_log(g_boot_log,"debug_exchange closed=1\n");}
            else
            {
                if(pressed&SCE_CTRL_LEFT)exchange_buying=false;
                if(pressed&SCE_CTRL_RIGHT)exchange_buying=true;
                if(pressed&SCE_CTRL_UP)exchange_selection=(exchange_selection+vita_item_count-1)%vita_item_count;
                if(pressed&SCE_CTRL_DOWN)exchange_selection=(exchange_selection+1)%vita_item_count;
                if(pressed&SCE_CTRL_LTRIGGER)exchange_multiplier=exchange_multiplier==10?5:1;
                if(pressed&SCE_CTRL_RTRIGGER)exchange_multiplier=exchange_multiplier==1?5:10;
                for(unsigned search=0;search<vita_item_count;++search){
                    const auto item=static_cast<VitaInventoryItem>(exchange_selection);
                    const unsigned available=exchange_buying?(loot_open>=0?loot_world.containers[loot_open].items[exchange_selection]:
                        trader_stock.items[exchange_selection]):inventory.count(item);
                    if(available&&(loot_open>=0||VitaInventory::tradable(item)))break;
                    exchange_selection=(exchange_selection+vita_item_count+((pressed&SCE_CTRL_UP)?-1:1))%vita_item_count;
                }
                const auto item=static_cast<VitaInventoryItem>(exchange_selection);
                if(loot_open>=0)
                {
                    bool success=false;
                    if(pressed&SCE_CTRL_TRIANGLE)success=loot_world.take_all(loot_open,inventory)>0;
                    else if(interact_pressed&&exchange_buying)
                    {
                        const auto count=loot_world.containers[loot_open].items[exchange_selection];
                        success=loot_world.take(loot_open,inventory,item,std::min(count,VitaInventory::lot(item)*exchange_multiplier));
                        success=loot_world.take_cash(loot_open,inventory)||success;
                    }
                    if(interact_pressed||(pressed&SCE_CTRL_TRIANGLE)){
                        exchange_result=success?VitaTradeResult::Success:loot_world.containers[loot_open].empty()?VitaTradeResult::Empty:VitaTradeResult::Capacity;
                        write_boot_log(g_boot_log,success?"debug_loot taken=1\n":"debug_loot taken=0\n");}
                }
                else
                {
                    if(interact_pressed){
                        const unsigned have=exchange_buying?trader_stock.items[exchange_selection]:inventory.count(item);
                        const unsigned proposed=exchange_buying?trade_basket.buy[exchange_selection]:trade_basket.sell[exchange_selection];
                        const unsigned opposite=exchange_buying?trade_basket.sell[exchange_selection]:trade_basket.buy[exchange_selection];
                        const unsigned available=have-std::min(have,proposed)+opposite;
                        const bool added=trade_basket.add(exchange_buying,item,std::min(available,VitaInventory::lot(item)*exchange_multiplier),inventory,trader_stock);
                        exchange_result=added?VitaTradeResult::Ready:VitaTradeResult::Stock;
                    }
                    if(pressed&SCE_CTRL_CROSS){trade_basket.undo(exchange_buying,item,VitaInventory::lot(item)*exchange_multiplier);exchange_result=VitaTradeResult::Ready;}
                    if(pressed&SCE_CTRL_TRIANGLE){
                        exchange_result=trade_basket.commit(inventory,trader_stock);
                        char line[100];snprintf(line,sizeof(line),"debug_trade result=%u money=%d weight=%.2f\n",
                            unsigned(exchange_result),inventory.money(),inventory.weight());write_boot_log(g_boot_log,line);}
                }
            }
        }
		else if (menu_consumed_input)
		{
			if (toggle_inventory)
			{
				inventory_open = !inventory_open;
				pda_open = false;
				if (inventory_open) inventory_ui.reset_scroll();
				write_boot_log(g_boot_log, inventory_open ? "debug_inventory opened\n" : "debug_inventory closed\n");
			}
			else if (toggle_pda)
			{
				pda_open = !pda_open;
				inventory_open = false;
				write_boot_log(g_boot_log, pda_open ? "debug_pda opened\n" : "debug_pda closed\n");
			}
			else if (pressed & SCE_CTRL_CIRCLE)
			{
				write_boot_log(g_boot_log, inventory_open ? "debug_inventory closed\n" : "debug_pda closed\n");
				inventory_open = pda_open = false;
			}
			else if (inventory_open)
			{
				const auto previous_selection = inventory.selected();
				if (pressed & (SCE_CTRL_LEFT | SCE_CTRL_UP))
					inventory.move_selection(-1);
				else if (pressed & (SCE_CTRL_RIGHT | SCE_CTRL_DOWN))
					inventory.move_selection(1);
				if (inventory.selected() != previous_selection) inventory_ui.reset_scroll();
				inventory_ui.scroll_description(player_stick_axis(pad.ry) * frame_seconds * 160.f);
                if(interact_pressed)
                {
                    float endurance=movement.endurance();
                    const auto selected=inventory.selected();
                    const bool changed=inventory.toggle_pistol(selected)||inventory.toggle_outfit(selected)||inventory.select_ammo(selected)||inventory.toggle_accessory(selected)||inventory.select_grenade(selected)||
                        inventory.use(selected,player_health,endurance);
                    if(changed)movement.restore(static_cast<unsigned>(movement.stance()),endurance);
                    char line[110];snprintf(line,sizeof(line),"debug_inventory use=%u success=%u health=%u food=%u\n",
                        unsigned(selected),unsigned(changed),player_health,inventory.food());write_boot_log(g_boot_log,line);
                }
                if(pressed&SCE_CTRL_TRIANGLE)
                {
                    const auto item=inventory.selected();
                    float floor=ground_y;
                    const float x=moving_player_eye[0]+sinf(view_yaw)*.7f,z=moving_player_eye[2]+cosf(view_yaw)*.7f;
                    const float origin[]={moving_player_eye[0],ground_y+.35f,moving_player_eye[2]};
                    const float ray[]={sinf(view_yaw),0,cosf(view_yaw)};
                    const bool dropped=!player_ground.blocks_ray(origin,ray,.7f)&&
                        player_ground.height_at(x,z,ground_y,.5f,.5f,floor)&&
                        loot_world.drop(inventory,item,std::min(inventory.count(item),VitaInventory::lot(item)),{x,floor,z});
                    write_boot_log(g_boot_log,dropped?"debug_inventory drop=1\n":"debug_inventory drop=0\n");
                }
			}
		}
		else if (allegro_dialog_open || flash_dialog_open)
		{
			dialog_consumed_input = true;
			if (allegro_dialog_open)
			{
				if (pressed & SCE_CTRL_CIRCLE)
					allegro_dialog_open = false;
				else if (interact_pressed)
				{
					if (++allegro_page == flash_lines.allegro.size())
					{
						allegro_dialog_open = false;
						if (inventory.receive_flash())
							write_boot_log(g_boot_log, "debug_flash_quest received=1\n");
					}
				}
			}
			else if (pressed & SCE_CTRL_CIRCLE)
				flash_dialog_open = false;
			else if (interact_pressed)
			{
				if (flash_page == 0 && inventory.deliver_flash())
				{
					flash_page = 1;
					write_boot_log(g_boot_log, "debug_flash_quest delivered=1 reward_ru=1500\n");
				}
				else
					flash_dialog_open = false;
			}
		}
		else if (scout_dialog_open)
		{
			dialog_consumed_input = true;
			if (pressed & SCE_CTRL_CIRCLE)
			{
				scout_dialog_open = false;
				game_audio.stop_speech();
				write_boot_log(g_boot_log, "debug_scout_dialog closed\n");
			}
			else
			{
				if (scout_page == vita_scout_choice_page)
				{
					if (pressed & SCE_CTRL_UP) scout_choice = 0;
					else if (pressed & SCE_CTRL_DOWN) scout_choice = 1;
				}
				if (interact_pressed)
				{
					if (scout_page < vita_scout_choice_page)
						++scout_page;
					else if (scout_page == vita_scout_choice_page)
					{
						scout_met = true;
						game_audio.stop_speech();
						if (scout_choice == 0)
						{
							scout_dialog_open = false;
							assault_with_group = true;
							game_audio.play(VitaSound::ScoutReady, 0.76f);
							write_boot_log(g_boot_log, "debug_npc_voice scout=ready\n");
							write_boot_log(g_boot_log, "debug_scout_dialog completed plan=group\n");
						}
						else
						{
							scout_page = vita_scout_decline_page;
							write_boot_log(g_boot_log, "debug_scout_dialog choice=solo\n");
						}
					}
					else
					{
						scout_dialog_open = false;
						write_boot_log(g_boot_log, "debug_scout_dialog completed plan=solo\n");
					}
				}
			}
		}
		else if (wolf_dialog_open)
		{
			dialog_consumed_input = true;
			if (pressed & SCE_CTRL_CIRCLE)
			{
				wolf_dialog_open = false;
				game_audio.stop_speech();
				write_boot_log(g_boot_log, "debug_wolf_dialog closed\n");
			}
			else
			{
				if (wolf_page == vita_wolf_choice_page)
				{
					if (pressed & SCE_CTRL_UP) wolf_choice = 0;
					else if (pressed & SCE_CTRL_DOWN) wolf_choice = 1;
				}
				if (interact_pressed)
				{
					if (wolf_page == vita_wolf_choice_page)
					{
						wolf_mission_accepted = wolf_choice == 0;
						wolf_page = wolf_mission_accepted ? vita_wolf_accept_page : vita_wolf_decline_page;
						if (wolf_mission_accepted)
						{
							// The original orders are addressed to the scouts over the radio.
							game_audio.play(VitaSound::WolfOrders, 0.78f);
							write_boot_log(g_boot_log, "debug_npc_voice wolf=orders\n");
						}
						write_boot_log(g_boot_log, wolf_mission_accepted ? "debug_wolf_dialog accepted=1\n" :
							"debug_wolf_dialog accepted=0\n");
					}
					else if (wolf_page < vita_wolf_choice_page)
						++wolf_page;
					else
					{
						wolf_dialog_open = false;
						write_boot_log(g_boot_log, "debug_wolf_dialog completed\n");
					}
				}
			}
		}
		else if (trader_dialog_open)
		{
			dialog_consumed_input = true;
			if (pressed & SCE_CTRL_CIRCLE)
			{
				trader_dialog_open = false;
				game_audio.stop_speech();
				write_boot_log(g_boot_log, "debug_trader_dialog closed\n");
			}
			else if (!trader_confirmed)
			{
				if (pressed & SCE_CTRL_UP)
					trader_choice = 0;
				else if (pressed & SCE_CTRL_DOWN)
					trader_choice = 1;
				if (interact_pressed)
				{
					trader_confirmed = true;
					game_audio.stop_speech();
					if (inventory.grant_starting_equipment())
						write_boot_log(g_boot_log, "debug_inventory grant=sidorovitch pm=1 reserve=40 loaded=8 knife=1\n");
					if (door_ready && !door_unlocked)
					{
						door_unlocked = true;
						write_boot_log(g_boot_log, "debug_bunker_door unlocked=1\n");
					}
					write_boot_log(g_boot_log, trader_choice == 0 ? "debug_trader_dialog choice=31\n" :
						"debug_trader_dialog choice=32\n");
				}
			}
            else
            {
                if(pressed&SCE_CTRL_UP)trader_choice=0;
                if(pressed&SCE_CTRL_DOWN)trader_choice=1;
                if(interact_pressed){
                    trader_dialog_open=false;
                    if(trader_choice==0){trade_open=true;trade_basket={};exchange_buying=true;exchange_selection=0;exchange_multiplier=1;
                        exchange_result=VitaTradeResult::Ready;write_boot_log(g_boot_log,"debug_trade opened_from_dialog=1\n");}
                }
            }
		}
        else if(focused_loot>=0&&!diagnostic_toggle&&interact_pressed)
        {
            loot_open=focused_loot;exchange_buying=true;exchange_selection=0;exchange_multiplier=1;
            exchange_result=VitaTradeResult::Ready;dialog_consumed_input=true;movement.cancel_input();
            loot_world.take_cash(loot_open,inventory);
            write_boot_log(g_boot_log,"debug_loot opened=1\n");
        }
        else if (allegro_focused && !diagnostic_toggle && interact_pressed)
		{
			dialog_consumed_input = true;
			allegro_dialog_open = true;
			allegro_page = 0;
			write_boot_log(g_boot_log, "debug_allegro_dialog opened\n");
		}
		else if (flash_focused && !diagnostic_toggle && interact_pressed)
		{
			dialog_consumed_input = true;
			flash_dialog_open = true;
			flash_page = 0;
			write_boot_log(g_boot_log, "debug_flash_quest trader_opened=1\n");
		}
		else if (trader_focused && !diagnostic_toggle && player_health &&
			interact_pressed)
		{
			dialog_consumed_input = true;
            trader_dialog_open=true;
            trader_confirmed=inventory.state().equipment!=0;
			trader_choice = 0;
			write_boot_log(g_boot_log, "debug_trader_dialog opened\n");
			if (!trader_intro_played)
			{
				trader_intro_played = true;
				game_audio.speak(VitaSound::TraderIntro);
				write_boot_log(g_boot_log, "debug_npc_voice trader=intro\n");
			}
		}
		else if (scout_focused && !diagnostic_toggle && interact_pressed)
		{
			dialog_consumed_input = true;
			scout_dialog_open = true;
			scout_page = scout_choice = 0;
			write_boot_log(g_boot_log, "debug_scout_dialog opened\n");
			game_audio.speak(VitaSound::ScoutCall);
			write_boot_log(g_boot_log, "debug_npc_voice scout=call\n");
		}
		else if (wolf_focused && !diagnostic_toggle && interact_pressed)
		{
			dialog_consumed_input = true;
			wolf_dialog_open = true;
			wolf_page = wolf_mission_accepted ? vita_wolf_accept_page : 0;
			wolf_choice = 0;
			write_boot_log(g_boot_log, "debug_wolf_dialog opened\n");
		}
		else if (door_focused && !diagnostic_toggle && interact_pressed)
		{
			dialog_consumed_input = true;
			if (door_unlocked)
			{
				door_open_start = now;
				write_boot_log(g_boot_log, "debug_bunker_door opening=1\n");
			}
		}
		if (dialog_consumed_input)
			pad.buttons &= SCE_CTRL_SELECT;

		if (!save_menu_open && door_open_start && !door_open_logged)
		{
			const float progress = std::min(1.f, static_cast<float>(now - door_open_start) / 650000.f);
			const float angle = bunker_door_open_angle * progress * progress * (3.f - 2.f * progress);
			Fmatrix swing;
			swing.setXYZ(0.f, angle, 0.f);
			door_transform.mul_43(door_closed_transform, swing);
			door_panel = bunker_door_panel(door_transform, door_minimum, door_maximum);
			if (progress >= 1.f)
			{
				door_open_logged = true;
				write_boot_log(g_boot_log, "debug_bunker_door opened=1\n");
			}
		}

		const bool start_pressed = !quit_requested && !diagnostic_toggle && (pad.buttons & SCE_CTRL_START) != 0;
		if (use_actor_view && !player_view && start_pressed && !start_held)
		{
			vsync_enabled = !vsync_enabled;
			vglWaitVblankStart(vsync_enabled ? GL_TRUE : GL_FALSE);
			char line[96];
			snprintf(line, sizeof(line), "debug_player_vsync frame=%u enabled=%u\n",
				frame_number, static_cast<unsigned>(vsync_enabled));
			write_boot_log(g_boot_log, line);
		}
		start_held = start_pressed;
		const bool dds_toggle = player_view && (pad.buttons & dds_toggle_buttons) == dds_toggle_buttons;
		if (dds_toggle && !dds_toggle_held)
		{
			dds_paused = !dds_paused;
			next_mobile_material = 0;
			char line[96];
			snprintf(line, sizeof(line), "debug_player_dds_pause frame=%u paused=%u\n",
				frame_number, static_cast<unsigned>(dds_paused));
			write_boot_log(g_boot_log, line);
		}
		dds_toggle_held = dds_toggle;
		if (dds_toggle)
			pad.buttons &= ~dds_toggle_buttons;
		// In player view the right stick controls yaw, so the horizontal D-pad is available for comparisons.
		constexpr u32 render_compare_directions = SCE_CTRL_LEFT | SCE_CTRL_RIGHT;
		if (player_view && !dialog_consumed_input)
		{
			const u32 compare_pressed = pad.buttons & pressed & render_compare_directions;
			if (compare_pressed & SCE_CTRL_LEFT)
				sky_enabled = !sky_enabled;
			if (foliage_shader_ready && (compare_pressed & SCE_CTRL_RIGHT))
				foliage_cutout_enabled = !foliage_cutout_enabled;
			if (compare_pressed)
			{
				char line[140];
				snprintf(line, sizeof(line), "debug_player_render_compare frame=%u sky=%u foliage_cutout=%u foliage_ready=%u\n",
					frame_number, static_cast<unsigned>(sky_enabled), static_cast<unsigned>(foliage_cutout_enabled),
					static_cast<unsigned>(foliage_shader_ready));
				write_boot_log(g_boot_log, line);
			}
			pad.buttons &= ~render_compare_directions;
		}
		const bool coverage_pressed = player_view &&
			(pad.buttons & terrain_coverage_buttons) == terrain_coverage_buttons;
		if (coverage_pressed && !terrain_coverage_held)
		{
			terrain_coverage = !terrain_coverage;
			terrain_coverage_refresh = terrain_coverage;
			char line[96];
			snprintf(line, sizeof(line), "debug_player_terrain_coverage_toggle frame=%u enabled=%u\n",
				frame_number, static_cast<unsigned>(terrain_coverage));
			write_boot_log(g_boot_log, line);
		}
		terrain_coverage_held = coverage_pressed;
		if (coverage_pressed)
			pad.buttons &= ~terrain_coverage_buttons;
		const bool drawn_pistol = player_view && inventory.firearm();
		const bool drawn_knife = player_view && inventory.weapon() == VitaWeapon::Knife;
		const bool terrain_chord = (pad.buttons & SCE_CTRL_UP) ||
			(!drawn_pistol && (pad.buttons & SCE_CTRL_LTRIGGER)) ||
			(!drawn_pistol && !drawn_knife && (pad.buttons & SCE_CTRL_RTRIGGER));
		const bool isolate_terrain = (pad.buttons & SCE_CTRL_CIRCLE) && (!player_view || terrain_chord) &&
			terrain_anchor < selected_visuals.size() && selected_visuals[terrain_anchor];
		if (dialog_consumed_input || isolate_terrain || !has_input || !player_health)
			movement.cancel_input();
		VitaTerrainSample terrain_sample = VitaTerrainSample::Shaded;
		if (isolate_terrain && (pad.buttons & SCE_CTRL_LTRIGGER))
			terrain_sample = VitaTerrainSample::BaseAlpha;
		else if (isolate_terrain && (pad.buttons & SCE_CTRL_RTRIGGER))
			terrain_sample = VitaTerrainSample::LightmapAlpha;
		if (!dialog_consumed_input && player_health && level_visuals_loaded && level_scale > 0.f)
		{
			const bool enter_bunker = !player_view && bunker_ready && interact_pressed;
			const bool view_toggle = use_actor_view && diagnostic_toggle;
			if ((view_toggle && !view_toggle_held) || enter_bunker)
			{
				player_view = !player_view;
				bunker_view = enter_bunker;
				bunker_surface = false;
				trader_idle_start = 0;
				bunker_selection_dirty = bunker_view;
				terrain_coverage = false;
				if (enter_bunker)
					pad.buttons &= ~SCE_CTRL_SQUARE;
			}
			view_toggle_held = view_toggle;
			if (!player_view && (pad.buttons & SCE_CTRL_LEFT)) view_yaw += 0.025f;
			if (!player_view && (pad.buttons & SCE_CTRL_RIGHT)) view_yaw -= 0.025f;
			if (!isolate_terrain && (pad.buttons & SCE_CTRL_UP)) view_pitch += 0.02f;
			if (pad.buttons & SCE_CTRL_DOWN) view_pitch -= 0.02f;
			if (!player_view && !isolate_terrain && (pad.buttons & SCE_CTRL_RTRIGGER)) view_scale *= 1.02f;
			if (!player_view && !isolate_terrain && (pad.buttons & SCE_CTRL_LTRIGGER)) view_scale *= 0.98f;
			if (view_toggle || (!player_view && (pad.buttons & SCE_CTRL_TRIANGLE)) || enter_bunker)
			{
				view_yaw = bunker_view ? bunker_yaw : (player_view ? initial_player_yaw : 0.f);
				view_pitch = bunker_view ? bunker_pitch : (player_view ? player_view_pitch : initial_view_pitch);
				view_scale = debug_view_scale;
				const float* reset_eye = bunker_view ? bunker_eye : player_eye;
				std::copy(reset_eye, reset_eye + 3, moving_player_eye);
				movement.reset_posture();
				jump.reset();
				ground_y = bunker_view ? bunker_ground_y : start_ground_y;
				grounded = bunker_view ? bunker_grounded : start_grounded;
				bunker_surface = false;
				bunker_selection_dirty = bunker_view;
			}
			else if (player_view && !isolate_terrain && has_input && analog_status >= 0)
			{
				const bool aim_held = drawn_pistol && pm_hud_ready && !reload_deadline && (pad.buttons & SCE_CTRL_LTRIGGER);
				const float look_step = player_look_speed * frame_seconds * settings.sensitivity/100.f / (aim_held ? (inventory.scoped()?2.8f:pm_aim_projection) : 1.f);
				view_yaw += player_stick_axis(pad.rx) * look_step;
				view_pitch -= player_stick_axis(pad.ry) * look_step * (settings.invert_y?-1.f:1.f);

				const float sideways = player_stick_axis(pad.lx);
				const float forward = -player_stick_axis(pad.ly);

				const float sy = sinf(view_yaw);
				const float cy = cosf(view_yaw);
				const float current_x = moving_player_eye[0], current_z = moving_player_eye[2];
				const bool bunker_stairs = bunker_view && at_bunker_stairs(current_x, current_z);
				const float height = movement.body_height();
				const bool can_stand = (!grounded && !jump.active()) || height >= 1.8f || !(released & SCE_CTRL_CIRCLE) ||
					can_stand_at(player_ground, current_x, current_z, ground_y, height);
				movement.update(frame_seconds, forward, sprint_pressed, (pressed & SCE_CTRL_CIRCLE) && !terrain_chord,
					(pad.buttons & SCE_CTRL_CIRCLE) && !terrain_chord, can_stand,
					(!bunker_view || (bunker_surface && !bunker_stairs))&&!inventory.overweight());
				if (grounded && ground_ready && !quit_requested && (pressed & SCE_CTRL_CROSS) &&
					movement.stance() == VitaPlayerStance::Standing && !inventory.overweight())
				{
					jump.start(current_x, ground_y, current_z);
					grounded = false;
					write_boot_log(g_boot_log, "debug_player_jump started\n");
				}
				const float distance = player_move_speed * movement.speed_factor() * inventory.carry_speed() * frame_seconds /
					std::max(1.f, sqrtf(sideways * sideways + forward * forward));
				float next_x = current_x + (sideways * cy + forward * sy) * distance;
				float next_z = current_z + (forward * cy - sideways * sy) * distance;
				bool walked=false,walk_hit=false;
				if ((grounded || jump.active()) && (next_x != current_x || next_z != current_z))
				{
					const SceUInt64 start = sceKernelGetProcessTimeWide();
                    bool contact=false;
                    if(grounded&&!jump.active()&&ground_ready){
                        walked=true;float next_y=ground_y;
                        walk_hit=player_ground.walk_move(current_x,current_z,ground_y,next_x,next_y,next_z,contact,
                            bunker_view&&!bunker_surface&&door_ready?&door_panel:nullptr,
                            bunker_stairs?.65f:.45f,movement.body_height());
                        if(walk_hit)ground_y=next_y;
                    }else contact=player_ground.slide_move(current_x,current_z,ground_y,next_x,next_z,
                        bunker_view&&!bunker_surface&&door_ready?&door_panel:nullptr,
                        jump.active()?0.f:bunker_stairs?.65f:.45f,movement.body_height());
					wall_peak_us = std::max(wall_peak_us, sceKernelGetProcessTimeWide() - start);
					if (contact)
					{
						++wall_contacts;
						const bool stopped = next_x == current_x && next_z == current_z;
						if (stopped)
							++wall_stops;
						if (wall_contacts <= 8 || wall_contacts % 128 == 0)
						{
							char line[160];
							snprintf(line, sizeof(line),
								"debug_player_wall frame=%u x=%.2f z=%.2f next_x=%.2f next_z=%.2f stopped=%u\n",
								frame_number, current_x, current_z, next_x, next_z, static_cast<unsigned>(stopped));
							write_boot_log(g_boot_log, line);
						}
					}
				}
				bool can_move = true;
				if (jump.active())
				{
					const auto result = jump.advance(player_ground, frame_seconds, movement.body_height(), next_x, ground_y, next_z);
					grounded = result != VitaPlayerJump::Result::Airborne;
					if (grounded)
						write_boot_log(g_boot_log, result == VitaPlayerJump::Result::Landed ?
							"debug_player_jump landed\n" : "debug_player_jump returned_to_ground\n");
				}
				else if (ground_ready && (next_x != moving_player_eye[0] || next_z != moving_player_eye[2]))
				{
					const SceUInt64 start = sceKernelGetProcessTimeWide();
					const bool hit = walked?walk_hit:player_ground_height(player_ground, next_x, next_z, current_x, current_z,
						bunker_stairs, ground_y);
					ground_peak_us = std::max(ground_peak_us, sceKernelGetProcessTimeWide() - start);
					if (hit)
					{
						grounded = true;
						moving_player_eye[1] = ground_y + movement.eye_height();
						++ground_hits;
					}
					else
					{
						can_move = !grounded;
						++ground_misses;
						if (!can_move)
							++ground_blocked;
					}
					const unsigned samples = ground_hits + ground_misses;
					if (samples <= 4 || samples % 128 == 0)
					{
						char line[160];
						snprintf(line, sizeof(line),
							"debug_player_ground frame=%u x=%.2f z=%.2f ground_y=%.2f eye_y=%.2f hit=%u blocked=%u\n",
							frame_number, next_x, next_z, ground_y, moving_player_eye[1],
							static_cast<unsigned>(hit), static_cast<unsigned>(!can_move));
						write_boot_log(g_boot_log, line);
					}
				}
				if (can_move)
				{
					moving_player_eye[0] = next_x;
					moving_player_eye[2] = next_z;
				}
				if (grounded || jump.active())
					moving_player_eye[1] = ground_y + movement.eye_height();
				movement.finish_move(frame_seconds, moving_player_eye[0] != current_x || moving_player_eye[2] != current_z);
			}
			if (bunker_view && grounded)
			{
				// Switch to the exterior renderer near the top of the stairs.
				const float rise = ground_y - bunker_ground_y;
				const bool on_stairs = at_bunker_stairs(moving_player_eye[0], moving_player_eye[2]);
				const bool at_surface = rise > (bunker_surface ? 2.45f : 2.75f) || (bunker_surface && !on_stairs);
				if (at_surface != bunker_surface)
				{
					bunker_surface = at_surface;
					bunker_selection_dirty = true;
					char line[128];
					snprintf(line, sizeof(line), "debug_bunker_exit surface=%u x=%.2f y=%.2f z=%.2f\n",
						static_cast<unsigned>(bunker_surface), moving_player_eye[0], ground_y, moving_player_eye[2]);
					write_boot_log(g_boot_log, line);
				}
			}
			const float min_pitch = player_view ? 0.95f : -0.2f;
			const float max_pitch = player_view ? 2.15f : 1.45f;
			if (view_pitch < min_pitch) view_pitch = min_pitch;
			if (view_pitch > max_pitch) view_pitch = max_pitch;
			if (view_scale < level_scale * 0.25f) view_scale = level_scale * 0.25f;
			if (view_scale > level_scale * 6.f) view_scale = level_scale * 6.f;
		}
		const bool outdoor_player = player_view && !isolate_terrain && (!bunker_view ||
			(bunker_surface && !at_bunker_stairs(moving_player_eye[0], moving_player_eye[2])));
		const bool menu_open = pda_open || save_menu_open;
		const bool sky_visible = g_zone!=3 && (outdoor_player || (player_view && !isolate_terrain && bunker_view &&
			door_open_logged && at_bunker_stairs(moving_player_eye[0], moving_player_eye[2]) &&
			moving_player_eye[1] > bunker_ground_y + 2.5f));
		const bool weapon_input = player_view && player_health && !dialog_consumed_input && !isolate_terrain && !diagnostic_toggle && !quit_requested;
		if (weapon_input && !grenade_pending && (pressed & SCE_CTRL_TRIANGLE))
		{
			const VitaWeapon previous = inventory.weapon();
			inventory.cycle_weapon();
			if (previous != inventory.weapon())
			{
				reload_deadline = 0;
				weapon_action_time = 0;
				shoot_clip = nullptr;
				knife_hit_pending = false;
				const char* const modes[] = {"holstered", "pm", "knife","bm16","ak74","grenade"};
				char line[70];
				snprintf(line, sizeof(line), "debug_weapon selected=%s\n", modes[static_cast<unsigned>(inventory.weapon())]);
				write_boot_log(g_boot_log, line);
			}
		}
		ensure_hud(inventory,grenade_pending||(grenade_throw_started&&now-grenade_throw_started<1200000));
        const auto& active_clips=inventory.weapon()==VitaWeapon::Pistol?pm_clips:primary_clips;
        const auto& active_reload=active_clips[size_t(VitaWeaponClip::Reload)];
        const auto& active_reload_empty=active_clips[size_t(VitaWeaponClip::ReloadEmpty)];
        if(!inventory.firearm())reload_deadline=0;
		if (weapon_input && reload_deadline && now >= reload_deadline)
		{
			inventory.reload_pistol();
			reload_deadline = 0;
			char line[86];
			snprintf(line, sizeof(line), "debug_weapon reload loaded=%d reserve=%d\n", inventory.loaded(), inventory.reserve());
			write_boot_log(g_boot_log, line);
		}
		if (weapon_input && inventory.firearm() && reload_held &&
			!reload_deadline && unsigned(inventory.loaded()) < inventory.magazine_capacity() && inventory.reserve())
		{
			const bool empty = inventory.loaded() == 0;
			reload_clip = empty ? &active_reload_empty : &active_reload;
			shoot_clip = nullptr;
			reload_started = now;
			reload_deadline = now + (reload_clip->source_frames ? reload_clip->duration_us() : 2700000);
			write_boot_log(g_boot_log, empty ? "debug_weapon reload_begin clip=reload\n" :
				"debug_weapon reload_begin clip=reload_full\n");
			game_audio.play(inventory.weapon()==VitaWeapon::Shotgun?VitaSound::ShotgunReload:inventory.weapon()==VitaWeapon::Rifle?VitaSound::RifleReload:VitaSound::Reload,0.70f);
		}
		const float audio_cy = cosf(view_yaw), audio_sy = sinf(view_yaw);
		const auto sound_pan = [&](float x, float z)
		{
			const float dx = x - moving_player_eye[0], dz = z - moving_player_eye[2];
			return (dx * audio_cy - dz * audio_sy) / std::max(1.f, hypotf(dx, dz));
		};
		const auto voice_at = [&](VitaSound phrase, const Fvector& source, float range, float gain)
		{
			const float distance = hypotf(source.x - moving_player_eye[0], source.z - moving_player_eye[2]);
			if (distance < range)
			{
				game_audio.play(phrase, gain * (1.f - distance / range), sound_pan(source.x, source.z) * 0.7f);
				return true;
			}
			return false;
		};
		// scripts\\magnitofon\\magnitofon_2 in l01_escape/level.snd_static.
		const bool outside_audio=g_zone!=3&&outdoor_player&&!menu_consumed_input&&!any_dialog&&!quit_requested&&player_health;
		game_audio.set_ambience(outside_audio);
		if(outside_audio&&now>=next_ambient)
		{
			const VitaSound sounds[]={VitaSound::Bird1,VitaSound::Crow1,VitaSound::Bird2,VitaSound::Crow2};
			game_audio.play(sounds[ambient_variant%4],.30f,(ambient_variant&1)?.45f:-.35f);
			next_ambient=now+11000000+(ambient_variant++%4)*2300000;
		}
		constexpr float radio_x = -245.38423f, radio_y = -23.94432f, radio_z = -133.36070f;
		const float radio_distance = hypotf(hypotf(radio_x - moving_player_eye[0], radio_z - moving_player_eye[2]),
			radio_y - moving_player_eye[1]);
		const float radio_volume = player_view && bunker_view && !bunker_surface && !quit_requested && !save_menu_open
			? std::max(0.f, 1.f - radio_distance / 12.f) * (trader_dialog_open ? 0.16f : 0.42f) : 0.f;
		game_audio.set_radio(radio_volume, sound_pan(radio_x, radio_z) * 0.65f);
		if ((radio_volume > 0.f) != radio_audible && weapon_audio_ready && game_audio.radio_ready())
		{
			radio_audible = radio_volume > 0.f;
			write_boot_log(g_boot_log, radio_audible ? "debug_bunker_radio audible=1\n" : "debug_bunker_radio audible=0\n");
		}
        unsigned shot_seed=unsigned(now);
        const auto attack=[&](bool knife){
            const float horizontal=sinf(view_pitch);
            const float base[]={horizontal*sinf(view_yaw),-cosf(view_pitch),horizontal*cosf(view_yaw)};
            const bool shotgun=inventory.weapon()==VitaWeapon::Shotgun;
            const bool slug=inventory.current_ammo()==VitaInventoryItem::Slug;
            const unsigned pellets=knife?1:shotgun&&!slug?8:1;
            std::array<unsigned,9> hits{};
            for(unsigned pellet=0;pellet<pellets;++pellet){
                const auto random=[&](){shot_seed=shot_seed*1664525u+1013904223u;return float((shot_seed>>8)&65535)/32767.5f-1.f;};
                const float spread=knife?0:shotgun&&!slug?.035f:.003f;
                const float x=random()*spread,y=random()*spread;
                float direction[]={base[0]+x*cosf(view_yaw),base[1]+y,base[2]-x*sinf(view_yaw)};
                const float length=std::sqrt(direction[0]*direction[0]+direction[1]*direction[1]+direction[2]*direction[2]);
                for(auto&v:direction)v/=length;
                float nearest=knife?1.7f:shotgun&&!slug?35.f:90.f;size_t victim=9;
                if(outdoor_player&&scout_met&&guard_model_ready)for(unsigned i=0;i<6;++i){
                    if(!guards_placed[i]||!guard_health[i])continue;
                    const float hit=guard_ray_distance(moving_player_eye,direction,guard_transforms[i].c,nearest);
                    if(hit<nearest){nearest=hit;victim=i;}
                }
                if(outdoor_player&&g_zone==1&&road_encounter.ready)for(unsigned i=0;i<3;++i){
                    const auto&a=road_encounter.battle.guards[i];if(!a.health)continue;
                    Fvector feet;feet.set(a.position[0],a.position[1],a.position[2]);
                    const float hit=guard_ray_distance(moving_player_eye,direction,feet,nearest);
                    if(hit<nearest){nearest=hit;victim=6+i;}
                }
                if(victim<9&&!player_ground.blocks_ray(moving_player_eye,direction,nearest))
                    hits[victim]+=knife||slug||inventory.ammo_mode(inventory.active_slot())?2:1;
            }
            for(unsigned i=0;i<3;++i)if(hits[6+i]){
                auto&a=road_encounter.battle.guards[i];a.health-=std::min(a.health,hits[6+i]);road_encounter.flags|=2;
                Fvector feet;feet.set(a.position[0],a.position[1],a.position[2]);
                voice_at(game_audio.bandit_voice(i,a.health?2:3),feet,24.f,.8f);
            }
            for(unsigned victim=0;victim<6;++victim)if(hits[victim]){
                farm_fight_started=true;guard_health[victim]-=std::min(guard_health[victim],hits[victim]);
                if(g_zone!=2)voice_at(game_audio.bandit_voice(victim,guard_health[victim]?2:3),guard_transforms[victim].c,24.f,.8f);
                char line[120];snprintf(line,sizeof(line),"debug_weapon attack=%s guard=%u hits=%u health=%u\n",
                    knife?"knife":inventory.weapon()==VitaWeapon::Shotgun?"bm16":inventory.weapon()==VitaWeapon::Rifle?"ak74":"pm",
                    victim,hits[victim],guard_health[victim]);write_boot_log(g_boot_log,line);
            }
        };
        const auto& throw_begin=grenade_clips[size_t(VitaWeaponClip::KnifeWindup)];
        const auto& throw_act=grenade_clips[size_t(VitaWeaponClip::KnifeStrike)];
        const SceUInt64 throw_duration=throw_begin.duration_us()+throw_act.duration_us();
        if(weapon_input&&inventory.weapon()==VitaWeapon::Grenade&&grenade_hud_ready&&
            (pressed&SCE_CTRL_RTRIGGER)&&!grenade_pending&&(!grenade_throw_started||now-grenade_throw_started>=std::max<SceUInt64>(800000,throw_duration))&&
            std::any_of(grenades.bodies.begin(),grenades.bodies.end(),[](const VitaGrenadeBody&g){return !g.active;}))
        {
            grenade_thrown_type=inventory.grenade()==VitaInventoryItem::F1;
            if(inventory.throw_grenade()){grenade_pending=true;grenade_throw_started=now;write_boot_log(g_boot_log,"debug_grenade pin=1\n");}
        }
        if(weapon_input&&grenade_pending&&now-grenade_throw_started>=throw_begin.duration_us()){
            const std::array<float,3> direction={sinf(view_pitch)*sinf(view_yaw),-cosf(view_pitch),sinf(view_pitch)*cosf(view_yaw)};
            const std::array<float,3> origin={moving_player_eye[0],moving_player_eye[1]-.1f,moving_player_eye[2]};
            const bool launched=grenades.launch(origin,direction,grenade_thrown_type);grenade_pending=false;
            write_boot_log(g_boot_log,launched?"debug_grenade launched=1\n":"debug_grenade launched=0\n");
        }
        if(player_view&&!menu_consumed_input&&!dialog_consumed_input)
            for(const auto& explosion:grenades.update(player_ground,frame_seconds)){
                for(unsigned i=0;i<6;++i)if(scout_met&&guard_health[i]&&guards_placed[i]){
                    const auto&c=guard_transforms[i].c;
                    const unsigned damage=VitaGrenades::damage(player_ground,explosion,{c.x,c.y+.8f,c.z});
                    if(damage)guard_health[i]-=std::min(guard_health[i],(damage+24)/25);
                }
                for(unsigned i=0;i<3;++i)if(scout_met&&scout_ready[i]&&battle.scouts[i].health){
                    auto&a=battle.scouts[i];const auto&c=scout_transforms[i].c;
                    a.health-=std::min(a.health,VitaGrenades::damage(player_ground,explosion,{c.x,c.y+.8f,c.z}));
                    if(g_zone&&i==0)a.health=std::max(1u,a.health);
                }
                if(g_zone==1&&road_encounter.ready)for(unsigned i=0;i<4;++i){
                    auto&a=i?road_encounter.battle.guards[i-1]:road_encounter.battle.scouts[0];
                    const auto&c=a.position;const unsigned raw=VitaGrenades::damage(player_ground,explosion,{c[0],c[1]+.8f,c[2]});
                    const unsigned damage=i?(raw+24)/25:raw;a.health-=std::min(a.health,damage);
                    if(!i)a.health=std::max(1u,a.health);
                }
                const unsigned damage=inventory.absorb_damage(VitaGrenades::damage(player_ground,explosion,
                    {moving_player_eye[0],moving_player_eye[1]-.5f,moving_player_eye[2]}));
                player_health-=std::min(player_health,damage);if(damage)player_hit_time=now;
                game_audio.play(VitaSound::GrenadeExplosion,.95f,sound_pan(explosion.position[0],explosion.position[2])*.6f);
                auto oldest=std::min_element(explosion_flashes.begin(),explosion_flashes.end(),[](const ExplosionFlash&a,const ExplosionFlash&b){return a.time<b.time;});
                *oldest={explosion.position,now};
                write_boot_log(g_boot_log,"debug_grenade exploded=1\n");
            }
		const bool knife = inventory.weapon() == VitaWeapon::Knife && knife_hud_ready;
		const bool pistol = inventory.firearm() && (inventory.weapon()==VitaWeapon::Pistol?pm_hud_ready:primary_hud_ready);
		if ((!weapon_input || !knife) && knife_hit_pending)
		{
			knife_hit_pending = false;
			weapon_action_time = 0;
		}
		if (knife_hit_pending && now - weapon_action_time >= knife_windup_us)
		{
			knife_hit_pending = false;
			attack(true);
		}
		const SceUInt64 cooldown = knife ? (knife_motion_us ? knife_motion_us : 400000) : inventory.weapon()==VitaWeapon::Rifle?100000:inventory.weapon()==VitaWeapon::Shotgun?550000:190000;
		if (weapon_input && !grenade_pending && ((pressed & SCE_CTRL_RTRIGGER)||(inventory.weapon()==VitaWeapon::Rifle&&(pad.buttons&SCE_CTRL_RTRIGGER))) && !reload_deadline && (knife || pistol) &&
			(!weapon_action_time || now - weapon_action_time >= cooldown))
		{
			weapon_action_time = now;
			if (knife)
			{
				write_boot_log(g_boot_log, "debug_weapon knife_start\n");
				knife_hit_pending = knife_windup_us != 0;
				if (!knife_hit_pending)
					attack(true);
			}
			else if (inventory.fire_pistol())
			{
				shoot_clip = &active_clips[size_t(inventory.weapon()==VitaWeapon::Shotgun&&inventory.loaded()==0?VitaWeaponClip::ShootLast:inventory.loaded()?VitaWeaponClip::Shoot:VitaWeaponClip::ShootLast)];
				game_audio.play(inventory.weapon()==VitaWeapon::Shotgun?VitaSound::Shotgun:inventory.weapon()==VitaWeapon::Rifle?VitaSound::RifleShot:inventory.silenced()?VitaSound::SilencedShot:VitaSound::Pistol);
				attack(false);
			}
			else
			{
				shoot_clip = nullptr;
				game_audio.play(VitaSound::Empty, 0.65f);
				write_boot_log(g_boot_log, "debug_weapon empty=1\n");
			}
		}
		const bool aiming = weapon_input && pistol && !reload_deadline && (pad.buttons & SCE_CTRL_LTRIGGER);
		const float world_projection = aiming ? (inventory.scoped()?2.8f:pm_aim_projection) : 1.f;
		if (aiming != logged_aim)
		{
			write_boot_log(g_boot_log, aiming ? "debug_weapon aim=1\n" : "debug_weapon aim=0\n");
			logged_aim = aiming;
		}
		std::array<bool, 3> scout_walking{};
		if(g_zone&&((g_zone==3)||campaign_distance<80)&&!farm_cleared)farm_fight_started=true;
        if (outdoor_player && assault_with_group && !menu_consumed_input && player_health && (!g_zone||farm_fight_started))
		{
			assault_elapsed += frame_seconds;
			for (size_t i = 0; i < scout_routes.size(); ++i)
			{
				const auto& route = scout_routes[i];
				const float seconds = assault_elapsed - static_cast<float>(i) * 1.25f;
    if(battle.scouts[i].engaged){scout_route_rejoin[i]=true;continue;}
    if (route.size() < 2 || seconds <= 0.f || !battle.scouts[i].health)continue;
    if(scout_route_rejoin[i])
    {
     const auto& c=scout_transforms[i].c;
     const auto closest=std::min_element(route.begin(),route.end(),[&](const ScoutRouteSample&a,const ScoutRouteSample&b){
      return std::hypot(a.position[0]-c.x,a.position[2]-c.z)<std::hypot(b.position[0]-c.x,b.position[2]-c.z);
     });
     scout_route_metres[i]=closest->metres;scout_route_rejoin[i]=false;
    }
				const float end = route.back().metres;
				scout_route_metres[i]=std::min(scout_route_metres[i]+frame_seconds*2.2f,end);
    const float distance = scout_route_metres[i];
				const auto next = std::lower_bound(route.begin() + 1, route.end(), distance,
					[](const ScoutRouteSample& sample, float metres) { return sample.metres < metres; });
				const auto& from = *(next - 1);
				const auto& to = *next;
				const float t = (distance - from.metres) / (to.metres - from.metres);
				Fvector position;
				position.set(from.position[0] + t * (to.position[0] - from.position[0]),
					from.position[1] + t * (to.position[1] - from.position[1]),
					from.position[2] + t * (to.position[2] - from.position[2]));
				const auto previous=scout_transforms[i].c;
				const float dx=position.x-previous.x,dz=position.z-previous.z;
				const float span=std::hypot(dx,dz),limit=2.2f*frame_seconds;
				if(span>limit&&span>.001f)
				{
					position.x=previous.x+dx*limit/span;position.z=previous.z+dz*limit/span;
				}
				player_ground.slide_move(previous.x,previous.z,previous.y,position.x,position.z);
				float floor=previous.y;
				if(player_ground.height_at(position.x,position.z,previous.y,.45f,.65f,floor))position.y=floor;
				else position=previous;
				scout_transforms[i].setXYZ(0.f, -atan2f(to.position[0] - from.position[0],
					to.position[2] - from.position[2]), 0.f);
				scout_transforms[i].c = position;
				scout_walking[i] = distance < end || std::hypot(position.x-to.position[0],position.z-to.position[2])>.15f;
				if (i == 0 && !scout_walking[i] && !assault_arrival_logged)
				{
					write_boot_log(g_boot_log, "debug_cordon_assault leader_arrived=1\n");
     char arrival[160];
     snprintf(arrival,sizeof(arrival),"debug_scout_position member=%u x=%.2f y=%.2f z=%.2f target=%d\n",
      unsigned(i),position.x,position.y,position.z,battle.scouts[i].target);write_boot_log(g_boot_log,arrival);

					assault_arrival_logged = true;
				}
			}
		}
		if (!menu_consumed_input && !any_dialog && !quit_requested)
			npc_animation_seconds += frame_seconds;
		if (outdoor_player && weapon_input && scout_met && guard_model_ready && !menu_consumed_input && (!g_zone||farm_fight_started))
		{
			for(size_t i=0;i<battle.guards.size();++i)battle.guards[i].health=guards_placed[i]?guard_health[i]:0;
			for(size_t i=0;i<battle.scouts.size();++i)
			{
				auto&a=battle.scouts[i];const auto&c=scout_transforms[i].c;
				if(!a.engaged){a.position={c.x,c.y,c.z};a.home=a.position;}
			}
			const std::array<float,3> player_feet={moving_player_eye[0],moving_player_eye[1]-1.2f,moving_player_eye[2]};
			const auto shots=battle.update(player_ground,now,player_feet,assault_with_group,player_health);
            if(g_zone&&battle.scouts[0].health==0)battle.scouts[0].health=1;
			farm_fight_started = farm_fight_started || battle.active;
			const auto render_npc_position=[&](Fvector& current,const std::array<float,3>& goal)
			{
				const float dx=goal[0]-current.x,dy=goal[1]-current.y,dz=goal[2]-current.z;
				const float distance=std::hypot(dx,dz);
				const float factor=distance>.001f?std::min(1.f,1.3f*frame_seconds/distance):1.f;
				current.x+=dx*factor;current.y+=dy*factor;current.z+=dz*factor;
			};
			for(size_t i=0;i<battle.guards.size();++i)
			{
				const auto&a=battle.guards[i];
				const auto previous=guard_transforms[i].c;
				guard_transforms[i].setXYZ(0.f,a.yaw,0.f);guard_transforms[i].c=previous;
				render_npc_position(guard_transforms[i].c,a.position);
			}
			if(assault_with_group)
				for(size_t i=0;i<battle.scouts.size();++i)
				{
					const auto&a=battle.scouts[i];
					if(a.engaged)
					{
						const auto previous=scout_transforms[i].c;
						scout_transforms[i].setXYZ(0.f,a.yaw,0.f);scout_transforms[i].c=previous;
						render_npc_position(scout_transforms[i].c,a.position);
						scout_walking[i]=a.walking;
					}
				}
			for(const auto&shot:shots)
			{
				farm_fight_started=true;
				const auto& a=shot.ally?battle.scouts[shot.shooter]:battle.guards[shot.shooter];
				const float volume=std::max(.12f,.76f-std::hypot(a.position[0]-moving_player_eye[0],a.position[2]-moving_player_eye[2])*.018f);
				game_audio.play((g_zone&&shot.ally)||g_zone==2?VitaSound::RifleShot:shot.ally?VitaSound::Shotgun:VitaSound::Guard,volume,sound_pan(a.position[0],a.position[2]));
				if(shot.ally)
				{
					if(shot.target>=0 && guard_health[shot.target] && shot.damage)
					{
						--guard_health[shot.target];
						battle.guards[shot.target].health=guard_health[shot.target];
						const auto& c=guard_transforms[shot.target].c;
						if(g_zone!=2)voice_at(game_audio.bandit_voice(shot.target,!guard_health[shot.target]?3:2),c,28.f,.8f);
					}
				}
				else if(shot.target>=0)
				{
					auto& ally=battle.scouts[shot.target];
					ally.health-=std::min(ally.health,shot.damage);
                    if(g_zone&&shot.target==0)ally.health=std::max(1u,ally.health);
				}
				else
				{
					player_health-=std::min(player_health,inventory.absorb_damage(shot.damage));
					if(shot.damage)player_hit_time=now;
				}
				auto& callout_deadline=shot.ally?next_ally_callout:next_bandit_callout;
				if(now>=callout_deadline)
				{
					const auto& c=shot.ally?scout_transforms[shot.shooter].c:guard_transforms[shot.shooter].c;
					if(shot.ally||g_zone!=2)voice_at(shot.ally?game_audio.ally_voice(shot.shooter):game_audio.bandit_voice(shot.shooter,bandit_called[shot.shooter]?1:0),c,30.f,.82f);
					callout_deadline=now+4500000+(shot.shooter%3)*630000;
					if(!shot.ally)bandit_called[shot.shooter]=true;
				}
				char line[120];
				snprintf(line,sizeof(line),"debug_npc_combat ally=%u shooter=%u target=%d damage=%u\n",
					unsigned(shot.ally),shot.shooter,shot.target,shot.damage);write_boot_log(g_boot_log,line);
			}
		}
        if(g_zone==1&&road_encounter.ready&&outdoor_player&&weapon_input&&!menu_consumed_input&&!any_dialog){
            const std::array<float,3> feet={moving_player_eye[0],ground_y,moving_player_eye[2]};
            const auto shots=road_encounter.update(player_ground,now,feet,player_health);
            for(const auto&shot:shots){
                const auto&a=shot.ally?road_encounter.battle.scouts[shot.shooter]:road_encounter.battle.guards[shot.shooter];
                const float distance=std::hypot(a.position[0]-feet[0],a.position[2]-feet[2]);
                game_audio.play(VitaSound::Guard,std::max(.08f,.76f-distance*.018f),sound_pan(a.position[0],a.position[2]));
                if(!shot.ally&&shot.target<0&&shot.damage){player_health-=std::min(player_health,shot.damage);player_hit_time=now;}
            }
        }
		size_t last_bandit = guards_placed.size();
		if (!farm_cleared && scout_met && farm_fight_started)
		{
			size_t survivors = 0;
			for (size_t i = 0; i < guards_placed.size(); ++i)
			{
				if (guards_placed[i] && guard_health[i] != 0)
				{
					++survivors;
					last_bandit = i;
				}
			}
			farm_cleared = survivors == 0;
			if (survivors != 1)
				last_bandit = guards_placed.size();
			if (farm_cleared)
				write_boot_log(g_boot_log, "debug_flash_quest farm_cleared=1\n");
		}
		if (outdoor_player && assault_with_group && farm_fight_started && player_health && assault_elapsed >= 2.f &&
			!scout_victory_called)
		{
			if (farm_cleared)
			{
				scout_victory_called = voice_at(VitaSound::ScoutVictory, scout_transforms[0].c, 28.f, 0.92f);
				if (scout_victory_called)
					write_boot_log(g_boot_log, "debug_npc_voice scout=victory\n");
			}
			else if (!scout_attack_called)
			{
				scout_attack_called = voice_at(VitaSound::ScoutAttack, scout_transforms[0].c, 28.f, 0.92f);
				if (scout_attack_called)
					write_boot_log(g_boot_log, "debug_npc_voice scout=attack\n");
			}
		}
		const SceUInt64 begin_start = sceKernelGetProcessTimeWide();
		const bool post_requested=player_view&&!menu_open&&(settings.aa==1||(settings.dof&&graphics.msaa()==0));
		const bool post_ready=post_requested&&postprocess.begin(graphics.width(),graphics.height(),settings.aa==1,settings.dof&&graphics.msaa()==0);
		if(post_requested&&!post_ready)
		{
			write_boot_log(g_boot_log,"debug_postprocess fallback=1\n");
			if(postprocess.error()[0])write_boot_log(g_boot_log,postprocess.error());
			if(settings.aa==1)settings.aa=0;settings.dof=0;
		}
		graphics.begin_frame(0.015f, 0.025f, 0.055f);
		const SceUInt64 begin_end = sceKernelGetProcessTimeWide();
		SceUInt64 draw_start = begin_end;
		unsigned meshes_drawn = 0;
		size_t indices_drawn = 0;
		uint64_t view_us = 0, state_us = 0, indexed_us = 0, draw_log_us = 0;
		uint64_t slowest_mesh_us = 0;
		u32 slowest_visual = 0;
		MobileDDSJob::State dds_draw_state = MobileDDSJob::Idle;
		bool drew_level_geometry = false, sky_drawn = false;
		LodCaptureMode lod_capture_mode = LodCaptureMode::Geometry;
		if (!menu_open && level_visuals_loaded && level_scale > 0.f)
		{
			float render_center[3] = {level_center[0], level_center[1], level_center[2]};
			const float render_scale = player_view ? player_view_scale : view_scale;
			if (player_view)
				compute_player_view_center(moving_player_eye, render_scale, view_yaw, view_pitch, render_center);
			if (player_view && !isolate_terrain)
			{
				if (diagnostic_toggle && !dialog_consumed_input)
				{
					if (mobile_active && terrain_coverage)
						terrain_coverage_refresh = true;
					mobile_active = false;
					const float* reset_eye = bunker_view ? bunker_eye : player_eye;
					selection_x = reset_eye[0];
					selection_y = reset_eye[1];
					selection_z = reset_eye[2];
					selection_yaw = bunker_view ? bunker_yaw : initial_player_yaw;
					selection_pitch = bunker_view ? bunker_pitch : player_view_pitch;
					selection_time = now;
				}
				const float dx = moving_player_eye[0] - selection_x;
				const float dy = moving_player_eye[1] - selection_y;
				const float dz = moving_player_eye[2] - selection_z;
				const bool use_bunker_shell = bunker_view && at_bunker_stairs(moving_player_eye[0], moving_player_eye[2]);
				if (bunker_selection_dirty || use_bunker_shell != bunker_shell_selected || (now - selection_time >= 100000 &&
					(dx * dx + dy * dy + dz * dz >= 1.f || fabsf(view_yaw - selection_yaw) >= 0.07f ||
					 fabsf(view_pitch - selection_pitch) >= 0.07f)))
				{
					const SceUInt64 start = sceKernelGetProcessTimeWide();
					const bool bunker_entry = bunker_selection_dirty;
					bunker_selection_dirty = false;
					const bool bunker_interior = bunker_view && !bunker_surface;
					select_mobile_view(render_center, render_scale, view_yaw, view_pitch, moving_player_eye,
						bunker_interior, use_bunker_shell);
					if (use_bunker_shell != bunker_shell_selected || (bunker_entry && use_bunker_shell))
					{
						unsigned shell_draws = 0;
						if (use_bunker_shell)
							for (size_t index : bunker_shell)
								shell_draws += std::find(mobile_selection.indices.begin(), mobile_selection.indices.end(), index)
									!= mobile_selection.indices.end();
						char line[144];
						snprintf(line, sizeof(line), "debug_bunker_passage active=%u surface=%u structural_meshes=%u\n",
							static_cast<unsigned>(use_bunker_shell), static_cast<unsigned>(bunker_surface), shell_draws);
						write_boot_log(g_boot_log, line);
					}
					bunker_shell_selected = use_bunker_shell;
					if (bunker_entry)
					{
						char line[160];
						snprintf(line, sizeof(line), "debug_trader_bunker_view frame=%u x=%.2f y=%.2f z=%.2f meshes=%u\n",
							frame_number, moving_player_eye[0], moving_player_eye[1], moving_player_eye[2],
							static_cast<unsigned>(mobile_selection.indices.size()));
						write_boot_log(g_boot_log, line);
					}
					mobile_active = true;
					next_mobile_material = 0;
					selection_x = moving_player_eye[0];
					selection_y = moving_player_eye[1];
					selection_z = moving_player_eye[2];
					selection_yaw = view_yaw;
					selection_pitch = view_pitch;
					selection_time = now;
					++selection_updates;
					for (size_t index : mobile_selection.indices)
					{
						const u16 shader = level_visuals[index].shader_id;
						if (shader < texture_keys.size())
							texture_last_selection[texture_keys[shader]] = selection_updates;
					}
					frame_selection_us = sceKernelGetProcessTimeWide() - start;
					mobile_select_peak_us = std::max(mobile_select_peak_us, frame_selection_us);
					if (selection_updates <= 12 || selection_updates % 32 == 0)
					{
						char line[300];
						snprintf(line, sizeof(line),
							"debug_player_dynamic update=%u meshes=%u candidates=%u indices=%u select_us=%u texture_bytes=%u spatial_candidates=%u nearby_terrain=%u nearby_terrain_reserved=%u structures=%u\n",
							selection_updates, static_cast<unsigned>(mobile_selection.indices.size()),
							static_cast<unsigned>(mobile_selection.candidates), static_cast<unsigned>(mobile_selection.index_count),
							static_cast<unsigned>(frame_selection_us), static_cast<unsigned>(texture_memory_used),
							static_cast<unsigned>(mobile_candidates.size()),
							static_cast<unsigned>(mobile_selection.nearby_terrain),
							static_cast<unsigned>(mobile_selection.nearby_terrain_selected), static_cast<unsigned>(mobile_selection.focus_visuals));
						write_boot_log(g_boot_log, line);
					}
				}
			}
			if (mobile_dds.state.load() == MobileDDSJob::Ready)
			{
				const SceUInt64 start = sceKernelGetProcessTimeWide();
				const u16 shader = mobile_dds.shader;
				const size_t key = texture_keys[shader];
				const bool wanted = player_view && !isolate_terrain && mobile_active &&
					texture_last_selection[key] == selection_updates;
				const size_t required = mobile_dds.required_bytes;
				bool loaded = false;
				const char* reason = nullptr;
				BaseTextureStatus status = BaseTextureStatus::Ready;
				if (!wanted)
				{
					reason = "out_of_view";
					status = BaseTextureStatus::Unrequested;
					texture_attempted[shader] = 0;
					++mobile_texture_cancellations;
				}
				else if (required > mobile_dds.budget ||
					(mobile_dds.decoded && !reserve_mobile_texture(required, frame_number)))
				{
					status = BaseTextureStatus::Budget;
					texture_blocks[key] = required;
					texture_attempted[shader] = 0;
					++async_dds_budget_refusals;
					// The camera may have freed space since the worker received its budget.
					if (required <= reclaimable_texture_bytes())
						next_mobile_material = 0;
				}
				else if (mobile_dds.decoded)
				{
					std::unique_ptr<VitaGLTexture> texture(new VitaGLTexture());
					loaded = texture->upload_dds(mobile_dds.image, &mobile_dds.timing);
					if (loaded)
					{
						texture_memory_used += texture->uploaded_size();
                        if(vita_structure_texture(*mobile_dds.name)&&texture->uploaded_size()>128u*128u*4)structure_quality_bytes+=texture->uploaded_size();
						for (size_t alias = 0; alias < texture_keys.size(); ++alias)
						{
							if (texture_keys[alias] != key)
								continue;
							base_texture_handles[alias] = texture->handle();
							base_texture_status[alias] = BaseTextureStatus::Ready;
							texture_attempted[alias] = 1;
						}
						level_textures[shader] = std::move(texture);
						++texture_loaded_count;
					}
					else
					{
						status = BaseTextureStatus::UploadFailed;
						++texture_invalid_count;
					}
				}
				else if (!mobile_dds.found)
				{
					status = BaseTextureStatus::NotFound;
					++texture_missing_count;
				}
				else
				{
					status = mobile_dds.opened ? BaseTextureStatus::DecodeFailed : BaseTextureStatus::ReadFailed;
					++texture_invalid_count;
				}
				base_texture_status[shader] = status;
				if (wanted && status != BaseTextureStatus::Budget)
					texture_blocks[key] = 0;

				frame_load_us = sceKernelGetProcessTimeWide() - start;
				++async_dds_completed;
				async_dds_peak_upload_us = std::max(async_dds_peak_upload_us, mobile_dds.timing.upload_us);
				char line[768];
				snprintf(line, sizeof(line),
					"debug_player_dds_async request_frame=%u upload_frame=%u texture=%s loaded=%u reason=%s required_bytes=%u budget_bytes=%u lookup_us=%llu read_us=%llu decode_us=%llu upload_us=%llu\n",
					mobile_dds.frame, frame_number, mobile_dds.name->c_str(), static_cast<unsigned>(loaded),
					reason ? reason : base_texture_status_name(status), static_cast<unsigned>(required),
					static_cast<unsigned>(mobile_dds.budget),
					static_cast<unsigned long long>(mobile_dds.lookup_us), static_cast<unsigned long long>(mobile_dds.read_us),
					static_cast<unsigned long long>(mobile_dds.timing.decode_us),
					static_cast<unsigned long long>(mobile_dds.timing.upload_us));
				write_boot_log(g_boot_log, line);
				mobile_dds.state.store(MobileDDSJob::Idle);
			}
			if (player_view && !isolate_terrain)
			{
				const SceUInt64 materials_start = sceKernelGetProcessTimeWide();
				const u32 base_attempts_before = texture_attempted_count;
				const size_t lightmaps_before = loaded_lightmap_names.size();
				size_t available = mobile_texture_limit - texture_memory_used;
				bool counted_evictable_textures = mobile_dds_worker < 0;
				while (mobile_active && next_mobile_material < mobile_selection.indices.size() &&
					mobile_dds.state.load() == MobileDDSJob::Idle)
				{
					const u16 shader = level_visuals[mobile_selection.indices[next_mobile_material++]].shader_id;
					load_lightmap(shader, &texture_profile);
					if (shader >= level_texture_names.size() || level_texture_names[shader].empty() ||
						texture_attempted[shader])
						continue;
					if (dds_paused)
					{
						load_base(shader, mobile_texture_limit, &texture_profile, nullptr, 0, false);
						continue;
					}
					const size_t required = texture_blocks[texture_keys[shader]];
					if (!counted_evictable_textures)
					{
						available = reclaimable_texture_bytes();
						counted_evictable_textures = true;
					}
					if (required > available)
					{
						base_texture_status[shader] = BaseTextureStatus::Budget;
						continue;
					}
					load_base(shader, mobile_texture_limit, &texture_profile,
						mobile_dds_worker >= 0 ? &mobile_dds : nullptr, available);
					if (required != 0 && mobile_dds.state.load() != MobileDDSJob::Idle)
						++mobile_texture_retries;
					if (texture_attempted_count != base_attempts_before)
						break;
				}
				if ((texture_attempted_count != base_attempts_before && mobile_dds.state.load() == MobileDDSJob::Idle) ||
					loaded_lightmap_names.size() != lightmaps_before)
				{
					const SceUInt64 elapsed = sceKernelGetProcessTimeWide() - materials_start;
					frame_load_us += elapsed;
					++mobile_texture_batches;
					mobile_texture_total_us += elapsed;
					mobile_texture_peak_us = std::max(mobile_texture_peak_us, elapsed);
					if (mobile_texture_batches <= 12 || mobile_texture_batches % 32 == 0)
					{
						char line[160];
						snprintf(line, sizeof(line), "debug_player_dynamic_textures batch=%u load_us=%u base_attempts=%u new_lightmaps=%u\n",
							mobile_texture_batches, static_cast<unsigned>(elapsed), texture_attempted_count - base_attempts_before,
							static_cast<unsigned>(loaded_lightmap_names.size() - lightmaps_before));
						write_boot_log(g_boot_log, line);
					}
				}
			}
			mobile_texture_peak_bytes = std::max(mobile_texture_peak_bytes, texture_memory_used - pinned_texture_bytes);
			draw_start = sceKernelGetProcessTimeWide();
			sky_drawn = sky_visible && sky_enabled && cordon_sky.draw(view_yaw, view_pitch, world_projection);
			if (sky_drawn && !sky_draw_logged)
			{
				const GLenum error = glGetError();
				char line[96];
				snprintf(line, sizeof(line), "debug_cordon_sky drawn=1 gl_error=0x%x\n", static_cast<unsigned>(error));
				write_boot_log(g_boot_log, line);
				sky_draw_logged = true;
			}
			dds_draw_state = mobile_dds.state.load();
			const auto set_render_view = [&](const float* transform, bool foliage = false)
			{
				const uint64_t start = player_view ? sceKernelGetProcessTimeWide() : 0;
				debug_renderer.set_geometry_view(render_center, render_scale, view_yaw, view_pitch, transform, player_view, foliage, world_projection);
				if (player_view)
					view_us += sceKernelGetProcessTimeWide() - start;
			};
			set_render_view(nullptr);
			const float* active_transform = 0;
			const bool use_textures = player_view || (pad.buttons & SCE_CTRL_SQUARE) == 0;
			const auto npc_pose = [&](const StalkerVisual& visual, unsigned identity, bool walking,
				const VitaNpcAgent* agent = nullptr)
			{
				VitaNpcClip clip=VitaNpcClip::Idle;
				float seconds=npc_animation_seconds+identity*.73f;
				bool loop=true;
				if(agent&&!agent->health){clip=VitaNpcClip::Dead;seconds=0;loop=false;}
                else if(agent && agent->shot_time && now-agent->shot_time<420000)
				{clip=VitaNpcClip::Fire;seconds=float(now-agent->shot_time)/1000000.f;loop=false;}
				else if(agent && agent->reload_until>now)
				{
					clip=VitaNpcClip::Reload;
					const auto& reload=visual.animations[static_cast<size_t>(clip)];
					const float duration=float(agent->reload_until-agent->shot_time)/1000000.f-.42f;
					const float elapsed=float(now-agent->shot_time)/1000000.f-.42f;
					seconds=duration>0.f?elapsed/duration*reload.count/reload.fps:0.f;loop=false;
				}
				else if(walking)clip=VitaNpcClip::Walk;
				else if(agent && agent->engaged)clip=VitaNpcClip::Aim;
				else if(static_cast<unsigned>(seconds/7.f)%2)clip=VitaNpcClip::Alternate;
				return visual.animations[static_cast<size_t>(clip)].sample(seconds,loop);
			};
			const auto draw_npc_weapon = [&](size_t kind, const VitaStalkerWeaponPose& pose, const Fmatrix& actor, VitaNpcPoseSample sample)
			{
				if (!npc_weapon_ready[kind] || !pose.ready) return;
				const auto& matrix = sample.first && sample.first <= pose.walk.size() ? pose.walk[sample.first - 1] : pose.idle;
				const auto& next_matrix=sample.next && sample.next<=pose.walk.size()?pose.walk[sample.next-1]:pose.idle;
				Fmatrix hand, mounted, world;
				for(size_t k=0;k<16;++k)hand.m[k/4][k%4]=matrix[k]+(next_matrix[k]-matrix[k])*sample.blend;
				mounted.mul_43(hand, npc_weapon_offsets[kind]);
				world.mul_43(actor, mounted);
				set_render_view(world.m[0]);
				bool submitted = false;
				for (const auto& part : npc_weapons[kind])
					if (part.indices.index_count() && debug_renderer.draw_geometry_range(part.vertices, part.indices, 0,
							part.vertices.vertex_count(), 0, part.indices.index_count(), use_textures ? part.texture.handle() : 0))
					{
						drew_level_geometry = true;
						++meshes_drawn;
						indices_drawn += part.indices.index_count();
						submitted = true;
					}
				if (submitted && !npc_weapon_draw_logged[kind])
				{
					char line[80];
					snprintf(line, sizeof(line), "debug_npc_weapons drawn=%u\n", unsigned(kind));
					write_boot_log(g_boot_log, line);
					npc_weapon_draw_logged[kind] = true;
				}
			};

            if(g_zone==3&&campaign_story<7&&strelok_item_ready){
                Fmatrix item;item.setXYZ(-.003072292f,0.f,0.f);item.c.set(vita_strelok_flash[0],vita_strelok_flash[1],vita_strelok_flash[2]);
                set_render_view(item.m[0]);
                for(const auto&part:strelok_item)if(part.indices.index_count())
                    debug_renderer.draw_geometry_range(part.vertices,part.indices,0,part.vertices.vertex_count(),0,part.indices.index_count(),use_textures?part.texture.handle():0);
                set_render_view(nullptr);
            }
			const bool use_lightmaps = use_textures && (player_view || (pad.buttons & SCE_CTRL_CROSS) == 0);
			const bool use_detail = use_lightmaps && !(isolate_terrain && (pad.buttons & SCE_CTRL_UP));
			const bool use_mobile_selection = player_view && !isolate_terrain && mobile_active;
			const bool use_player_selection = player_view && !isolate_terrain && !use_mobile_selection && !player_selection.indices.empty();
			const std::vector<size_t>& active_visuals = use_mobile_selection ? mobile_selection.indices :
				(use_player_selection ? player_selection.indices : render_visual_indices);
			const bool left_trigger = !pistol && (pad.buttons & SCE_CTRL_LTRIGGER) != 0;
			const bool right_trigger = !pistol && !knife && (pad.buttons & SCE_CTRL_RTRIGGER) != 0;
			const bool hide_general = use_player_selection && player_lod_draws.empty() && left_trigger && !right_trigger;
			const bool compare_lod_alpha = left_trigger && right_trigger;
			const size_t active_count = hide_general ? player_general_start : active_visuals.size();
			const bool coverage_active = terrain_coverage && player_view && !isolate_terrain;
			const bool coverage_rebuilt = coverage_active &&
				(terrain_coverage_refresh || terrain_coverage_selection != selection_updates);
			if (coverage_rebuilt)
			{
				terrain_coverage_refresh = false;
				terrain_coverage_selection = selection_updates;
				terrain_coverage_draws.clear();
				std::vector<u8> selected(level_visuals.size(), 0);
				for (const auto index : active_visuals)
					selected[index] = 1;
				std::vector<size_t> spatial;
				mobile_spatial_index.query(render_center, render_scale, view_yaw, view_pitch, vita_player_fov_scale, spatial);
				std::vector<u8> spatial_visible(level_visuals.size(), 0);
				for (const auto index : spatial)
					spatial_visible[index] = 1;

				std::vector<std::pair<float, size_t>> omitted;
				unsigned nearby_selected = 0;
				for (const auto& candidate : terrain_near_eye(moving_player_eye))
				{
					if (selected[candidate.second])
						++nearby_selected;
					else
						omitted.push_back(candidate);
				}
				constexpr size_t extra_index_limit = 300000, extra_mesh_limit = 96;
				size_t added_indices = 0;
				unsigned logged = 0;
				for (const auto& candidate : omitted)
				{
					const size_t index = candidate.second;
					const VitaDebugVisualRange& visual = level_visuals[index];
					if (terrain_coverage_draws.size() >= extra_mesh_limit ||
						visual.index_count > extra_index_limit - added_indices)
						continue;
					terrain_coverage_draws.push_back(index);
					added_indices += visual.index_count;
					if (logged < 24)
					{
						float score = 0.f;
						u32 screen_cell = 0;
						const bool view_candidate = score_visual_for_view(visual, render_center, render_scale,
							view_yaw, view_pitch, vita_player_fov_scale, score, screen_cell);
						char line[288];
						snprintf(line, sizeof(line),
							"debug_player_terrain_coverage_mesh frame=%u visual=%u shader=%u indices=%u distance_m=%.2f spatial=%u score_visible=%u\n",
							frame_number, visual.visual_id, static_cast<unsigned>(visual.shader_id), visual.index_count,
							sqrtf(candidate.first), static_cast<unsigned>(spatial_visible[index]),
							static_cast<unsigned>(view_candidate));
						write_boot_log(g_boot_log, line);
						++logged;
					}
				}
				char line[256];
				snprintf(line, sizeof(line),
					"debug_player_terrain_coverage_view frame=%u x=%.2f y=%.2f z=%.2f yaw_deg=%.1f pitch=%.3f nearby_selected=%u omitted=%u added=%u added_indices=%u\n",
					frame_number, moving_player_eye[0], moving_player_eye[1], moving_player_eye[2],
					view_yaw * 57.2957795f, view_pitch, nearby_selected, static_cast<unsigned>(omitted.size()),
					static_cast<unsigned>(terrain_coverage_draws.size()), static_cast<unsigned>(added_indices));
				write_boot_log(g_boot_log, line);
			}
			unsigned fallback_logs_this_frame = 0;
			const auto draw_level_visual = [&](size_t i, bool cutout = false, bool barbed_wire = false)
			{
				const PlayerVegetation vegetation = use_player_selection
					? player_general_vegetation[i] : PlayerVegetation::None;
				if (!compare_lod_alpha && right_trigger && vegetation != PlayerVegetation::None)
					return;
				if (use_player_selection && left_trigger && use_textures &&
					player_lod_slots[i] < player_lod_draws.size())
				{
					if (player_lod_first[i])
					{
						if (active_transform)
						{
							set_render_view(nullptr);
							active_transform = nullptr;
						}
						const PlayerLodDraw& lod = player_lod_draws[player_lod_slots[i]];
						const bool use_original_mip = lod.original_mip_vertices != nullptr;
						const VitaGLVertexBuffer& vertices = use_original_mip ? *lod.original_mip_vertices : *lod.vertices;
						const GLuint texture = use_original_mip ? lod_original_mip.handle() : lod_atlas.handle();
						const bool alpha_drawn = compare_lod_alpha && lod.alpha_vertices &&
							debug_renderer.draw_lod_alpha(*lod.alpha_vertices, lod_indices, texture);
						if (alpha_drawn || debug_renderer.draw_geometry_range(vertices, lod_indices, 0, 4, 0, 6, texture))
						{
							drew_level_geometry = true;
							++meshes_drawn;
							indices_drawn += 6;
							lod_capture_mode = alpha_drawn ? LodCaptureMode::AlphaThreshold : LodCaptureMode::Blend;
						}
					}
					return;
				}
				const size_t visual_index = active_visuals[i];
				if (isolate_terrain && visual_index != terrain_anchor) return;
				const VitaDebugVisualRange& range = level_visuals[visual_index];
				const float* transform = range.has_tree_transform ? range.tree_transform : 0;
				if (transform != active_transform)
				{
					set_render_view(transform, cutout);
					active_transform = transform;
				}
				VitaGLVertexBuffer* vertices = level_buffers.vertex_buffer(range.vertex_buffer);
				VitaGLIndexBuffer* indices = level_buffers.index_buffer(range.index_buffer);
				GLuint texture = use_textures && range.shader_id < base_texture_handles.size()
					? base_texture_handles[range.shader_id] : 0;
				GLuint lightmap = use_lightmaps && range.shader_id < level_lightmap_handles.size()
					? level_lightmap_handles[range.shader_id] : 0;
				const bool terrain = range.shader_id < level_shader_names.size() &&
					level_shader_names[range.shader_id].find("terrain") != std::string::npos;
				if (barbed_wire && (!texture || !vertices ||
					!vertices->has_attribute(static_cast<GLuint>(debug_renderer.texcoord_attribute_location()))))
					return;
				VitaGeometryDrawTiming mesh_timing;
				if (vertices && indices && debug_renderer.draw_geometry_range(*vertices, *indices,
					range.first_vertex, range.vertex_count, range.first_index, range.index_count,
					texture, lightmap, terrain, range.has_tree_transform, terrain_sample,
					use_detail && range.shader_id < terrain_detail_handles.size() ? terrain_detail_handles[range.shader_id] : 0,
					player_view ? &mesh_timing : nullptr, cutout))
				{
					state_us += mesh_timing.state_us;
					indexed_us += mesh_timing.indexed_us;
					const uint64_t mesh_us = mesh_timing.state_us + mesh_timing.indexed_us;
					if (mesh_us > slowest_mesh_us)
					{
						slowest_mesh_us = mesh_us;
						slowest_visual = range.visual_id;
					}
					drew_level_geometry = true;
					++meshes_drawn;
					indices_drawn += range.index_count;
					if (barbed_wire && !barbed_wire_draw_logged)
					{
						char line[128];
						snprintf(line, sizeof(line), "debug_barbed_wire visual=%u shader=%u blend=1 depth_write=0\n",
							range.visual_id, static_cast<unsigned>(range.shader_id));
						write_boot_log(g_boot_log, line);
						barbed_wire_draw_logged = true;
					}
					if (use_mobile_selection && use_textures && range.shader_id < fallback_logged.size() &&
						fallback_logs_this_frame < 4)
					{
						const u16 shader = range.shader_id;
						BaseTextureStatus fallback = base_texture_status[shader];
						if (level_texture_names[shader].empty())
							fallback = BaseTextureStatus::NoTexture;
						else if (!vertices->has_attribute(static_cast<GLuint>(debug_renderer.texcoord_attribute_location())))
							fallback = BaseTextureStatus::NoUV;
						const unsigned flag = 1u << static_cast<unsigned>(fallback);
						if ((!texture || fallback == BaseTextureStatus::NoUV) && !(fallback_logged[shader] & flag))
						{
							fallback_logged[shader] |= flag;
							++fallback_logs_this_frame;
							++texture_fallbacks_logged;
							char line[768];
							snprintf(line, sizeof(line),
								"debug_player_texture_fallback frame=%u visual=%u shader_id=%u material=%s texture=%s reason=%s budget_left=%u\n",
								frame_number, range.visual_id, static_cast<unsigned>(shader),
								level_shader_names[shader].c_str(), level_texture_names[shader].c_str(),
								base_texture_status_name(fallback), static_cast<unsigned>(mobile_texture_limit - texture_memory_used));
							const uint64_t log_start = sceKernelGetProcessTimeWide();
							write_boot_log(g_boot_log, line);
							draw_log_us += sceKernelGetProcessTimeWide() - log_start;
						}
					}
				}
			};
			foliage_draws.clear();
			translucent_draws.clear();
			const bool sort_foliage = outdoor_player && use_textures && !(use_player_selection && left_trigger);
			const float horizontal = sinf(view_pitch);
			const float forward_x = horizontal * sinf(view_yaw);
			const float forward_y = -cosf(view_pitch);
			const float forward_z = horizontal * cosf(view_yaw);
			const VitaViewFrustum current_frustum(render_center, render_scale, view_yaw, view_pitch,
				vita_player_fov_scale * world_projection);
			unsigned mobile_visible = 0;
			size_t mobile_indices = 0;
			for (size_t i = 0; i < active_count; ++i)
			{
				const auto& visual = level_visuals[active_visuals[i]];
				if (use_mobile_selection)
				{
					if (mobile_visible >= debug_visual_limit)
						break;
					if (visual.index_count > reference_index_budget - mobile_indices ||
						!current_frustum.intersects(visual.bounds_minimum, visual.bounds_maximum))
						continue;
					++mobile_visible;
					mobile_indices += visual.index_count;
				}
				const u16 shader = visual.shader_id;
				const bool wire = use_textures && shader < barbed_wire_materials.size() && barbed_wire_materials[shader];
				const bool foliage = sort_foliage && visual.has_bounds && shader < foliage_materials.size() &&
					foliage_materials[shader] && base_texture_handles[shader];
				if (!wire && !foliage)
				{
					draw_level_visual(i);
					continue;
				}
				const auto* vertices = level_buffers.vertex_buffer(visual.vertex_buffer);
				if (!wire && (!vertices || !vertices->has_attribute(static_cast<GLuint>(debug_renderer.texcoord_attribute_location()))))
				{
					draw_level_visual(i);
					continue;
				}
				const float depth = visual.bounds_center[0] * forward_x +
					visual.bounds_center[1] * forward_y + visual.bounds_center[2] * forward_z;
				if (foliage && foliage_cutout_enabled)
					foliage_draws.emplace_back(depth, i);
				else
					translucent_draws.emplace_back(-depth, i);
			}
			mobile_visible_peak = std::max(mobile_visible_peak, mobile_visible);
			mobile_indices_peak = std::max(mobile_indices_peak, static_cast<unsigned>(mobile_indices));
			if (coverage_active)
			{
				unsigned coverage_submitted = 0;
				if (!terrain_coverage_draws.empty())
					set_render_view(nullptr);
				for (const auto index : terrain_coverage_draws)
				{
					const VitaDebugVisualRange& visual = level_visuals[index];
					const VitaGLVertexBuffer* vertices = level_buffers.vertex_buffer(visual.vertex_buffer);
					const VitaGLIndexBuffer* indices = level_buffers.index_buffer(visual.index_buffer);
					if (vertices && indices && debug_renderer.draw_geometry_range(*vertices, *indices,
						visual.first_vertex, visual.vertex_count, visual.first_index, visual.index_count,
						0, 0, true, false, VitaTerrainSample::Coverage))
					{
						drew_level_geometry = true;
						++meshes_drawn;
						indices_drawn += visual.index_count;
						++coverage_submitted;
					}
				}
				if (coverage_rebuilt)
				{
					char line[128];
					snprintf(line, sizeof(line), "debug_player_terrain_coverage_draw frame=%u submitted=%u requested=%u\n",
						frame_number, coverage_submitted, static_cast<unsigned>(terrain_coverage_draws.size()));
					write_boot_log(g_boot_log, line);
				}
			}
			if (player_view && wolf_visible && !isolate_terrain)
			{
				Fmatrix transform = wolf_spawn_transform;
				if (wolf_dialog_open)
				{
					const float yaw = -atan2f(moving_player_eye[0] - wolf_target.x, moving_player_eye[2] - wolf_target.z);
					transform.setXYZ(0.f, yaw, 0.f);
					transform.c = wolf_spawn_transform.c;
				}
				set_render_view(transform.m[0]);
				const auto wolf_pose=npc_pose(wolf_visuals[0],0,false);
				for (auto& part : wolf_visuals)
				{
					part.vertices.select_position_frame(wolf_pose.first);
					if (debug_renderer.draw_npc(part.vertices,part.indices,wolf_pose.first,wolf_pose.next,wolf_pose.blend,use_textures ? part.texture.handle() : 0))
					{
						drew_level_geometry = true;
						++meshes_drawn;
						indices_drawn += part.indices.index_count();
						if (!wolf_draw_logged)
						{
							write_boot_log(g_boot_log, "debug_wolf submitted=1\n");
							wolf_draw_logged = true;
						}
					}
				}
				draw_npc_weapon(0, wolf_visuals[0].weapon_pose, transform, wolf_pose);
			}
			if (player_view && scouts_visible && !isolate_terrain)
			{
				for (unsigned i = 0; i < 3; ++i)
				{
					const auto& position = scout_transforms[i].c;
					if (!scout_ready[i] || hypotf(moving_player_eye[0] - position.x, moving_player_eye[2] - position.z) >= 55.f)
						continue;
					Fmatrix transform = scout_transforms[i];
					if (i == 0 && scout_dialog_open)
					{
						const float yaw = -atan2f(moving_player_eye[0] - scout_target.x, moving_player_eye[2] - scout_target.z);
						transform.setXYZ(0.f, yaw, 0.f);
						transform.c = scout_transforms[i].c;
					}
					set_render_view(transform.m[0]);
					const auto frame=npc_pose(scout_visuals[i][0],i+1,scout_walking[i],&battle.scouts[i]);
					for (unsigned j = 0; j < 2; ++j)
					{
						auto& part = scout_visuals[i][j];
						part.vertices.select_position_frame(frame.first);
						const GLuint texture = part.texture.handle() ? part.texture.handle() :
							j == 0 ? scout_visuals[0][0].texture.handle() : 0;
						if (debug_renderer.draw_npc(part.vertices,part.indices,frame.first,frame.next,frame.blend,use_textures ? texture : 0))
						{
							drew_level_geometry = true;
							++meshes_drawn;
							indices_drawn += part.indices.index_count();
							if (!scout_draw_logged)
							{
								write_boot_log(g_boot_log, "debug_scout submitted=1\n");
								scout_draw_logged = true;
							}
						}
					}
					if(battle.scouts[i].health)draw_npc_weapon(g_zone?0:1, scout_visuals[i][0].weapon_pose, transform, frame);
				}
			}
			if (outdoor_player && scout_met && allegro_model_ready &&
				allegro_distance < 40.f)
			{
				Fmatrix transform = allegro_transform;
				if (allegro_dialog_open)
				{
					const float yaw = -atan2f(moving_player_eye[0] - allegro_transform.c.x,
						moving_player_eye[2] - allegro_transform.c.z);
					transform.setXYZ(0.f, yaw, 0.f);
					transform.c = allegro_transform.c;
				}
				set_render_view(transform.m[0]);
				for (const auto& part : allegro_visual)
					if (debug_renderer.draw_geometry_range(part.vertices, part.indices, 0, part.vertices.vertex_count(),
						0, part.indices.index_count(), use_textures ? part.texture.handle() : 0))
					{
						drew_level_geometry = true;
						++meshes_drawn;
						indices_drawn += part.indices.index_count();
					if (!allegro_draw_logged)
					{
						write_boot_log(g_boot_log, "debug_allegro submitted=1\n");
						allegro_draw_logged = true;
					}
					}
			}
			if (outdoor_player && scout_met && guard_model_ready)
			{
				for (size_t i = 0; i < guards_placed.size(); ++i)
				{
					const auto& guard = guard_transforms[i];
					if (!guards_placed[i] || hypotf(moving_player_eye[0] - guard.c.x, moving_player_eye[2] - guard.c.z) >= 45.f)
						continue;
					set_render_view(guard.m[0]);
					const auto guard_pose=npc_pose(guard_visual[0],i+4,battle.guards[i].walking,&battle.guards[i]);
					for (auto& part : guard_visual)
					{
						part.vertices.select_position_frame(guard_pose.first);
						if (debug_renderer.draw_npc(part.vertices,part.indices,guard_pose.first,guard_pose.next,guard_pose.blend,use_textures ? part.texture.handle() : 0))
						{
							drew_level_geometry = true;
							++meshes_drawn;
							indices_drawn += part.indices.index_count();
							if (!guard_draw_logged)
							{
								write_boot_log(g_boot_log, "debug_cordon_assault guards_drawn=1\n");
								guard_draw_logged = true;
							}
						}
					}
					if(guard_health[i])draw_npc_weapon(g_zone==2?0:2, guard_visual[0].weapon_pose, guard, guard_pose);
				}
			}
            if(g_zone==1&&road_encounter.ready&&outdoor_player&&!isolate_terrain)
                for(unsigned i=0;i<4;++i){
                    const auto&a=i?road_encounter.battle.guards[i-1]:road_encounter.battle.scouts[0];
                    if(std::hypot(a.position[0]-moving_player_eye[0],a.position[2]-moving_player_eye[2])>55.f)continue;
                    if((i&&!guard_model_ready)||(!i&&!road_model_ready))continue;
                    auto*parts=i?guard_visual:road_visual;
                    Fmatrix transform;transform.setXYZ(0,a.yaw,0);transform.c.set(a.position[0],a.position[1],a.position[2]);
                    set_render_view(transform.m[0]);const auto pose=npc_pose(parts[0],12+i,a.walking,&a);
                    for(unsigned j=0;j<2;++j){auto&part=parts[j];part.vertices.select_position_frame(pose.first);
                        if(debug_renderer.draw_npc(part.vertices,part.indices,pose.first,pose.next,pose.blend,use_textures?part.texture.handle():0)){
                            drew_level_geometry=true;++meshes_drawn;indices_drawn+=part.indices.index_count();
                        }
                    }
                    if(a.health)draw_npc_weapon(2,parts[0].weapon_pose,transform,pose);
                }
			if (player_view && bunker_view && !bunker_surface && door_ready && !isolate_terrain)
			{
				set_render_view(door_transform.m[0]);
				if (debug_renderer.draw_geometry_range(door_vertices, door_indices, 0, door_vertices.vertex_count(),
					0, door_indices.index_count(), use_textures ? door_texture.handle() : 0))
				{
					drew_level_geometry = true;
					++meshes_drawn;
					indices_drawn += door_indices.index_count();
				}
			}
			if (player_view && bunker_view && !bunker_surface && trader_model_ready && !isolate_terrain)
			{
				const size_t idle_frames = trader_vertices.position_frame_count();
				if (idle_frames > 1)
				{
					if (!trader_idle_start)
						trader_idle_start = now;
					const SceUInt64 elapsed_frames = (now - trader_idle_start) * vita_trader_idle_fps / 1000000;
					trader_vertices.select_position_frame(static_cast<size_t>(elapsed_frames % idle_frames));
					if (elapsed_frames > 0 && !trader_idle_logged)
					{
						write_boot_log(g_boot_log, "debug_trader_idle advancing=1\n");
						trader_idle_logged = true;
					}
					if (elapsed_frames >= idle_frames && !trader_idle_loop_logged)
					{
						write_boot_log(g_boot_log, "debug_trader_idle looped=1\n");
						trader_idle_loop_logged = true;
					}
				}
				set_render_view(trader_transform.m[0]);
				VitaGeometryDrawTiming model_timing;
				if (debug_renderer.draw_geometry_range(trader_vertices, trader_indices,
					0, trader_vertices.vertex_count(), 0, trader_indices.index_count(),
					use_textures ? trader_texture.handle() : 0, 0, false, false, VitaTerrainSample::Shaded, 0, &model_timing))
				{
					drew_level_geometry = true;
					++meshes_drawn;
					indices_drawn += trader_indices.index_count();
					state_us += model_timing.state_us;
					indexed_us += model_timing.indexed_us;
					if (!trader_model_draw_logged)
					{
						write_boot_log(g_boot_log, "debug_trader_model submitted=1\n");
						trader_model_draw_logged = true;
					}
				}
			}
			if (!foliage_draws.empty())
			{
				std::sort(foliage_draws.begin(), foliage_draws.end());
				set_render_view(nullptr, true);
				active_transform = nullptr;
				glDisable(GL_BLEND);
				for (const auto& draw : foliage_draws)
					draw_level_visual(draw.second, true);
				glEnable(GL_BLEND);
			}
			if (!translucent_draws.empty())
			{
				std::sort(translucent_draws.begin(), translucent_draws.end());
				set_render_view(nullptr);
				active_transform = nullptr;
				// Transparent texels must not occlude terrain or other alpha surfaces.
				glDepthMask(GL_FALSE);
				for (const auto& draw : translucent_draws)
				{
					const u16 shader = level_visuals[active_visuals[draw.second]].shader_id;
					draw_level_visual(draw.second, false, barbed_wire_materials[shader]);
				}
				glDepthMask(GL_TRUE);
			}
		}
		const bool mobile_view = player_view && mobile_active && !isolate_terrain;
		if (!menu_open && !drew_level_geometry && !mobile_view && level_geometry_loaded && level_buffers.vertex_buffer_count() && level_buffers.index_buffer_count())
		{
			VitaGLVertexBuffer* vertices = level_buffers.vertex_buffer(0);
			VitaGLIndexBuffer* indices = level_buffers.index_buffer(0);
			if (vertices && indices && debug_renderer.draw_geometry(*vertices, *indices))
			{
				drew_level_geometry = true;
				++meshes_drawn;
				indices_drawn += indices->index_count();
			}
		}
		if (!menu_open && !drew_level_geometry && !mobile_view)
			debug_renderer.draw();
		if (movement.stance() != logged_stance || movement.sprinting() != logged_sprint)
		{
			static const char* const stances[] = {"standing", "crouch", "low_crouch"};
			char line[104];
			snprintf(line, sizeof(line), "debug_player_posture stance=%s sprint=%u endurance=%.2f\n",
				stances[static_cast<unsigned>(movement.stance())], static_cast<unsigned>(movement.sprinting()), movement.endurance());
			write_boot_log(g_boot_log, line);
			logged_stance = movement.stance();
			logged_sprint = movement.sprinting();
		}
        for(unsigned i=0;i<6;++i)if(guards_placed[i]&&!guard_health[i]){
            const auto&c=guard_transforms[i].c;loot_world.body(i,{c.x,c.y,c.z},g_zone);}
        for(unsigned i=0;i<3;++i)if(scout_ready[i]&&!battle.scouts[i].health){
            const auto&c=scout_transforms[i].c;loot_world.body(6+i,{c.x,c.y,c.z},g_zone);}
        if(player_health&&player_view&&!menu_consumed_input&&!dialog_consumed_input)
        {
            survival_seconds+=frame_seconds;
            while(survival_seconds>=4.f){inventory.survival_tick(player_health);survival_seconds-=4.f;}
        }

        if((g_zone==1||g_zone==2)&&farm_cleared)campaign_story=std::max(campaign_story,g_zone==1?2u:4u);

        campaign_story=vita_stash_progress(g_zone,campaign_story,{moving_player_eye[0],ground_y,moving_player_eye[2]});
        const VitaZoneExit*near_exit=nullptr;
        for(const auto&exit:vita_zone_exits)
            if(vita_exit_near(exit,g_zone,{moving_player_eye[0],ground_y,moving_player_eye[2]}))near_exit=&exit;
        if(near_exit&&interact_pressed&&!dialog_consumed_input&&!any_dialog&&grounded&&!jump.active()&&
            !reload_deadline&&!grenade_pending&&now>=transition_retry_after&&zone_confirmation_ui_ready&&
            std::none_of(grenades.bodies.begin(),grenades.bodies.end(),[](const VitaGrenadeBody&g){return g.active;})&&
            vita_exit_allowed(*near_exit,campaign_story)){
            if(zone_confirmation.open(unsigned(near_exit-vita_zone_exits.data()))){
                pause_started=now;movement.cancel_input();
                char log[100];snprintf(log,sizeof(log),"debug_zone_confirmation opened=1 from=%u to=%u\n",near_exit->from,near_exit->to);write_boot_log(g_boot_log,log);
            }
        }
        if(zone_transition_confirmed&&zone_exit_choice<vita_zone_exits.size()){
            const auto&exit=vita_zone_exits[zone_exit_choice];
            transition_retry_after=now+2000000;
            VitaSaveState destination;
            if(vita_exit_near(exit,g_zone,{moving_player_eye[0],ground_y,moving_player_eye[2]})&&
                vita_exit_allowed(exit,campaign_story)&&change_vita_zone(capture_save(),exit,destination)){
                game_audio.stop_speech();
                // A confirmed transition retains the original checkpoint and loading screen.
                postprocess.finish();
                relaunch_vita_state(graphics,destination);
                save_menu_open=true;save_message=VitaSaveMessage::LoadFailed;
            }else write_boot_log(g_boot_log,"debug_zone_confirmation invalidated=1\n");
        }

        if(loot_visual_ready&&player_view&&!menu_open&&!isolate_terrain)
        {
            float loot_center[3];
            compute_player_view_center(moving_player_eye,player_view_scale,view_yaw,view_pitch,loot_center);
            glDepthMask(GL_FALSE);
            for(const auto& c:loot_world.containers)
            {
                if(c.kind<2||c.empty()||std::hypot(c.position[0]-moving_player_eye[0],c.position[2]-moving_player_eye[2])>20.f)continue;
                unsigned item=0;while(item<vita_item_count&&!c.items[item])++item;
                if(item==vita_item_count)continue;
                Fmatrix transform;transform.setXYZ(0,-view_yaw,0);
                transform.c.set(c.position[0],c.position[1]+.02f,c.position[2]);
                debug_renderer.set_geometry_view(loot_center,player_view_scale,view_yaw,view_pitch,transform.m[0],true,false,world_projection);
                debug_renderer.draw_geometry_range(loot_vertices,loot_indices,0,4,0,6,inventory_ui.icons[item].handle());
            }
            glDepthMask(GL_TRUE);
        }
        if(player_view&&!menu_open&&!isolate_terrain){
            float center[3];compute_player_view_center(moving_player_eye,player_view_scale,view_yaw,view_pitch,center);
            for(const auto&g:grenades.bodies)if(g.active&&grenade_world_ready[g.type]){
                Fmatrix transform;transform.setXYZ(g.age*3,g.age*5,0);transform.c.set(g.position[0],g.position[1],g.position[2]);
                debug_renderer.set_geometry_view(center,player_view_scale,view_yaw,view_pitch,transform.m[0],true,false,world_projection);
                for(const auto&part:grenade_world[g.type])if(part.vertices.vertex_count())
                    debug_renderer.draw_geometry_range(part.vertices,part.indices,0,part.vertices.vertex_count(),0,part.indices.index_count(),part.texture.handle());
            }
            glDepthMask(GL_FALSE);glBlendFunc(GL_SRC_ALPHA,GL_ONE);
            for(const auto&flash:explosion_flashes)if(flash.time&&now-flash.time<2133333){
                const unsigned frame=std::min(63u,unsigned((now-flash.time)*30/1000000));
                Fmatrix transform;transform.setXYZ(0,-view_yaw,0);transform.i.mul(10.f);transform.j.mul(10.f);
                transform.c.set(flash.position[0],flash.position[1]-.1f,flash.position[2]);
                debug_renderer.set_geometry_view(center,player_view_scale,view_yaw,view_pitch,transform.m[0],true,false,world_projection);
                if(loot_visual_ready&&explosion_frames[frame].handle())
                    debug_renderer.draw_geometry_range(loot_vertices,loot_indices,0,4,0,6,explosion_frames[frame].handle());
            }
            glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glDepthMask(GL_TRUE);
        }
        int target_relation=0;unsigned target_name=5;
        if(player_view&&player_health&&!menu_consumed_input&&!dialog_consumed_input)
        {
            const float direction[]={sinf(view_pitch)*sinf(view_yaw),-cosf(view_pitch),sinf(view_pitch)*cosf(view_yaw)};
            float closest=65;
            const auto offer=[&](const Fvector&feet,int relation,unsigned name){
                const float hit=guard_ray_distance(moving_player_eye,direction,feet,closest);
                if(hit<closest){closest=hit;target_relation=relation;target_name=name;}
            };
            if(wolf_visible)offer(wolf_spawn_transform.c,1,1);
            if(scouts_visible)for(unsigned i=0;i<3;++i)if(scout_ready[i]&&battle.scouts[i].health)offer(scout_transforms[i].c,1,i?5:2);
            if(!g_zone&&allegro_model_ready&&scout_met)offer(allegro_transform.c,1,3);
            if(trader_nearby)offer(trader_transform.c,1,0);
            if(outdoor_player&&scout_met)for(unsigned i=0;i<6;++i)if(guards_placed[i]&&guard_health[i])offer(guard_transforms[i].c,-1,4);
            if(g_zone==1&&road_encounter.ready)for(unsigned i=0;i<4;++i){
                const auto&a=i?road_encounter.battle.guards[i-1]:road_encounter.battle.scouts[0];if(!a.health)continue;
                Fvector feet;feet.set(a.position[0],a.position[1],a.position[2]);offer(feet,i?-1:1,i?4:5);
            }
            if(target_relation&&player_ground.blocks_ray(moving_player_eye,direction,closest))target_relation=0;
        }
		const auto current_flash = inventory.flash_status();
		postprocess.finish();
		const bool dialog_open = zone_dialog_active || trader_dialog_open || wolf_dialog_open || scout_dialog_open || allegro_dialog_open || flash_dialog_open || campaign_open || exchange_open();
		if (!inventory_open && !save_menu_open && player_view && !isolate_terrain && !dialog_open)
		{
			const auto draw_weapon = [&](auto& parts, size_t frame, const float center[3]) {
				glClear(GL_DEPTH_BUFFER_BIT);
				debug_renderer.set_geometry_view(center, 16.f, 0.f, 1.5707963f, nullptr, false, false, 0.63f);
                for(auto& part:parts)
                {
                    if(!part.vertices.vertex_count())continue;
                    part.vertices.select_position_frame(frame);
                    if(part.body_visible)debug_renderer.draw_geometry_range(part.vertices,part.indices,0,part.vertices.vertex_count(),0,part.indices.index_count(),part.texture.handle());
                    for(unsigned a=0;a<2;++a)if(part.addon_indices[a].index_count()&&inventory.attachment(a==0?1:4))
                        debug_renderer.draw_geometry_range(part.vertices,part.addon_indices[a],0,part.vertices.vertex_count(),0,part.addon_indices[a].index_count(),part.texture.handle());
				}
			};
			const SceUInt64 action_age = weapon_action_time ? now - weapon_action_time : knife_motion_us;
			if (!pda_open && player_health && pistol)
			{
				size_t frame = 0;
				const bool shot_active = shoot_clip && action_age < shoot_clip->duration_us();
				const float recoil = shoot_clip && !shoot_clip->source_frames && action_age < 180000 && !reload_deadline
					? 1.f - static_cast<float>(action_age) / 180000.f : 0.f;
				float center[] = {0.f, -0.025f * recoil, 0.5f + 0.03f * recoil};
				if (reload_deadline)
				{
					frame = reload_clip->frame_at(now - reload_started);
					if (!reload_clip->source_frames)
						center[1] += 0.13f;
				}
				else
				{
					if (shot_active)
						frame = shoot_clip->frame_at(action_age);
					if (aiming)
					{
						if (!shot_active)
							frame = active_clips[size_t(VitaWeaponClip::Aim)].frame_at(0);
						// Original PM HUD zoom_offset, applied relative to the weapon camera.
                        const bool shotgun=inventory.weapon()==VitaWeapon::Shotgun,rifle=inventory.weapon()==VitaWeapon::Rifle;
                        center[0]+=shotgun?.1679f:rifle?.1124f:.1636f;
                        center[1]-=shotgun?.06656f:rifle?.0168f:.0668f;
                        center[2]+=rifle?.115f:shotgun?0.f:.06f;
					}
				}
				if(aiming&&inventory.scoped()&&inventory_ui.scope.handle())inventory_ui.draw_scope();
                else if(inventory.weapon()==VitaWeapon::Pistol)draw_weapon(pm_hud,frame,center);else draw_weapon(primary_hud,frame,center);
			}
			else if(!pda_open&&player_health&&grenade_hud_ready&&(inventory.weapon()==VitaWeapon::Grenade||
                (grenade_throw_started&&now-grenade_throw_started<throw_duration))){
                const float center[]={0,0,.5f};
                const SceUInt64 age=grenade_throw_started?now-grenade_throw_started:throw_duration;
                const size_t frame=age<throw_begin.duration_us()?throw_begin.frame_at(age):
                    age<throw_duration?throw_act.frame_at(age-throw_begin.duration_us()):0;
                draw_weapon(grenade_hud,frame,center);
            }
            else if (!pda_open && player_health && knife)
			{
				size_t frame = 0;
				if (action_age < knife_windup_us)
					frame = knife_windup.frame_at(action_age);
				else if (action_age < knife_motion_us)
					frame = knife_strike.frame_at(action_age - knife_windup_us);
				const float center[] = {0.f, 0.f, 0.5f};
				draw_weapon(knife_hud, frame, center);
			}
			const bool outside = outdoor_player;
			VitaHudObjective objective = VitaHudObjective::None;
			const float* marker = nullptr;
			if (current_flash == VitaFlashStatus::Delivered)
				objective = VitaHudObjective::FlashDelivered;
			else if (current_flash == VitaFlashStatus::Carrying)
			{
				objective = VitaHudObjective::ReturnFlash;
				marker = trader_spawn_position;
			}
			else if (bunker_view && !outside && trader_ui_ready)
				objective = door_unlocked ? VitaHudObjective::LeaveBunker : VitaHudObjective::TalkToTrader;
			else if (outside && door_open_logged && wolf_ui_ready)
			{
				if (!wolf_mission_accepted)
				{
					objective = VitaHudObjective::TalkToWolf;
					marker = wolf_spawn.position;
				}
				else if (!scout_met && scout_ui_ready)
				{
					objective = VitaHudObjective::MeetScouts;
					marker = scout_positions[0].data();
				}
				else
				{
					objective = farm_cleared && flash_ui_ready ? VitaHudObjective::TalkToAllegro : VitaHudObjective::HelpAllegro;
					if (objective == VitaHudObjective::TalkToAllegro)
						marker = allegro_point.data();
					else if (last_bandit < guards_placed.size())
					{
						objective = VitaHudObjective::LastBandit;
						marker = assault_paths.guards[last_bandit].data();
					}
					else if (assault_paths.ready)
						marker = assault_paths.farm_bounds.data();
				}
			}
            if(campaign_story||g_zone){
                objective=VitaHudObjective::None;marker=nullptr;
                if(campaign_story>=7){if(g_zone==3)marker=vita_zone_exits[5].center.data();}
                else if(g_zone==0)marker=vita_zone_exits[0].center.data();
                else if(g_zone==1&&campaign_story>=3)marker=vita_zone_exits[2].center.data();
                else if(g_zone==2&&campaign_story>=5)marker=vita_zone_exits[4].center.data();
                else if(g_zone==3)marker=campaign_story>=7?vita_zone_exits[5].center.data():
                    campaign_story>=6?vita_strelok_flash.data():vita_strelok_ladder_bottom.data();
                else if(farm_cleared)marker=&scout_transforms[0].c.x;
                else for(unsigned i=0;i<6;++i)if(guard_health[i]){marker=&guard_transforms[i].c.x;break;}
                const unsigned key=g_zone*16+campaign_story+(farm_cleared?8:0);
                if(key!=last_campaign_key){
                    const char*caption=campaign_story>=7?vita_text("Trouver Spectre","Find Ghost"):!g_zone?vita_text("Rejoindre la D\xe9" "charge","Reach the Garbage"):
                        g_zone==1?(campaign_story>=3?vita_text("Rejoindre Agroprom","Reach Agroprom"):
                         farm_cleared?vita_text("Parler \xe0 Seriy","Talk to Seriy"):vita_text("D\xe9" "fendre le hangar","Defend the hangar")):
                        campaign_story>=7?vita_text("Trouver Spectre","Find Ghost"):
                        campaign_story>=6?vita_text("Trouver les informations de Strelok","Find Strelok's information"):
                        campaign_story>=5?vita_text("Trouver la planque de Strelok","Find Strelok's stash"):
                         farm_cleared?vita_text("Parler \xe0 la Taupe","Talk to Mole"):vita_text("Aider la Taupe","Help Mole");
                    trader_ui.set_campaign_objective(caption);
                    char trace[100];snprintf(trace,sizeof(trace),"debug_campaign zone=%u story=%u cleared=%u\n",g_zone,campaign_story,unsigned(farm_cleared));
                    write_boot_log(g_boot_log,trace);last_campaign_key=key;campaign_visible_until=now+objective_duration_us;
                }
                if(!pda_open&&!save_consumed_input&&(now<campaign_visible_until||start_pressed))trader_ui.draw_campaign_objective();
            }
			if (pda_open)
				hud.draw_pda(moving_player_eye, view_yaw, marker);
			else if (hud_ready)
				{hud.hud_scale=settings.hud_scale/100.f;hud.draw(moving_player_eye, view_yaw, outside, marker, movement, player_health, !aiming,target_relation);}
            if(target_relation>0&&!pda_open){if(g_zone&&target_name==2)trader_ui.draw_campaign_target();else trader_ui.draw_target(target_name);}
			if (!pda_open && inventory_ready && inventory.firearm())
				inventory_ui.draw_weapon_ammo(inventory, reload_deadline != 0,settings.hud_scale/100.f);
			if (objective != last_hud_objective)
			{
				objective_visible_until = now + objective_duration_us;
				static const char* const names[] = {"none", "trader", "bunker_exit", "wolf", "scouts", "allegro", "talk_allegro", "return_flash", "flash_delivered", "last_bandit"};
				static_assert(sizeof(names) / sizeof(names[0]) == static_cast<unsigned>(VitaHudObjective::Count));
				char line[112];
				snprintf(line, sizeof(line), "debug_hud objective=%s map=%u marker=%u\n",
					names[static_cast<unsigned>(objective)], static_cast<unsigned>(outside && hud.map.handle()),
					static_cast<unsigned>(marker != nullptr));
				write_boot_log(g_boot_log, line);
				last_hud_objective = objective;
			}
			if (trader_ui_ready && !save_consumed_input && (now < objective_visible_until || start_pressed))
				trader_ui.draw_objective(objective);
		}
		trader_ui.show_prompts=settings.hints!=0;
		if (!save_consumed_input)
		{
            if(campaign_open)trader_ui.draw_campaign(true);
            else if((road_focused||campaign_focused)&&!any_dialog&&!pda_open&&!inventory_open)trader_ui.draw_campaign(false);
            else if(exchange_open())
                inventory_ui.draw_exchange(inventory,loot_open>=0?&loot_world.containers[loot_open]:nullptr,
                    trader_stock,trade_basket,exchange_buying,exchange_selection,exchange_multiplier,exchange_result);
            else if (inventory_open)
                inventory_ui.draw(inventory, player_health);
			else if (!pda_open && (allegro_dialog_open || (allegro_focused && !dialog_open && !dialog_consumed_input)))
				trader_ui.draw_flash(true, allegro_dialog_open, allegro_page);
			else if (!pda_open && (flash_dialog_open || (flash_focused && !dialog_open && !dialog_consumed_input)))
				trader_ui.draw_flash(false, flash_dialog_open, flash_page);
			else if (!pda_open && scout_ui_ready && (scout_dialog_open || (scout_focused && !wolf_dialog_open && !trader_dialog_open)))
				trader_ui.draw_scout(scout_dialog_open, scout_page, scout_choice);
			else if (!pda_open && wolf_ui_ready && (wolf_dialog_open || (wolf_focused && !trader_dialog_open)))
				trader_ui.draw_wolf(wolf_dialog_open, wolf_page, wolf_choice);
			else if (!pda_open && trader_ui_ready && (trader_dialog_open || (bunker_view && ((trader_focused && current_flash != VitaFlashStatus::Carrying) || (door_focused && !door_open_start)))))
			{
				const VitaDoorPrompt door_prompt = !trader_focused && door_focused
					? (door_unlocked ? VitaDoorPrompt::Open : VitaDoorPrompt::Locked) : VitaDoorPrompt::None;
				trader_ui.draw(trader_dialog_open, trader_choice, trader_confirmed, door_prompt);
			}
		}
        if(stash_action>=0&&trader_ui_ready&&!any_dialog&&!inventory_open&&!pda_open&&!save_consumed_input)
            trader_ui.draw_stash_prompt(unsigned(stash_action));
        if(near_exit&&!zone_confirmation.active()&&!save_consumed_input&&!inventory_open&&!pda_open&&!any_dialog&&!exchange_open())
            trader_ui.draw_zone_exit(unsigned(near_exit-vita_zone_exits.data()),vita_exit_allowed(*near_exit,campaign_story));
        if(settings.hints&&inventory_ready&&!save_consumed_input&&!inventory_open&&!pda_open&&!exchange_open()&&!any_dialog)
        {
            if(focused_loot>=0)inventory_ui.draw_hint(loot_world.containers[focused_loot].kind==1?0:1);
            
        }
        if (!menu_open && !inventory_open && !exchange_open() && inventory_ready && player_view && (!player_health || (player_hit_time && now - player_hit_time < 300000)))
		{
			const float strength = player_health ? 1.f - static_cast<float>(now - player_hit_time) / 300000.f : 1.f;
			inventory_ui.draw_damage(strength, player_health == 0);
		}
        if(zone_confirmation.active())
            trader_ui.draw_zone_confirmation(vita_zone_exits[zone_confirmation.exit()].to);
		if (save_menu_open && pause_resources_active)
			pause_menu.draw(graphics, save_choice, true, false, true, player_health == 0, save_message);
		const SceUInt64 draw_end = sceKernelGetProcessTimeWide();
		if (!menu_open && player_view && !isolate_terrain)
		{
			// A capture records the mode actually submitted, including a missing-LOD fallback.
			glEnable(GL_SCISSOR_TEST);
			glScissor(930, 514, 18, 18);
			switch (lod_capture_mode)
			{
			case LodCaptureMode::AlphaThreshold: glClearColor(0.20f, 1.f, 0.22f, 1.f); break;
			case LodCaptureMode::Blend: glClearColor(0.12f, 0.9f, 1.f, 1.f); break;
			case LodCaptureMode::Geometry:
				if (terrain_coverage)
					glClearColor(1.f, 0.12f, 0.8f, 1.f);
				else if (outdoor_player && !sky_enabled)
					glClearColor(0.58f, 0.30f, 1.f, 1.f);
				else if (outdoor_player && !foliage_cutout_enabled)
					glClearColor(1.f, 1.f, 1.f, 1.f);
				else if (vsync_enabled)
					glClearColor(1.f, 0.64f, 0.1f, 1.f);
				else
					glClearColor(0.25f, 0.40f, 1.f, 1.f);
				break;
			}
			glClear(GL_COLOR_BUFFER_BIT);
			glDisable(GL_SCISSOR_TEST);
		}
		const SceUInt64 present_start = sceKernelGetProcessTimeWide();
		if (player_view)
		{
			g_present_timing = {};
			g_measured_present_thread.store(render_thread);
		}
		graphics.present();
		const SceUInt64 present_end = sceKernelGetProcessTimeWide();
		if (player_view)
			g_measured_present_thread.store(-1);
		const SceUInt64 frame_us = present_end - now - frame_modal_us;
		previous_present_end = present_end;
		const int render_mode = outdoor_player && !menu_consumed_input ? (sky_drawn ? 1 : 0) + (foliage_cutout_enabled ? 2 : 0) : -1;
		if (render_mode != measured_render_mode)
		{
			measured_render_mode = render_mode;
			render_sample_frames = 0;
			render_sample_over_budget = 0;
			render_sample_us = 0;
		}
		else if (render_mode >= 0)
		{
			++render_sample_frames;
			render_sample_us += interval_us;
			render_sample_over_budget += interval_us > 33333;
			if (render_sample_frames == 120)
			{
				char line[160];
				snprintf(line, sizeof(line),
					"debug_player_render_sample frame=%u sky=%u foliage_cutout=%u mean_us=%u over_30fps_budget=%u frames=120\n",
					frame_number, static_cast<unsigned>(sky_drawn), static_cast<unsigned>(foliage_cutout_enabled),
					static_cast<unsigned>(render_sample_us / render_sample_frames), render_sample_over_budget);
				write_boot_log(g_boot_log, line);
				render_sample_frames = 0;
				render_sample_over_budget = 0;
				render_sample_us = 0;
			}
		}
		if (player_view)
		{
			const bool new_peak = frame_us > peak_frame_us || interval_us > peak_interval_us;
			peak_frame_us = std::max(peak_frame_us, frame_us);
			peak_interval_us = std::max(peak_interval_us, interval_us);
			VSyncRunStats& stats = vsync_stats[vsync_enabled];
			++stats.frames;
			stats.total_us += frame_us;
			stats.peak_us = std::max(stats.peak_us, frame_us);
			stats.queue_total_us += g_present_timing.queue_us;
			stats.queue_peak_us = std::max(stats.queue_peak_us, g_present_timing.queue_us);
			const bool long_frame = frame_us >= 50000;
			if (long_frame)
			{
				++long_frames;
				++stats.long_frames;
			}
			if ((long_frame && (new_peak || long_frames <= 32 || long_frames % 32 == 0 ||
				frame_us >= 66667)) || gap_us >= 50000 ||
				(new_peak && interval_us >= 50000))
			{
				char line[1024];
				snprintf(line, sizeof(line),
					"debug_player_long_frame frame=%u x=%.2f z=%.2f yaw_deg=%.1f total_us=%llu"
					" select_us=%llu load_us=%llu present_us=%llu input_us=%llu begin_us=%llu"
					" prepare_us=%llu draw_us=%llu marker_us=%llu gap_us=%llu interval_us=%llu"
					" meshes=%u indices=%u mobile=%u view_us=%llu state_us=%llu indexed_us=%llu draw_log_us=%llu"
					" slowest_visual=%u slowest_mesh_us=%llu dds_paused=%u dds_state=%u dds_request_frame=%u"
					" present_end_scene_us=%llu present_queue_us=%llu present_sema_us=%llu vsync=%u\n",
					frame_number, moving_player_eye[0], moving_player_eye[2], view_yaw * 57.2957795f,
					static_cast<unsigned long long>(frame_us), static_cast<unsigned long long>(frame_selection_us),
					static_cast<unsigned long long>(frame_load_us), static_cast<unsigned long long>(present_end - present_start),
					static_cast<unsigned long long>(begin_start - now - frame_modal_us), static_cast<unsigned long long>(begin_end - begin_start),
					static_cast<unsigned long long>(draw_start - begin_end), static_cast<unsigned long long>(draw_end - draw_start),
					static_cast<unsigned long long>(present_start - draw_end), static_cast<unsigned long long>(gap_us),
					static_cast<unsigned long long>(interval_us), meshes_drawn, static_cast<unsigned>(indices_drawn),
					static_cast<unsigned>(mobile_view),
					static_cast<unsigned long long>(view_us), static_cast<unsigned long long>(state_us),
					static_cast<unsigned long long>(indexed_us), static_cast<unsigned long long>(draw_log_us),
					slowest_visual, static_cast<unsigned long long>(slowest_mesh_us),
					static_cast<unsigned>(dds_paused), static_cast<unsigned>(dds_draw_state),
					dds_draw_state == MobileDDSJob::Idle ? 0 : mobile_dds.frame,
					static_cast<unsigned long long>(g_present_timing.end_scene_us),
					static_cast<unsigned long long>(g_present_timing.queue_us),
					static_cast<unsigned long long>(g_present_timing.sema_us),
					static_cast<unsigned>(vsync_enabled));
				write_boot_log(g_boot_log, line);
			}
		}

		if (quit_requested || quit_from_menu)
			break;
	}

	if (mobile_dds_worker >= 0)
	{
		mobile_dds.stopping.store(true);
		sceKernelSignalSema(mobile_dds.wake, 1);
		sceKernelWaitThreadEnd(mobile_dds_worker, nullptr, nullptr);
		sceKernelDeleteThread(mobile_dds_worker);
	}
	if (mobile_dds.wake >= 0)
		sceKernelDeleteSema(mobile_dds.wake);

	save_browser.release();
	pause_music.release();
	pause_menu.release();
	postprocess.release();
	game_audio.release();
	flush_boot_log(g_boot_log);
	if (g_boot_log >= 0)
	{
		char line[200];
		snprintf(line, sizeof(line), "debug_player_visibility_end peak_meshes=%u peak_indices=%u peak_select_us=%llu\n",
		mobile_visible_peak, mobile_indices_peak, static_cast<unsigned long long>(mobile_select_peak_us));
	write_boot_log(g_boot_log, line);
	snprintf(line, sizeof(line), "debug_player_dynamic_end updates=%u texture_bytes=%u load_batches=%u load_mean_us=%u load_peak_us=%u\n",
			selection_updates, static_cast<unsigned>(texture_memory_used), mobile_texture_batches,
			static_cast<unsigned>(mobile_texture_batches ? mobile_texture_total_us / mobile_texture_batches : 0),
			static_cast<unsigned>(mobile_texture_peak_us));
		write_boot_log(g_boot_log, line);
		snprintf(line, sizeof(line), "debug_player_frame_profile_end long_frames=%u peak_us=%llu peak_interval_us=%llu\n",
			long_frames, static_cast<unsigned long long>(peak_frame_us),
			static_cast<unsigned long long>(peak_interval_us));
		write_boot_log(g_boot_log, line);
		for (unsigned mode = 0; mode < 2; ++mode)
		{
			const VSyncRunStats& stats = vsync_stats[mode];
			if (stats.frames == 0)
				continue;
			snprintf(line, sizeof(line),
				"debug_player_vsync_end enabled=%u frames=%u long_frames=%u mean_us=%llu peak_us=%llu queue_mean_us=%llu queue_peak_us=%llu\n",
				mode, stats.frames, stats.long_frames,
				static_cast<unsigned long long>(stats.total_us / stats.frames),
				static_cast<unsigned long long>(stats.peak_us),
				static_cast<unsigned long long>(stats.queue_total_us / stats.frames),
				static_cast<unsigned long long>(stats.queue_peak_us));
			write_boot_log(g_boot_log, line);
		}
		snprintf(line, sizeof(line),
			"debug_player_ground_end hits=%u misses=%u blocked=%u peak_query_us=%llu\n",
			ground_hits, ground_misses, ground_blocked, static_cast<unsigned long long>(ground_peak_us));
		write_boot_log(g_boot_log, line);
		snprintf(line, sizeof(line), "debug_player_wall_end contacts=%u stops=%u peak_query_us=%llu\n",
			wall_contacts, wall_stops, static_cast<unsigned long long>(wall_peak_us));
		write_boot_log(g_boot_log, line);
		snprintf(line, sizeof(line), "debug_player_base_texture_cache reused=%u unique_loaded=%u\n",
			base_texture_reuses, texture_loaded_count);
		write_boot_log(g_boot_log, line);
		snprintf(line, sizeof(line), "debug_player_dds_worker_end completed=%u peak_upload_us=%llu budget_refusals=%u fallback_logs=%u remaining_bytes=%u\n",
			async_dds_completed, static_cast<unsigned long long>(async_dds_peak_upload_us), async_dds_budget_refusals,
			texture_fallbacks_logged, static_cast<unsigned>(mobile_texture_limit - texture_memory_used));
		write_boot_log(g_boot_log, line);
		snprintf(line, sizeof(line), "debug_player_texture_cache_end evictions=%u budget_retries=%u cancelled=%u peak_mobile_bytes=%u pinned_bytes=%u\n",
			mobile_texture_evictions, mobile_texture_retries, mobile_texture_cancellations,
			static_cast<unsigned>(mobile_texture_peak_bytes), static_cast<unsigned>(pinned_texture_bytes));
		write_boot_log(g_boot_log, line);
		sceIoClose(g_boot_log);
		g_boot_log = -1;
	}
	for (PlayerLodDraw& lod : player_lod_draws)
	{
		lod.vertices->release();
		if (lod.original_mip_vertices)
			lod.original_mip_vertices->release();
		if (lod.alpha_vertices)
			lod.alpha_vertices->release();
	}
	lod_original_mip.release();
	lod_indices.release();
	lod_atlas.release();
	level_buffers.release();
	for (size_t i = 0; i < level_textures.size(); ++i)
		if (level_textures[i])
			level_textures[i]->release();
	for (size_t i = 0; i < loaded_lightmap_textures.size(); ++i)
		if (loaded_lightmap_textures[i])
			loaded_lightmap_textures[i]->release();
	for (VitaGLTexture& detail : terrain_detail_textures)
		detail.release();
	for(auto&part:strelok_item){part.texture.release();part.indices.release();part.vertices.release();}
	for (auto& weapon : npc_weapons)
		for (auto& part : weapon)
		{
			part.texture.release();
			part.indices.release();
			part.vertices.release();
		}
	for (auto& part : allegro_visual)
	{
		part.texture.release();
		part.indices.release();
		part.vertices.release();
	}
	for (auto& part : guard_visual)
	{
		part.texture.release();
		part.indices.release();
		part.vertices.release();
	}
	for (auto& scout : scout_visuals)
		for (auto& part : scout)
		{
			part.texture.release();
			part.indices.release();
			part.vertices.release();
		}
	for (auto& part : wolf_visuals)
	{
		part.texture.release();
		part.indices.release();
		part.vertices.release();
	}
	door_texture.release();
	door_indices.release();
	door_vertices.release();
	trader_texture.release();
	trader_indices.release();
	trader_vertices.release();
	if (rear_sprint_ready)
		sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_STOP);
	hud.release();
	for(auto&kind:grenade_world)for(auto&part:kind){part.vertices.release();part.indices.release();part.texture.release();}
    for(auto&frame:explosion_frames)frame.release();
	release_hud(pm_hud);release_hud(knife_hud);release_hud(primary_hud);release_hud(grenade_hud);
	loot_vertices.release();loot_indices.release();
	inventory_ui.release();
	trader_ui.release();
	cordon_sky.release();
	debug_renderer.release();
	Core._destroy();
	sceKernelExitProcess(0);
	return 0;
}
