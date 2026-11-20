#include "vita_trader_model.h"
#include "xrCore.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>

namespace
{
struct Chunk
{
	const u8* data = nullptr;
	size_t size = 0;

	bool skip(size_t bytes)
	{
		if (bytes > size)
			return false;
		data += bytes;
		size -= bytes;
		return true;
	}

	template <class T>
	bool read(T& value)
	{
		if (sizeof(value) > size)
			return false;
		memcpy(&value, data, sizeof(value));
		return skip(sizeof(value));
	}

	bool read_string(std::string_view& text)
	{
		if (!size)
			return false;
		const auto* end = static_cast<const u8*>(memchr(data, 0, size));
		if (!end)
			return false;
		text = {reinterpret_cast<const char*>(data), static_cast<size_t>(end - data)};
		return skip(text.size() + 1);
	}

	Chunk next(u32& id)
	{
		u32 bytes;
		if (!read(id) || !read(bytes) || bytes > size)
			return {};
		const Chunk result{data, bytes};
		skip(bytes);
		return result;
	}
};

static Chunk find_chunk(Chunk container, u32 wanted)
{
	u32 id;
	for (Chunk chunk; (chunk = container.next(id)).data;)
		if (id == wanted)
			return chunk;
	return {};
}

constexpr size_t max_bones = 64;
constexpr u32 no_bone = max_bones;

struct TraderBone
{
	std::string_view name;
	u32 parent;
	Fmatrix inverse_bind;
	Fmatrix pose;
	Fmatrix skin;
};

static bool finite(const Fvector& vector)
{
	return std::isfinite(vector.x) && std::isfinite(vector.y) && std::isfinite(vector.z);
}

static bool read_bind_pose(Chunk model, TraderBone (&bones)[max_bones], u32& count)
{
	auto names = find_chunk(model, 13); // OGF_S_BONE_NAMES
	auto ik = find_chunk(model, 16);    // OGF_S_IKDATA
	if (!names.read(count) || count == 0 || count > max_bones)
		return false;

	Fmatrix bind[max_bones];
	for (u32 i = 0; i < count; ++i)
	{
		auto& bone = bones[i];
		std::string_view parent, material;
		u32 ik_version;
		Fvector angles, translation;
		// OBB (60 bytes), collision shape (112), IK parameters (72 + optional friction).
		if (!names.read_string(bone.name) || bone.name.empty() || !names.read_string(parent) || !names.skip(60) ||
			!ik.read(ik_version) || ik_version > 1 || !ik.read_string(material) ||
			!ik.skip(112 + 72 + (ik_version ? 4 : 0)) ||
			!ik.read(angles) || !ik.read(translation) || !ik.skip(16) ||
			!finite(angles) || !finite(translation))
			return false;

		Fmatrix local;
		local.setXYZi(angles);
		local.c.set(translation);
		if (i == 0 && parent.empty())
		{
			bone.parent = no_bone;
			bind[i] = local;
		}
		else
		{
			const auto* found = std::find_if(bones, bones + i, [&](const TraderBone& candidate) {
				return candidate.name == parent;
			});
			if (found == bones + i)
				return false;
			bone.parent = static_cast<u32>(found - bones);
			bind[i].mul_43(bind[bone.parent], local);
		}
		bone.inverse_bind.invert(bind[i]);
	}
	return true;
}

static bool read_track_order(Chunk params, const TraderBone* bones, u32 count, u32 (&order)[max_bones])
{
	u16 version, partitions;
	if (!params.read(version) || version != 3 || !params.read(partitions) || partitions == 0 || partitions > 4)
		return false;

	std::fill(order, order + count, no_bone);
	bool mapped[max_bones] = {};
	u32 total = 0;
	for (u16 part = 0; part < partitions; ++part)
	{
		std::string_view name;
		u16 tracks;
		if (!params.read_string(name) || !params.read(tracks) || tracks > count - total)
			return false;
		total += tracks;
		for (u16 i = 0; i < tracks; ++i)
		{
			u32 slot;
			if (!params.read_string(name) || !params.read(slot) || slot >= count || order[slot] != no_bone)
				return false;
			const auto* bone = std::find_if(bones, bones + count, [&](const TraderBone& candidate) {
				return candidate.name == name;
			});
			const auto index = static_cast<u32>(bone - bones);
			if (index == count || mapped[index])
				return false;
			mapped[index] = true;
			order[slot] = index;
		}
	}
	return total == count;
}

static bool read_keys(Chunk& track, u32 frames, u32 step, u32 bone_count, Fmatrix* first)
{
	u8 flags;
	if (!track.read(flags) || (flags & ~3u))
		return false;
	constexpr size_t rotation_size = 4 * sizeof(s16);
	constexpr size_t translation_size = 3 * sizeof(s8);
	const bool has_rotation_keys = (flags & 2) == 0;
	const bool has_translation_keys = (flags & 1) != 0;
	if (has_rotation_keys && !track.skip(sizeof(u32)))
		return false;
	const u8* rotations = track.data;
	if (!track.skip((has_rotation_keys ? frames : 1u) * rotation_size))
		return false;

	const u8* translations = nullptr;
	Fvector scale, offset;
	if (has_translation_keys)
	{
		if (!track.skip(sizeof(u32)))
			return false;
		translations = track.data;
		if (!track.skip(frames * translation_size) || !track.read(scale))
			return false;
	}
	if (!track.read(offset))
		return false;

	constexpr float key_scale = 1.f / 32767.f;
	for (u32 frame = 0; frame < frames; frame += step)
	{
		s16 rotation[4];
		memcpy(rotation, rotations + (has_rotation_keys ? frame * rotation_size : 0), sizeof(rotation));
		Fquaternion q;
		q.x = rotation[0] * key_scale;
		q.y = rotation[1] * key_scale;
		q.z = rotation[2] * key_scale;
		q.w = rotation[3] * key_scale;
		Fvector position = offset;
		if (has_translation_keys)
		{
			s8 key[3];
			memcpy(key, translations + frame * translation_size, sizeof(key));
			position.x += key[0] * scale.x;
			position.y += key[1] * scale.y;
			position.z += key[2] * scale.z;
		}
		const float length = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
		if (!finite(position) || length < 0.98f || length > 1.02f)
			return false;
		first[(frame / step) * bone_count].mk_xform(q, position);
	}
	return true;
}

struct SkinWeights
{
	u16 bone[2];
	float second;
};

static Chunk find_named_motion(Chunk motion, std::string_view wanted)
{
	auto clips = find_chunk(motion, 14); // OGF_S_MOTIONS
	u32 id;
	for (Chunk clip; (clip = clips.next(id)).data;)
	{
		std::string_view name;
		if (id != 0 && clip.read_string(name) && name == wanted)
			return clip;
	}
	return {};
}

static bool bake_animation(Chunk model, Chunk params, Chunk clip, bool static_pose,
	const u8* source_vertices, size_t stride, std::vector<VitaTraderVertex>& vertices,
	Fvector& head, std::vector<float>& positions, bool require_head = true, u32* source_frames = nullptr, u32 animation_step = 30 / vita_trader_idle_fps, std::vector<std::array<float, 16>>* weapon_frames = nullptr, Chunk lower_pose = {}, bool lower_walk = false, size_t max_position_bytes = 5u*1024u*1024u)
{
	TraderBone bones[max_bones];
	u32 count, order[max_bones];
	if (!read_bind_pose(model, bones, count) || !read_track_order(params, bones, count, order))
		return false;
	u32 frames;
	// Sample the original 30 Hz clip at 15 Hz to limit the position buffer.
	const size_t frame_floats = vertices.size() * 3;
	if (!clip.read(frames) || frames == 0 || frames > 10000)
		return false;
	const u32 step = static_pose ? frames : animation_step;
	u32 sampled_frames = (frames + step - 1) / step;
	if (sampled_frames > max_position_bytes / sizeof(float) / frame_floats)
		return false;

	std::vector<Fmatrix> local(sampled_frames * count);
	for (u32 slot = 0; slot < count; ++slot)
		if (!read_keys(clip, frames, step, count, local.data() + order[slot]))
			return false;
	if (clip.size != 0)
		return false;
	// Torso clips are animation-slot specific. Keep the full-body standing/walking legs.
	if (lower_pose.size)
	{
		u32 lower_frames;
		if (!lower_pose.read(lower_frames) || !lower_frames || lower_frames > 10000) return false;
		const u32 lower_step = lower_walk ? 1 : lower_frames;
		const u32 lower_count = lower_walk ? lower_frames : 1;
		std::vector<Fmatrix> lower(lower_count * count);
		for (u32 slot = 0; slot < count; ++slot)
			if (!read_keys(lower_pose, lower_frames, lower_step, count, lower.data() + order[slot])) return false;
		if (lower_pose.size) return false;
		if (lower_walk)
		{
			std::vector<Fmatrix> upper(local.begin(), local.begin() + count);
			sampled_frames = (lower_frames + step - 1) / step;
			if (sampled_frames > max_position_bytes / sizeof(float) / frame_floats) return false;
			local.resize(sampled_frames * count);
			for (u32 frame=0; frame<sampled_frames; ++frame)
				std::copy(upper.begin(), upper.end(), local.begin() + frame * count);
		}
		for (u32 bone = 0; bone < count; ++bone)
		{
			bool upper = false;
			for (u32 ancestor = bone; ancestor != no_bone; ancestor = bones[ancestor].parent)
				if (bones[ancestor].name == "bip01_spine") { upper = true; break; }
			if (!upper)
				for (u32 frame = 0; frame < sampled_frames; ++frame)
					local[frame * count + bone] = lower[(lower_walk ? (frame * step) % lower_count : 0) * count + bone];
		}
	}
	const auto* head_bone = std::find_if(bones, bones + count, [](const TraderBone& bone) {
		return bone.name == "bip01_head";
	});
	if (require_head && head_bone == bones + count)
		return false;

	const auto* hand = std::find_if(bones, bones + count, [](const TraderBone& b) { return b.name == "bip01_r_hand"; });
	const auto* finger = std::find_if(bones, bones + count, [](const TraderBone& b) { return b.name == "bip01_r_finger2"; });
	std::vector<std::array<float, 16>> attachments;
	if (weapon_frames && hand != bones + count && finger != bones + count)
		attachments.reserve(sampled_frames);

	std::vector<SkinWeights> weights(vertices.size());
	for (auto& weight : weights)
	{
		if (stride == 60)
		{
			u32 bone;
			memcpy(&bone, source_vertices + 56, sizeof(bone));
			if (bone >= count)
				return false;
			weight = {{static_cast<u16>(bone), static_cast<u16>(bone)}, 0.f};
		}
		else
		{
			memcpy(weight.bone, source_vertices, sizeof(weight.bone));
			memcpy(&weight.second, source_vertices + 52, sizeof(weight.second));
		}
		if (weight.bone[0] >= count || weight.bone[1] >= count || !(weight.second >= 0.f && weight.second <= 1.f))
			return false;
		source_vertices += stride;
	}

	// Prepare the complete clip before changing the bind-pose fallback mesh.
	std::vector<float> baked(frame_floats * sampled_frames);
	float* output = baked.data();
	for (u32 frame = 0; frame < sampled_frames; ++frame)
	{
		for (u32 i = 0; i < count; ++i)
		{
			auto& bone = bones[i];
			const Fmatrix& key = local[frame * count + i];
			if (bone.parent == no_bone)
				bone.pose = key;
			else
				bone.pose.mul_43(bones[bone.parent].pose, key);
			bone.skin.mul_43(bone.pose, bone.inverse_bind);
		}
		if (weapon_frames && hand != bones + count && finger != bones + count)
		{
			// Original CWeapon::UpdateXForm, hd1Hand: right hand and right finger.
			Fvector d, r, n;
			d.sub(finger->pose.c, hand->pose.c);
			if (d.square_magnitude() < 1e-12f)
				return false;
			d.normalize();
			r.crossproduct(hand->pose.j, d);
			n.crossproduct(d, r);
			if (n.square_magnitude() < 1e-12f)
				return false;
			n.normalize();
			Fmatrix attachment;
			attachment.set(r, n, d, hand->pose.c);
			attachments.emplace_back();
			memcpy(attachments.back().data(), attachment.m, sizeof(attachment.m));
		}
		if (frame == 0 && head_bone != bones + count)
			head = head_bone->pose.c;

		for (size_t i = 0; i < vertices.size(); ++i)
		{
			const auto* xyz = vertices[i].position;
			const auto& weight = weights[i];
			Fvector original, first, second;
			original.set(xyz[0], xyz[1], xyz[2]);
			bones[weight.bone[0]].skin.transform_tiny(first, original);
			if (weight.bone[0] != weight.bone[1])
			{
				bones[weight.bone[1]].skin.transform_tiny(second, original);
				original.lerp(first, second, weight.second);
			}
			else
				original = first;
			if (!finite(original))
				return false;
			*output++ = original.x;
			*output++ = original.y;
			*output++ = original.z;
		}
	}
	for (size_t i = 0; i < vertices.size(); ++i)
		memcpy(vertices[i].position, baked.data() + 3 * i, sizeof(vertices[i].position));
	positions.swap(baked);
	if (weapon_frames) weapon_frames->swap(attachments);
	if (source_frames)
		*source_frames = frames;
	return true;
}

static bool read_skin_mesh(Chunk mesh, bool two_bones, std::vector<VitaTraderVertex>& vertices,
	std::vector<u16>& indices, std::string& texture, Chunk& vertex_data)
{
	const auto header = find_chunk(mesh, 1);
	const auto material = find_chunk(mesh, 2);
	vertex_data = find_chunk(mesh, 3);
	const auto index_data = find_chunk(mesh, 4);
	const auto windows = find_chunk(mesh, 6);
	if (header.size < 2 || header.data[0] != 4 || (header.data[1] != 4 && header.data[1] != 5) ||
		vertex_data.size < 8 || index_data.size < 4 || (header.data[1] == 4 && windows.size < 28) || material.size == 0)
		return false;

	const auto* texture_end = static_cast<const u8*>(memchr(material.data, 0, material.size));
	if (!texture_end || texture_end == material.data || texture_end - material.data >= 256)
		return false;

	u32 vertex_header[2], index_count;
	memcpy(vertex_header, vertex_data.data, sizeof(vertex_header));
	memcpy(&index_count, index_data.data, sizeof(index_count));
	u32 vertex_count = vertex_header[1], first_index = 0, draw_indices = index_count;
	if (header.data[1] == 4)
	{
		// The first slide window selects the highest detail triangle list.
		u32 window_count;
		u16 triangles, vertices;
		memcpy(&window_count, windows.data + 16, sizeof(window_count));
		memcpy(&first_index, windows.data + 20, sizeof(first_index));
		memcpy(&triangles, windows.data + 24, sizeof(triangles));
		memcpy(&vertices, windows.data + 26, sizeof(vertices));
		if (window_count == 0 || window_count > (windows.size - 20) / 8)
			return false;
		vertex_count = vertices;
		draw_indices = 3u * triangles;
	}
	constexpr u32 one_bone_vertex_format = 0x12071980;
	constexpr u32 two_bone_vertex_format = 0x240e3300;
	const size_t vertex_stride = two_bones ? 64 : 60;
	const size_t position_offset = two_bones ? 4 : 0;
	const size_t uv_offset = two_bones ? 56 : 48;
	if (vertex_header[0] != (two_bones ? two_bone_vertex_format : one_bone_vertex_format) ||
		vertex_header[1] > (vertex_data.size - 8) / vertex_stride ||
		vertex_count == 0 || vertex_count > 65536 || vertex_count > vertex_header[1] ||
		index_count > (index_data.size - 4) / sizeof(u16) ||
		draw_indices == 0 || first_index > index_count || draw_indices > index_count - first_index)
		return false;

	vertices.resize(vertex_count);
	const u8* source_vertex = vertex_data.data + 8;
	for (VitaTraderVertex& vertex : vertices)
	{
		memcpy(vertex.position, source_vertex + position_offset, sizeof(vertex.position));
		memcpy(vertex.uv, source_vertex + uv_offset, sizeof(vertex.uv));
		if (!std::isfinite(vertex.position[0]) || !std::isfinite(vertex.position[1]) ||
			!std::isfinite(vertex.position[2]) || !std::isfinite(vertex.uv[0]) || !std::isfinite(vertex.uv[1]))
			return false;
		source_vertex += vertex_stride;
	}
	indices.resize(draw_indices);
	memcpy(indices.data(), index_data.data + 4 + first_index * sizeof(u16), draw_indices * sizeof(u16));
	if (std::any_of(indices.begin(), indices.end(), [vertex_count](u16 index) { return index >= vertex_count; }))
		return false;
	texture.assign(reinterpret_cast<const char*>(material.data), texture_end - material.data);
	return true;
}
}

