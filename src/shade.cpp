

#include "shade.hpp"

#include "mods/svc/hook.hpp"

#include "d/actor/d_a_alink.h"
#include "d/actor/d_a_mirror.h"
#include "d/d_com_inf_game.h"
#include "d/d_kankyo.h"
#include "f_op/f_op_actor_mng.h"
#include "m_Do/m_Do_ext.h"
#include "JSystem/J3DGraphAnimator/J3DJoint.h"
#include "JSystem/J3DGraphAnimator/J3DModel.h"
#include "JSystem/J3DGraphAnimator/J3DModelData.h"
#include "JSystem/J3DGraphBase/J3DSys.h"
#include "JSystem/J3DGraphBase/J3DTransform.h"
#include "JSystem/JKernel/JKRHeap.h"
#include "JSystem/JUtility/JUTNameTab.h"

#include <cstring>

DEFINE_HOOK_SYMBOL("daAlink_c::modelDraw", void(daAlink_c*, J3DModel*, int), ShadeModelDrawHook);
DEFINE_HOOK_SYMBOL("daAlink_c::setItemMatrix", void(daAlink_c*, int), ShadeItemMatrixHook);
DEFINE_HOOK_SYMBOL("daAlink_c::shadowDraw", void(daAlink_c*), ShadeShadowHook);
DEFINE_HOOK(&daAlink_c::draw, ShadeAlinkDrawHook);

