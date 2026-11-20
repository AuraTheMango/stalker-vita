#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

class VitaCformGround
{
public:
	static constexpr std::size_t material_count = 1u << 14;
	static constexpr std::size_t max_file_bytes = 64u * 1024u * 1024u;
	using Read = bool (*)(void* source, void* destination, std::size_t bytes);

	bool load(Read read, void* source, std::size_t size, float center_x, float center_z,
		const std::vector<std::uint8_t>& passable_materials = {}, const float* destination = nullptr, bool full_map = false);
	struct DoorPanel;
 bool height_at(float x, float z, float reference_y, float rise, float drop, float& height, bool highest = false) const;
 bool support_at(float x,float z,float reference_y,float rise,float drop,float& height,bool highest=true) const;
 bool walk_move(float x,float z,float ground_y,float& target_x,float& target_y,float& target_z,bool& contact,
     const DoorPanel* door,float step_height=.45f,float body_height=1.8f) const;
	struct DoorPanel
	{
		float corners[4][2]; // XZ perimeter in world space.
		float bottom, top;
	};
	bool slide_move(float x, float z, float ground_y, float& target_x, float& target_z,
		const DoorPanel* door = nullptr, float step_height = 0.45f, float body_height = 1.80f) const;
	bool blocks_ray(const float origin[3], const float direction[3], float length) const;
	std::size_t face_count() const { return m_faces.size(); }
	std::size_t wall_count() const { return m_walls.size(); }
    std::size_t memory_bytes() const;

private:
	struct Vertex { float x, y, z; };
	struct Face { std::uint32_t vertex[3]; };
	struct Bounds {
        Vertex minimum,maximum;
        bool outside(const Vertex& lo,const Vertex& hi) const {
            return maximum.x<lo.x||minimum.x>hi.x||maximum.y<lo.y||minimum.y>hi.y||maximum.z<lo.z||minimum.z>hi.z;
        }
    };
    static constexpr std::uint32_t bounds_group=64;
    struct Cell
	{
        std::uint32_t ground_first=0, ground_count=0;
        std::uint32_t wall_first=0, wall_count=0;
        std::uint32_t ground_bounds=0,wall_bounds=0;
	};

	std::size_t cell(float coordinate, float origin, std::size_t count) const;

	std::vector<Vertex> m_vertices;
    std::vector<std::uint32_t> m_ground_refs, m_wall_refs;
	std::vector<Face> m_faces;
	std::vector<Face> m_walls;
	std::vector<Cell> m_cells;
    std::vector<Bounds> m_ground_bounds,m_wall_bounds;
	float m_min_x = 0.f, m_min_z = 0.f, m_max_x = 0.f, m_max_z = 0.f;
	float m_cell_width = 8.f;
	std::size_t m_columns = 0, m_rows = 0;
};