bool read_vita_trader_model(const void* model, size_t model_size, const void* motion, size_t motion_size,
	std::vector<VitaTraderVertex>& vertices, std::vector<uint16_t>& indices, std::string& texture,
	float target[3], std::vector<float>& animation_positions)
{
	animation_positions.clear();
	const Chunk root{static_cast<const u8*>(model), model_size};
	Chunk vertex_data;
	if (!read_skin_mesh(find_chunk(find_chunk(root, 9), 0), true, vertices, indices, texture, vertex_data))
		return false;
	Fvector head;
	const Chunk animation{static_cast<const u8*>(motion), motion_size};
	if (motion_size && bake_animation(root, find_chunk(animation, 15), find_named_motion(animation, vita_trader_idle_clip),
		false, vertex_data.data + 8, 64, vertices, head, animation_positions))
	{
		target[0] = head.x;
		target[1] = head.y;
		target[2] = head.z;
	}
	return true;
}


bool read_vita_trader_door(const void* model, size_t model_size, std::vector<VitaTraderVertex>& vertices,
	std::vector<uint16_t>& indices, std::string& texture)
{
	Chunk vertex_data;
	const Chunk root{static_cast<const u8*>(model), model_size};
	return read_skin_mesh(find_chunk(find_chunk(root, 9), 0), false, vertices, indices, texture, vertex_data);
}


