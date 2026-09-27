

#include "shade.hpp"

#include "d/d_resorce.h"
#include "m_Do/m_Do_dvd_thread.h"
#include "m_Do/m_Do_ext.h"
#include "JSystem/J3DGraphAnimator/J3DModel.h"
#include "JSystem/J3DGraphAnimator/J3DModelData.h"
#include "JSystem/J3DGraphLoader/J3DAnmLoader.h"
#include "JSystem/JKernel/JKRArchive.h"
#include "JSystem/JKernel/JKRExpHeap.h"
#include "JSystem/JKernel/JKRMemArchive.h"
#include "JSystem/JKernel/JKRSolidHeap.h"

#include <cstdio>
#include <cstring>

namespace {

const int kMaxArcs = 4;

struct Arc {
    char name[16] = {};
    mDoDvdThd_mountArchive_c* job = nullptr;
    JKRArchive* archive = nullptr;
    bool failed = false;
};
Arc s_arcs[kMaxArcs];

JKRExpHeap* s_heap = nullptr;
bool s_heapTried = false;
const u32 kHeapWanted = 24u * 1024u * 1024u;

JKRHeap* heap() {
    if (s_heap != nullptr || s_heapTried) return s_heap;
    s_heapTried = true;
    JKRHeap* root = JKRHeap::getRootHeap();
    if (root == nullptr) return nullptr;
    const u32 rootFree = root->getFreeSize();
    u32 size = kHeapWanted;
    if (rootFree < size + 32u * 1024u * 1024u) {
        size = rootFree > 40u * 1024u * 1024u ? rootFree - 32u * 1024u * 1024u : 0;
    }
    if (size < 8u * 1024u * 1024u) {
        mods::log::warn("heros_shade: only {} KB free in the root heap - not loading him", rootFree / 1024);
        return nullptr;
    }
    s_heap = JKRExpHeap::create(size, root, false);
    if (s_heap != nullptr) s_heap->setName("HerosShadeHeap");
    return s_heap;
}

Arc* find(const char* name) {
    for (Arc& a : s_arcs) {
        if (a.name[0] != '\0' && std::strcmp(a.name, name) == 0) return &a;
    }
    return nullptr;
}

u32 node_type_for_index(JKRArchive* archive, u32 index) {
    for (int i = 0; i < archive->countDirectory(); ++i) {
        const JKRArchive::SDIDirEntry& dir = archive->mNodes[i];
        if (index >= dir.first_file_index && index < dir.first_file_index + dir.num_entries) return dir.type;
    }
    return 0;
}

struct PartHeap {
    J3DModelData* data = nullptr;
    JKRSolidHeap* heap = nullptr;
};
const int kPartHeaps = 16;
PartHeap s_parts[kPartHeaps];

struct ModelHeap {
    J3DModel* model = nullptr;
    JKRSolidHeap* heap = nullptr;
};
const int kModelHeaps = 16;
ModelHeap s_models[kModelHeaps];

JKRArchive::SDIFileEntry* entry_of(Arc* a, const char* file) {
    if (a == nullptr || a->archive == nullptr || file == nullptr) return nullptr;
    JKRArchive::SDIFileEntry* entry = a->archive->findNameResource(file);
    if (entry == nullptr) mods::log::warn("heros_shade: '{}' has no '{}'", a->name, file);
    return entry;
}

}

JKRHeap* arc_heap_if_any() {
    return s_heap;
}

bool arc_request(const char* name) {
    if (name == nullptr || name[0] == '\0') return false;
    if (Arc* have = find(name)) return !have->failed;
    Arc* slot = nullptr;
    for (Arc& a : s_arcs) {
        if (a.name[0] == '\0') {
            slot = &a;
            break;
        }
    }
    if (slot == nullptr) return false;
    std::strncpy(slot->name, name, sizeof(slot->name) - 1);
    char path[64];
    std::snprintf(path, sizeof(path), "/res/Object/%s.arc", slot->name);
    slot->job = mDoDvdThd_mountArchive_c::create(path, mDoDvd_MOUNT_DIRECTION_HEAD, heap());
    if (slot->job == nullptr) {
        slot->failed = true;
        mods::log::warn("heros_shade: could not start mounting '{}'", slot->name);
        return false;
    }
    return true;
}

int arc_poll(const char* name) {
    Arc* a = find(name);
    if (a == nullptr || a->failed) return -1;
    if (a->job != nullptr) {
        if (!a->job->sync()) return 0;
        a->archive = static_cast<JKRArchive*>(a->job->getArchive());
        a->job->destroy();
        a->job = nullptr;
        if (a->archive == nullptr) {
            a->failed = true;
            mods::log::warn("heros_shade: mounting '{}' produced no archive", a->name);
            return -1;
        }
        mods::log::info("heros_shade: '{}' mounted, {} files", a->name, static_cast<int>(a->archive->countFile()));
    }
    return a->archive != nullptr ? 1 : -1;
}

