#include "vita_cform_ground.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

std::size_t VitaCformGround::cell(float coordinate, float origin, std::size_t count) const
{
	const float position = (coordinate - origin) / m_cell_width;
	return static_cast<std::size_t>(std::max(0.f, std::min(position, static_cast<float>(count - 1))));
}

bool VitaCformGround::load(Read read, void* source, std::size_t size, float center_x, float center_z,
	const std::vector<std::uint8_t>& passable_materials, const float* destination, bool full_map)
{
	// level.cform version 4 stores hdrCFORM, Fvector vertices, then CDB::TRI records.
	struct Header
	{
		std::uint32_t version, vertices, faces;
		Vertex minimum, maximum;
	};
	struct DiskFace { std::uint32_t vertex[3], material_and_sector; };
	static_assert(sizeof(Header) == 36 && sizeof(Vertex) == 12 && sizeof(DiskFace) == 16, "CFORM layout");

	if (!read || size < sizeof(Header) || size > max_file_bytes)
		return false;
	Header header;
	if (!read(source, &header, sizeof(header)))
		return false;
	const std::size_t payload = size - sizeof(header);
	if (header.version != 4 || !header.vertices || !header.faces ||
		header.vertices > payload / sizeof(Vertex))
		return false;
	const std::size_t vertex_bytes = header.vertices * sizeof(Vertex);
	if (payload - vertex_bytes != static_cast<std::uint64_t>(header.faces) * sizeof(DiskFace))
		return false;

	if (!std::isfinite(header.minimum.x) || !std::isfinite(header.minimum.z) ||
		!std::isfinite(header.maximum.x) || !std::isfinite(header.maximum.z))
		return false;
	constexpr float village_radius = 160.f;
	VitaCformGround ground;
	ground.m_min_x = std::max(header.minimum.x, center_x - village_radius);
	ground.m_min_z = std::max(header.minimum.z, center_z - village_radius);
	ground.m_max_x = std::min(header.maximum.x, center_x + village_radius);
	ground.m_max_z = std::min(header.maximum.z, center_z + village_radius);
	if (destination)
	{
		constexpr float scout_margin = 32.f;
		ground.m_min_x = std::min(ground.m_min_x, std::max(header.minimum.x, destination[0] - scout_margin));
		ground.m_max_x = std::max(ground.m_max_x, std::min(header.maximum.x, destination[0] + scout_margin));
		ground.m_min_z = std::min(ground.m_min_z, std::max(header.minimum.z, destination[2] - scout_margin));
		ground.m_max_z = std::max(ground.m_max_z, std::min(header.maximum.z, destination[2] + scout_margin));
	}
 if(full_map){ground.m_min_x=header.minimum.x;ground.m_max_x=header.maximum.x;
  ground.m_min_z=header.minimum.z;ground.m_max_z=header.maximum.z;}
	const float span_x = ground.m_max_x - ground.m_min_x;
	const float span_z = ground.m_max_z - ground.m_min_z;
	if (!std::isfinite(span_x) || !std::isfinite(span_z) || span_x <= 0.f || span_z <= 0.f)
		return false;
	ground.m_cell_width = std::max(8.f, std::max(span_x, span_z) / 256.f);
	ground.m_columns = static_cast<std::size_t>(span_x / ground.m_cell_width) + 1;
	ground.m_rows = static_cast<std::size_t>(span_z / ground.m_cell_width) + 1;
	ground.m_cells.resize(ground.m_columns * ground.m_rows);
	std::vector<Vertex> vertices(header.vertices);
	if (!read(source, vertices.data(), vertex_bytes))
		return false;
	for (const Vertex& vertex : vertices)
		if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) || !std::isfinite(vertex.z))
			return false;

	std::array<DiskFace, 512> batch;
	const std::size_t max_cell_references = (full_map?8u:2u) * 1024u * 1024u;
	std::size_t references = 0;
	for (std::size_t first = 0; first < header.faces;)
	{
		const std::size_t batch_size = std::min(batch.size(), header.faces - first);
		if (!read(source, batch.data(), batch_size * sizeof(DiskFace)))
			return false;
		first += batch_size;
		for (std::size_t i = 0; i < batch_size; ++i)
		{
			const DiskFace& face = batch[i];
			if (face.vertex[0] >= header.vertices || face.vertex[1] >= header.vertices || face.vertex[2] >= header.vertices)
				return false;
			const std::size_t material = face.material_and_sector & (material_count - 1);
			if (material < passable_materials.size() && passable_materials[material])
				continue;
			const Vertex& a = vertices[face.vertex[0]];
			const Vertex& b = vertices[face.vertex[1]];
			const Vertex& c = vertices[face.vertex[2]];
			const float min_x = std::min({a.x, b.x, c.x}), max_x = std::max({a.x, b.x, c.x});
			const float min_z = std::min({a.z, b.z, c.z}), max_z = std::max({a.z, b.z, c.z});
			if (max_x < ground.m_min_x || min_x > ground.m_max_x ||
				max_z < ground.m_min_z || min_z > ground.m_max_z)
				continue;
			const float bx = b.x - a.x, by = b.y - a.y, bz = b.z - a.z;
			const float cx = c.x - a.x, cy = c.y - a.y, cz = c.z - a.z;
			const float nx = by * cz - bz * cy;
			const float ny = bz * cx - bx * cz;
			const float nz = bx * cy - by * cx;
			const float horizontal_normal = nx * nx + nz * nz;
			if (!(horizontal_normal + ny * ny > 1e-12f))
				continue;
			const bool walkable = std::fabs(ny) >= 1e-6f && ny * ny >= horizontal_normal;

			const std::size_t x0 = ground.cell(min_x, ground.m_min_x, ground.m_columns);
			const std::size_t z0 = ground.cell(min_z, ground.m_min_z, ground.m_rows);
			const std::size_t x1 = ground.cell(max_x, ground.m_min_x, ground.m_columns);
			const std::size_t z1 = ground.cell(max_z, ground.m_min_z, ground.m_rows);
			const std::size_t count = (x1 - x0 + 1) * (z1 - z0 + 1);
			if (count > max_cell_references - references)
				return false;
			references += count;
			auto& faces = walkable ? ground.m_faces : ground.m_walls;
			faces.push_back({{face.vertex[0],face.vertex[1],face.vertex[2]}});
			for (std::size_t z = z0; z <= z1; ++z)
			{
				for (std::size_t x = x0; x <= x1; ++x)
				{
					Cell& cell = ground.m_cells[z * ground.m_columns + x];
					++(walkable ? cell.ground_count : cell.wall_count);
				}
			}
		}
	}
	if (ground.m_faces.empty())
		return false;
    // Keep one copy of each used source vertex. Coordinates remain full precision.
    std::vector<std::uint32_t> remap(vertices.size(),UINT32_MAX);
    for(const auto*bank:{&ground.m_faces,&ground.m_walls})
        for(const auto&face:*bank)for(auto v:face.vertex)remap[v]=0;
    std::size_t used=0;
    for(std::size_t i=0;i<vertices.size();++i)if(remap[i]!=UINT32_MAX){
        remap[i]=static_cast<std::uint32_t>(used);
        vertices[used++]=vertices[i];
    }
    for(auto*bank:{&ground.m_faces,&ground.m_walls})
        for(auto&face:*bank)for(auto&v:face.vertex)v=remap[v];
    std::vector<std::uint32_t>().swap(remap);
    vertices.resize(used);vertices.shrink_to_fit();ground.m_vertices=std::move(vertices);
    ground.m_faces.shrink_to_fit();ground.m_walls.shrink_to_fit();

    // CSR cell lists have exact capacities and no per-cell heap allocations.
    std::uint32_t floor_total=0,wall_total=0;
    for(auto&cell:ground.m_cells){
        cell.ground_first=floor_total;floor_total+=cell.ground_count;cell.ground_count=0;
        cell.wall_first=wall_total;wall_total+=cell.wall_count;cell.wall_count=0;
    }
    ground.m_ground_refs.resize(floor_total);ground.m_wall_refs.resize(wall_total);
    const auto fill_cells=[&](const std::vector<Face>&faces,bool walkable){
        for(std::size_t i=0;i<faces.size();++i){
            const auto&face=faces[i];
            const auto&a=ground.m_vertices[face.vertex[0]],&b=ground.m_vertices[face.vertex[1]],&c=ground.m_vertices[face.vertex[2]];
            const auto x0=ground.cell(std::min({a.x,b.x,c.x}),ground.m_min_x,ground.m_columns);
            const auto x1=ground.cell(std::max({a.x,b.x,c.x}),ground.m_min_x,ground.m_columns);
            const auto z0=ground.cell(std::min({a.z,b.z,c.z}),ground.m_min_z,ground.m_rows);
            const auto z1=ground.cell(std::max({a.z,b.z,c.z}),ground.m_min_z,ground.m_rows);
            for(std::size_t z=z0;z<=z1;++z)for(std::size_t x=x0;x<=x1;++x){
                auto&cell=ground.m_cells[z*ground.m_columns+x];
                if(walkable)ground.m_ground_refs[cell.ground_first+cell.ground_count++]=static_cast<std::uint32_t>(i);
                else ground.m_wall_refs[cell.wall_first+cell.wall_count++]=static_cast<std::uint32_t>(i);
            }
        }
    };
    fill_cells(ground.m_faces,true);fill_cells(ground.m_walls,false);
	*this = std::move(ground);
	return true;
}

