

#include "shade.hpp"

#include "d/actor/d_a_alink.h"
#include "d/actor/d_a_mirror.h"
#include "d/d_com_inf_game.h"
#include "d/d_kankyo.h"
#include "m_Do/m_Do_ext.h"
#include "JSystem/J3DGraphAnimator/J3DAnimation.h"
#include "JSystem/J3DGraphAnimator/J3DJoint.h"
#include "JSystem/J3DGraphAnimator/J3DModel.h"
#include "JSystem/J3DGraphAnimator/J3DModelData.h"
#include "JSystem/J3DGraphBase/J3DSys.h"
#include "JSystem/JKernel/JKRHeap.h"

namespace {

const char kArc[] = "GWolf";
const char kModel[] = "gw.bmd";
const int kMaxJoints = 80;

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

J3DModel* s_model = nullptr;
mDoExt_brkAnm* s_glow = nullptr;
mDoExt_btkAnm* s_shimmer = nullptr;
dKy_tevstr_c s_tev;
int s_tevRoom = -1000;
bool s_failed = false;
Mtx s_pose[kMaxJoints];
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

bool build() {
    J3DModelData* data = arc_load(kArc, kModel);
    if (data == nullptr) return false;
    if (data->getJointNum() > kMaxJoints) {
        arc_free_data(data);
        return false;
    }
    J3DModel* model = shade_create_model(data, 0x80000, 0x11020284);
    if (model == nullptr) {
        arc_free_data(data);
        return false;
    }
    auto* glow = static_cast<J3DAnmTevRegKey*>(arc_load_anm(kArc, "gw.brk"));
    auto* shimmer = static_cast<J3DAnmTextureSRTKey*>(arc_load_anm(kArc, "gw.btk"));
    for (u16 j = 0; j < data->getJointNum(); ++j) data->getJointNodePointer(j)->setCallBack(pose_callback);
    JKRHeap* heap = arc_heap_if_any();
    JKRHeap* previous = heap != nullptr ? heap->becomeCurrentHeap() : nullptr;
    if (glow != nullptr) {
        s_glow = JKR_NEW mDoExt_brkAnm();
        if (s_glow != nullptr && !s_glow->init(data, glow, 1, 2, 1.0f, 0, -1)) s_glow = nullptr;
    }
    if (shimmer != nullptr) {
        s_shimmer = JKR_NEW mDoExt_btkAnm();
        if (s_shimmer != nullptr && !s_shimmer->init(data, shimmer, 1, 2, 1.0f, 0, -1)) s_shimmer = nullptr;
    }
    if (previous != nullptr) previous->becomeCurrentHeap();
    s_model = model;
    dKy_tevstr_init(&s_tev, 0, 0xFF);
    mods::log::info("heros_shade: the golden wolf is ready");
    return true;
}

bool wanted(daAlink_c* alink) {
    return shade_options().wolf && alink != nullptr && alink->mClothesChangeWaitTimer == 0 &&
           alink->checkWolf() && alink->mpLinkModel != nullptr;
}

bool ready(daAlink_c* alink) {
    if (!wanted(alink) || !arc_ready()) return false;
    if (s_model != nullptr) return true;
    if (s_failed) return false;
    if (!build()) {
        s_failed = true;
        mods::log::warn("heros_shade: the golden wolf could not be loaded");
        return false;
    }
    return true;
}

void draw(J3DModel* wolf, dKy_tevstr_c* tev) {
    J3DModelData* data = s_model->getModelData();
    const int count = data->getJointNum() < wolf->getModelData()->getJointNum() ? data->getJointNum()
                                                                                 : wolf->getModelData()->getJointNum();
    for (int j = 0; j < count; ++j) cMtx_copy(wolf->getAnmMtx(j), s_pose[j]);
    if (s_glow != nullptr) {
        s_glow->entry(data);
        s_glow->play();
    }
    if (s_shimmer != nullptr) {
        s_shimmer->entry(data);
        s_shimmer->play();
    }
    s_model->setBaseTRMtx(wolf->getBaseTRMtx());
    s_posing = s_pose;
    s_posingCount = count;
    s_model->calc();
    s_posing = nullptr;
    const int room = tev != nullptr ? tev->room_no : 0;
    if (room != s_tevRoom) {
        dKy_tevstr_init(&s_tev, static_cast<s8>(room), 0xFF);
        s_tevRoom = room;
    }
    MtxP base = wolf->getBaseTRMtx();
    cXyz at(base[0][3], base[1][3], base[2][3]);
    g_env_light.settingTevStruct(5, &at, &s_tev);
    g_env_light.setLightTevColorType_MAJI(s_model, &s_tev);
    mDoExt_modelEntryDL(s_model);
    daMirror_c::entry(s_model);
}

}

bool wolf_model_draw(daAlink_c* alink, J3DModel* model, int noDraw) {
    if (!alink->checkWolf()) return false;
    bool chain = false;
    for (int i = 0; i < 4; ++i) chain = chain || model == alink->mpWlChainModels[i];
    if (model != alink->mpLinkModel && !chain) return false;
    if (!ready(alink)) return false;
    if (model == alink->mpLinkModel && noDraw == 0) draw(model, &alink->tevStr);
    return true;
}

J3DModel* wolf_local_model() {
    daAlink_c* alink = daAlink_getAlinkActorClass();
    return s_model != nullptr && wanted(alink) ? s_model : nullptr;
}

void wolf_frame() {
    if (s_arc == kArcMounting || (s_arc == kArcNone && shade_options().wolf)) arc_ready();
}

void wolf_init() {}
