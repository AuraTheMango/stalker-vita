#pragma once
#include <array>
#include <cmath>
#include <string>
#include <cstdint>
constexpr unsigned vita_zone_count=3;
enum class VitaZone : unsigned { Cordon, Garbage, Agroprom };
enum class VitaStory : unsigned { Start, North, SeriyDefended, SeriySpoken, MoleDefended, MoleSpoken };
struct VitaCampaignZone {
 const char* directory;const char* french;const char* english;const char* leader;
 std::array<std::array<float,3>,3> allies;
 std::array<std::array<float,3>,6> enemies;
};
inline const std::array<VitaCampaignZone,vita_zone_count> vita_campaign_zones={{
 {"l01_escape","Cordon","Cordon","Sidorovitch",{},{}},
 {"l02_garbage","D\xe9" "charge","Garbage","Seriy",
 {{{-64.80000305f,0.89999992f,9.60000038f},{-65.43804169f,0.95255733f,5.59999990f},{-62.38321686f,0.75535583f,7.67736864f}}},
 {{{-112.97131348f,0.43993551f,11.35588455f},{-112.97131348f,0.43993551f,12.55588436f},{-115.15847015f,0.43582308f,12.58610725f},{-113.97125244f,0.40320161f,10.55954266f},{-113.97131348f,0.43805525f,11.85588455f},{-114.77130890f,0.43655103f,11.35588455f}}}},
 {"l03_agroprom","Agroprom","Agroprom","La Taupe",
 {{{36.34757996f,2.50003338f,-14.48793125f},{46.22327423f,-0.19999886f,33.30349350f},{32.03531647f,0.00000000f,31.14222527f}}},
 {{{55.65224075f,-0.19998488f,-22.16777420f},{51.81308365f,0.00001030f,-19.11696053f},{58.05597687f,-0.19998787f,-23.08341217f},{-7.20311928f,-0.19999631f,27.49285316f},{-8.66508579f,-0.19999570f,26.25509644f},{1.72623515f,-0.21799757f,33.08926773f}}}},
}};
struct VitaZoneExit { unsigned from,to;std::array<float,3> center,arrival;float yaw; };
inline const std::array<VitaZoneExit,4> vita_zone_exits={{
 {0,1,{29.34641457f,16.55619621f,686.24163818f},{33.57174683f,0.48402405f,-296.03451538f},0.f},
 {1,0,{33.57174683f,0.48402405f,-310.03451538f},{29.34641457f,16.55619621f,672.24163818f},3.14159265f},
 {1,2,{-293.05130005f,11.19600010f,-20.98036957f},{272.31765747f,0.32664949f,11.29901409f},-1.5707963f},
 {2,1,{286.31765747f,0.32664949f,11.29901409f},{-279.05130005f,11.19600010f,-20.98036957f},1.5707963f},
}};
inline bool vita_exit_near(const VitaZoneExit&e,unsigned zone,const std::array<float,3>&feet){
 return e.from==zone&&std::hypot(feet[0]-e.center[0],feet[2]-e.center[2])<11.f&&std::abs(feet[1]-e.center[1])<20.f;
}
inline bool vita_exit_allowed(const VitaZoneExit&e,unsigned story){
 return e.to<e.from||(e.to==1&&story>=1)||(e.to==2&&story>=3);
}
