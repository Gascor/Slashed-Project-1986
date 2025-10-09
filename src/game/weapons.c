#include "engine/weapons.h"

#include <math.h>
#include <string.h>

#define MAX_RUNTIME_RECOIL 22.0f

#define WEAPON_DEF(ID, NAME, SHORT, CAT, MODE, CLIP, RESERVE, RATE, RELOAD, RECOIL, RECOVERY, SPREAD, BURST, PELLETS, SPEED) \
    [ID] = {                                                                                                           \
        .name = NAME,                                                                                                   \
        .short_name = SHORT,                                                                                            \
        .category = CAT,                                                                                                \
        .fire_mode = MODE,                                                                                              \
        .clip_size = (CLIP),                                                                                            \
        .ammo_reserve = (RESERVE),                                                                                      \
        .fire_rate = (RATE),                                                                                            \
        .reload_time = (RELOAD),                                                                                        \
        .recoil_per_shot = (RECOIL),                                                                                    \
        .recoil_recovery = (RECOVERY),                                                                                  \
        .spread = (SPREAD),                                                                                             \
        .burst_count = (BURST),                                                                                         \
        .pellets_per_shot = (PELLETS),                                                                                  \
        .projectile_speed = (SPEED)                                                                                     \
    }

static const WeaponDefinition g_weapon_definitions[WEAPON_ID_COUNT] = {
    WEAPON_DEF(WEAPON_ID_NONE,             "Unarmed",             "None",    WEAPON_CATEGORY_SPECIAL, WEAPON_FIRE_MODE_SEMI, 0,   0,   0.0f,  0.0f,  0.0f,  10.0f, 0.0f, 0, 0, 0.0f),
    WEAPON_DEF(WEAPON_ID_GLOCK_17,         "Glock 17",            "G17",     WEAPON_CATEGORY_PISTOL,  WEAPON_FIRE_MODE_SEMI, 17, 102, 7.5f,  1.8f,  2.5f,  9.5f,  0.6f, 0, 1, 380.0f),
    WEAPON_DEF(WEAPON_ID_BERETTA_M9,       "Beretta M9",          "M9",      WEAPON_CATEGORY_PISTOL,  WEAPON_FIRE_MODE_SEMI, 15, 90,  7.0f,  1.9f,  2.8f,  9.0f,  0.65f,0, 1, 370.0f),
    WEAPON_DEF(WEAPON_ID_HK_USP_TACTICAL,  "HK USP Tactical",     "USP",     WEAPON_CATEGORY_PISTOL,  WEAPON_FIRE_MODE_SEMI, 12, 84,  6.8f,  2.0f,  3.0f,  9.2f,  0.7f, 0, 1, 360.0f),
    WEAPON_DEF(WEAPON_ID_SIG_P320,         "SIG P320",            "P320",    WEAPON_CATEGORY_PISTOL,  WEAPON_FIRE_MODE_SEMI, 17, 102, 7.2f,  1.8f,  2.6f,  9.4f,  0.6f, 0, 1, 380.0f),
    WEAPON_DEF(WEAPON_ID_FN_FIVE_SEVEN,    "FN Five-seveN",       "Five7",   WEAPON_CATEGORY_PISTOL,  WEAPON_FIRE_MODE_SEMI, 20, 120, 8.0f,  2.1f,  2.2f,  9.8f,  0.55f,0,1, 420.0f),
    WEAPON_DEF(WEAPON_ID_CZ_SHADOW2,       "CZ Shadow 2",         "Shadow",  WEAPON_CATEGORY_PISTOL,  WEAPON_FIRE_MODE_SEMI, 19, 114, 7.6f,  1.9f,  2.4f,  9.7f,  0.6f, 0, 1, 380.0f),
    WEAPON_DEF(WEAPON_ID_DESERT_EAGLE,     "Desert Eagle .50",    "DE50",    WEAPON_CATEGORY_PISTOL,  WEAPON_FIRE_MODE_SEMI, 7,  56,  3.2f,  2.6f,  6.5f,  7.5f,  1.2f, 0, 1, 410.0f),
    WEAPON_DEF(WEAPON_ID_WALTHER_PPQ,      "Walther PPQ",         "PPQ",     WEAPON_CATEGORY_PISTOL,  WEAPON_FIRE_MODE_SEMI, 15, 90,  7.1f,  1.8f,  2.5f,  9.3f,  0.62f,0,1, 370.0f),
    WEAPON_DEF(WEAPON_ID_COLT_M1911,       "Colt M1911",          "M1911",   WEAPON_CATEGORY_PISTOL,  WEAPON_FIRE_MODE_SEMI, 8,  56,  6.0f,  2.0f,  3.8f,  8.6f,  0.8f, 0, 1, 360.0f),
    WEAPON_DEF(WEAPON_ID_SIG_P226_LEGION,  "SIG P226 Legion",     "P226",    WEAPON_CATEGORY_PISTOL,  WEAPON_FIRE_MODE_SEMI, 15, 90,  6.9f,  2.0f,  2.9f,  9.1f,  0.65f,0,1, 370.0f),
    WEAPON_DEF(WEAPON_ID_M4A1,             "Colt M4A1",           "M4A1",    WEAPON_CATEGORY_RIFLE,   WEAPON_FIRE_MODE_AUTO, 30, 180, 11.0f, 2.4f,  4.2f,  8.5f,  0.45f,0,1, 880.0f),
    WEAPON_DEF(WEAPON_ID_AK74N,            "AK-74N",              "AK-74",   WEAPON_CATEGORY_RIFLE,   WEAPON_FIRE_MODE_AUTO, 30, 210, 10.5f, 2.6f,  4.8f,  8.0f,  0.5f, 0, 1, 900.0f),
    WEAPON_DEF(WEAPON_ID_HK416,            "HK416",               "HK416",   WEAPON_CATEGORY_RIFLE,   WEAPON_FIRE_MODE_AUTO, 30, 180, 11.3f, 2.4f,  4.0f,  8.8f,  0.44f,0,1, 880.0f),
    WEAPON_DEF(WEAPON_ID_SCAR_L,           "FN SCAR-L",           "SCAR-L",  WEAPON_CATEGORY_RIFLE,   WEAPON_FIRE_MODE_AUTO, 30, 180, 10.2f, 2.5f,  4.6f,  8.2f,  0.48f,0,1, 870.0f),
    WEAPON_DEF(WEAPON_ID_FAMAS_F1,         "FAMAS F1",            "FAMAS",   WEAPON_CATEGORY_RIFLE,   WEAPON_FIRE_MODE_BURST,25, 200, 14.0f, 2.3f,  4.4f,  8.4f,  0.46f,3,1, 900.0f),
    WEAPON_DEF(WEAPON_ID_GALIL_ACE23,      "Galil ACE 23",        "ACE23",   WEAPON_CATEGORY_RIFLE,   WEAPON_FIRE_MODE_AUTO, 30, 210, 10.8f, 2.5f,  4.7f,  8.1f,  0.5f, 0, 1, 880.0f),
    WEAPON_DEF(WEAPON_ID_STEYR_AUG_A3,     "Steyr AUG A3",        "AUG",     WEAPON_CATEGORY_RIFLE,   WEAPON_FIRE_MODE_AUTO, 30, 180, 11.0f, 2.4f,  4.1f,  8.9f,  0.42f,0,1, 870.0f),
    WEAPON_DEF(WEAPON_ID_G36C,             "HK G36C",             "G36C",    WEAPON_CATEGORY_RIFLE,   WEAPON_FIRE_MODE_AUTO, 30, 180, 10.6f, 2.4f,  4.3f,  8.6f,  0.47f,0,1, 880.0f),
    WEAPON_DEF(WEAPON_ID_QBZ95,            "QBZ-95",              "QBZ95",   WEAPON_CATEGORY_RIFLE,   WEAPON_FIRE_MODE_AUTO, 30, 180, 10.0f, 2.5f,  4.5f,  8.3f,  0.49f,0,1, 870.0f),
    WEAPON_DEF(WEAPON_ID_AN94,             "AN-94",               "AN-94",   WEAPON_CATEGORY_RIFLE,   WEAPON_FIRE_MODE_BURST,30, 210, 16.0f, 2.5f,  4.9f,  8.1f,  0.48f,2,1, 900.0f),
    WEAPON_DEF(WEAPON_ID_SIG_MCX_SPEAR,    "SIG MCX Spear",       "MCX",     WEAPON_CATEGORY_RIFLE,   WEAPON_FIRE_MODE_AUTO, 25, 175, 9.5f,  2.7f,  5.1f,  7.8f,  0.5f, 0, 1, 880.0f),
    WEAPON_DEF(WEAPON_ID_TAVOR_TAR21,      "IWI Tavor TAR-21",    "TAR21",   WEAPON_CATEGORY_RIFLE,   WEAPON_FIRE_MODE_AUTO, 30, 210, 10.8f, 2.4f,  4.4f,  8.5f,  0.46f,0,1, 870.0f),
    WEAPON_DEF(WEAPON_ID_ACR_SPR,          "Remington ACR SPR",   "ACR",     WEAPON_CATEGORY_RIFLE,   WEAPON_FIRE_MODE_AUTO, 30, 210, 10.4f, 2.5f,  4.2f,  8.7f,  0.45f,0,1, 880.0f),
    WEAPON_DEF(WEAPON_ID_AKM,              "AKM",                 "AKM",     WEAPON_CATEGORY_RIFLE,   WEAPON_FIRE_MODE_AUTO, 30, 210, 8.8f,  2.6f,  5.4f,  7.5f,  0.55f,0,1, 880.0f),
    WEAPON_DEF(WEAPON_ID_HK_G3A3,          "HK G3A3",             "G3A3",    WEAPON_CATEGORY_RIFLE,   WEAPON_FIRE_MODE_AUTO, 20, 140, 8.4f,  2.8f,  5.8f,  7.2f,  0.58f,0,1, 880.0f),
    WEAPON_DEF(WEAPON_ID_MP5A5,            "HK MP5A5",            "MP5",     WEAPON_CATEGORY_SMG,     WEAPON_FIRE_MODE_AUTO, 30, 240, 12.0f, 2.1f,  3.4f,  9.4f,  0.52f,0,1, 400.0f),
    WEAPON_DEF(WEAPON_ID_MP7A1,            "HK MP7A1",            "MP7",     WEAPON_CATEGORY_SMG,     WEAPON_FIRE_MODE_AUTO, 40, 240, 13.5f, 2.0f,  3.0f,  9.6f,  0.5f, 0, 1, 450.0f),
    WEAPON_DEF(WEAPON_ID_P90_TR,           "FN P90 TR",           "P90",     WEAPON_CATEGORY_SMG,     WEAPON_FIRE_MODE_AUTO, 50, 250, 14.0f, 2.2f,  2.8f,  9.8f,  0.48f,0,1, 430.0f),
    WEAPON_DEF(WEAPON_ID_UMP45,            "HK UMP45",            "UMP45",   WEAPON_CATEGORY_SMG,     WEAPON_FIRE_MODE_AUTO, 25, 200, 9.5f,  2.2f,  3.8f,  9.0f,  0.56f,0,1, 370.0f),
    WEAPON_DEF(WEAPON_ID_VECTOR_45,        "KRISS Vector .45",    "Vector",  WEAPON_CATEGORY_SMG,     WEAPON_FIRE_MODE_AUTO, 25, 200, 15.0f, 2.3f,  3.2f,  9.5f,  0.5f, 0, 1, 390.0f),
    WEAPON_DEF(WEAPON_ID_PP19_BIZON,       "PP-19 Bizon",         "Bizon",   WEAPON_CATEGORY_SMG,     WEAPON_FIRE_MODE_AUTO, 64, 320, 10.5f, 2.5f,  3.6f,  9.1f,  0.54f,0,1, 400.0f),
    WEAPON_DEF(WEAPON_ID_UZI_PRO,          "Uzi Pro",             "Uzi",     WEAPON_CATEGORY_SMG,     WEAPON_FIRE_MODE_AUTO, 32, 224, 11.5f, 2.1f,  3.9f,  8.9f,  0.58f,0,1, 380.0f),
    WEAPON_DEF(WEAPON_ID_SCORPION_EVO3,    "CZ Scorpion EVO 3",   "EVO3",    WEAPON_CATEGORY_SMG,     WEAPON_FIRE_MODE_AUTO, 30, 240, 12.8f, 2.2f,  3.3f,  9.4f,  0.52f,0,1, 400.0f),
    WEAPON_DEF(WEAPON_ID_MP9,              "B&T MP9",             "MP9",     WEAPON_CATEGORY_SMG,     WEAPON_FIRE_MODE_AUTO, 30, 240, 13.2f, 2.2f,  3.1f,  9.5f,  0.5f, 0, 1, 420.0f),
    WEAPON_DEF(WEAPON_ID_APC9,             "B&T APC9",            "APC9",    WEAPON_CATEGORY_SMG,     WEAPON_FIRE_MODE_AUTO, 30, 240, 11.8f, 2.1f,  3.2f,  9.3f,  0.51f,0,1, 410.0f),
    WEAPON_DEF(WEAPON_ID_BENELLI_M4,       "Benelli M4",          "M4",      WEAPON_CATEGORY_SHOTGUN, WEAPON_FIRE_MODE_SEMI, 8,  48,  1.8f,  2.9f,  7.2f,  6.2f,  1.4f, 0, 8,  350.0f),
    WEAPON_DEF(WEAPON_ID_REMINGTON_870,    "Remington 870",       "870",     WEAPON_CATEGORY_SHOTGUN, WEAPON_FIRE_MODE_SEMI, 6,  42,  1.5f,  3.1f,  7.5f,  6.0f,  1.5f, 0, 8,  320.0f),
    WEAPON_DEF(WEAPON_ID_MOSSBERG_590A1,   "Mossberg 590A1",      "590A1",   WEAPON_CATEGORY_SHOTGUN, WEAPON_FIRE_MODE_SEMI, 8,  40,  1.6f,  3.0f,  7.0f,  6.1f,  1.45f,0,8,  320.0f),
    WEAPON_DEF(WEAPON_ID_SPAS12,           "Franchi SPAS-12",     "SPAS12",  WEAPON_CATEGORY_SHOTGUN, WEAPON_FIRE_MODE_SEMI, 8,  48,  1.7f,  3.2f,  7.6f,  5.8f,  1.5f, 0, 8,  320.0f),
    WEAPON_DEF(WEAPON_ID_AA12,             "MPS AA-12",           "AA-12",   WEAPON_CATEGORY_SHOTGUN, WEAPON_FIRE_MODE_AUTO, 20, 100, 4.8f,  2.8f,  6.8f,  6.5f,  1.6f,0,8,  300.0f),
    WEAPON_DEF(WEAPON_ID_KELTEC_KSG,       "Kel-Tec KSG",         "KSG",     WEAPON_CATEGORY_SHOTGUN, WEAPON_FIRE_MODE_SEMI, 12, 48,  1.4f,  3.3f,  7.4f,  5.9f,  1.55f,0,8,  300.0f),
    WEAPON_DEF(WEAPON_ID_AWM_338,          "Accuracy AWM .338",   "AWM",     WEAPON_CATEGORY_SNIPER,  WEAPON_FIRE_MODE_SEMI, 5,  35,  0.8f,  3.6f,  9.5f,  5.5f,  0.2f, 1,1, 1100.0f),
    WEAPON_DEF(WEAPON_ID_BARRETT_M82A1,    "Barrett M82A1",       "M82A1",   WEAPON_CATEGORY_SNIPER,  WEAPON_FIRE_MODE_SEMI, 10, 40,  1.0f,  4.2f,  10.5f,5.0f,  0.25f,0,1, 1050.0f),
    WEAPON_DEF(WEAPON_ID_SAKO_TRG42,       "Sako TRG 42",         "TRG42",   WEAPON_CATEGORY_SNIPER,  WEAPON_FIRE_MODE_SEMI, 5,  35,  0.9f,  3.5f,  8.8f,  5.6f,  0.18f,0,1, 1080.0f),
    WEAPON_DEF(WEAPON_ID_CHEYTAC_M200,     "CheyTac M200",        "M200",    WEAPON_CATEGORY_SNIPER,  WEAPON_FIRE_MODE_SEMI, 7,  35,  0.85f, 3.8f,  9.8f,  5.2f,  0.15f,0,1, 1120.0f),
    WEAPON_DEF(WEAPON_ID_DRAGUNOV_SVD,     "Dragunov SVD",        "SVD",     WEAPON_CATEGORY_SNIPER,  WEAPON_FIRE_MODE_SEMI, 10, 60,  1.6f,  3.2f,  7.2f,  6.2f,  0.32f,0,1, 970.0f),
    WEAPON_DEF(WEAPON_ID_M24_SWS,          "M24 SWS",             "M24",     WEAPON_CATEGORY_SNIPER,  WEAPON_FIRE_MODE_SEMI, 5,  35,  0.75f, 3.4f,  8.5f,  5.7f,  0.18f,0,1, 1080.0f),
    WEAPON_DEF(WEAPON_ID_M249_SAW,         "FN M249 SAW",         "M249",    WEAPON_CATEGORY_LMG,     WEAPON_FIRE_MODE_AUTO, 100,300, 8.5f,  4.5f,  6.8f,  6.8f,  0.6f, 0, 1, 850.0f),
    WEAPON_DEF(WEAPON_ID_PKP_PECHENEG,     "PKP Pecheneg",        "PKP",     WEAPON_CATEGORY_LMG,     WEAPON_FIRE_MODE_AUTO, 100,300, 8.8f,  4.6f,  7.2f,  6.5f,  0.62f,0,1, 860.0f),
    WEAPON_DEF(WEAPON_ID_MG4,              "HK MG4",              "MG4",     WEAPON_CATEGORY_LMG,     WEAPON_FIRE_MODE_AUTO, 120,360, 9.2f,  4.2f,  6.4f,  7.1f,  0.58f,0,1, 870.0f),
    WEAPON_DEF(WEAPON_ID_RPK16,            "Kalashnikov RPK-16",  "RPK16",   WEAPON_CATEGORY_LMG,     WEAPON_FIRE_MODE_AUTO, 95, 285, 9.0f,  4.1f,  6.0f,  7.3f,  0.55f,0,1, 860.0f),
    WEAPON_DEF(WEAPON_ID_HK_MG5,           "HK MG5",              "MG5",     WEAPON_CATEGORY_LMG,     WEAPON_FIRE_MODE_AUTO, 120,360, 9.5f,  4.4f,  6.6f,  7.0f,  0.6f, 0, 1, 870.0f),
    WEAPON_DEF(WEAPON_ID_NEGEV_NG7,        "IWI Negev NG7",       "NG7",     WEAPON_CATEGORY_LMG,     WEAPON_FIRE_MODE_AUTO, 150,450, 9.0f,  4.3f,  6.9f,  6.9f,  0.6f, 0, 1, 860.0f),
};