J3DModelData* arc_load(const char* name, const char* file) {
    Arc* a = find(name);
    JKRArchive::SDIFileEntry* entry = entry_of(a, file);
    if (entry == nullptr) return nullptr;
    JKRArchive* archive = a->archive;
    const u32 index = static_cast<u32>(entry - archive->mFiles);
    void* raw = archive->getIdxResource(index);
    if (raw == nullptr) return nullptr;
    const u32 size = archive->getExpandedResSize(raw);
    const u32 type = node_type_for_index(archive, index);
    if (size < 32 || size > 16u * 1024u * 1024u ||
        (type != 'BMDR' && type != 'BMWR' && type != 'BMDE' && type != 'BMWE' && type != 'BMDV')) {
        return nullptr;
    }
    JKRHeap* parent = heap();
    const u32 want = size * 4u + 1024u * 1024u;
    if (parent == nullptr || parent->getFreeSize() < want + 2u * 1024u * 1024u) {
        mods::log::warn("heros_shade: no room to load '{}'", file);
        return nullptr;
    }
    int slot = -1;
    for (int i = 0; i < kPartHeaps && slot < 0; ++i) {
        if (s_parts[i].data == nullptr) slot = i;
    }
    if (slot < 0) return nullptr;
    JKRSolidHeap* part = JKRSolidHeap::create(want, parent, false);
    if (part == nullptr) return nullptr;
    JKRHeap* previous = part->becomeCurrentHeap();
    u8* copy = JKR_NEW_ARRAY_ARGS(u8, size, 32);
    J3DModelData* data = nullptr;
    if (copy != nullptr) {
        std::memcpy(copy, raw, size);
        data = dRes_info_c::loaderBasicBmd(type, copy);
    }
    if (data != nullptr && (type == 'BMWR' || type == 'BMWE')) {

        dRes_info_c::offWarpMaterial(data);
        data->simpleCalcMaterial(const_cast<MtxP>(j3dDefaultMtx));
        data->makeSharedDL();
    }
    if (previous != nullptr) previous->becomeCurrentHeap();
    if (data == nullptr) {
        part->destroy();
        return nullptr;
    }
    part->adjustSize();
    s_parts[slot].data = data;
    s_parts[slot].heap = part;
    return data;
}

void* arc_load_anm(const char* name, const char* file) {
    Arc* a = find(name);
    JKRArchive::SDIFileEntry* entry = entry_of(a, file);
    if (entry == nullptr) return nullptr;
    void* raw = a->archive->getIdxResource(static_cast<u32>(entry - a->archive->mFiles));
    if (raw == nullptr) return nullptr;
    const u32 size = a->archive->getExpandedResSize(raw);
    JKRHeap* parent = heap();
    if (size < 32 || size > 1024u * 1024u || parent == nullptr ||
        parent->getFreeSize() < size * 2u + 2u * 1024u * 1024u) {
        return nullptr;
    }
    JKRHeap* previous = parent->becomeCurrentHeap();
    u8* copy = JKR_NEW_ARRAY_ARGS(u8, size, 32);
    void* anm = nullptr;
    if (copy != nullptr) {
        std::memcpy(copy, raw, size);
        anm = J3DAnmLoaderDataBase::load(copy);
    }
    if (previous != nullptr) previous->becomeCurrentHeap();
    return anm;
}

void arc_free_data(J3DModelData* data) {
    if (data == nullptr) return;
    for (PartHeap& p : s_parts) {
        if (p.data != data) continue;
        p.heap->destroy();
        p = PartHeap{};
        return;
    }
}

J3DModel* shade_create_model(J3DModelData* data, u32 modelFlag, u32 differedDlistFlag) {
    JKRHeap* parent = heap();
    if (data == nullptr || parent == nullptr || parent->getFreeSize() < 4u * 1024u * 1024u) return nullptr;
    int slot = -1;
    for (int i = 0; i < kModelHeaps && slot < 0; ++i) {
        if (s_models[i].model == nullptr) slot = i;
    }
    if (slot < 0) return nullptr;
    const u32 sizes[] = {512u * 1024u, 2u * 1024u * 1024u};
    for (u32 size : sizes) {
        JKRSolidHeap* part = JKRSolidHeap::create(size, parent, false);
        if (part == nullptr) continue;
        JKRHeap* previous = part->becomeCurrentHeap();
        J3DModel* model = mDoExt_J3DModel__create(data, modelFlag, differedDlistFlag);
        if (previous != nullptr) previous->becomeCurrentHeap();
        if (model == nullptr) {
            part->destroy();
            continue;
        }
        part->adjustSize();
        s_models[slot].model = model;
        s_models[slot].heap = part;
        return model;
    }
    return nullptr;
}

void shade_free_model(J3DModel* model) {
    if (model == nullptr) return;
    for (ModelHeap& m : s_models) {
        if (m.model != model) continue;
        m.heap->destroy();
        m = ModelHeap{};
        return;
    }
}