bool VitaCformGround::height_at(float x, float z, float reference_y, float rise, float drop, float& height, bool highest) const
{
	if (m_cells.empty() || !(x >= m_min_x && x <= m_max_x && z >= m_min_z && z <= m_max_z))
		return false;
	const auto column = cell(x, m_min_x, m_columns);
	const auto row = cell(z, m_min_z, m_rows);
	float closest = std::max(rise, drop);
	bool found = false;
	const auto& list=m_cells[row*m_columns+column];
    for(std::uint32_t entry=0;entry<list.ground_count;++entry)
    {
        const auto index=m_ground_refs[list.ground_first+entry];
		const Face& face = m_faces[index];
		const Vertex& a = m_vertices[face.vertex[0]];
		const Vertex& b = m_vertices[face.vertex[1]];
		const Vertex& c = m_vertices[face.vertex[2]];
		const float bx = b.x - a.x, bz = b.z - a.z;
		const float cx = c.x - a.x, cz = c.z - a.z;
		const float dx = x - a.x, dz = z - a.z;
		const float determinant = bx * cz - bz * cx;
		const float u = (dx * cz - dz * cx) / determinant;
		const float v = (bx * dz - bz * dx) / determinant;
		constexpr float edge_tolerance = 1e-5f;
		if (u < -edge_tolerance || v < -edge_tolerance || u + v > 1.f + edge_tolerance)
			continue;
		const float y = a.y + u * (b.y - a.y) + v * (c.y - a.y);
		const float delta = y - reference_y;
		if (delta >= -drop && delta <= rise && (!found || (highest ? y > height : std::fabs(delta) < closest)))
		{
			height = y;
			closest = std::fabs(delta);
			found = true;
		}
	}
	return found;
}


