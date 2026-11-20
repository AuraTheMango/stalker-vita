#include "vita_game_audio.h"

#include <psp2/audioout.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr.h>
#include <algorithm>

namespace
{
	constexpr unsigned effect_count = static_cast<unsigned>(VitaSound::Empty) + 1;

	std::array<int, 2> stereo_gains(float volume, float pan)
	{
		pan = std::clamp(pan, -1.f, 1.f);
		volume = std::clamp(volume, 0.f, 1.f);
		return {static_cast<int>(256.f * volume * (1.f - std::max(0.f, pan))),
			static_cast<int>(256.f * volume * (1.f + std::min(0.f, pan)))};
	}

	bool load_sample(const char* path, std::vector<int16_t>& samples, size_t max_samples)
	{
		SceIoStat stat{};
		if (sceIoGetstat(path, &stat) < 0 || stat.st_size <= 0 ||
			static_cast<uint64_t>(stat.st_size) > max_samples * sizeof(int16_t) ||
			stat.st_size % sizeof(int16_t))
			return false;
		const SceUID file = sceIoOpen(path, SCE_O_RDONLY, 0);
		if (file < 0)
			return false;
		samples.resize(stat.st_size / sizeof(int16_t));
		auto* bytes = reinterpret_cast<uint8_t*>(samples.data());
		size_t remaining = static_cast<size_t>(stat.st_size);
		while (remaining)
		{
			const int count = sceIoRead(file, bytes, remaining);
			if (count <= 0)
				break;
			bytes += count;
			remaining -= static_cast<size_t>(count);
		}
		sceIoClose(file);
		if (remaining)
			samples.clear();
		return remaining == 0;
	}
}

VitaGameAudio::~VitaGameAudio()
{
	release();
}

