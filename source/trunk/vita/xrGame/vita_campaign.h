#pragma once
#include <array>
#include <cmath>
#include <string>
#include <cstdint>
constexpr unsigned vita_zone_count=4;
enum class VitaZone : unsigned { Cordon, Garbage, Agroprom, AgropromUnderground };
enum class VitaStory : unsigned { Start, North, SeriyDefended, SeriySpoken, MoleDefended, MoleSpoken, StashFound, StrelokFlash };
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
 {"l03u_agr_underground","Souterrains d'Agroprom","Agroprom Underground","PDA",{},
 {{{-52.6651f,-6.5172f,11.1983f},{-51.1456f,-6.5081f,10.5045f},{-50.3122f,-6.5248f,10.6821f},
 {-52.7430f,-6.5109f,10.3439f},{-51.4358f,-6.4903f,11.0943f},{-50.9309f,-6.5260f,12.0503f}}}},
}};
struct VitaZoneExit { unsigned from,to;std::array<float,3> center,arrival;float yaw;float radius=11.f,height=20.f; };
inline const std::array<VitaZoneExit,6> vita_zone_exits={{
 {0,1,{29.34641457f,16.55619621f,686.24163818f},{33.57174683f,0.48402405f,-296.03451538f},0.f},
 {1,0,{33.57174683f,0.48402405f,-310.03451538f},{29.34641457f,16.55619621f,672.24163818f},3.14159265f},
 {1,2,{-293.05130005f,11.19600010f,-20.98036957f},{272.31765747f,0.32664949f,11.29901409f},-1.5707963f},
 {2,1,{286.31765747f,0.32664949f,11.29901409f},{-279.05130005f,11.19600010f,-20.98036957f},1.5707963f},
 {2,3,{-75.06146f,1.77897f,9.56245f},{-38.98491f,-3.89550f,38.02235f},-3.13717f,4.f,6.f},
 {3,2,{-38.98491f,-3.89550f,38.02235f},{-75.11304f,1.79279f,11.22266f},-2.23859f,2.8f,3.f},
}};
inline bool vita_exit_near(const VitaZoneExit&e,unsigned zone,const std::array<float,3>&feet){
 return e.from==zone&&std::hypot(feet[0]-e.center[0],feet[2]-e.center[2])<e.radius&&std::abs(feet[1]-e.center[1])<e.height;
}
inline bool vita_exit_allowed(const VitaZoneExit&e,unsigned story){
 return e.to<e.from||(e.to==1&&story>=1)||(e.to==2&&story>=3)||(e.to==3&&story>=5);
}


// Original all.spawn cache room, flash and ladder. Keep vertical tests: the tunnels overlap.
inline constexpr std::array<float,3> vita_strelok_flash={-69.73111f,-5.71550f,-64.84039f};
inline constexpr std::array<float,3> vita_strelok_room={-76.02141f,-6.8732f,-71.06113f};
// Walkable approach to the original ladder, outside the collision of its pipe.
inline constexpr std::array<float,3> vita_strelok_ladder_bottom={-87.f,-15.870f,-71.5f};
inline constexpr std::array<float,3> vita_strelok_ladder_top={-78.f,-6.8732f,-70.86f};
inline bool vita_stash_near(const std::array<float,3>&feet,const std::array<float,3>&target,float radius,float height){
 return std::hypot(feet[0]-target[0],feet[2]-target[2])<radius&&std::abs(feet[1]-target[1])<height;
}
inline unsigned vita_stash_progress(unsigned zone,unsigned story,const std::array<float,3>&feet){
 return zone==3&&story==5&&vita_stash_near(feet,vita_strelok_room,7.f,1.5f)?6:story;
}