bool read_vita_stalker_model(const void* model, size_t model_size, const std::vector<uint8_t>& params,
	const std::vector<uint8_t>& keys, VitaStalkerMesh (&parts)[2], float target[3], bool& posed, const std::vector<uint8_t>* walk_keys, VitaStalkerWeaponPose* weapon_pose)
{
	const Chunk root{static_cast<const u8*>(model), model_size};
	const auto children = find_chunk(root, 9);
	Chunk vertices[2];
	for (u32 i = 0; i < 2; ++i)
	{
		auto& part = parts[i];
		if (!read_skin_mesh(find_chunk(children, i), true, part.vertices, part.indices, part.texture, vertices[i]))
			return false;
	}
	posed = false;
	VitaStalkerWeaponPose attachment;
	if (weapon_pose) *weapon_pose = {};
	if (walk_keys)
	{
		Chunk walk{walk_keys->data(), walk_keys->size()};
		std::string_view walk_name;
		if (walk.read_string(walk_name) && walk_name == vita_stalker_walk_clip)
		{
			for (u32 i = 0; i < 2; ++i)
			{
				auto mesh = parts[i].vertices;
				Fvector unused;
				bake_animation(root, {params.data(), params.size()}, walk, false,
					vertices[i].data + 8, 64, mesh, unused, parts[i].walk_positions, true, nullptr,
						30 / vita_stalker_walk_fps, i == 0 && weapon_pose ? &attachment.walk : nullptr);
			}
			if (parts[0].walk_positions.empty() || parts[1].walk_positions.empty())
			{
				for (auto& part : parts) part.walk_positions.clear();
				attachment.walk.clear();
			}
		}
	}
	Chunk clip{keys.data(), keys.size()};
	std::string_view name;
	if (clip.read_string(name) && (name == vita_stalker_idle_clip || name == "norm_idle_0"))
	{
		// Apply the same standing pose to both meshes, or keep both in the bind pose.
		VitaStalkerMesh posed_parts[2];
		Fvector head;
		std::vector<std::array<float, 16>> idle;
		for (u32 i = 0; i < 2; ++i)
		{
			posed_parts[i].vertices = parts[i].vertices;
			std::vector<float> positions;
			if (!bake_animation(root, {params.data(), params.size()}, clip, true,
				vertices[i].data + 8, 64, posed_parts[i].vertices, head, positions, true, nullptr,
					30 / vita_trader_idle_fps, i == 0 && weapon_pose ? &idle : nullptr))
				return true;
		}
		for (u32 i = 0; i < 2; ++i)
			parts[i].vertices.swap(posed_parts[i].vertices);
		target[0] = head.x;
		target[1] = head.y;
		target[2] = head.z;
		posed = true;
		if (weapon_pose && idle.size() == 1)
		{
			attachment.idle = idle.front();
			attachment.ready = true;
			*weapon_pose = std::move(attachment);
		}
	}
	return true;
}