bool VitaGameAudio::initialize(bool menu_music)
{
	static const char* const paths[] = {
		"app0:audio/pm_shot.pcm", "app0:audio/guard_shot.pcm", "app0:audio/pm_reload.pcm", "app0:audio/empty.pcm",
		"app0:audio/trader_intro.pcm", "app0:audio/wolf_orders.pcm", "app0:audio/scout_call.pcm",
		"app0:audio/scout_ready.pcm", "app0:audio/scout_attack.pcm", "app0:audio/scout_victory.pcm",
		"app0:audio/bandit_alert1.pcm", "app0:audio/bandit_alert2.pcm",
		"app0:audio/bandit_attack1.pcm", "app0:audio/bandit_attack2.pcm",
		"app0:audio/bandit_hit1.pcm", "app0:audio/bandit_hit2.pcm",
		"app0:audio/bandit_death1.pcm", "app0:audio/bandit_death2.pcm",
		"app0:audio/shotgun.pcm",
		"app0:audio/bandit_1_enemy_1.pcm",
		"app0:audio/bandit_1_enemy_2.pcm",
		"app0:audio/bandit_1_enemy_3.pcm",
		"app0:audio/bandit_1_attack_1.pcm",
		"app0:audio/bandit_1_attack_2.pcm",
		"app0:audio/bandit_1_attack_3.pcm",
		"app0:audio/bandit_1_hit_1.pcm",
		"app0:audio/bandit_1_hit_2.pcm",
		"app0:audio/bandit_1_hit_3.pcm",
		"app0:audio/bandit_1_death_1.pcm",
		"app0:audio/bandit_1_death_2.pcm",
		"app0:audio/bandit_1_death_3.pcm",
		"app0:audio/bandit_2_enemy_1.pcm",
		"app0:audio/bandit_2_enemy_2.pcm",
		"app0:audio/bandit_2_enemy_3.pcm",
		"app0:audio/bandit_2_attack_1.pcm",
		"app0:audio/bandit_2_attack_2.pcm",
		"app0:audio/bandit_2_attack_3.pcm",
		"app0:audio/bandit_2_hit_1.pcm",
		"app0:audio/bandit_2_hit_2.pcm",
		"app0:audio/bandit_2_hit_3.pcm",
		"app0:audio/bandit_2_death_1.pcm",
		"app0:audio/bandit_2_death_2.pcm",
		"app0:audio/bandit_2_death_3.pcm",
		"app0:audio/bandit_3_enemy_1.pcm",
		"app0:audio/bandit_3_enemy_2.pcm",
		"app0:audio/bandit_3_enemy_3.pcm",
		"app0:audio/bandit_3_attack_1.pcm",
		"app0:audio/bandit_3_attack_2.pcm",
		"app0:audio/bandit_3_attack_3.pcm",
		"app0:audio/bandit_3_hit_1.pcm",
		"app0:audio/bandit_3_hit_2.pcm",
		"app0:audio/bandit_3_hit_3.pcm",
		"app0:audio/bandit_3_death_1.pcm",
		"app0:audio/bandit_3_death_2.pcm",
		"app0:audio/bandit_3_death_3.pcm",
		"app0:audio/ally_1_1.pcm",
		"app0:audio/ally_1_2.pcm",
		"app0:audio/ally_1_3.pcm",
		"app0:audio/ally_2_1.pcm",
		"app0:audio/ally_2_2.pcm",
		"app0:audio/ally_2_3.pcm",
		"app0:audio/ally_3_1.pcm",
		"app0:audio/ally_3_2.pcm",
		"app0:audio/ally_3_3.pcm",
		"app0:audio/ambient_wind.pcm",
		"app0:audio/bird1.pcm",
		"app0:audio/bird2.pcm",
		"app0:audio/crow1.pcm",
		"app0:audio/crow2.pcm",
        "app0:audio/rifle_shot.pcm","app0:audio/rifle_reload.pcm","app0:audio/shotgun_reload.pcm",
        "app0:audio/grenade_explosion.pcm","app0:audio/silenced_shot.pcm"
	};
	static_assert(sizeof(paths) / sizeof(paths[0]) == static_cast<unsigned>(VitaSound::Count));
	for (unsigned i = 0; !menu_music && i < m_samples.size(); ++i)
		if (!load_sample(paths[i], m_samples[i], i < effect_count ? 4 * 48000 : 45 * 24000) && i < effect_count)
			return false;
	m_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, block_size, 48000, SCE_AUDIO_OUT_MODE_STEREO);
	if (m_port < 0)
		return false;
	m_radio_stereo = menu_music;
	const char* const radio_path = menu_music ? "app0:audio/main_menu.pcm" : "app0:audio/bunker_radio.pcm";
	SceIoStat radio_stat{};
	if (sceIoGetstat(radio_path, &radio_stat) >= 0 && radio_stat.st_size > 0 &&
		radio_stat.st_size % (sizeof(int16_t) * (m_radio_stereo ? 2 : 1)) == 0)
		m_radio = sceIoOpen(radio_path, SCE_O_RDONLY, 0);
	if (menu_music && m_radio < 0)
	{
		release();
		return false;
	}
	m_wake = sceKernelCreateSema("stalker_audio_events", 0, 0, 1, nullptr);
	if (m_wake < 0)
	{
		release();
		return false;
	}
	m_thread = sceKernelCreateThread("stalker_audio", entry, 0x10000100, 0x10000, 0, 0, nullptr);
	if (m_thread < 0)
	{
		release();
		return false;
	}
	VitaGameAudio* self = this;
	m_stopping.store(false);
	m_running.store(true);
	if (sceKernelStartThread(m_thread, sizeof(self), &self) < 0)
	{
		m_running.store(false);
		sceKernelDeleteThread(m_thread);
		m_thread = -1;
		release();
		return false;
	}
	return true;
}

unsigned VitaGameAudio::voice_count() const
{
	unsigned count=0;
	for(unsigned i=effect_count;i<static_cast<unsigned>(VitaSound::AmbientWind);++i)
		if(i!=static_cast<unsigned>(VitaSound::Shotgun)&&!m_samples[i].empty())++count;
	return count;
}

void VitaGameAudio::set_levels(unsigned master,unsigned music,unsigned effects,unsigned voices,unsigned ambient)
{
	const auto level=[](unsigned v){return std::min(100u,v)/5;};
	m_levels.store(level(master)|(level(music)<<5)|(level(effects)<<10)|(level(voices)<<15)|(level(ambient)<<20));
}
void VitaGameAudio::set_ambience(bool enabled)
{
	if(m_ambience.exchange(enabled)==enabled)return;
	if(m_wake>=0)sceKernelSignalSema(m_wake,1);
}
VitaSound VitaGameAudio::bandit_voice(unsigned speaker,unsigned event)
{
	const unsigned bank=(speaker%3)*4+event%4;
	m_random^=m_random<<13;m_random^=m_random>>17;m_random^=m_random<<5;
	auto& previous=m_variants[bank];previous=(previous+1+m_random%2)%3;
	return static_cast<VitaSound>(static_cast<unsigned>(VitaSound::BanditVariants)+bank*3+previous);
}
VitaSound VitaGameAudio::ally_voice(unsigned speaker)
{
	auto& previous=m_variants[12+speaker%3];previous=(previous+1+(m_random++%2))%3;
	return static_cast<VitaSound>(static_cast<unsigned>(VitaSound::AllyVariants)+(speaker%3)*3+previous);
}