size_t weapon_definition_count(void)
{
    return WEAPON_ID_COUNT;
}

WeaponId weapon_definition_id_by_index(size_t index)
{
    if (index >= WEAPON_ID_COUNT) {
        return WEAPON_ID_NONE;
    }
    return (WeaponId)index;
}

const WeaponDefinition *weapon_definition(WeaponId id)
{
    if (id < 0 || id >= WEAPON_ID_COUNT) {
        return NULL;
    }
    return &g_weapon_definitions[id];
}

const WeaponDefinition *weapon_definition_by_index(size_t index)
{
    if (index >= WEAPON_ID_COUNT) {
        return NULL;
    }
    return &g_weapon_definitions[index];
}

void weapon_state_clear(WeaponState *weapon)
{
    if (!weapon) {
        return;
    }
    memset(weapon, 0, sizeof(*weapon));
    weapon->id = WEAPON_ID_NONE;
    weapon->definition = weapon_definition(WEAPON_ID_NONE);
}

static void weapon_apply_definition(WeaponState *weapon,
                                    const WeaponDefinition *definition,
                                    int ammo_in_clip,
                                    int ammo_reserve)
{
    weapon->definition = definition;
    weapon->base_clip_size = definition ? definition->clip_size : 0;
    weapon->base_fire_rate = definition ? definition->fire_rate : 0.0f;
    weapon->base_recoil_recovery_rate = definition ? definition->recoil_recovery : 0.0f;
    weapon->base_spread = definition ? definition->spread : 0.0f;
    weapon->reload_time = definition ? definition->reload_time : 0.0f;
    weapon->reloading = false;
    weapon->reload_timer = 0.0f;
    weapon->cooldown = 0.0f;
    weapon->recoil = 0.0f;
    weapon->burst_shots_remaining = 0;
    weapon->burst_timer = 0.0f;

    weapon_reset_stats(weapon);

    if (ammo_in_clip >= 0) {
        weapon->ammo_in_clip = ammo_in_clip;
    } else {
        weapon->ammo_in_clip = weapon->clip_size;
    }
    if (weapon->ammo_in_clip > weapon->clip_size) {
        weapon->ammo_in_clip = weapon->clip_size;
    }

    if (ammo_reserve >= 0) {
        weapon->ammo_reserve = ammo_reserve;
    } else {
        weapon->ammo_reserve = definition ? definition->ammo_reserve : 0;
    }
    if (weapon->ammo_reserve < 0) {
        weapon->ammo_reserve = 0;
    }
}