namespace
{
struct Point
{
	float x, z;

	Point operator+(Point other) const { return {x + other.x, z + other.z}; }
	Point operator-(Point other) const { return {x - other.x, z - other.z}; }
	Point operator*(float scale) const { return {x * scale, z * scale}; }
	float dot(Point other) const { return x * other.x + z * other.z; }
};

constexpr float body_radius = 0.30f;
constexpr float contact_margin = 0.005f;
constexpr float motion_epsilon = 1e-6f;

struct WallSweep
{
	Point start, motion;
	Point normal{};
	float fraction = 1.f;

	void test_end(Point end)
	{
		const Point offset = start - end;
		const float approach = motion.dot(offset);
		if (approach >= -motion_epsilon)
			return;

		const float motion_squared = motion.dot(motion);
		const float distance_squared = offset.dot(offset) - body_radius * body_radius;
		const float discriminant = approach * approach - motion_squared * distance_squared;
		if (discriminant < 0.f)
			return;
		const float hit = distance_squared <= 0.f ? 0.f : (-approach - std::sqrt(discriminant)) / motion_squared;
		if (hit >= fraction)
			return;

		const Point separation = offset + motion * hit;
		const float length = std::sqrt(separation.dot(separation));
		if (length > motion_epsilon)
		{
			fraction = hit;
			normal = separation * (1.f / length);
		}
	}

	void test_edge(Point a, Point b)
	{
		test_end(a);
		const Point edge = b - a;
		const float length = std::sqrt(edge.dot(edge));
		if (length <= motion_epsilon)
			return;
		const Point tangent = edge * (1.f / length);
		Point side{-tangent.z, tangent.x};
		float distance = (start - a).dot(side);
		if (distance < 0.f)
		{
			side = side * -1.f;
			distance = -distance;
		}
		const float approach = motion.dot(side);
		if (distance <= motion_epsilon || approach >= -motion_epsilon)
			return;

		const float hit = distance <= body_radius ? 0.f : (body_radius - distance) / approach;
		if (hit >= fraction)
			return;
		const float along = (start + motion * hit - a).dot(tangent);
		if (along >= 0.f && along <= length)
		{
			fraction = hit;
			normal = side;
		}
	}
};
}