bool read_vita_npc_animations(const void* model, size_t model_size, const std::vector<uint8_t>& params,
	const std::vector<uint8_t>& standing, const std::vector<uint8_t>& walking, const VitaNpcMotions& motions,
	VitaStalkerMesh (&parts)[2], VitaStalkerWeaponPose& pose, VitaNpcRanges& ranges)
{
	const Chunk root{static_cast<const u8*>(model), model_size};
	const auto children = find_chunk(root, 9);
	Chunk streams[2];
	for (u32 i = 0; i < 2; ++i) streams[i] = find_chunk(find_chunk(children, i), 3);
	std::vector<float> all[2];
	std::vector<std::array<float, 16>> attachments;
	VitaNpcRanges result{};
	const unsigned limits[] = {15,15,12,8,20,16,1};
	for (size_t slot = 0; slot < result.size(); ++slot)
	{
		const bool walk = slot == static_cast<size_t>(VitaNpcClip::Walk);
		const bool dead=slot==static_cast<size_t>(VitaNpcClip::Dead);
  const size_t source_slot = dead ? 5 : walk ? 0 : slot > 4 ? 4 : slot;
		const auto& bytes = motions[source_slot];
		Chunk clip{bytes.data(), bytes.size()}, lower{walk ? walking.data() : standing.data(), walk ? walking.size() : standing.size()};
		std::string_view name;
		u32 frames;
		if (!clip.read_string(name) || clip.size < 4 || (!dead&&!lower.read_string(name))) return false;
  if(dead)lower={};
		memcpy(&frames, clip.data, 4);
		if(dead&&frames>240)return false;
        const unsigned step = dead ? 1u : walk ? 2u : std::max(2u, (frames + limits[slot] - 1) / limits[slot]);
		std::vector<float> positions[2];
		std::vector<std::array<float, 16>> hand_frames;
		Fvector head;
		for (u32 part = 0; part < 2; ++part)
		{
			auto mesh = parts[part].vertices;
			// Always skin the original bind vertices, not the previously posed mesh.
			Chunk raw;
			std::vector<u16> unused_indices;
			std::string unused_texture;
			if (!read_skin_mesh(find_chunk(children, part), true, mesh, unused_indices, unused_texture, raw)) return false;
			if (!bake_animation(root, {params.data(),params.size()}, clip, false, streams[part].data + 8, 64,
						mesh, head, positions[part], true, nullptr, step, part == 0 ? &hand_frames : nullptr, lower, walk)) return false;
		}
        if(dead)
        {
            // The named dead_stop clip is a standing placeholder; keep the last full-body death frame.
            for(unsigned part=0;part<2;++part){
                const size_t size=parts[part].vertices.size()*3;
                if(positions[part].size()<size)return false;
                positions[part].erase(positions[part].begin(),positions[part].end()-size);
            }
            if(hand_frames.empty())return false;
            const auto last=hand_frames.back();hand_frames.assign(1,last);
        }
		const size_t count = positions[0].size() / (3 * parts[0].vertices.size());
		if (count != hand_frames.size() || !count) return false;
		// Walking legs loop at their original 30 Hz speed while the held torso breathes.
		if (walk)
		{
			Chunk walk_clip{walking.data(),walking.size()};
			walk_clip.read_string(name);
			u32 walk_frames = 0; memcpy(&walk_frames, walk_clip.data, 4);
			result[slot].fps = float(count) * 30.f / float(walk_frames);
		}
		else result[slot].fps = 30.f / step;
		result[slot].first = attachments.size() + 1;
		result[slot].count = count;
		attachments.insert(attachments.end(), hand_frames.begin(), hand_frames.end());
		for (u32 part = 0; part < 2; ++part)
		{
			if ((all[part].size() + positions[part].size()) * sizeof(float) > 4u * 1024u * 1024u) return false;
			all[part].insert(all[part].end(), positions[part].begin(), positions[part].end());
		}
	}
	for (u32 part=0;part<2;++part) parts[part].walk_positions.swap(all[part]);
	pose.walk.swap(attachments);
	pose.ready = true;
	ranges = result;
	return true;
}