void weapon_init(WeaponState *weapon)
{
    if (!weapon) {
        return;
    }

    weapon_state_clear(weapon);
    weapon_state_equip(weapon, WEAPON_ID_GLOCK_17, -1, -1);
}

bool weapon_state_equip(WeaponState *weapon, WeaponId id, int ammo_in_clip, int ammo_reserve)
{
    if (!weapon) {
        return false;
    }

    if (id <= WEAPON_ID_NONE || id >= WEAPON_ID_COUNT) {
        weapon_state_clear(weapon);
        return true;
    }

    const WeaponDefinition *definition = weapon_definition(id);
    if (!definition) {
        weapon_state_clear(weapon);
        return false;
    }

    weapon->id = id;
    weapon_apply_definition(weapon, definition, ammo_in_clip, ammo_reserve);
    return true;
}

WeaponId weapon_state_id(const WeaponState *weapon)
{
    if (!weapon) {
        return WEAPON_ID_NONE;
    }
    return weapon->id;
}

bool weapon_state_is_unarmed(const WeaponState *weapon)
{
    if (!weapon || !weapon->definition) {
        return true;
    }
    return weapon->id == WEAPON_ID_NONE || weapon->definition->clip_size <= 0;
}

const char *weapon_state_display_name(const WeaponState *weapon)
{
    if (!weapon || !weapon->definition) {
        return "Unarmed";
    }
    return weapon->definition->name;
}

