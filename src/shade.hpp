#pragma once

#include "mods/api.h"
#include "mods/svc/config.h"
#include "mods/svc/log.hpp"

#include "dolphin/types.h"

#include <cstdint>

class daAlink_c;
class J3DModel;
class J3DModelData;
class JKRHeap;

extern const ConfigService* svc_config;

struct ShadeOptions {
    bool body;
    bool allOutfits;
    bool outfit[4];
    bool silent;
    bool wolf;
    bool sword;
    bool swordFor[3];
    bool shield;
    bool shieldFor[3];
};

enum { kOutfitHero, kOutfitOrdon, kOutfitZora, kOutfitMagic };
enum { kSwordWood, kSwordOrdon, kSwordMaster };
enum { kShieldOrdon, kShieldWood, kShieldHylian };

const ShadeOptions& shade_options();

bool arc_request(const char* name);
int arc_poll(const char* name);
J3DModelData* arc_load(const char* name, const char* file);
void* arc_load_anm(const char* name, const char* file);
void arc_free_data(J3DModelData* data);
JKRHeap* arc_heap_if_any();
J3DModel* shade_create_model(J3DModelData* data, u32 modelFlag, u32 differedDlistFlag);
void shade_free_model(J3DModel* model);

void shade_init();
void wolf_init();
void shade_frame(daAlink_c* alink);
void wolf_frame();

bool wolf_model_draw(daAlink_c* alink, J3DModel* model, int noDraw);
J3DModel* wolf_local_model();

bool shade_is_him(daAlink_c* alink);

void voice_init();
