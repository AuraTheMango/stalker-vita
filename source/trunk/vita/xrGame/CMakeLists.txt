cmake_minimum_required(VERSION 3.16)

project(stalker_vita VERSION 0.0.28 LANGUAGES CXX)

include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
add_subdirectory("../xrCore" xrCore)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../xrRender" xrRender)

set(STALKER_ASSET_DIR "${CMAKE_SOURCE_DIR}/../../../../assets" CACHE PATH "Private audio and generated LiveArea assets")
set(VITA_APP_NAME "Stalker Vita")
set(VITA_TITLEID "STLK00001")
# The Vita APP_VER field uses NN.NN. The project release version names the VPK.
set(VITA_APP_VERSION "01.34")

add_executable(stalker_vita
	main.cpp
	vita_save.cpp
 vita_postprocess.cpp
	vita_cform_ground.cpp
	vita_trader_ui.cpp
	vita_trader_model.cpp
	vita_hud.cpp
	vita_game_audio.cpp
)

target_compile_options(stalker_vita PRIVATE
	-fno-operator-names
	-Os
)

set_property(TARGET stalker_vita APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/vita_sce_gap.ld")
target_link_options(stalker_vita PRIVATE
    "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/vita_sce_gap.ld"
	# Keep enough space after the executable segment for vita-elf-create metadata.
	-Wl,-z,separate-code
	# Observe the SDK calls made by vitaGL during presentation.
	-Wl,--wrap=sceGxmEndScene
	-Wl,--wrap=sceGxmDisplayQueueAddEntry
	-Wl,--wrap=sceKernelWaitSema
)

target_link_libraries(stalker_vita PRIVATE
	xrCore_vita
	xrRender_vita_gl
	SceAppMgr_stub
	SceCtrl_stub
	SceTouch_stub
	ScePgf_stub
	SceAudio_stub
)

vita_create_self(stalker_vita.self stalker_vita UNSAFE)
vita_create_vpk(stalker_vita-${PROJECT_VERSION}.vpk ${VITA_TITLEID} stalker_vita.self
	VERSION ${VITA_APP_VERSION}
	NAME ${VITA_APP_NAME}
    FILE "${STALKER_ASSET_DIR}/audio/rifle_shot.pcm" audio/rifle_shot.pcm
    FILE "${STALKER_ASSET_DIR}/audio/rifle_reload.pcm" audio/rifle_reload.pcm
    FILE "${STALKER_ASSET_DIR}/audio/shotgun_reload.pcm" audio/shotgun_reload.pcm
    FILE "${STALKER_ASSET_DIR}/audio/grenade_explosion.pcm" audio/grenade_explosion.pcm
    FILE "${STALKER_ASSET_DIR}/audio/silenced_shot.pcm" audio/silenced_shot.pcm
	FILE "${STALKER_ASSET_DIR}/audio/pm_shot.pcm" audio/pm_shot.pcm
	FILE "${STALKER_ASSET_DIR}/audio/guard_shot.pcm" audio/guard_shot.pcm
	FILE "${STALKER_ASSET_DIR}/audio/pm_reload.pcm" audio/pm_reload.pcm
	FILE "${STALKER_ASSET_DIR}/audio/empty.pcm" audio/empty.pcm
	FILE "${STALKER_ASSET_DIR}/audio/trader_intro.pcm" audio/trader_intro.pcm
	FILE "${STALKER_ASSET_DIR}/audio/wolf_orders.pcm" audio/wolf_orders.pcm
	FILE "${STALKER_ASSET_DIR}/audio/scout_call.pcm" audio/scout_call.pcm
	FILE "${STALKER_ASSET_DIR}/audio/scout_ready.pcm" audio/scout_ready.pcm
	FILE "${STALKER_ASSET_DIR}/audio/scout_attack.pcm" audio/scout_attack.pcm
	FILE "${STALKER_ASSET_DIR}/audio/scout_victory.pcm" audio/scout_victory.pcm
	FILE "${STALKER_ASSET_DIR}/audio/bandit_alert1.pcm" audio/bandit_alert1.pcm
	FILE "${STALKER_ASSET_DIR}/audio/bandit_alert2.pcm" audio/bandit_alert2.pcm
	FILE "${STALKER_ASSET_DIR}/audio/bandit_attack1.pcm" audio/bandit_attack1.pcm
	FILE "${STALKER_ASSET_DIR}/audio/bandit_attack2.pcm" audio/bandit_attack2.pcm
	FILE "${STALKER_ASSET_DIR}/audio/bandit_hit1.pcm" audio/bandit_hit1.pcm
	FILE "${STALKER_ASSET_DIR}/audio/bandit_hit2.pcm" audio/bandit_hit2.pcm
	FILE "${STALKER_ASSET_DIR}/audio/bandit_death1.pcm" audio/bandit_death1.pcm
	FILE "${STALKER_ASSET_DIR}/audio/bandit_death2.pcm" audio/bandit_death2.pcm
	FILE "${STALKER_ASSET_DIR}/audio/bunker_radio.pcm" audio/bunker_radio.pcm
	FILE "${STALKER_ASSET_DIR}/audio/main_menu.pcm" audio/main_menu.pcm
	FILE "${STALKER_ASSET_DIR}/audio/shotgun.pcm" audio/shotgun.pcm
	FILE "${STALKER_ASSET_DIR}/audio/bandit_1_enemy_1.pcm" audio/bandit_1_enemy_1.pcm
	FILE "${STALKER_ASSET_DIR}/audio/bandit_1_enemy_2.pcm" audio/bandit_1_enemy_2.pcm
	FILE "${STALKER_ASSET_DIR}/audio/bandit_1_enemy_3.pcm" audio/bandit_1_enemy_3.pcm
	FILE "${STALKER_ASSET_DIR}/audio/bandit_1_attack_1.pcm" audio/bandit_1_attack_1.pcm
	FILE "${STALKER_ASSET_DIR}/audio/bandit_1_attack_2.pcm" audio/bandit_1_attack_2.pcm
	FILE "${STALKER_ASSET_DIR}/audio/bandit_1_attack_3.pcm" audio/bandit_1_attack_3.pcm
	FILE "${STALKER_ASSET_DIR}/audio/bandit_1_hit_1.pcm" audio/bandit_1_hit_1.pcm
	FILE "${STALKER_ASSET_DIR}/audio/bandit_1_hit_2.pcm" audio/bandit_1_hit_2.pcm
	FILE "${STALKER_ASSET_DIR}/audio/bandit_1_hit_3.pcm" audio/bandit_1_hit_3.pcm
	FILE "${STALKER_ASSET_DIR}/audio/bandit_1_death_1.pcm" audio/bandit_1_death_1.pcm
	FILE "${STALKER_ASSET_DIR}/audio/bandit_1_death_2.pcm" audio/bandit_1_death_2.pcm
	FILE "${STALKER_ASSET_DIR}/audio/bandit_1_death_3.pcm" audio/bandit_1_death_3.pcm
	FILE "${STALKER_ASSET_DIR}/audio/bandit_2_enemy_1.pcm" audio/bandit_2_enemy_1.pcm
	FILE "${STALKER_ASSET_DIR}/audio/bandit_2_enemy_2.pcm" audio/bandit_2_enemy_2.pcm
	FILE "${STALKER_ASSET_DIR}/audio/bandit_2_enemy_3.pcm" audio/bandit_2_enemy_3.pcm
	FILE "${STALKER_ASSET_DIR}/audio/bandit_2_attack_1.pcm" audio/bandit_2_attack_1.pcm
	FILE "${STALKER_ASSET_DIR}/audio/bandit_2_attack_2.pcm" audio/bandit_2_attack_2.pcm
	FILE "${STALKER_ASSET_DIR}/audio/bandit_2_attack_3.pcm" audio/bandit_2_attack_3.pcm
	FILE "${STALKER_ASSET_DIR}/audio/bandit_2_hit_1.pcm" audio/bandit_2_hit_1.pcm
	FILE "${STALKER_ASSET_DIR}/audio/bandit_2_hit_2.pcm" audio/bandit_2_hit_2.pcm
	FILE "${STALKER_ASSET_DIR}/audio/bandit_2_hit_3.pcm" audio/bandit_2_hit_3.pcm
	FILE "${STALKER_ASSET_DIR}/audio/bandit_2_death_1.pcm" audio/bandit_2_death_1.pcm
	FILE "${STALKER_ASSET_DIR}/audio/bandit_2_death_2.pcm" audio/bandit_2_death_2.pcm
	FILE "${STALKER_ASSET_DIR}/audio/bandit_2_death_3.pcm" audio/bandit_2_death_3.pcm
	FILE "${STALKER_ASSET_DIR}/audio/bandit_3_enemy_1.pcm" audio/bandit_3_enemy_1.pcm
	FILE "${STALKER_ASSET_DIR}/audio/bandit_3_enemy_2.pcm" audio/bandit_3_enemy_2.pcm
	FILE "${STALKER_ASSET_DIR}/audio/bandit_3_enemy_3.pcm" audio/bandit_3_enemy_3.pcm
	FILE "${STALKER_ASSET_DIR}/audio/bandit_3_attack_1.pcm" audio/bandit_3_attack_1.pcm
	FILE "${STALKER_ASSET_DIR}/audio/bandit_3_attack_2.pcm" audio/bandit_3_attack_2.pcm
	FILE "${STALKER_ASSET_DIR}/audio/bandit_3_attack_3.pcm" audio/bandit_3_attack_3.pcm
	FILE "${STALKER_ASSET_DIR}/audio/bandit_3_hit_1.pcm" audio/bandit_3_hit_1.pcm
	FILE "${STALKER_ASSET_DIR}/audio/bandit_3_hit_2.pcm" audio/bandit_3_hit_2.pcm
	FILE "${STALKER_ASSET_DIR}/audio/bandit_3_hit_3.pcm" audio/bandit_3_hit_3.pcm
	FILE "${STALKER_ASSET_DIR}/audio/bandit_3_death_1.pcm" audio/bandit_3_death_1.pcm
	FILE "${STALKER_ASSET_DIR}/audio/bandit_3_death_2.pcm" audio/bandit_3_death_2.pcm
	FILE "${STALKER_ASSET_DIR}/audio/bandit_3_death_3.pcm" audio/bandit_3_death_3.pcm
	FILE "${STALKER_ASSET_DIR}/audio/ally_1_1.pcm" audio/ally_1_1.pcm
	FILE "${STALKER_ASSET_DIR}/audio/ally_1_2.pcm" audio/ally_1_2.pcm
	FILE "${STALKER_ASSET_DIR}/audio/ally_1_3.pcm" audio/ally_1_3.pcm
	FILE "${STALKER_ASSET_DIR}/audio/ally_2_1.pcm" audio/ally_2_1.pcm
	FILE "${STALKER_ASSET_DIR}/audio/ally_2_2.pcm" audio/ally_2_2.pcm
	FILE "${STALKER_ASSET_DIR}/audio/ally_2_3.pcm" audio/ally_2_3.pcm
	FILE "${STALKER_ASSET_DIR}/audio/ally_3_1.pcm" audio/ally_3_1.pcm
	FILE "${STALKER_ASSET_DIR}/audio/ally_3_2.pcm" audio/ally_3_2.pcm
	FILE "${STALKER_ASSET_DIR}/audio/ally_3_3.pcm" audio/ally_3_3.pcm
	FILE "${STALKER_ASSET_DIR}/audio/ambient_wind.pcm" audio/ambient_wind.pcm
	FILE "${STALKER_ASSET_DIR}/audio/bird1.pcm" audio/bird1.pcm
	FILE "${STALKER_ASSET_DIR}/audio/bird2.pcm" audio/bird2.pcm
	FILE "${STALKER_ASSET_DIR}/audio/crow1.pcm" audio/crow1.pcm
	FILE "${STALKER_ASSET_DIR}/audio/crow2.pcm" audio/crow2.pcm
	FILE "${STALKER_ASSET_DIR}/sce_sys/icon0.png" sce_sys/icon0.png
	FILE "${STALKER_ASSET_DIR}/sce_sys/livearea/contents/bg.png" sce_sys/livearea/contents/bg.png
	FILE "${STALKER_ASSET_DIR}/sce_sys/livearea/contents/startup.png" sce_sys/livearea/contents/startup.png
	FILE "${STALKER_ASSET_DIR}/sce_sys/livearea/contents/template.xml" sce_sys/livearea/contents/template.xml
)
