#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

using VitaPatrolPoint = std::array<float, 3>;

// A patrol used for this encounter is either a single marker or a directed chain.
inline bool read_vita_patrol(const void* points, size_t point_bytes, const void* links, size_t link_bytes,
	std::vector<VitaPatrolPoint>& route)
{
	const auto* commands = static_cast<const uint8_t*>(points);
	uint16_t count = 0;
	if (!commands || point_bytes < sizeof(count))
		return false;
	memcpy(&count, commands, sizeof(count));
	if (!count || count > 64)
		return false;
	commands += sizeof(count);
	point_bytes -= sizeof(count);
	std::array<VitaPatrolPoint, 64> positions{};
	for (unsigned i = 0; i < count; ++i)
	{
		constexpr size_t point_fields = 3 * sizeof(float) + sizeof(uint32_t);
		if (point_bytes <= point_fields)
			return false;
		memcpy(positions[i].data(), commands, 3 * sizeof(float));
		for (float coord : positions[i])
			if (!std::isfinite(coord) || std::fabs(coord) >= 2000.f)
				return false;
		const auto* end = static_cast<const uint8_t*>(memchr(commands + point_fields, 0, point_bytes - point_fields));
		if (!end)
			return false;
		point_bytes -= static_cast<size_t>(end + 1 - commands);
		commands = end + 1;
	}
	if (point_bytes != 0)
		return false;

	const auto* edges = static_cast<const uint8_t*>(links);
	uint16_t edge_count = 0;
	constexpr size_t edge_size = 2 * sizeof(uint16_t) + sizeof(float);
	if (!edges || link_bytes < sizeof(edge_count))
		return false;
	memcpy(&edge_count, edges, sizeof(edge_count));
	if (edge_count != count - 1 || link_bytes != sizeof(edge_count) + edge_size * edge_count)
		return false;
	edges += sizeof(edge_count);
	std::array<int, 64> successors;
	successors.fill(-1);
	std::array<bool, 64> incoming{};
	for (unsigned i = 0; i < edge_count; ++i, edges += edge_size)
	{
		uint16_t endpoints[2];
		memcpy(endpoints, edges, sizeof(endpoints));
		const auto from = endpoints[0], to = endpoints[1];
		if (from >= count || to >= count || successors[from] >= 0 || incoming[to])
			return false;
		successors[from] = to;
		incoming[to] = true;
	}
	int first = 0;
	while (first < count && incoming[first])
		++first;
	std::vector<VitaPatrolPoint> candidate;
	for (int point = first < count ? first : -1; point >= 0 && candidate.size() < count; point = successors[point])
		candidate.push_back(positions[point]);
	if (candidate.size() != count)
		return false;
	route.swap(candidate);
	return true;
}
