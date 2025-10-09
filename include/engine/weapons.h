#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "engine/math.h"

typedef enum WeaponCategory {
    WEAPON_CATEGORY_PISTOL = 0,
    WEAPON_CATEGORY_SMG,
    WEAPON_CATEGORY_RIFLE,
    WEAPON_CATEGORY_SHOTGUN,
    WEAPON_CATEGORY_SNIPER,
    WEAPON_CATEGORY_LMG,
    WEAPON_CATEGORY_SPECIAL
} WeaponCategory;

typedef enum WeaponFireMode {
    WEAPON_FIRE_MODE_SEMI = 0,
    WEAPON_FIRE_MODE_AUTO,
    WEAPON_FIRE_MODE_BURST
} WeaponFireMode;

typedef enum WeaponId {
    WEAPON_ID_NONE = 0,
    WEAPON_ID_GLOCK_17,
    WEAPON_ID_BERETTA_M9,
    WEAPON_ID_HK_USP_TACTICAL,
    WEAPON_ID_SIG_P320,
    WEAPON_ID_FN_FIVE_SEVEN,
    WEAPON_ID_CZ_SHADOW2,
    WEAPON_ID_DESERT_EAGLE,
    WEAPON_ID_WALTHER_PPQ,
    WEAPON_ID_COLT_M1911,
    WEAPON_ID_SIG_P226_LEGION,
    WEAPON_ID_M4A1,
    WEAPON_ID_AK74N,
    WEAPON_ID_HK416,
    WEAPON_ID_SCAR_L,
    WEAPON_ID_FAMAS_F1,
    WEAPON_ID_GALIL_ACE23,
    WEAPON_ID_STEYR_AUG_A3,
    WEAPON_ID_G36C,
    WEAPON_ID_QBZ95,
    WEAPON_ID_AN94,
    WEAPON_ID_SIG_MCX_SPEAR,
    WEAPON_ID_TAVOR_TAR21,
    WEAPON_ID_ACR_SPR,
    WEAPON_ID_AKM,
    WEAPON_ID_HK_G3A3,
    WEAPON_ID_MP5A5,
    WEAPON_ID_MP7A1,
    WEAPON_ID_P90_TR,
    WEAPON_ID_UMP45,
    WEAPON_ID_VECTOR_45,
    WEAPON_ID_PP19_BIZON,
    WEAPON_ID_UZI_PRO,
    WEAPON_ID_SCORPION_EVO3,
    WEAPON_ID_MP9,
    WEAPON_ID_APC9,
    WEAPON_ID_BENELLI_M4,
    WEAPON_ID_REMINGTON_870,
    WEAPON_ID_MOSSBERG_590A1,
    WEAPON_ID_SPAS12,
    WEAPON_ID_AA12,
    WEAPON_ID_KELTEC_KSG,
    WEAPON_ID_AWM_338,
    WEAPON_ID_BARRETT_M82A1,
    WEAPON_ID_SAKO_TRG42,
    WEAPON_ID_CHEYTAC_M200,
    WEAPON_ID_DRAGUNOV_SVD,
    WEAPON_ID_M24_SWS,
    WEAPON_ID_M249_SAW,
    WEAPON_ID_PKP_PECHENEG,
    WEAPON_ID_MG4,
    WEAPON_ID_RPK16,
    WEAPON_ID_HK_MG5,
    WEAPON_ID_NEGEV_NG7,
    WEAPON_ID_COUNT
} WeaponId;

typedef struct WeaponDefinition {
    const char *name;
    const char *short_name;
    WeaponCategory category;
    WeaponFireMode fire_mode;
    int clip_size;
    int ammo_reserve;
    float fire_rate;
    float reload_time;
    float recoil_per_shot;
    float recoil_recovery;
    float spread;
    int burst_count;
    int pellets_per_shot;
    float projectile_speed;
} WeaponDefinition;

typedef enum WeaponItemType {
    WEAPON_ITEM_NONE = 0,
    WEAPON_ITEM_EXTENDED_MAG,
    WEAPON_ITEM_RECOIL_STABILIZER,
    WEAPON_ITEM_TRIGGER_TUNING
} WeaponItemType;

typedef struct WeaponItem {
    WeaponItemType type;
    float amount;
    bool equipped;
} WeaponItem;

typedef struct WeaponState {
    WeaponId id;
    const WeaponDefinition *definition;
    int clip_size;
    int ammo_in_clip;
    int ammo_reserve;
    float fire_rate;
    float cooldown;
    float reload_time;
    float reload_timer;
    bool reloading;
    float recoil;
    float recoil_recovery_rate;
    float spread;
    int base_clip_size;
    float base_fire_rate;
    float base_recoil_recovery_rate;
    float base_spread;
    int burst_shots_remaining;
    float burst_timer;
} WeaponState;

#define GAME_MAX_WEAPON_ITEMS 16

typedef struct GameInventory {
    WeaponItem weapon_items[GAME_MAX_WEAPON_ITEMS];
    size_t weapon_item_count;
} GameInventory;

// Weapon definitions
size_t weapon_definition_count(void);
WeaponId weapon_definition_id_by_index(size_t index);
const WeaponDefinition *weapon_definition(WeaponId id);
const WeaponDefinition *weapon_definition_by_index(size_t index);

// Weapon state helpers
void weapon_init(WeaponState *weapon);
void weapon_state_clear(WeaponState *weapon);
bool weapon_state_equip(WeaponState *weapon, WeaponId id, int ammo_in_clip, int ammo_reserve);
WeaponId weapon_state_id(const WeaponState *weapon);
bool weapon_state_is_unarmed(const WeaponState *weapon);
const char *weapon_state_display_name(const WeaponState *weapon);
WeaponFireMode weapon_state_fire_mode(const WeaponState *weapon);
void weapon_reset_stats(WeaponState *weapon);

// Weapon update
typedef struct WeaponUpdateInput {
    float dt;
    bool fire_down;
    bool fire_pressed;
    bool fire_released;
    bool reload_requested;
} WeaponUpdateInput;

typedef struct WeaponUpdateResult {
    bool fired;
    bool started_reload;
    bool finished_reload;
} WeaponUpdateResult;

WeaponUpdateResult weapon_update(WeaponState *weapon, const WeaponUpdateInput *input);
void weapon_apply_item(WeaponState *weapon, const WeaponItem *item);
void weapon_apply_inventory(WeaponState *weapon, const GameInventory *inventory);
WeaponItem weapon_item_make(WeaponItemType type, float amount);
const char *weapon_item_display_name(WeaponItemType type);

// Inventory functions
void inventory_init(GameInventory *inventory);
void inventory_clear(GameInventory *inventory);
bool inventory_add_item(GameInventory *inventory, const WeaponItem *item);
size_t inventory_apply_equipped(const GameInventory *inventory, WeaponState *weapon);