namespace {

const char kArc[] = "KN_a";
const char kArmour[] = "kn_a.bmd";
const char kGhost[] = "kn_am_gt.bmd";
const char kSheath[] = "kn_pod.bmd";
const int kMaxJoints = 64;
const f32 kBackDepth = 1.5f;

struct Finger {
    const char* name;
    f32 closeDegrees;
    bool right;
};
const Finger kFingers[4] = {{"fingerL1", -65.0f, false}, {"fingerL2", -55.0f, false},
    {"fingerR1", -65.0f, true}, {"fingerR2", -55.0f, true}};
const char* const kHidden[2] = {"weaponL", "weaponR"};

enum ArcState { kArcNone, kArcMounting, kArcReady, kArcFailed };
ArcState s_arc = kArcNone;

bool arc_ready() {
    if (s_arc == kArcReady) return true;
    if (s_arc == kArcFailed) return false;
    if (s_arc == kArcNone) s_arc = arc_request(kArc) ? kArcMounting : kArcFailed;
    if (s_arc == kArcFailed) return false;
    const int ready = arc_poll(kArc);
    if (ready == 0) return false;
    s_arc = ready > 0 ? kArcReady : kArcFailed;
    return s_arc == kArcReady;
}

bool s_learned = false;
int s_count = 0;
int s_parent[kMaxJoints];
int s_order[kMaxJoints];
Mtx s_restWorld[kMaxJoints];
Mtx s_restLocal[kMaxJoints];
S16Vec s_restAngle[kMaxJoints];
Vec s_offset[kMaxJoints];
f32 s_rootHeight = 1.0f;
int s_fingerJoint[4] = {-1, -1, -1, -1};
int s_hiddenJoint[2] = {-1, -1};

void rotation_only(const Mtx in, Mtx out) {
    cMtx_copy(in, out);
    out[0][3] = out[1][3] = out[2][3] = 0.0f;
}

int joint_named(J3DModelData* data, const char* name) {
    JUTNameTab* names = data->getJointName();
    if (names == nullptr || name == nullptr) return -1;
    for (u16 i = 0; i < data->getJointNum(); ++i) {
        const char* n = names->getName(i);
        if (n != nullptr && std::strcmp(n, name) == 0) return i;
    }
    return -1;
}

void walk_him(J3DJoint* joint, const Mtx parentWorld, int parent, Mtx* world, int& n) {
    for (; joint != nullptr; joint = joint->getYounger()) {
        const int j = joint->getJntNo();
        if (j >= kMaxJoints) continue;
        const J3DTransformInfo& info = joint->getTransformInfo();
        Mtx local;
        J3DGetTranslateRotateMtx(info, local);
        MTXConcat(parentWorld, local, world[j]);
        rotation_only(local, s_restLocal[j]);
        s_restAngle[j] = info.mRotation;
        s_offset[j] = info.mTranslate;
        s_parent[j] = parent;
        s_order[n++] = j;
        walk_him(joint->getChild(), world[j], j, world, n);
    }
}

void walk_world(J3DJoint* joint, const Mtx parentWorld, Mtx* world) {
    for (; joint != nullptr; joint = joint->getYounger()) {
        const int j = joint->getJntNo();
        if (j >= kMaxJoints) continue;
        Mtx local;
        J3DGetTranslateRotateMtx(joint->getTransformInfo(), local);
        MTXConcat(parentWorld, local, world[j]);
        walk_world(joint->getChild(), world[j], world);
    }
}

void learn(J3DModelData* data) {
    if (s_learned) return;
    s_count = data->getJointNum();
    Mtx identity;
    MTXIdentity(identity);
    Mtx world[kMaxJoints];
    int n = 0;
    walk_him(data->getJointNodePointer(0), identity, -1, world, n);
    for (int j = 0; j < s_count; ++j) rotation_only(world[j], s_restWorld[j]);
    s_rootHeight = world[0][1][3] > 1.0f ? world[0][1][3] : 1.0f;
    for (int f = 0; f < 4; ++f) s_fingerJoint[f] = joint_named(data, kFingers[f].name);
    for (int h = 0; h < 2; ++h) s_hiddenJoint[h] = joint_named(data, kHidden[h]);
    s_learned = true;
}

struct LinkRest {
    J3DModelData* data = nullptr;
    u16 joints = 0;
    f32 rootY = 0.0f;
    Mtx rest[kMaxJoints];
    int follow[kMaxJoints];
    f32 scale = 1.0f;
};
LinkRest s_linkRests[4];
int s_linkRestNext = 0;

LinkRest* link_rest(J3DModelData* data, J3DModelData* his) {
    if (data == nullptr || data->getJointNum() == 0 || data->getJointNum() > kMaxJoints) return nullptr;
    const f32 rootY = data->getJointNodePointer(0)->getTransformInfo().mTranslate.y;
    for (LinkRest& r : s_linkRests) {
        if (r.data == data && r.joints == data->getJointNum() && r.rootY == rootY) return &r;
    }
    LinkRest& r = s_linkRests[s_linkRestNext];
    s_linkRestNext = (s_linkRestNext + 1) % 4;
    Mtx identity;
    MTXIdentity(identity);
    Mtx world[kMaxJoints];
    walk_world(data->getJointNodePointer(0), identity, world);
    for (int j = 0; j < data->getJointNum(); ++j) rotation_only(world[j], r.rest[j]);
    r.data = data;
    r.joints = data->getJointNum();
    r.rootY = rootY;
    r.scale = world[0][1][3] > 1.0f ? world[0][1][3] / s_rootHeight : 1.0f;
    JUTNameTab* names = his->getJointName();
    for (int j = 0; j < s_count; ++j) {
        const char* name = names != nullptr ? names->getName(static_cast<u16>(j)) : nullptr;
        r.follow[j] = name != nullptr ? joint_named(data, name) : -1;
    }
    return &r;
}

struct Rig {
    J3DModel* model = nullptr;
    J3DModel* ghost = nullptr;