bool VitaCformGround::slide_move(float x, float z, float ground_y, float& target_x, float& target_z,
	const DoorPanel* door, float step_height, float body_height) const
{
	if (m_cells.empty() || (m_walls.empty() && !door))
		return false;

	// Clip steep triangles to the player's body above the maximum ground step.
	const auto clip_height = [](const Vertex* input, unsigned count, Vertex* output, float height, bool above)
	{
		unsigned result = 0;
		Vertex previous = input[count - 1];
		bool previous_inside = above ? previous.y >= height : previous.y <= height;
		for (unsigned i = 0; i < count; ++i)
		{
			const Vertex& current = input[i];
			const bool inside = above ? current.y >= height : current.y <= height;
			if (inside != previous_inside)
			{
				const float t = (height - previous.y) / (current.y - previous.y);
				output[result++] = {
					previous.x + (current.x - previous.x) * t, height,
					previous.z + (current.z - previous.z) * t
				};
			}
			if (inside)
				output[result++] = current;
			previous = current;
			previous_inside = inside;
		}
		return result;
	};

	const float bottom = ground_y + step_height + 0.01f;
	const float top = ground_y + body_height;
	Point position{x, z};
	Point motion{target_x - x, target_z - z};
	bool collided = false;
	for (unsigned contact = 0; contact < 3; ++contact)
	{
		const float distance = std::sqrt(motion.dot(motion));
		if (distance <= motion_epsilon)
			break;
		const Point end = position + motion;
		const float min_x = std::min(position.x, end.x) - body_radius;
		const float min_z = std::min(position.z, end.z) - body_radius;
		const float max_x = std::max(position.x, end.x) + body_radius;
		const float max_z = std::max(position.z, end.z) + body_radius;
		const auto x0 = cell(min_x, m_min_x, m_columns), x1 = cell(max_x, m_min_x, m_columns);
		const auto z0 = cell(min_z, m_min_z, m_rows), z1 = cell(max_z, m_min_z, m_rows);
		WallSweep sweep{position, motion};
		if (door && door->bottom <= top && door->top >= bottom)
		{
			for (unsigned i = 0; i < 4; ++i)
			{
				const auto& a = door->corners[i];
				const auto& b = door->corners[(i + 1) % 4];
				sweep.test_edge({a[0], a[1]}, {b[0], b[1]});
			}
		}
		for (std::size_t row = z0; row <= z1; ++row)
		{
			for (std::size_t column = x0; column <= x1; ++column)
			{
				const auto& list=m_cells[row*m_columns+column];
                for(std::uint32_t entry=0;entry<list.wall_count;++entry)
                {
                    const auto index=m_wall_refs[list.wall_first+entry];
					const Face& wall = m_walls[index];
					const Vertex &a = m_vertices[wall.vertex[0]], &b = m_vertices[wall.vertex[1]], &c = m_vertices[wall.vertex[2]];
					if (std::max({a.x, b.x, c.x}) < min_x || std::min({a.x, b.x, c.x}) > max_x ||
						std::max({a.z, b.z, c.z}) < min_z || std::min({a.z, b.z, c.z}) > max_z ||
						std::max({a.y, b.y, c.y}) < bottom || std::min({a.y, b.y, c.y}) > top)
						continue;
					const Vertex triangle[] = {a, b, c};
					Vertex lower[4], clipped[5];
					const unsigned count = clip_height(triangle, 3, lower, bottom, true);
					if (!count)
						continue;
					const unsigned edges = clip_height(lower, count, clipped, top, false);
					for (unsigned i = 0; i < edges; ++i)
					{
						const Vertex& from = clipped[i];
						const Vertex& to = clipped[(i + 1) % edges];
						sweep.test_edge({from.x, from.z}, {to.x, to.z});
					}
				}
			}
		}
		if (sweep.fraction == 1.f)
		{
			position = end;
			break;
		}
		collided = true;
		position = position + motion * std::max(0.f, sweep.fraction - contact_margin / distance);
		motion = motion * (1.f - sweep.fraction);
		motion = motion - sweep.normal * std::min(0.f, motion.dot(sweep.normal));
	}
	target_x = position.x;
	target_z = position.z;
	return collided;
}

