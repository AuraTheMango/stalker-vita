#pragma once
#include "vita_inventory.h"
struct VitaItemArt{unsigned x,y,columns,rows;const char* name_key;const char* description_key;const char* french;const char* english;};
inline constexpr VitaItemArt vita_item_art[]={
{18,6,1,1,"wpn-pm","enc_weapons1_wpn-pm","PM","PM"},
{11,12,1,1,"ammo-9x18-fmj","enc_weapons1_ammo_ammo-9x18-fmj","9x18 FMJ","9x18 FMJ"},
{18,8,2,1,"st_knife","enc_weapons1_wpn-knife","Couteau","Knife"},
{3,19,1,1,"item_flash_esc_name","item_flash_esc_desc","Cle USB d'Allegro","Allegro USB drive"},
{6,14,2,1,"Medkit","enc_equipment_medi_medkit1","Trousse de soins","Medkit"},
{3,18,1,1,"bandage","enc_equipment_medi_bandage1","Bandage","Bandage"},
{11,9,1,1,"Bread","enc_equipment_food_bread1","Pain","Bread"},
{19,12,1,1,"Kolbasa","enc_equipment_food_kolbasa1","Saucisson","Sausage"},
{9,13,1,1,"Conserva","enc_equipment_food_conserva1","Conserve","Canned food"},
{11,0,1,1,"energy_drink_name","enc_equipment_energydrink","Boisson","Energy drink"},
{0,18,2,2,"outfit_soldier_name","outfit_soldier_description","Veste novice","Novice jacket"},
{12,0,2,2,"bandit_outfit_name","bandit_outfit_description","Tenue de bandit","Bandit jacket"},
{6,1,3,1,"wpn-bm16","enc_weapons1_wpn-bm16","BM16","BM16"},
{5,5,5,2,"wpn-ak74","enc_weapons1_wpn-ak74","AK-74","AK-74"},
{13,9,2,1,"ammo-12x70-buck","enc_weapons1_ammo_ammo-12x70-buck","12x70 chevrotine","12x70 buckshot"},
{11,10,2,1,"ammo-12x76-zhekan","enc_weapons1_ammo_ammo-12x76-zhekan","12x76 balle","12x76 slug"},
{16,11,2,1,"ammo-5.45x39-fmj","enc_weapons1_ammo_ammo-5.45x39-fmj","5.45x39 FMJ","5.45x39 FMJ"},
{12,11,2,1,"ammo-5.45x39-ap","enc_weapons1_ammo_ammo-5.45x39-ap","5.45x39 AP","5.45x39 AP"},
{13,12,1,1,"","enc_weapons1_ammo_ammo-9x18-pbp","9x18 PBP","9x18 PBP"},
{10,9,1,1,"grenade-rgd5","enc_weapons1_grenade-rgd5","RGD-5","RGD-5"},
{9,9,1,1,"grenade-f1","enc_weapons1_grenade-f1","F1","F1"},
{18,1,1,1,"wpn-addon-silencer","enc_weapons1_wpn-addon-silencer","Silencieux (PM)","Silencer (PM)"},
{18,2,2,1,"wpn-addon-scope","enc_weapons1_wpn-addon-scope","Lunette (AK-74)","Scope (AK-74)"},
};
static_assert(sizeof(vita_item_art)/sizeof(vita_item_art[0])==vita_item_count);