WeaponFireMode weapon_state_fire_mode(const WeaponState *weapon)
{
    if (!weapon || !weapon->definition) {
        return WEAPON_FIRE_MODE_SEMI;
    }
    return weapon->definition->fire_mode;
}

void weapon_reset_stats(WeaponState *weapon)
{
    if (!weapon) {
        return;
    }

    if (weapon_state_is_unarmed(weapon)) {
        weapon->clip_size = 0;
        weapon->fire_rate = 0.0f;
        weapon->recoil_recovery_rate = 0.0f;
        weapon->spread = 0.0f;
        if (weapon->ammo_in_clip > 0) {
            weapon->ammo_in_clip = 0;
        }
        return;
    }

    weapon->clip_size = weapon->base_clip_size;
    weapon->fire_rate = weapon->base_fire_rate;
    weapon->recoil_recovery_rate = weapon->base_recoil_recovery_rate;
    weapon->spread = weapon->base_spread;
    if (weapon->ammo_in_clip > weapon->clip_size) {
        weapon->ammo_in_clip = weapon->clip_size;
    }
}

static void weapon_clamp_runtime(WeaponState *weapon)
{
    if (!weapon) {
        return;
    }

    if (weapon_state_is_unarmed(weapon)) {
        weapon->clip_size = 0;
        weapon->ammo_in_clip = 0;
        weapon->fire_rate = 0.0f;
        weapon->recoil_recovery_rate = 0.0f;
        return;
    }

    if (weapon->clip_size < 1) {
        weapon->clip_size = 1;
    }
    if (weapon->fire_rate < 1.0f) {
        weapon->fire_rate = 1.0f;
    }
    if (weapon->recoil_recovery_rate < 0.1f) {
        weapon->recoil_recovery_rate = 0.1f;
    }
    if (weapon->ammo_in_clip > weapon->clip_size) {
        weapon->ammo_in_clip = weapon->clip_size;
    }
    if (weapon->ammo_in_clip < 0) {
        weapon->ammo_in_clip = 0;
    }
}

