#pragma once

#include <psp2/types.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

enum class VitaSound
{
	Pistol, Guard, Reload, Empty,
	TraderIntro, WolfOrders, ScoutCall, ScoutReady, ScoutAttack, ScoutVictory,
	BanditAlert1, BanditAlert2, BanditAttack1, BanditAttack2,
	BanditHit1, BanditHit2, BanditDeath1, BanditDeath2,
	Shotgun,
	BanditVariants,
	AllyVariants = static_cast<int>(BanditVariants) + 36,
	AmbientWind = static_cast<int>(AllyVariants) + 9,
	Bird1, Bird2, Crow1, Crow2,
    RifleShot,RifleReload,ShotgunReload,GrenadeExplosion,SilencedShot,
	Count
};

class VitaGameAudio
{
public:
	~VitaGameAudio();
	bool initialize(bool menu_music = false);
	void play(VitaSound sound, float volume = 0.85f, float pan = 0.f);
	void speak(VitaSound sound);
	void stop_speech();
	void set_levels(unsigned master, unsigned music, unsigned effects, unsigned voices, unsigned ambient);
	void set_ambience(bool enabled);
	VitaSound bandit_voice(unsigned speaker, unsigned event);
	VitaSound ally_voice(unsigned speaker);

	void set_radio(float volume, float pan = 0.f);
	unsigned voice_count() const;
	bool radio_ready() const { return m_radio >= 0; }
	void release();

private:
	enum class Action { Effect, Speak, StopSpeech };
	struct Event
	{
		Action action;
		VitaSound sound;
		int left, right;
	};

	void enqueue(Action action, VitaSound sound, float volume, float pan);
	static int entry(SceSize size, void* args);
	int run();

	static constexpr unsigned block_size = 512;
	std::array<std::vector<int16_t>, static_cast<unsigned>(VitaSound::Count)> m_samples;
	std::array<Event, 16> m_queue{};
	std::atomic<unsigned> m_read{0}, m_write{0};
	std::atomic<uint32_t> m_radio_gains{0};
	std::atomic<uint32_t> m_levels{20u | (14u<<5) | (20u<<10) | (20u<<15) | (12u<<20)};
	std::atomic<bool> m_ambience{false};
	std::array<unsigned,24> m_variants{};
	uint32_t m_random = 0x93216acf;

	std::atomic<bool> m_stopping{false}, m_running{false};
	bool m_radio_stereo = false;
	SceUID m_thread = -1, m_wake = -1, m_radio = -1;
	int m_port = -1;
};