    mDoExt_invisibleModel inv;
    bool invReady = false;
    dKy_tevstr_c ghostTev;
    int ghostTevRoom = -1000;
    f32 grip[2] = {0.0f, 0.0f};
    f32 scale = 1.0f;
    Mtx pose[kMaxJoints];
    Mtx rigid[kMaxJoints];
};
Rig s_rig;
bool s_rigFailed = false;

const Mtx* s_posing = nullptr;
int s_posingCount = 0;

int pose_callback(J3DJoint* joint, int phase) {
    if (phase != 0 || s_posing == nullptr) return 1;
    const u16 j = joint->getJntNo();
    J3DModel* model = j3dSys.getModel();
    if (j >= s_posingCount || model == nullptr) return 1;
    Mtx m;
    cMtx_copy(s_posing[j], m);
    model->setAnmMtx(j, m);
    cMtx_copy(m, J3DSys::mCurrentMtx);
    return 1;
}

J3DModel* posed_model(const char* file) {
    J3DModelData* data = arc_load(kArc, file);
    if (data == nullptr) return nullptr;
    if (data->getJointNum() > kMaxJoints) {
        arc_free_data(data);
        return nullptr;
    }
    J3DModel* model = shade_create_model(data, 0x80000, 0x11000284);
    if (model == nullptr) {
        arc_free_data(data);
        return nullptr;
    }
    for (u16 j = 0; j < data->getJointNum(); ++j) data->getJointNodePointer(j)->setCallBack(pose_callback);
    return model;
}

void rig_free() {
    if (s_rig.invReady && s_rig.inv.mpPackets != nullptr) JKR_DELETE_ARRAY(s_rig.inv.mpPackets);
    for (J3DModel* m : {s_rig.model, s_rig.ghost}) {
        if (m == nullptr) continue;
        J3DModelData* data = m->getModelData();
        shade_free_model(m);
        arc_free_data(data);
    }
    s_rig = Rig{};
    s_rig.inv.mpPackets = nullptr;
    s_rig.inv.mModel = nullptr;
}

bool rig_build() {
    s_rig.inv.mpPackets = nullptr;
    s_rig.inv.mModel = nullptr;
    s_rig.model = posed_model(kArmour);
    if (s_rig.model == nullptr) {
        rig_free();
        return false;
    }
    learn(s_rig.model->getModelData());
    s_rig.ghost = posed_model(kGhost);

    JKRHeap* heap = arc_heap_if_any();
    JKRHeap* previous = heap != nullptr ? heap->becomeCurrentHeap() : nullptr;
    s_rig.invReady = s_rig.ghost != nullptr && s_rig.inv.create(s_rig.ghost, 1) != 0;
    if (previous != nullptr) previous->becomeCurrentHeap();
    if (!s_rig.invReady) {
        rig_free();
        return false;
    }
    dKy_tevstr_init(&s_rig.ghostTev, 0, 0xFF);
    mods::log::info("heros_shade: he is ready ({} joints)", s_count);
    return true;
}

int finger_of(int joint) {
    for (int f = 0; f < 4; ++f) {
        if (s_fingerJoint[f] == joint) return f;
    }
    return -1;
}

void retarget(J3DModel* link, const LinkRest& lr) {
    Mtx rot[kMaxJoints];
    Vec pos[kMaxJoints];
    const f32 s = s_rig.scale;
    for (int k = 0; k < s_count; ++k) {
        const int j = s_order[k];
        const int p = s_parent[j];
        const int l = lr.follow[j];
        const int finger = finger_of(j);
        if (l >= 0) {
            Mtx now, inv, delta;
            rotation_only(link->getAnmMtx(l), now);
            MTXInverse(lr.rest[l], inv);
            MTXConcat(now, inv, delta);
            MTXConcat(delta, s_restWorld[j], rot[j]);
        } else if (p >= 0 && finger >= 0) {
            const Finger& f = kFingers[finger];
            const f32 bend = s_rig.grip[f.right ? 1 : 0] * f.closeDegrees * (f.right ? -1.0f : 1.0f);
            const S16Vec& a = s_restAngle[j];
            Mtx local;
            J3DGetTranslateRotateMtx(a.x, static_cast<s16>(a.y + bend * (32768.0f / 180.0f)), a.z, 0.0f,
                0.0f, 0.0f, local);
            MTXConcat(rot[p], local, rot[j]);
        } else if (p >= 0) {
            MTXConcat(rot[p], s_restLocal[j], rot[j]);
        } else {
            rotation_only(link->getAnmMtx(0), rot[j]);
        }
        if (p < 0) {
            MtxP centre = link->getAnmMtx(l >= 0 ? l : 0);
            pos[j].x = centre[0][3];
            pos[j].y = centre[1][3];
            pos[j].z = centre[2][3];
        } else {
            Vec along;
            MTXMultVec(rot[p], &s_offset[j], &along);
            pos[j].x = pos[p].x + along.x * s;
            pos[j].y = pos[p].y + along.y * s;
            pos[j].z = pos[p].z + along.z * s;
        }

        const f32 drawn = (j == s_hiddenJoint[0] || j == s_hiddenJoint[1]) ? s * 0.0001f : s;
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                s_rig.pose[j][r][c] = rot[j][r][c] * drawn;
                s_rig.rigid[j][r][c] = rot[j][r][c];
            }
        }
        s_rig.pose[j][0][3] = s_rig.rigid[j][0][3] = pos[j].x;
        s_rig.pose[j][1][3] = s_rig.rigid[j][1][3] = pos[j].y;
        s_rig.pose[j][2][3] = s_rig.rigid[j][2][3] = pos[j].z;
    }
}

