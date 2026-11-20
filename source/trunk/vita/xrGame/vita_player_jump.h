#pragma once

#include "vita_cform_ground.h"

class VitaPlayerJump
{
public:
	enum class Result { Airborne, Landed, Returned };

	bool active() const { return m_active; }
	void reset() { m_active = false; }

	void start(float x, float y, float z)
	{
		m_start_x = x;
		m_start_y = y;
		m_start_z = z;
		m_velocity = 6.f; // Cordon actor jump_speed.
		m_active = true;
	}

	Result advance(const VitaCformGround& ground, float seconds, float height, float& x, float& y, float& z)
	{
		constexpr float gravity = 2.f * 9.81f;
		const float next_y = y + (m_velocity - 0.5f * gravity * seconds) * seconds;
		m_velocity -= gravity * seconds;
		if (next_y > y)
		{
			constexpr float up[] = {0.f, 1.f, 0.f};
			constexpr float footprint[][2] = {{0.f, 0.f}, {0.22f, 0.f}, {-0.22f, 0.f}, {0.f, 0.22f}, {0.f, -0.22f}};
			constexpr float head_probe_offset = 0.05f; // Start behind the ray's near cutoff.
			for (const auto& offset : footprint)
			{
				const float head[] = {x + offset[0], y + height - head_probe_offset, z + offset[1]};
				if (ground.blocks_ray(head, up, next_y - y + head_probe_offset))
				{
					m_velocity = 0.f;
					return Result::Airborne;
				}
			}
		}
		else
		{
			float landing = y;
			constexpr float landing_slop = 0.005f;
			if (ground.height_at(x, z, y + landing_slop, 0.f, y - next_y + landing_slop, landing))
			{
				y = landing;
				reset();
				return Result::Landed;
			}
			// The level collision is limited to the playable part of the Cordon.
			if (next_y < m_start_y - 4.f)
			{
				x = m_start_x;
				y = m_start_y;
				z = m_start_z;
				reset();
				return Result::Returned;
			}
		}
		y = next_y;
		return Result::Airborne;
	}

private:
	float m_start_x = 0.f, m_start_y = 0.f, m_start_z = 0.f, m_velocity = 0.f;
	bool m_active = false;
};
