#pragma once

#include <cstdint>

enum class VitaButtonAction { None, Tap, Hold };

class VitaButtonGesture
{
public:
	static constexpr uint64_t hold_time_us = 450000;

	VitaButtonAction update(bool down, bool enabled, uint64_t now)
	{
		if (!enabled)
			m_consumed = true;
		if (!down)
		{
			const bool pending = m_down && !m_consumed;
			const bool held = pending && now - m_start >= hold_time_us;
			m_down = false;
			m_consumed = false;
			return held ? VitaButtonAction::Hold : pending ? VitaButtonAction::Tap : VitaButtonAction::None;
		}
		if (!m_down)
		{
			m_down = true;
			m_start = now;
		}
		if (!m_consumed && now - m_start >= hold_time_us)
		{
			m_consumed = true;
			return VitaButtonAction::Hold;
		}
		return VitaButtonAction::None;
	}

private:
	uint64_t m_start = 0;
	bool m_down = false;
	bool m_consumed = false;
};