int his_joint_for(const LinkRest& lr, int l) {
    for (int j = 0; j < s_count; ++j) {
        if (lr.follow[j] == l) return j;
    }
    return -1;
}

void approach(f32& value, f32 target) {
    const f32 step = 0.2f;
    if (value < target) {
        value = value + step > target ? target : value + step;
    } else {
        value = value - step < target ? target : value - step;
    }
}

void calc_posed(J3DModel* model, J3DModel* link) {
    model->setBaseTRMtx(link->getBaseTRMtx());
    s_posing = s_rig.pose;
    s_posingCount = s_count;
    model->calc();
    s_posing = nullptr;
}

void rig_draw(J3DModel* link, bool gripL, bool gripR, dKy_tevstr_c* tev) {
    LinkRest* lr = link_rest(link->getModelData(), s_rig.model->getModelData());
    if (lr == nullptr) return;
    s_rig.scale = lr->scale;
    approach(s_rig.grip[0], gripL ? 1.0f : 0.0f);
    approach(s_rig.grip[1], gripR ? 1.0f : 0.0f);
    retarget(link, *lr);

    calc_posed(s_rig.model, link);
    g_env_light.setLightTevColorType_MAJI(s_rig.model, tev);
    mDoExt_modelEntryDL(s_rig.model);
    daMirror_c::entry(s_rig.model);

    calc_posed(s_rig.ghost, link);
    const int room = tev != nullptr ? tev->room_no : 0;
    if (room != s_rig.ghostTevRoom) {
        dKy_tevstr_init(&s_rig.ghostTev, static_cast<s8>(room), 0xFF);
        s_rig.ghostTevRoom = room;
    }
    MtxP base = link->getBaseTRMtx();
    cXyz at(base[0][3], base[1][3], base[2][3]);
    g_env_light.settingTevStruct(7, &at, &s_rig.ghostTev);
    s_rig.ghostTev.TevColor.r = 0;
    s_rig.ghostTev.TevColor.g = 0;
    s_rig.ghostTev.TevColor.b = 0;
    s_rig.ghostTev.TevColor.a = 255;
    g_env_light.setLightTevColorType_MAJI(s_rig.ghost, &s_rig.ghostTev);
    J3DDrawBuffer* saved0 = j3dSys.getDrawBuffer(0);
    J3DDrawBuffer* saved1 = j3dSys.getDrawBuffer(1);
    if (dKy_darkworld_check()) dComIfGd_setListDark();
    s_rig.inv.entryDL(nullptr);
    j3dSys.setDrawBuffer(saved0, 0);
    j3dSys.setDrawBuffer(saved1, 1);
    daMirror_c::entry(s_rig.ghost);
}