void weapon_apply_item(WeaponState *weapon, const WeaponItem *item)
{
    if (!weapon || !item || !item->equipped || weapon_state_is_unarmed(weapon)) {
        return;
    }

    switch (item->type) {
    case WEAPON_ITEM_EXTENDED_MAG: {
        const float factor = (item->amount > -0.9f) ? (1.0f + item->amount) : 0.1f;
        weapon->clip_size = (int)((float)weapon->clip_size * factor + 0.5f);
        break;
    }
    case WEAPON_ITEM_RECOIL_STABILIZER:
        weapon->recoil_recovery_rate *= (1.0f + item->amount);
        break;
    case WEAPON_ITEM_TRIGGER_TUNING:
        weapon->fire_rate *= (1.0f + item->amount);
        break;
    default:
        break;
    }

    weapon_clamp_runtime(weapon);
}

void weapon_apply_inventory(WeaponState *weapon, const GameInventory *inventory)
{
    if (!weapon || !inventory) {
        return;
    }

    weapon_reset_stats(weapon);
    for (size_t i = 0; i < inventory->weapon_item_count; ++i) {
        weapon_apply_item(weapon, &inventory->weapon_items[i]);
    }
    weapon_clamp_runtime(weapon);
}

static void weapon_consume_ammo(WeaponState *weapon, WeaponUpdateResult *result)
{
    if (!weapon || weapon->ammo_in_clip <= 0) {
        return;
    }

    weapon->ammo_in_clip -= 1;
    if (weapon->fire_rate > 0.0f) {
        weapon->cooldown = 1.0f / weapon->fire_rate;
    } else {
        weapon->cooldown = 0.5f;
    }

    float recoil_add = weapon->definition ? weapon->definition->recoil_per_shot : 3.0f;
    weapon->recoil += recoil_add;
    if (weapon->recoil > MAX_RUNTIME_RECOIL) {
        weapon->recoil = MAX_RUNTIME_RECOIL;
    }

    result->fired = true;

    if (weapon->ammo_in_clip == 0 && weapon->ammo_reserve > 0) {
        weapon->reloading = true;
        weapon->reload_timer = weapon->reload_time;
        result->started_reload = true;
        weapon->burst_shots_remaining = 0;
    }
}