void VitaGameAudio::play(VitaSound sound, float volume, float pan)
{
	enqueue(Action::Effect, sound, volume, pan);
}

void VitaGameAudio::speak(VitaSound sound)
{
	enqueue(Action::Speak, sound, 0.85f, 0.f);
}

void VitaGameAudio::stop_speech()
{
	enqueue(Action::StopSpeech, VitaSound::Count, 0.f, 0.f);
}

void VitaGameAudio::set_radio(float volume, float pan)
{
	if (m_radio < 0 || !m_running.load(std::memory_order_acquire))
		return;
	const auto gains = stereo_gains(volume, pan);
	const uint32_t packed = static_cast<uint32_t>(gains[0]) | (static_cast<uint32_t>(gains[1]) << 16);
	if (m_radio_gains.exchange(packed, std::memory_order_relaxed) != packed)
		sceKernelSignalSema(m_wake, 1);
}

void VitaGameAudio::enqueue(Action action, VitaSound sound, float volume, float pan)
{
	if (!m_running.load(std::memory_order_acquire))
		return;
	const auto index = static_cast<unsigned>(sound);
	if (action != Action::StopSpeech && (index >= m_samples.size() || m_samples[index].empty()))
		return;
	const unsigned write = m_write.load(std::memory_order_relaxed);
	const unsigned next = (write + 1) % m_queue.size();
	if (next == m_read.load(std::memory_order_acquire))
		return;
	const auto gains = stereo_gains(volume, pan);
	m_queue[write] = {action, sound, gains[0], gains[1]};
	m_write.store(next, std::memory_order_release);
	sceKernelSignalSema(m_wake, 1);
}

int VitaGameAudio::entry(SceSize, void* args)
{
	return (*static_cast<VitaGameAudio**>(args))->run();
}