bool gripping(daAlink_c* alink, J3DShape* shown) {
    if (shown == nullptr || alink->mpLinkHandModel == nullptr) return false;
    J3DModelData* hands = alink->mpLinkHandModel->getModelData();
    for (u16 i = 0; i < hands->getMaterialNum(); ++i) {
        J3DMaterial* mat = hands->getMaterialNodePointer(i);
        if (mat != nullptr && mat->getShape() == shown) return true;
    }
    return false;
}

const f32 kWeaponScale = 103.0f / 154.5f;

J3DModelData* s_swordData = nullptr;
J3DModelData* s_shieldData = nullptr;
J3DModelData* s_sheathData = nullptr;
J3DModel* s_sword = nullptr;
J3DModel* s_shield = nullptr;
J3DModel* s_sheathModel = nullptr;
bool s_weaponsFailed = false;

J3DModelData* cut_out(const char* keep) {
    J3DModelData* data = arc_load(kArc, kArmour);
    if (data == nullptr) return nullptr;
    const int k = joint_named(data, keep);
    bool compensated = true;
    for (u16 j = 1; j < data->getJointNum(); ++j) {
        compensated = compensated && data->getJointNodePointer(j)->getScaleCompensate() != 0;
    }
    if (k < 0 || !compensated) {
        arc_free_data(data);
        return nullptr;
    }
    for (u16 j = 0; j < data->getJointNum(); ++j) {
        J3DTransformInfo& info = data->getJointNodePointer(j)->getTransformInfo();
        const f32 scale = j == k ? kWeaponScale : 0.0001f;
        info.mScale.x = info.mScale.y = info.mScale.z = scale;
        info.mRotation.x = info.mRotation.y = info.mRotation.z = 0;
        info.mTranslate.x = info.mTranslate.y = info.mTranslate.z = 0.0f;
    }
    return data;
}

J3DModelData* sheath() {
    J3DModelData* data = arc_load(kArc, kSheath);
    if (data == nullptr) return nullptr;
    if (data->getJointNum() < 2) {
        arc_free_data(data);
        return nullptr;
    }
    J3DTransformInfo& root = data->getJointNodePointer(0)->getTransformInfo();
    root.mScale.x = root.mScale.y = root.mScale.z = 1.0f;
    root.mRotation.x = 0;
    root.mRotation.y = cM_deg2s(33.1f);
    root.mRotation.z = 0;
    root.mTranslate.x = -18.5f;
    root.mTranslate.y = 0.14f;
    root.mTranslate.z = 12.2f;
    J3DTransformInfo& mesh = data->getJointNodePointer(1)->getTransformInfo();
    mesh.mScale.x = mesh.mScale.y = mesh.mScale.z = kWeaponScale;
    mesh.mRotation.x = cM_deg2s(90.0f);
    mesh.mRotation.y = 0;
    mesh.mRotation.z = cM_deg2s(-20.58f);
    mesh.mTranslate.x = 16.102f;
    mesh.mTranslate.y = 0.036f;
    mesh.mTranslate.z = 0.544f;
    JUTNameTab* names = data->getMaterialName();
    for (u16 i = 0; i < data->getMaterialNum(); ++i) {
        const char* name = names != nullptr ? names->getName(i) : nullptr;
        J3DMaterial* mat = data->getMaterialNodePointer(i);
        if (name != nullptr && std::strcmp(name, "KN_grip_m") == 0 && mat != nullptr && mat->getShape() != nullptr) {
            mat->getShape()->hide();
        }
    }
    return data;
}

