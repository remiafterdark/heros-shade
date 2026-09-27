

#include "shade.hpp"

#include "mods/service.hpp"
#include "mods/svc/hook.hpp"
#include "mods/svc/ui.h"

#include <cstdint>

DEFINE_MOD();
IMPORT_SERVICE(ConfigService, svc_config);
IMPORT_SERVICE(HookService, svc_hook);
IMPORT_SERVICE(LogService, svc_log);
IMPORT_SERVICE(UiService, svc_ui);

namespace {

struct Var {
    const char* name;
    bool fallback;
    ConfigVarHandle handle;
};

enum VarId {
    kVarBody, kVarAllOutfits, kVarHero, kVarOrdon, kVarZora, kVarMagic, kVarSilent,
    kVarWolf,
    kVarSword, kVarSwordWood, kVarSwordOrdon, kVarSwordMaster,
    kVarShield, kVarShieldOrdon, kVarShieldWood, kVarShieldHylian,
    kVarCount,
};
Var s_vars[kVarCount] = {
    {"body", true, 0},
    {"all_outfits", true, 0},
    {"outfit_hero", true, 0},
    {"outfit_ordon", true, 0},
    {"outfit_zora", true, 0},
    {"outfit_magic", true, 0},
    {"silent", true, 0},
    {"wolf", true, 0},
    {"sword", true, 0},
    {"sword_wood", true, 0},
    {"sword_ordon", true, 0},
    {"sword_master", true, 0},
    {"shield", true, 0},
    {"shield_ordon", true, 0},
    {"shield_wood", true, 0},
    {"shield_hylian", true, 0},
};

ShadeOptions s_options{};

bool get(VarId id) {
    bool value = s_vars[id].fallback;
    if (s_vars[id].handle != 0) svc_config->get_bool(mod_ctx, s_vars[id].handle, &value);
    return value;
}

void read_options() {
    s_options.body = get(kVarBody);
    s_options.allOutfits = get(kVarAllOutfits);
    s_options.outfit[kOutfitHero] = get(kVarHero);
    s_options.outfit[kOutfitOrdon] = get(kVarOrdon);
    s_options.outfit[kOutfitZora] = get(kVarZora);
    s_options.outfit[kOutfitMagic] = get(kVarMagic);
    s_options.silent = get(kVarSilent);
    s_options.wolf = get(kVarWolf);
    s_options.sword = get(kVarSword);
    s_options.swordFor[kSwordWood] = get(kVarSwordWood);
    s_options.swordFor[kSwordOrdon] = get(kVarSwordOrdon);
    s_options.swordFor[kSwordMaster] = get(kVarSwordMaster);
    s_options.shield = get(kVarShield);
    s_options.shieldFor[kShieldOrdon] = get(kVarShieldOrdon);
    s_options.shieldFor[kShieldWood] = get(kVarShieldWood);
    s_options.shieldFor[kShieldHylian] = get(kVarShieldHylian);
}

void* as_data(VarId id) { return reinterpret_cast<void*>(static_cast<uintptr_t>(id)); }

bool needs_off(ModContext*, void* data) {
    return !get(static_cast<VarId>(reinterpret_cast<uintptr_t>(data)));
}

bool outfit_off(ModContext*, void*) {
    return !get(kVarBody) || get(kVarAllOutfits);
}

void toggle(UiElementHandle pane, const char* label, VarId id, const char* help, UiPredicateFn disabled = nullptr,
    VarId needs = kVarCount) {
    if (s_vars[id].handle == 0) return;
    UiControlDesc desc = UI_CONTROL_DESC_INIT;
    desc.kind = UI_CONTROL_TOGGLE;
    desc.label = label;
    desc.binding = UI_BINDING_CONFIG_VAR;
    desc.config_var = s_vars[id].handle;
    desc.help_rml = help;
    if (disabled != nullptr) {
        desc.is_disabled = disabled;
        desc.user_data = as_data(needs);
    }
    svc_ui->pane_add_control(mod_ctx, pane, &desc, nullptr);
}

ModResult build_panel(ModContext*, UiElementHandle pane, void*, ModError*) {
    svc_ui->pane_add_section(mod_ctx, pane, "Hero's Shade");
    toggle(pane, "Be the Hero's Shade", kVarBody, "His armour and ghostly body in place of Link's.");
    toggle(pane, "In every outfit", kVarAllOutfits, "Off: only in the outfits picked below.", needs_off, kVarBody);
    toggle(pane, "Hero's clothes", kVarHero, nullptr, outfit_off);
    toggle(pane, "Ordon clothes", kVarOrdon, nullptr, outfit_off);
    toggle(pane, "Zora armor", kVarZora, nullptr, outfit_off);
    toggle(pane, "Magic armor", kVarMagic, nullptr, outfit_off);
    toggle(pane, "Silent", kVarSilent, "No voice while you are him. The wolf keeps his.");

    svc_ui->pane_add_section(mod_ctx, pane, "Wolf");
    toggle(pane, "Golden wolf", kVarWolf, "The wolf is his golden wolf.");

    svc_ui->pane_add_section(mod_ctx, pane, "Sword");
    toggle(pane, "His sword", kVarSword, "His sword, and his sheath, in place of the swords picked below.");
    toggle(pane, "Over the Wooden Sword", kVarSwordWood, nullptr, needs_off, kVarSword);
    toggle(pane, "Over the Ordon Sword", kVarSwordOrdon, nullptr, needs_off, kVarSword);
    toggle(pane, "Over the Master Sword", kVarSwordMaster, nullptr, needs_off, kVarSword);

    svc_ui->pane_add_section(mod_ctx, pane, "Shield");
    toggle(pane, "His shield", kVarShield, "His shield in place of the shields picked below.");
    toggle(pane, "Over the Ordon Shield", kVarShieldOrdon, nullptr, needs_off, kVarShield);
    toggle(pane, "Over the Wooden Shield", kVarShieldWood, nullptr, needs_off, kVarShield);
    toggle(pane, "Over the Hylian Shield", kVarShieldHylian, nullptr, needs_off, kVarShield);
    return MOD_OK;
}

}

const ShadeOptions& shade_options() {
    return s_options;
}

extern "C" {
MOD_EXPORT ModResult mod_initialize(ModError*) {
    for (Var& v : s_vars) {
        ConfigVarDesc desc = CONFIG_VAR_DESC_INIT;
        desc.name = v.name;
        desc.type = CONFIG_VAR_BOOL;
        desc.default_bool = v.fallback;
        if (svc_config->register_var(mod_ctx, &desc, &v.handle) != MOD_OK) v.handle = 0;
    }
    read_options();
    shade_init();
    UiModsPanelDesc panel = UI_MODS_PANEL_DESC_INIT;
    panel.build = build_panel;
    svc_ui->register_mods_panel(mod_ctx, &panel);
    return MOD_OK;
}

MOD_EXPORT ModResult mod_update(ModError*) {
    read_options();
    return MOD_OK;
}

MOD_EXPORT ModResult mod_shutdown(ModError*) {
    return MOD_OK;
}
}
