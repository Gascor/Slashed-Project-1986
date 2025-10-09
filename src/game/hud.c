#include "engine/hud.h"
#include "engine/player.h"
#include "engine/weapons.h"
#include "engine/network.h"
#include "engine/renderer.h"

#include <string.h>
#include <math.h>

void hud_init(HudState *hud)
{
    memset(hud, 0, sizeof(*hud));
    hud->crosshair_base = 0.02f;
    hud->crosshair_spread = 0.0f;
    hud->damage_flash = 0.0f;
    hud->network_indicator_timer = 0.0f;
}

void hud_update(HudState *hud, const PlayerState *player, const WeaponState *weapon, float dt)
{
    // Update crosshair spread based on weapon recoil
    hud->crosshair_spread = hud->crosshair_base + (weapon->recoil * 0.01f);
    
    // Update damage flash
    if (hud->damage_flash > 0.0f) {
        hud->damage_flash -= dt * 2.0f; // Flash duration
        if (hud->damage_flash < 0.0f) {
            hud->damage_flash = 0.0f;
        }
    }
    
    // Update network indicator
    if (hud->network_indicator_timer > 0.0f) {
        hud->network_indicator_timer -= dt;
        if (hud->network_indicator_timer < 0.0f) {
            hud->network_indicator_timer = 0.0f;
        }
    }
    
    (void)player; // Suppress unused variable warning for now
}

void hud_render(const HudState *hud, Renderer *renderer, const PlayerState *player, 
                const WeaponState *weapon, const NetworkClientStats *net_stats)
{
    // This would contain the actual HUD rendering code
    // For now, just suppress unused parameter warnings
    (void)hud;
    (void)renderer;
    (void)player;
    (void)weapon;
    (void)net_stats;
    
    // TODO: Implement HUD rendering
    // - Health and armor bars
    // - Ammo counter
    // - Crosshair
    // - Network stats
    // - Damage flash overlay
}

void hud_show_damage_flash(HudState *hud)
{
    hud->damage_flash = 1.0f; // Full intensity
}

void hud_set_crosshair_spread(HudState *hud, float spread)
{
    hud->crosshair_spread = hud->crosshair_base + spread;
}