bool read_vita_weapon_world(const void* model, size_t model_size, std::vector<VitaStalkerMesh>& parts)
{
	const Chunk root{static_cast<const u8*>(model), model_size};
	TraderBone bones[max_bones];
	u32 count = 0;
	if (!read_bind_pose(root, bones, count)) return false;
	bool hidden[max_bones] = {};
	for (u32 i = 0; i < count; ++i)
		hidden[i] = bones[i].name == "wpn_silencer" ||
			(bones[i].parent != no_bone && hidden[bones[i].parent]);
	auto children = find_chunk(root, 9);
	std::vector<VitaStalkerMesh> result;
	u32 id;
	for (Chunk mesh; (mesh = children.next(id)).data;)
	{
		if (result.size() >= 8) return false;
		VitaStalkerMesh part;
		Chunk stream;
		// Retail world weapons use the single-bone OGF vertex layout.
		if (!read_skin_mesh(mesh, false, part.vertices, part.indices, part.texture, stream)) return false;
		std::vector<uint16_t> visible;
		for (size_t triangle = 0; triangle + 2 < part.indices.size(); triangle += 3)
		{
			bool addon = false;
			for (size_t j = 0; j < 3; ++j)
			{
				u32 bone;
				memcpy(&bone, stream.data + 8 + part.indices[triangle + j] * 60 + 56, sizeof(bone));
				if (bone >= count) return false;
				addon = addon || hidden[bone];
			}
			if (!addon) visible.insert(visible.end(), part.indices.begin() + triangle, part.indices.begin() + triangle + 3);
		}
		part.indices.swap(visible);
		if (!part.indices.empty()) result.push_back(std::move(part));
	}
	if (result.empty()) return false;
	parts.swap(result);
	return true;
}

