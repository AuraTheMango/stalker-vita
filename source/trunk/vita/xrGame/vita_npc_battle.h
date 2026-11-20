
#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// Small Cordon encounter controller. Navigation and sight always use the original collision mesh.
struct VitaNpcAgent
{
	std::array<float,3> position{}, home{};
	unsigned health = 100, shots = 0;
	int target = -1;
	bool walking = false, engaged = false;
	float yaw = 0.f;
	uint64_t next_shot = 0, shot_time = 0, reload_until = 0, last_update = 0;
};
struct VitaNpcShot { bool ally; unsigned shooter; int target; unsigned damage; float distance; };
class VitaNpcBattle
{
public:
	std::array<VitaNpcAgent,6> guards{};
	std::array<VitaNpcAgent,3> scouts{};
	uint64_t next_tick = 0;
	unsigned cursor=0;
	bool active = false;

	void pause(uint64_t duration)
	{
		if(next_tick) next_tick+=duration;
		for(auto* bank:{guards.data(),scouts.data()})
		{
			const size_t count=bank==guards.data()?guards.size():scouts.size();
			for(size_t i=0;i<count;++i)
			{
				auto& a=bank[i];
				if(a.next_shot)a.next_shot+=duration;
				if(a.shot_time)a.shot_time+=duration;
				if(a.reload_until)a.reload_until+=duration;
				if(a.last_update)a.last_update+=duration;
			}
		}
	}

	template<class Ground>
	std::vector<VitaNpcShot> update(Ground& ground,uint64_t now,const std::array<float,3>& player,
		bool group, unsigned player_health)
	{
		std::vector<VitaNpcShot> events;
		if(now<next_tick)return events;
		float dt=.2f;
		// One guard and, on alternate updates, one ally: spread LOS and collision work over frames.
		next_tick=now+25000;
		const unsigned turn=cursor++%6;
		const auto distance=[](const auto&a,const auto&b){return std::hypot(a[0]-b[0],a[2]-b[2]);};
		const auto sight=[&](const auto&a,const auto&b,float range)
		{
			const float origin[]={a[0],a[1]+1.45f,a[2]};
			const float dx=b[0]-a[0],dy=b[1]+1.2f-origin[1],dz=b[2]-a[2];
			const float d=std::sqrt(dx*dx+dy*dy+dz*dz);
			if(d<.5f||d>range)return false;
			const float direction[]={dx/d,dy/d,dz/d};
			return !ground.blocks_ray(origin,direction,d-.2f);
		};
		const auto move=[&](VitaNpcAgent&a,float dx,float dz,float speed)
		{
			const float len=std::hypot(dx,dz);if(len<.01f)return;
			float x=a.position[0]+dx/len*speed*dt,z=a.position[2]+dz/len*speed*dt,y=a.position[1];
			ground.slide_move(a.position[0],a.position[2],y,x,z);
			if(std::hypot(x-a.home[0],z-a.home[2])<10.f &&
				ground.height_at(x,z,y,.45f,.65f,y))
			{
				a.walking=std::hypot(x-a.position[0],z-a.position[2])>.012f;
				if(a.walking)a.position={x,y,z};
			}
		};
		for(unsigned i=0;i<guards.size();++i)
		{
			if(i!=turn)continue;
			auto&a=guards[i];a.walking=false;a.target=-1;
			dt=a.last_update?std::min(.3f,float(now-a.last_update)/1000000.f):.2f;a.last_update=now;
			if(!a.health)continue;
			std::array<float,3> target=player;
			float nearest=sight(a.position,player,32.f)&&player_health?distance(a.position,player):1000.f;
			if(group)
				for(unsigned j=0;j<scouts.size();++j)
					if(scouts[j].health && sight(a.position,scouts[j].position,32.f))
					{
						const float d=distance(a.position,scouts[j].position);
						if(d<nearest){nearest=d;target=scouts[j].position;a.target=int(j);}
					}
			const bool visible=nearest<1000.f;
			if(visible){a.engaged=active=true;a.yaw=-std::atan2(target[0]-a.position[0],target[2]-a.position[2]);}
			if(!visible)
			{
				// Short patrol around the spawn; collisions prevent crossing fences or walls.
				const float phase=float(now%20000000)/1000000.f*.42f+i*1.71f;
				move(a,a.home[0]+std::sin(phase)*2.f-a.position[0],
					a.home[2]+std::cos(phase)*2.f-a.position[2],.65f);
				if(a.walking)a.yaw=-phase;
				continue;
			}
			if(now<a.reload_until)
			{
				move(a,a.home[0]-a.position[0],a.home[2]-a.position[2],.8f);
				continue;
			}
			// Advance to range, then alternate shooting and short lateral changes of cover.
			const float dx=target[0]-a.position[0],dz=target[2]-a.position[2];
			const bool reposition=(now/1800000+i)%3==0;
			if(nearest>17.f)move(a,dx,dz,1.15f);
			else if(reposition)move(a,((i&1)?-dz:dz),((i&1)?dx:-dx),.85f);
			if(!a.next_shot)a.next_shot=now+650000+i*95000;
			if(!a.walking && now>=a.next_shot)
			{
				a.shot_time=now;a.next_shot=now+1250000+(i%3)*170000;
				const unsigned damage=(++a.shots+i)%3?8:0;
				events.push_back({false,i,a.target,damage,nearest});
				if(a.shots%8==0){a.reload_until=now+2700000;a.next_shot=a.reload_until;}
			}
		}
		if(group)
			for(unsigned i=0;i<scouts.size();++i)
			{
				if(turn%2||i!=turn/2)continue;
				auto&a=scouts[i];a.target=-1;
				dt=a.last_update?std::min(.3f,float(now-a.last_update)/1000000.f):.2f;a.last_update=now;
				if(!a.health)continue;
				float nearest=1000.f;
				for(unsigned j=0;j<guards.size();++j)
					if(guards[j].health&&sight(a.position,guards[j].position,30.f))
					{
						const float d=distance(a.position,guards[j].position);
						if(d<nearest){nearest=d;a.target=int(j);}
					}
				if(a.target<0){a.engaged=false;continue;}
				a.engaged=active=true;a.walking=false;
				const auto& target=guards[a.target].position;
				a.yaw=-std::atan2(target[0]-a.position[0],target[2]-a.position[2]);
				if(nearest>15.f)move(a,target[0]-a.position[0],target[2]-a.position[2],1.1f);
				if(!a.next_shot)a.next_shot=now+900000+i*420000;
				if(!a.walking&&now>=a.next_shot&&now>=a.reload_until)
				{
					a.shot_time=now;a.next_shot=now+1400000+i*230000;
					// Shotgun effectiveness falls sharply beyond close range.
					const unsigned damage=(++a.shots+i)%(nearest<14.f?2:3)==0?1:0;
					events.push_back({true,i,a.target,damage,nearest});
					if(a.shots%2==0){a.reload_until=now+3200000;a.next_shot=a.reload_until;}
				}
			}
		return events;
	}
};
