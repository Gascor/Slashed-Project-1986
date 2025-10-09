#pragma once

#include <stdbool.h>

typedef struct HudState {
    float crosshair_base;
    float crosshair_spread;
    float damage_flash;
    float network_indicator_timer;
} HudState;

typedef struct Renderer Renderer;
typedef struct PlayerState PlayerState;
typedef struct WeaponState WeaponState;
typedef struct NetworkClientStats NetworkClientStats;

// HUD functions
void hud_init(HudState *hud);
void hud_update(HudState *hud, const PlayerState *player, const WeaponState *weapon, float dt);
void hud_render(const HudState *hud, Renderer *renderer, const PlayerState *player, 
                const WeaponState *weapon, const NetworkClientStats *net_stats);
void hud_show_damage_flash(HudState *hud);
void hud_set_crosshair_spread(HudState *hud, float spread);