bool read_vita_weapon_hud(const void* model, size_t model_size, bool pistol,
	std::vector<VitaWeaponHudMesh>& parts, VitaWeaponClips& clips,unsigned kind,bool include_addons)
{
	const Chunk root{static_cast<const u8*>(model), model_size};
	const auto params = find_chunk(root, 15);
	auto children = find_chunk(root, 9);
	if (!params.size)
		return false;

	// The original PM hides the silencer bone and its descendants for the starting weapon.
	// Its addon is skinned separately from the pistol and the hands.
	bool hidden_bones[max_bones] = {};
	u32 bone_count = 0;
	unsigned bone_addon[max_bones]={};
	if (pistol||include_addons)
	{
		TraderBone bones[max_bones];
		if (!read_bind_pose(root, bones, bone_count))
			return false;
        for(u32 i=0;i<bone_count;++i){
            bone_addon[i]=bones[i].name=="wpn_silencer"?1:bones[i].name=="wpn_scope"?4:
                bones[i].name=="wpn_grenade_launcher"?8:0;
            if(bones[i].parent!=no_bone)bone_addon[i]|=bone_addon[bones[i].parent];
            hidden_bones[i]=(bone_addon[i]&1)!=0;
        }
	}

	struct SkinStream
	{
		Chunk vertices;
		size_t stride;
	};
	std::vector<SkinStream> streams;
	std::vector<VitaWeaponHudMesh> result;
	u32 id;
	for (Chunk mesh; (mesh = children.next(id)).data;)
	{
		if (result.size() == 8)
			return false;
		auto header = find_chunk(mesh, 3);
		u32 format = 0;
		if (!header.read(format))
			return false;
		const bool two_bones = format == 0x240e3300u;
		VitaWeaponHudMesh part;
		Chunk vertices;
		if (!read_skin_mesh(mesh, two_bones, part.vertices, part.indices, part.texture, vertices))
			return false;
		const size_t stride = two_bones ? 64 : 60;
		const auto is_hidden = [&](u32 bone) { return bone < bone_count && hidden_bones[bone]; };
		if (pistol && !include_addons && std::all_of(part.indices.begin(), part.indices.end(), [&](u16 index) {
			const u8* vertex = vertices.data + 8 + index * stride;
			if (!two_bones)
			{
				u32 bone;
				memcpy(&bone, vertex + 56, sizeof(bone));
				return is_hidden(bone);
			}
			SkinWeights weights;
			memcpy(weights.bone, vertex, sizeof(weights.bone));
			memcpy(&weights.second, vertex + 52, sizeof(weights.second));
			return (weights.second == 1.f || is_hidden(weights.bone[0])) &&
				(weights.second == 0.f || is_hidden(weights.bone[1]));
		}))
			continue;
        if(include_addons){
            const auto addon=[&](u16 index){
                const u8* data=vertices.data+8+index*stride;
                if(!two_bones){u32 bone;memcpy(&bone,data+56,4);return bone<bone_count?bone_addon[bone]:0u;}
                SkinWeights w;memcpy(w.bone,data,4);memcpy(&w.second,data+52,4);
                const unsigned a=bone_addon[w.bone[0]],b=bone_addon[w.bone[1]];
                return w.second==0?a:w.second==1?b:a&b;
            };
            for(size_t i=0;i<part.indices.size();i+=3)
                part.triangle_addons.push_back(addon(part.indices[i])&addon(part.indices[i+1])&addon(part.indices[i+2]));
        }
		streams.push_back({vertices, stride});
		result.push_back(std::move(part));
	}
	if (result.empty())
		return false;

    const char* names[]={"idle",pistol?"idle_aim":nullptr,pistol?"reload_full":nullptr,pistol?"reload":nullptr,
        pistol?nullptr:"shoot1_start",pistol?nullptr:"shoot1_end",pistol?"shoot1":nullptr,pistol?"shoot_last":nullptr};
    if(kind==2){names[1]="idle_aim";names[2]="reload_left";names[3]="reload";names[4]=names[5]=nullptr;names[6]="shoot_left";names[7]="shoot_right";}
    if(kind==3){names[1]="idle_aim";names[2]=names[3]="reload";names[4]=names[5]=nullptr;names[6]=names[7]="shoot";}
    if(kind==4){names[0]="idle_0";names[1]=names[2]=names[3]=nullptr;names[4]="attack_0_begin";names[5]="attack_0_act";names[6]=names[7]=nullptr;}
    static_assert(std::size(names)==std::tuple_size<VitaWeaponClips>::value);
	VitaWeaponClips ranges{};
	size_t total_frames = 0;
	for (size_t i = 0; i < ranges.size(); ++i)
	{
        if(!names[i])continue;
        bool duplicate=false;
        for(size_t j=0;j<i;++j)if(names[j]&&strcmp(names[i],names[j])==0){ranges[i]=ranges[j];duplicate=true;break;}
        if(duplicate)continue;
		const bool static_pose = i <= static_cast<size_t>(VitaWeaponClip::Aim);
		const Chunk clip = find_named_motion(root, names[i]);
		std::vector<std::vector<float>> baked(result.size());
		u32 source_frames = 0;
		bool valid = clip.size != 0;
		for (size_t j = 0; valid && j < result.size(); ++j)
		{
			auto bind_vertices = result[j].vertices;
			const auto& stream = streams[j];
			Fvector unused;
			valid = bake_animation(root, params, clip, static_pose, stream.vertices.data + 8,
				stream.stride, bind_vertices, unused, baked[j], false, &source_frames, 1,nullptr,{},false,24u*1024u*1024u);
		}
		if (!valid)
		{
			if (i == 0)
				return false;
			continue;
		}
		const unsigned step = static_pose ? source_frames : 1;
		ranges[i] = {total_frames, source_frames, step};
		total_frames += (source_frames + step - 1) / step;
		for (size_t j = 0; j < result.size(); ++j)
			result[j].positions.insert(result[j].positions.end(), baked[j].begin(), baked[j].end());
	}
	for (auto& part : result)
		for (size_t v = 0; v < part.vertices.size(); ++v)
			memcpy(part.vertices[v].position, part.positions.data() + 3 * v, sizeof(part.vertices[v].position));
	parts.swap(result);
	clips = ranges;
	return true;
}
