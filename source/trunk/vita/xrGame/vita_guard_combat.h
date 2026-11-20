#pragma once

#include <cstdint>

class VitaGuardCombat
{
public:
	void pause(uint64_t duration)
	{
		if (m_next_check) m_next_check += duration;
		if (m_next_shot) m_next_shot += duration;
	}

	bool needs_visibility_check(uint64_t now, unsigned guard)
	{
		if (!m_next_check)
		{
			m_next_check = now + 1 + guard * 36000;
			return false;
		}
		if (now < m_next_check)
			return false;
		m_next_check = now + 200000;
		return true;
	}

	bool observe(uint64_t now, bool visible, unsigned guard)
	{
		if (!visible)
		{
			m_next_shot = 0;
			return false;
		}
		if (!m_next_shot)
		{
			m_next_shot = now + 640000 + guard * 105000;
			return false;
		}
		if (now < m_next_shot)
			return false;
		m_next_shot = now + 1150000 + (guard % 3) * 160000;
		++m_shots;
		return true;
	}

	unsigned damage(float distance, unsigned guard) const
	{
		const unsigned period = distance < 15.f ? 4 : 2;
		return (m_shots + guard) % period ? 9u : 0u;
	}

private:
	uint64_t m_next_check = 0;
	uint64_t m_next_shot = 0;
	unsigned m_shots = 0;
};