WeaponUpdateResult weapon_update(WeaponState *weapon, const WeaponUpdateInput *input)
{
    WeaponUpdateResult result = {0};
    if (!weapon || !input) {
        return result;
    }

    float dt = input->dt;
    if (dt < 0.0f) {
        dt = 0.0f;
    }

    if (weapon->cooldown > 0.0f) {
        weapon->cooldown -= dt;
        if (weapon->cooldown < 0.0f) {
            weapon->cooldown = 0.0f;
        }
    }

    if (weapon->recoil > 0.0f) {
        weapon->recoil -= weapon->recoil_recovery_rate * dt;
        if (weapon->recoil < 0.0f) {
            weapon->recoil = 0.0f;
        }
    }

    if (weapon->reloading) {
        weapon->reload_timer -= dt;
        if (weapon->reload_timer <= 0.0f) {
            int needed = weapon->clip_size - weapon->ammo_in_clip;
            if (needed > 0 && weapon->ammo_reserve > 0) {
                if (needed > weapon->ammo_reserve) {
                    needed = weapon->ammo_reserve;
                }
                weapon->ammo_in_clip += needed;
                weapon->ammo_reserve -= needed;
            }
            weapon->reload_timer = 0.0f;
            weapon->reloading = false;
            result.finished_reload = true;
        } else {
            return result;
        }
    }

    if (weapon_state_is_unarmed(weapon)) {
        return result;
    }

    if (input->reload_requested && !weapon->reloading && weapon->ammo_in_clip < weapon->clip_size && weapon->ammo_reserve > 0) {
        weapon->reloading = true;
        weapon->reload_timer = weapon->reload_time;
        weapon->burst_shots_remaining = 0;
        result.started_reload = true;
        return result;
    }

    if (weapon->cooldown > 0.0f || weapon->ammo_in_clip <= 0) {
        return result;
    }

    const WeaponDefinition *definition = weapon->definition;
    if (!definition) {
        return result;
    }

    if (definition->fire_mode == WEAPON_FIRE_MODE_BURST && input->fire_released) {
        weapon->burst_shots_remaining = 0;
    }

    switch (definition->fire_mode) {
    case WEAPON_FIRE_MODE_SEMI:
        if (input->fire_pressed) {
            weapon_consume_ammo(weapon, &result);
        }
        break;
    case WEAPON_FIRE_MODE_AUTO:
        if (input->fire_down) {
            weapon_consume_ammo(weapon, &result);
        }
        break;
    case WEAPON_FIRE_MODE_BURST:
        if (weapon->burst_shots_remaining > 0) {
            weapon_consume_ammo(weapon, &result);
            if (weapon->burst_shots_remaining > 0) {
                --weapon->burst_shots_remaining;
            }
            break;
        }
        if (input->fire_pressed) {
            int burst_count = definition->burst_count > 0 ? definition->burst_count : 1;
            weapon->burst_shots_remaining = burst_count - 1;
            weapon_consume_ammo(weapon, &result);
        }
        break;
    default:
        break;
    }

    return result;
}