int VitaGameAudio::run()
{
	struct Voice
	{
		const std::vector<int16_t>* samples = nullptr;
		size_t offset = 0;
		int left = 0, right = 0;
		bool half_rate = false;
		unsigned category = 3;
	};
	std::array<Voice, 9> voices{};
	auto& speech = voices.back();
	std::array<int16_t, 4096> radio_samples;
	size_t radio_cursor = 0, radio_count = 0;
	std::array<int, 2> radio_gains{};
	bool radio_failed = m_radio < 0;
	const auto next_radio_sample = [&]() -> int
	{
		if (radio_cursor == radio_count)
		{
			int bytes = sceIoRead(m_radio, radio_samples.data(), sizeof(radio_samples));
			if (bytes == 0 && sceIoLseek(m_radio, 0, SCE_SEEK_SET) == 0)
				bytes = sceIoRead(m_radio, radio_samples.data(), sizeof(radio_samples));
			if (bytes <= 0 || bytes % (sizeof(int16_t) * (m_radio_stereo ? 2 : 1)))
			{
				radio_failed = true;
				return 0;
			}
			radio_cursor = 0;
			radio_count = bytes / sizeof(int16_t);
		}
		return radio_samples[radio_cursor++];
	};
	alignas(64) int16_t buffers[2][block_size * 2];
	unsigned buffer_index = 0;
	size_t ambient_cursor=0;
	while (!m_stopping.load())
	{
		unsigned read = m_read.load(std::memory_order_relaxed);
		while (read != m_write.load(std::memory_order_acquire))
		{
			const Event event = m_queue[read];
			read = (read + 1) % m_queue.size();
			m_read.store(read, std::memory_order_release);
			if (event.action == Action::StopSpeech)
			{
				speech = {};
				continue;
			}
			const auto index = static_cast<unsigned>(event.sound);
			const Voice voice{&m_samples[index], 0, event.left, event.right, index >= effect_count, index < effect_count || event.sound == VitaSound::Shotgun || index>=unsigned(VitaSound::RifleShot) ? 2u :
				index >= static_cast<unsigned>(VitaSound::AmbientWind) ? 4u : 3u};
			if (event.action == Action::Speak)
			{
				speech = voice;
				continue;
			}
			const auto effects_end = voices.end() - 1;
			auto found = std::find_if(voices.begin(), effects_end, [](const Voice& voice) { return !voice.samples; });
			if (found == effects_end)
				found = std::min_element(voices.begin(), effects_end, [](const Voice& a, const Voice& b) {
					return a.left + a.right < b.left + b.right;
				});
			*found = voice;
		}
		const uint32_t levels=m_levels.load();
		const int master=levels&31;
		const int music=(levels>>5)&31, ambience=(levels>>20)&31;
		const auto& wind=m_samples[static_cast<unsigned>(VitaSound::AmbientWind)];
		const bool ambient_active=m_ambience.load()&&!wind.empty();
		const uint32_t packed = radio_failed ? 0 : m_radio_gains.load(std::memory_order_relaxed);
		const int target_left = packed & 0xffff;
		const int target_right = packed >> 16;
		const int start_left = radio_gains[0], start_right = radio_gains[1];
		radio_gains[0] += std::clamp(target_left - start_left, -8, 8);
		radio_gains[1] += std::clamp(target_right - start_right, -8, 8);
		const bool radio_active = !radio_failed && (start_left || start_right || target_left || target_right);
		if (!ambient_active && !radio_active && std::none_of(voices.begin(), voices.end(), [](const Voice& voice) { return voice.samples != nullptr; }))
		{
			if (sceKernelWaitSema(m_wake, 1, nullptr) < 0)
				break;
			continue;
		}
		int16_t* buffer = buffers[buffer_index];
		int radio_left = 0, radio_right = 0;
		for (unsigned i = 0; i < block_size; ++i)
		{
			int left = 0, right = 0;
			if (radio_active && !radio_failed)
			{
				if ((i & 1) == 0)
				{
					radio_left = next_radio_sample();
					radio_right = m_radio_stereo ? next_radio_sample() : radio_left;
				}
				left = radio_left * (start_left + (radio_gains[0] - start_left) * int(i) / int(block_size));
				right = radio_right * (start_right + (radio_gains[1] - start_right) * int(i) / int(block_size));
			}
			left=left*music/20;right=right*music/20;
			if(ambient_active)
			{
				const int value=wind[ambient_cursor]*60*ambience/20;
				left+=value;right+=value;
				if(i&1)ambient_cursor=(ambient_cursor+1)%wind.size();
			}
			for (auto& voice : voices)
			{
				if (!voice.samples)
					continue;
				const int value = (*voice.samples)[voice.offset];
				if (!voice.half_rate || (i & 1))
					++voice.offset;
				const int gain=(levels>>(voice.category*5))&31;
				left += value * voice.left * gain / 20;
				right += value * voice.right * gain / 20;
				if (voice.offset == voice.samples->size())
					voice.samples = nullptr;
			}
			buffer[2 * i] = static_cast<int16_t>(std::min(32767, std::max(-32768, left * master / (256 * 20))));
			buffer[2 * i + 1] = static_cast<int16_t>(std::min(32767, std::max(-32768, right * master / (256 * 20))));
		}
		if (sceAudioOutOutput(m_port, buffer) < 0)
			break;
		buffer_index ^= 1;
	}
	sceAudioOutOutput(m_port, nullptr);
	m_running.store(false, std::memory_order_release);
	return 0;
}

void VitaGameAudio::release()
{
	if (m_thread >= 0)
	{
		m_stopping.store(true);
		sceKernelSignalSema(m_wake, 1);
		sceKernelWaitThreadEnd(m_thread, nullptr, nullptr);
		sceKernelDeleteThread(m_thread);
		m_thread = -1;
	}
	if (m_wake >= 0)
	{
		sceKernelDeleteSema(m_wake);
		m_wake = -1;
	}
	if (m_radio >= 0)
	{
		sceIoClose(m_radio);
		m_radio = -1;
	}
	m_read.store(0);
	m_write.store(0);
	m_radio_gains.store(0);
	if (m_port >= 0)
	{
		sceAudioOutReleasePort(m_port);
		m_port = -1;
	}
}