bool build_weapons() {
    if (s_sword != nullptr && s_shield != nullptr && s_sheathModel != nullptr) return true;
    if (s_weaponsFailed || !arc_ready()) return false;
    if (s_swordData == nullptr) s_swordData = cut_out("weaponL");
    if (s_shieldData == nullptr) s_shieldData = cut_out("weaponR");
    if (s_sheathData == nullptr) s_sheathData = sheath();
    if (s_swordData == nullptr || s_shieldData == nullptr || s_sheathData == nullptr) {
        s_weaponsFailed = true;
        mods::log::warn("heros_shade: his sword and shield could not be cut out");
        return false;
    }
    if (s_sword == nullptr) s_sword = shade_create_model(s_swordData, 0x80000, 0x11000284);
    if (s_shield == nullptr) s_shield = shade_create_model(s_shieldData, 0x80000, 0x11000284);
    if (s_sheathModel == nullptr) s_sheathModel = shade_create_model(s_sheathData, 0x80000, 0x11000284);
    s_weaponsFailed = s_sword == nullptr || s_shield == nullptr || s_sheathModel == nullptr;
    return !s_weaponsFailed;
}

void draw_in_place(J3DModel* ours, J3DModel* links, daAlink_c* alink) {
    ours->setBaseTRMtx(links->getBaseTRMtx());
    ours->calc();
    g_env_light.setLightTevColorType_MAJI(ours, &alink->tevStr);
    mDoExt_modelEntryDL(ours);
}

int sword_kind(daAlink_c* alink) {
    J3DModel* sword = alink->mSwordModel;
    if (sword == nullptr) return -1;
    if (sword == alink->mWoodSwordModel) return kSwordWood;
    if (sword == alink->mpSwAModel) return kSwordOrdon;
    if (sword == alink->mpSwMModel) return kSwordMaster;
    return -1;
}

int shield_kind(daAlink_c* alink) {
    const char* arc = alink->mShieldArcName;
    if (arc == nullptr) return -1;
    if (std::strcmp(arc, "CWShd") == 0) return kShieldOrdon;
    if (std::strcmp(arc, "SWShd") == 0) return kShieldWood;
    if (std::strcmp(arc, "HyShd") == 0) return kShieldHylian;
    return -1;
}

bool sword_wanted(daAlink_c* alink) {
    const ShadeOptions& o = shade_options();
    const int kind = sword_kind(alink);
    return o.sword && kind >= 0 && o.swordFor[kind];
}

bool shield_wanted(daAlink_c* alink) {
    const ShadeOptions& o = shade_options();
    const int kind = shield_kind(alink);
    return o.shield && kind >= 0 && o.shieldFor[kind] && alink->mShieldChangeWaitTimer == 0;
}

int outfit_of() {
    switch (dComIfGs_getSelectEquipClothes()) {
    case dItemNo_WEAR_CASUAL_e: return kOutfitOrdon;
    case dItemNo_WEAR_ZORA_e: return kOutfitZora;
    case dItemNo_ARMOR_e: return kOutfitMagic;
    default: return kOutfitHero;
    }
}

bool body_wanted(daAlink_c* alink) {
    if (alink == nullptr || alink->mClothesChangeWaitTimer != 0) return false;
    if (alink->checkWolf() || alink->mpLinkModel == nullptr) return false;
    const ShadeOptions& o = shade_options();
    return o.body && (o.allOutfits || o.outfit[outfit_of()]);
}

}

bool shade_is_him(daAlink_c* alink) {
    const ShadeOptions& o = shade_options();
    if (alink->checkWolf()) return o.wolf;
    return o.body && (o.allOutfits || o.outfit[outfit_of()]);
}