WeaponItem weapon_item_make(WeaponItemType type, float amount)
{
    WeaponItem item;
    item.type = type;
    item.amount = amount;
    item.equipped = true;
    return item;
}

const char *weapon_item_display_name(WeaponItemType type)
{
    switch (type) {
    case WEAPON_ITEM_EXTENDED_MAG:
        return "Extended Mag";
    case WEAPON_ITEM_RECOIL_STABILIZER:
        return "Recoil Stabilizer";
    case WEAPON_ITEM_TRIGGER_TUNING:
        return "Trigger Tuning";
    default:
        break;
    }
    return "Attachment";
}

void inventory_init(GameInventory *inventory)
{
    if (!inventory) {
        return;
    }
    memset(inventory, 0, sizeof(*inventory));
}

void inventory_clear(GameInventory *inventory)
{
    if (inventory) {
        memset(inventory, 0, sizeof(*inventory));
    }
}

bool inventory_add_item(GameInventory *inventory, const WeaponItem *item)
{
    if (!inventory || !item || inventory->weapon_item_count >= GAME_MAX_WEAPON_ITEMS) {
        return false;
    }

    inventory->weapon_items[inventory->weapon_item_count++] = *item;
    return true;
}

size_t inventory_apply_equipped(const GameInventory *inventory, WeaponState *weapon)
{
    if (!inventory || !weapon) {
        return 0U;
    }

    size_t applied = 0U;
    weapon_reset_stats(weapon);
    for (size_t i = 0; i < inventory->weapon_item_count; ++i) {
        if (inventory->weapon_items[i].equipped) {
            weapon_apply_item(weapon, &inventory->weapon_items[i]);
            ++applied;
        }
    }
    weapon_clamp_runtime(weapon);
    return applied;
}