bool VitaCformGround::support_at(float x,float z,float reference_y,float rise,float drop,float& height,bool highest) const
{

 // Intersect a small support disk with the projected walkable triangles. Sparse rays
 // can all miss an open metal tread, even while part of the character rests on it.
 if(m_cells.empty())return false;
 constexpr float radius=.32f,radius_squared=radius*radius;
 const auto x0=cell(x-radius,m_min_x,m_columns),x1=cell(x+radius,m_min_x,m_columns);
 const auto z0=cell(z-radius,m_min_z,m_rows),z1=cell(z+radius,m_min_z,m_rows);
 bool found=false;float best=reference_y;
 for(std::size_t row=z0;row<=z1;++row)for(std::size_t column=x0;column<=x1;++column){
  const auto&list=m_cells[row*m_columns+column];
  for(std::uint32_t entry=0;entry<list.ground_count;++entry){
   const auto&face=m_faces[m_ground_refs[list.ground_first+entry]];
   const Vertex&a=m_vertices[face.vertex[0]],&b=m_vertices[face.vertex[1]],&c=m_vertices[face.vertex[2]];
   if(std::max({a.x,b.x,c.x})<x-radius||std::min({a.x,b.x,c.x})>x+radius||
      std::max({a.z,b.z,c.z})<z-radius||std::min({a.z,b.z,c.z})>z+radius||
      std::max({a.y,b.y,c.y})<reference_y-drop||std::min({a.y,b.y,c.y})>reference_y+rise)continue;
   const float bx=b.x-a.x,bz=b.z-a.z,cx=c.x-a.x,cz=c.z-a.z;
   const float determinant=bx*cz-bz*cx;
   const float u=((x-a.x)*cz-(z-a.z)*cx)/determinant,v=(bx*(z-a.z)-bz*(x-a.x))/determinant;
   float candidate=0,distance_squared=0;
   if(u>=0&&v>=0&&u+v<=1)candidate=a.y+u*(b.y-a.y)+v*(c.y-a.y);
   else{
    distance_squared=radius_squared+1;
    const Vertex vertices[]={a,b,c};
    for(unsigned i=0;i<3;++i){
     const auto&from=vertices[i];const auto&to=vertices[(i+1)%3];
     const float dx=to.x-from.x,dz=to.z-from.z,length=dx*dx+dz*dz;
     const float t=length>1e-12f?std::clamp(((x-from.x)*dx+(z-from.z)*dz)/length,0.f,1.f):0.f;
     const float xx=from.x+dx*t-x,zz=from.z+dz*t-z,d=xx*xx+zz*zz;
     if(d<distance_squared){distance_squared=d;candidate=from.y+(to.y-from.y)*t;}
    }
   }
   if(distance_squared>radius_squared||candidate<reference_y-drop||candidate>reference_y+rise)continue;
   if(!found||(highest?candidate>best:std::abs(candidate-reference_y)<std::abs(best-reference_y))){best=candidate;found=true;}
  }
 }
 if(found)height=best;return found;
}