namespace {

bool standing_in(daAlink_c* alink) {
    return s_rig.model != nullptr && body_wanted(alink);
}

HookAction on_model_draw_pre(ModContext*, void* args, void*, void*) {
    daAlink_c* alink = mods::arg<daAlink_c*>(args, 0);
    J3DModel* model = mods::arg<J3DModel*>(args, 1);
    const int noDraw = mods::arg<int>(args, 2);
    if (alink != daAlink_getAlinkActorClass() || model == nullptr) return HOOK_CONTINUE;

    if (wolf_model_draw(alink, model, noDraw)) return HOOK_SKIP_ORIGINAL;

    if ((model == alink->mSwordModel || model == alink->mSheathModel) && sword_wanted(alink) &&
        build_weapons()) {
        if (noDraw == 0) draw_in_place(model == alink->mSwordModel ? s_sword : s_sheathModel, model, alink);
        return HOOK_SKIP_ORIGINAL;
    }
    if (model == alink->mShieldModel && shield_wanted(alink) && build_weapons()) {
        if (noDraw == 0) draw_in_place(s_shield, model, alink);
        return HOOK_SKIP_ORIGINAL;
    }
    if (!standing_in(alink)) return HOOK_CONTINUE;
    const bool body = model == alink->mpLinkModel || model == alink->mpLinkHandModel ||
                      model == alink->mpLinkHatModel || model == alink->mpLinkFaceModel ||
                      model == alink->mpLinkBootModels[0] || model == alink->mpLinkBootModels[1];
    return body ? HOOK_SKIP_ORIGINAL : HOOK_CONTINUE;
}

struct HeldSwap {
    int link;
    Mtx saved;
};
const int kHeldMax = 8;
HeldSwap s_held[kHeldMax];
int s_heldCount = 0;

void hold_begin(J3DModel* link, const LinkRest& lr, const int* joints, int count, int pod) {
    s_heldCount = 0;
    for (int n = 0; n < count; ++n) {
        const int l = joints[n];
        if (l < 0 || l >= lr.joints) continue;
        bool already = false;
        for (int i = 0; i < s_heldCount; ++i) already = already || s_held[i].link == l;
        const int j = his_joint_for(lr, l);
        if (already || j < 0 || s_heldCount >= kHeldMax) continue;
        HeldSwap& h = s_held[s_heldCount++];
        h.link = l;
        cMtx_copy(link->getAnmMtx(l), h.saved);
        link->setAnmMtx(l, s_rig.rigid[j]);
    }

    const int spine = pod >= 0 && pod < lr.joints ? his_joint_for(lr, 2) : -1;
    if (spine >= 0 && s_heldCount < kHeldMax) {
        const J3DTransformInfo& info =
            link->getModelData()->getJointNodePointer(static_cast<u16>(pod))->getTransformInfo();
        Mtx local, at;
        J3DGetTranslateRotateMtx(info.mRotation.x, info.mRotation.y, info.mRotation.z, info.mTranslate.x,
            info.mTranslate.y * kBackDepth, info.mTranslate.z, local);
        MTXConcat(s_rig.rigid[spine], local, at);
        HeldSwap& h = s_held[s_heldCount++];
        h.link = pod;
        cMtx_copy(link->getAnmMtx(pod), h.saved);
        link->setAnmMtx(pod, at);
    }
}

HookAction on_item_matrix_pre(ModContext*, void* args, void*, void*) {
    s_heldCount = 0;
    daAlink_c* alink = mods::arg<daAlink_c*>(args, 0);
    if (alink != daAlink_getAlinkActorClass() || !standing_in(alink)) return HOOK_CONTINUE;
    J3DModel* link = alink->mpLinkModel;
    LinkRest* lr = link_rest(link->getModelData(), s_rig.model->getModelData());
    if (lr == nullptr) return HOOK_CONTINUE;
    s_rig.scale = lr->scale;
    retarget(link, *lr);
    const int joints[3] = {alink->mLeftItemJntNo, alink->mRightItemJntNo, 4};
    hold_begin(link, *lr, joints, 3, alink->field_0x30b6);
    return HOOK_CONTINUE;
}

void on_item_matrix_post(ModContext*, void* args, void*, void*) {
    daAlink_c* alink = mods::arg<daAlink_c*>(args, 0);
    J3DModel* link = alink != nullptr ? alink->mpLinkModel : nullptr;
    if (link != nullptr) {
        for (int i = 0; i < s_heldCount; ++i) link->setAnmMtx(s_held[i].link, s_held[i].saved);
    }
    s_heldCount = 0;
}

struct ShadowSwap {
    bool on = false;
    J3DModel* body = nullptr;
    J3DModel* face = nullptr;
    J3DModel* hat = nullptr;
    J3DModel* hand = nullptr;
};
ShadowSwap s_shadow;

HookAction on_shadow_pre(ModContext*, void* args, void*, void*) {
    s_shadow.on = false;
    daAlink_c* alink = mods::arg<daAlink_c*>(args, 0);
    if (alink != daAlink_getAlinkActorClass()) return HOOK_CONTINUE;
    const bool wolf = alink->checkWolf();
    J3DModel* ours = wolf ? wolf_local_model() : (standing_in(alink) ? s_rig.model : nullptr);
    if (ours == nullptr) return HOOK_CONTINUE;
    s_shadow.on = true;
    s_shadow.body = alink->mpLinkModel;
    s_shadow.face = alink->mpLinkFaceModel;
    s_shadow.hat = alink->mpLinkHatModel;
    s_shadow.hand = alink->mpLinkHandModel;
    alink->mpLinkModel = ours;
    if (!wolf) {
        alink->mpLinkFaceModel = nullptr;
        alink->mpLinkHatModel = nullptr;
        alink->mpLinkHandModel = nullptr;
    }
    return HOOK_CONTINUE;
}

void on_shadow_post(ModContext*, void* args, void*, void*) {
    if (!s_shadow.on) return;
    s_shadow.on = false;
    daAlink_c* alink = mods::arg<daAlink_c*>(args, 0);
    if (alink == nullptr) return;
    alink->mpLinkModel = s_shadow.body;
    alink->mpLinkFaceModel = s_shadow.face;
    alink->mpLinkHatModel = s_shadow.hat;
    alink->mpLinkHandModel = s_shadow.hand;
}

void on_draw_post(ModContext*, void* args, void*, void*) {
    daAlink_c* alink = mods::arg<daAlink_c*>(args, 0);
    if (alink == daAlink_getAlinkActorClass()) shade_frame(alink);
}

}

