#pragma once

#include <algorithm>
#include <cmath>

enum class VitaPlayerStance { Standing, Crouching, LowCrouch };

class VitaPlayerMovement
{
public:
	bool restore(unsigned stance, float endurance)
	{
		if (stance > static_cast<unsigned>(VitaPlayerStance::LowCrouch) ||
			!std::isfinite(endurance) || endurance < 0.f || endurance > 1.f)
			return false;
		cancel_input();
		m_stance = static_cast<VitaPlayerStance>(stance);
		m_endurance = endurance;
		m_sprint_exhausted = endurance < 0.30f;
		return true;
	}

	void update(float seconds, float forward, bool sprint_pressed, bool crouch_pressed,
		bool crouch_held, bool can_stand, bool sprint_allowed)
	{
		if (crouch_pressed)
		{
			m_crouch_pending = true;
			m_crouch_time = 0.f;
		}
		if (m_crouch_pending && crouch_held)
		{
			m_crouch_time += seconds;
			if (m_crouch_time >= 0.35f)
			{
				m_stance = VitaPlayerStance::LowCrouch;
				m_crouch_pending = false;
			}
		}
		else if (m_crouch_pending)
		{
			m_crouch_pending = false;
			if (m_stance == VitaPlayerStance::Standing)
				m_stance = VitaPlayerStance::Crouching;
			else if (can_stand)
				m_stance = VitaPlayerStance::Standing;
		}

		if (!sprint_allowed || m_stance != VitaPlayerStance::Standing || forward < 0.35f)
			m_sprinting = false;
		else if (sprint_pressed)
			m_sprinting = !m_sprinting && !m_sprint_exhausted;
	}

	void finish_move(float seconds, bool moving)
	{
		m_sprinting = m_sprinting && moving;
		m_endurance = std::clamp(m_endurance + seconds * (m_sprinting ? -0.027f : 0.16f), 0.f, 1.f);
		if (m_sprinting && m_endurance <= 0.20f)
		{
			m_sprinting = false;
			m_sprint_exhausted = true;
		}
		else if (m_endurance >= 0.30f)
			m_sprint_exhausted = false;
	}

	void cancel_input() { m_crouch_pending = m_sprinting = false; }
	void reset_posture() { cancel_input(); m_stance = VitaPlayerStance::Standing; }
	float speed_factor() const { return m_sprinting ? 1.8f : m_stance == VitaPlayerStance::Crouching ? 0.45f :
		m_stance == VitaPlayerStance::LowCrouch ? 0.30f : 1.f; }
	float body_height() const { return m_stance == VitaPlayerStance::Crouching ? 1.50f :
		m_stance == VitaPlayerStance::LowCrouch ? 1.20f : 1.80f; }
	float eye_height() const { return body_height() - 0.10f; }
	float endurance() const { return m_endurance; }
	bool sprinting() const { return m_sprinting; }
	VitaPlayerStance stance() const { return m_stance; }

private:
	VitaPlayerStance m_stance = VitaPlayerStance::Standing;
	float m_endurance = 1.f, m_crouch_time = 0.f;
	bool m_crouch_pending = false, m_sprinting = false, m_sprint_exhausted = false;
};