bool VitaCformGround::walk_move(float x,float z,float ground_y,float& target_x,float& target_y,float& target_z,
 bool& contact,const DoorPanel* door,float step_height,float body_height) const
{
 const float dx=target_x-x,dz=target_z-z,distance=std::hypot(dx,dz);
 const unsigned count=std::max(1u,std::min(8u,unsigned(std::ceil(distance/.12f))));
 // Ordinary flat walking keeps the inexpensive centre probes and one sweep.
 // The footprint/headroom solver is only needed at a discontinuity or obstacle.
 float flat_y=ground_y;bool flat=true;
 for(unsigned i=1;i<=count;++i){
  float h=ground_y;
  if(!height_at(x+dx*i/count,z+dz*i/count,ground_y,step_height,.75f,h,true)||std::abs(h-ground_y)>.015f){flat=false;break;}
  flat_y=h;
 }
 if(flat){float nx=target_x,nz=target_z;
  if(!slide_move(x,z,std::max(ground_y,flat_y),nx,nz,door,std::min(step_height,.15f),body_height)){
   target_y=flat_y;contact=false;return true;
  }
 }
 float px=x,pz=z,py=ground_y;bool supported=false;contact=false;
 constexpr float footprint[][2]={{0,0},{.22f,0},{-.22f,0},{0,.22f},{0,-.22f}};
 constexpr float up[]={0,1,0};
 const auto headroom=[&](float xx,float zz,float from,float to){
  if(to<=from+.001f)return true;
  for(const auto&offset:footprint){
   const float head[]={xx+offset[0],from+body_height-.05f,zz+offset[1]};
   if(blocks_ray(head,up,to-from+.05f))return false;
  }
  return true;
 };
 for(unsigned i=0;i<count;++i){
  float nx=px+dx/count,nz=pz+dz/count,ny=py;
  if(!support_at(nx,nz,py,step_height,.75f,ny))break;
  if(!headroom(px,pz,py,ny)||!headroom(nx,nz,py,ny)){contact=true;supported=true;break;}
  // Sweep the body at the supported elevation. A later riser inside its radius must not
  // pin the body to the lower tread. Real walls and the interactive door still obstruct it.
  contact=slide_move(px,pz,ny,nx,nz,door,std::min(step_height,.15f),body_height)||contact;
  float actual=py;
  if(!support_at(nx,nz,py,step_height,.75f,actual))break;
  if(!headroom(px,pz,py,actual)||!headroom(nx,nz,py,actual)){contact=true;supported=true;break;}
  px=nx;pz=nz;py=actual;supported=true;
 }
 target_x=px;target_z=pz;target_y=py;return supported;
}


bool VitaCformGround::blocks_ray(const float origin[3], const float direction[3], float length) const
{
	if (m_cells.empty() || !(length > 0.f))
		return false;

	const auto intersects = [&](const Face& face) {
        const Vertex &a=m_vertices[face.vertex[0]],&b=m_vertices[face.vertex[1]],&c=m_vertices[face.vertex[2]];
		const float e1[3] = {b.x - a.x, b.y - a.y, b.z - a.z};
		const float e2[3] = {c.x - a.x, c.y - a.y, c.z - a.z};
		const float p[3] = {direction[1] * e2[2] - direction[2] * e2[1],
			direction[2] * e2[0] - direction[0] * e2[2], direction[0] * e2[1] - direction[1] * e2[0]};
		const float det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
		if (std::fabs(det) < 1e-7f)
			return false;
		const float t[3] = {origin[0] - a.x, origin[1] - a.y, origin[2] - a.z};
		const float u = (t[0] * p[0] + t[1] * p[1] + t[2] * p[2]) / det;
		if (u < 0.f || u > 1.f)
			return false;
		const float q[3] = {t[1] * e1[2] - t[2] * e1[1],
			t[2] * e1[0] - t[0] * e1[2], t[0] * e1[1] - t[1] * e1[0]};
		const float v = (direction[0] * q[0] + direction[1] * q[1] + direction[2] * q[2]) / det;
		const float distance = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) / det;
		return v >= 0.f && u + v <= 1.f && distance > 0.035f && distance < length;
	};
	const auto end_x = origin[0] + direction[0] * length;
	const auto end_z = origin[2] + direction[2] * length;
	const auto x0 = cell(std::min(origin[0], end_x), m_min_x, m_columns);
	const auto z0 = cell(std::min(origin[2], end_z), m_min_z, m_rows);
	const auto x1 = cell(std::max(origin[0], end_x), m_min_x, m_columns);
	const auto z1 = cell(std::max(origin[2], end_z), m_min_z, m_rows);
	for (std::size_t z = z0; z <= z1; ++z)
		for (std::size_t x = x0; x <= x1; ++x)
		{
			const auto& cell = m_cells[z * m_columns + x];
			for(std::uint32_t i=0;i<cell.wall_count;++i)
				if (intersects(m_walls[m_wall_refs[cell.wall_first+i]]))
					return true;
			for(std::uint32_t i=0;i<cell.ground_count;++i)
				if (intersects(m_faces[m_ground_refs[cell.ground_first+i]]))
					return true;
		}
	return false;
}


std::size_t VitaCformGround::memory_bytes()const{
 return m_vertices.capacity()*sizeof(Vertex)+(m_faces.capacity()+m_walls.capacity())*sizeof(Face)+
  m_cells.capacity()*sizeof(Cell)+(m_ground_refs.capacity()+m_wall_refs.capacity())*sizeof(std::uint32_t);
}