void shade_frame(daAlink_c* alink) {
    wolf_frame();
    const ShadeOptions& o = shade_options();

    if (s_arc == kArcMounting || (s_arc == kArcNone && (o.body || o.sword || o.shield))) arc_ready();
    if (!body_wanted(alink) || !arc_ready()) return;
    if (s_rig.model == nullptr) {
        if (s_rigFailed) return;
        if (!rig_build()) {
            s_rigFailed = true;
            mods::log::warn("heros_shade: he could not be loaded");
            return;
        }
    }
    const bool gripL = gripping(alink, alink->field_0x06d0);
    const bool gripR = gripping(alink, alink->field_0x06d4);
    rig_draw(alink->mpLinkModel, gripL, gripR, &alink->tevStr);
}

void shade_init() {
    const int results[] = {
        static_cast<int>(mods::hook::add_pre<ShadeModelDrawHook>(on_model_draw_pre)),
        static_cast<int>(mods::hook::add_pre<ShadeItemMatrixHook>(on_item_matrix_pre)),
        static_cast<int>(mods::hook::add_post<ShadeItemMatrixHook>(on_item_matrix_post)),
        static_cast<int>(mods::hook::add_pre<ShadeShadowHook>(on_shadow_pre)),
        static_cast<int>(mods::hook::add_post<ShadeShadowHook>(on_shadow_post)),
        static_cast<int>(mods::hook::add_post<ShadeAlinkDrawHook>(on_draw_post)),
    };
    int failed = 0;
    for (int r : results) failed += r != 0 ? 1 : 0;
    mods::log::info("heros_shade: {} of 6 hooks in", 6 - failed);
    wolf_init();
    voice_init();
}
