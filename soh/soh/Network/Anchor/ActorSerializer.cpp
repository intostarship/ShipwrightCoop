#include "ActorSerializer.h"
#include <libultraship/libultraship.h>

extern "C" {
#include "macros.h"
#include "z64animation.h"
}

// All offsets from ActorSyncGenerated.inc are computed at COMPILE-TIME via offsetof()
// No runtime conversion needed - offsets are already correct!

/**
 * Get the actionFunc pointer from an actor using the syncInfo offset.
 */
static void* GetActorActionFunc(Actor* actor, ActorSyncInfo* syncInfo) {
    if (syncInfo->actionFuncOffset == 0) return nullptr;
    void** funcPtr = reinterpret_cast<void**>(
        reinterpret_cast<uintptr_t>(actor) + syncInfo->actionFuncOffset
    );
    return *funcPtr;
}

/**
 * Set the actionFunc pointer on an actor using the syncInfo offset.
 */
static void SetActorActionFunc(Actor* actor, ActorSyncInfo* syncInfo, void* func) {
    if (syncInfo->actionFuncOffset == 0) return;
    void** funcPtr = reinterpret_cast<void**>(
        reinterpret_cast<uintptr_t>(actor) + syncInfo->actionFuncOffset
    );
    *funcPtr = func;
}

/**
 * Get the SkelAnime pointer from an actor using the syncInfo offset.
 */
static SkelAnime* GetActorSkelAnime(Actor* actor, ActorSyncInfo* syncInfo) {
    if (syncInfo->skelAnimeOffset == 0) return nullptr;
    return reinterpret_cast<SkelAnime*>(
        reinterpret_cast<uintptr_t>(actor) + syncInfo->skelAnimeOffset
    );
}

/**
 * Get the jointTable pointer from an actor using the syncInfo offset.
 */
static Vec3s* GetActorJointTable(Actor* actor, ActorSyncInfo* syncInfo) {
    if (syncInfo->jointTableOffset == 0 || syncInfo->jointCount == 0) return nullptr;
    return reinterpret_cast<Vec3s*>(
        reinterpret_cast<uintptr_t>(actor) + syncInfo->jointTableOffset
    );
}

/**
 * Get the morphTable pointer from an actor using the syncInfo offset.
 */
static Vec3s* GetActorMorphTable(Actor* actor, ActorSyncInfo* syncInfo) {
    if (syncInfo->morphTableOffset == 0 || syncInfo->morphCount == 0) return nullptr;
    return reinterpret_cast<Vec3s*>(
        reinterpret_cast<uintptr_t>(actor) + syncInfo->morphTableOffset
    );
}

bool IsActorSyncable(s16 actorId) {
    return GetActorSyncInfo(actorId) != nullptr;
}

int GetActorActionFuncIndex(Actor* actor) {
    if (actor == nullptr) return -1;

    ActorSyncInfo* syncInfo = GetActorSyncInfo(actor->id);
    if (syncInfo == nullptr) return -1;

    void* actionFunc = GetActorActionFunc(actor, syncInfo);
    return GetActionFuncIndex(syncInfo, actionFunc);
}

bool SetActorActionFuncByIndex(Actor* actor, int index) {
    if (actor == nullptr || index < 0) return false;

    ActorSyncInfo* syncInfo = GetActorSyncInfo(actor->id);
    if (syncInfo == nullptr) return false;

    void* actionFunc = GetActionFuncByIndex(syncInfo, index);
    if (actionFunc == nullptr) return false;

    SetActorActionFunc(actor, syncInfo, actionFunc);
    return true;
}

/**
 * Serialize a field based on its type.
 */
static void SerializeField(nlohmann::json& state, Actor* actor, FieldDescriptor* field) {
    uintptr_t ptr = reinterpret_cast<uintptr_t>(actor) + field->offset;

    switch (field->type) {
        case FIELD_TYPE_U8:
            if (field->arraySize == 1) {
                state[field->name] = *reinterpret_cast<u8*>(ptr);
            } else {
                nlohmann::json arr = nlohmann::json::array();
                for (int i = 0; i < field->arraySize; i++) {
                    arr.push_back(reinterpret_cast<u8*>(ptr)[i]);
                }
                state[field->name] = arr;
            }
            break;
        case FIELD_TYPE_S8:
            if (field->arraySize == 1) {
                state[field->name] = *reinterpret_cast<s8*>(ptr);
            } else {
                nlohmann::json arr = nlohmann::json::array();
                for (int i = 0; i < field->arraySize; i++) {
                    arr.push_back(reinterpret_cast<s8*>(ptr)[i]);
                }
                state[field->name] = arr;
            }
            break;
        case FIELD_TYPE_U16:
            if (field->arraySize == 1) {
                state[field->name] = *reinterpret_cast<u16*>(ptr);
            } else {
                nlohmann::json arr = nlohmann::json::array();
                for (int i = 0; i < field->arraySize; i++) {
                    arr.push_back(reinterpret_cast<u16*>(ptr)[i]);
                }
                state[field->name] = arr;
            }
            break;
        case FIELD_TYPE_S16:
            if (field->arraySize == 1) {
                state[field->name] = *reinterpret_cast<s16*>(ptr);
            } else {
                nlohmann::json arr = nlohmann::json::array();
                for (int i = 0; i < field->arraySize; i++) {
                    arr.push_back(reinterpret_cast<s16*>(ptr)[i]);
                }
                state[field->name] = arr;
            }
            break;
        case FIELD_TYPE_U32:
            if (field->arraySize == 1) {
                state[field->name] = *reinterpret_cast<u32*>(ptr);
            } else {
                nlohmann::json arr = nlohmann::json::array();
                for (int i = 0; i < field->arraySize; i++) {
                    arr.push_back(reinterpret_cast<u32*>(ptr)[i]);
                }
                state[field->name] = arr;
            }
            break;
        case FIELD_TYPE_S32:
            if (field->arraySize == 1) {
                state[field->name] = *reinterpret_cast<s32*>(ptr);
            } else {
                nlohmann::json arr = nlohmann::json::array();
                for (int i = 0; i < field->arraySize; i++) {
                    arr.push_back(reinterpret_cast<s32*>(ptr)[i]);
                }
                state[field->name] = arr;
            }
            break;
        case FIELD_TYPE_F32:
            if (field->arraySize == 1) {
                state[field->name] = *reinterpret_cast<f32*>(ptr);
            } else {
                nlohmann::json arr = nlohmann::json::array();
                for (int i = 0; i < field->arraySize; i++) {
                    arr.push_back(reinterpret_cast<f32*>(ptr)[i]);
                }
                state[field->name] = arr;
            }
            break;
        case FIELD_TYPE_VEC3F:
            if (field->arraySize == 1) {
                Vec3f* v = reinterpret_cast<Vec3f*>(ptr);
                state[field->name] = {{"x", v->x}, {"y", v->y}, {"z", v->z}};
            } else {
                nlohmann::json arr = nlohmann::json::array();
                for (int i = 0; i < field->arraySize; i++) {
                    Vec3f* v = &reinterpret_cast<Vec3f*>(ptr)[i];
                    arr.push_back({{"x", v->x}, {"y", v->y}, {"z", v->z}});
                }
                state[field->name] = arr;
            }
            break;
        case FIELD_TYPE_VEC3S:
            if (field->arraySize == 1) {
                Vec3s* v = reinterpret_cast<Vec3s*>(ptr);
                state[field->name] = {{"x", v->x}, {"y", v->y}, {"z", v->z}};
            } else {
                nlohmann::json arr = nlohmann::json::array();
                for (int i = 0; i < field->arraySize; i++) {
                    Vec3s* v = &reinterpret_cast<Vec3s*>(ptr)[i];
                    arr.push_back({{"x", v->x}, {"y", v->y}, {"z", v->z}});
                }
                state[field->name] = arr;
            }
            break;
    }
}

nlohmann::json SerializeActorState(Actor* actor) {
    nlohmann::json state;

    if (actor == nullptr) return state;

    ActorSyncInfo* syncInfo = GetActorSyncInfo(actor->id);
    if (syncInfo == nullptr) return state;

    // Serialize actionFunc as index
    void* actionFunc = GetActorActionFunc(actor, syncInfo);
    int funcIndex = GetActionFuncIndex(syncInfo, actionFunc);
    if (funcIndex >= 0) {
        state["actionFuncIndex"] = funcIndex;
    }

    // Serialize jointTable if available (using actual offset from generated data)
    Vec3s* jointTable = GetActorJointTable(actor, syncInfo);
    if (jointTable != nullptr && syncInfo->jointCount > 0) {
        nlohmann::json joints = nlohmann::json::array();
        for (int i = 0; i < syncInfo->jointCount; i++) {
            joints.push_back({
                {"x", jointTable[i].x},
                {"y", jointTable[i].y},
                {"z", jointTable[i].z}
            });
        }
        state["jointTable"] = joints;
    }

    // Serialize morphTable if available (using actual offset from generated data)
    Vec3s* morphTable = GetActorMorphTable(actor, syncInfo);
    if (morphTable != nullptr && syncInfo->morphCount > 0) {
        nlohmann::json morphs = nlohmann::json::array();
        for (int i = 0; i < syncInfo->morphCount; i++) {
            morphs.push_back({
                {"x", morphTable[i].x},
                {"y", morphTable[i].y},
                {"z", morphTable[i].z}
            });
        }
        state["morphTable"] = morphs;
    }

    // Serialize SkelAnime frame/speed (safe fields only)
    SkelAnime* skelAnime = GetActorSkelAnime(actor, syncInfo);
    if (skelAnime != nullptr && skelAnime->animation != nullptr) {
        state["animFrame"] = skelAnime->curFrame;
        state["animSpeed"] = skelAnime->playSpeed;
        state["animStart"] = skelAnime->startFrame;
        state["animEnd"] = skelAnime->endFrame;
    }

    // Serialize all syncable state fields
    if (syncInfo->stateFields != nullptr) {
        for (int i = 0; i < syncInfo->stateFieldCount; i++) {
            FieldDescriptor* field = &syncInfo->stateFields[i];
            if (field->name != nullptr) {
                SerializeField(state, actor, field);
            }
        }
    }

    return state;
}

/**
 * Deserialize a field based on its type.
 */
static void DeserializeField(const nlohmann::json& state, Actor* actor, FieldDescriptor* field) {
    if (!state.contains(field->name)) return;

    uintptr_t ptr = reinterpret_cast<uintptr_t>(actor) + field->offset;
    const auto& value = state[field->name];

    switch (field->type) {
        case FIELD_TYPE_U8:
            if (field->arraySize == 1) {
                *reinterpret_cast<u8*>(ptr) = value.get<u8>();
            } else if (value.is_array()) {
                size_t count = std::min(value.size(), (size_t)field->arraySize);
                for (size_t i = 0; i < count; i++) {
                    reinterpret_cast<u8*>(ptr)[i] = value[i].get<u8>();
                }
            }
            break;
        case FIELD_TYPE_S8:
            if (field->arraySize == 1) {
                *reinterpret_cast<s8*>(ptr) = value.get<s8>();
            } else if (value.is_array()) {
                size_t count = std::min(value.size(), (size_t)field->arraySize);
                for (size_t i = 0; i < count; i++) {
                    reinterpret_cast<s8*>(ptr)[i] = value[i].get<s8>();
                }
            }
            break;
        case FIELD_TYPE_U16:
            if (field->arraySize == 1) {
                *reinterpret_cast<u16*>(ptr) = value.get<u16>();
            } else if (value.is_array()) {
                size_t count = std::min(value.size(), (size_t)field->arraySize);
                for (size_t i = 0; i < count; i++) {
                    reinterpret_cast<u16*>(ptr)[i] = value[i].get<u16>();
                }
            }
            break;
        case FIELD_TYPE_S16:
            if (field->arraySize == 1) {
                *reinterpret_cast<s16*>(ptr) = value.get<s16>();
            } else if (value.is_array()) {
                size_t count = std::min(value.size(), (size_t)field->arraySize);
                for (size_t i = 0; i < count; i++) {
                    reinterpret_cast<s16*>(ptr)[i] = value[i].get<s16>();
                }
            }
            break;
        case FIELD_TYPE_U32:
            if (field->arraySize == 1) {
                *reinterpret_cast<u32*>(ptr) = value.get<u32>();
            } else if (value.is_array()) {
                size_t count = std::min(value.size(), (size_t)field->arraySize);
                for (size_t i = 0; i < count; i++) {
                    reinterpret_cast<u32*>(ptr)[i] = value[i].get<u32>();
                }
            }
            break;
        case FIELD_TYPE_S32:
            if (field->arraySize == 1) {
                *reinterpret_cast<s32*>(ptr) = value.get<s32>();
            } else if (value.is_array()) {
                size_t count = std::min(value.size(), (size_t)field->arraySize);
                for (size_t i = 0; i < count; i++) {
                    reinterpret_cast<s32*>(ptr)[i] = value[i].get<s32>();
                }
            }
            break;
        case FIELD_TYPE_F32:
            if (field->arraySize == 1) {
                *reinterpret_cast<f32*>(ptr) = value.get<f32>();
            } else if (value.is_array()) {
                size_t count = std::min(value.size(), (size_t)field->arraySize);
                for (size_t i = 0; i < count; i++) {
                    reinterpret_cast<f32*>(ptr)[i] = value[i].get<f32>();
                }
            }
            break;
        case FIELD_TYPE_VEC3F:
            if (field->arraySize == 1 && value.contains("x") && value.contains("y") && value.contains("z")) {
                Vec3f* v = reinterpret_cast<Vec3f*>(ptr);
                v->x = value["x"].get<f32>();
                v->y = value["y"].get<f32>();
                v->z = value["z"].get<f32>();
            } else if (value.is_array()) {
                size_t count = std::min(value.size(), (size_t)field->arraySize);
                for (size_t i = 0; i < count; i++) {
                    if (value[i].contains("x") && value[i].contains("y") && value[i].contains("z")) {
                        Vec3f* v = &reinterpret_cast<Vec3f*>(ptr)[i];
                        v->x = value[i]["x"].get<f32>();
                        v->y = value[i]["y"].get<f32>();
                        v->z = value[i]["z"].get<f32>();
                    }
                }
            }
            break;
        case FIELD_TYPE_VEC3S:
            if (field->arraySize == 1 && value.contains("x") && value.contains("y") && value.contains("z")) {
                Vec3s* v = reinterpret_cast<Vec3s*>(ptr);
                v->x = value["x"].get<s16>();
                v->y = value["y"].get<s16>();
                v->z = value["z"].get<s16>();
            } else if (value.is_array()) {
                size_t count = std::min(value.size(), (size_t)field->arraySize);
                for (size_t i = 0; i < count; i++) {
                    if (value[i].contains("x") && value[i].contains("y") && value[i].contains("z")) {
                        Vec3s* v = &reinterpret_cast<Vec3s*>(ptr)[i];
                        v->x = value[i]["x"].get<s16>();
                        v->y = value[i]["y"].get<s16>();
                        v->z = value[i]["z"].get<s16>();
                    }
                }
            }
            break;
    }
}

void DeserializeActorState(Actor* actor, const nlohmann::json& state) {
    if (actor == nullptr || state.empty()) return;

    ActorSyncInfo* syncInfo = GetActorSyncInfo(actor->id);
    if (syncInfo == nullptr) return;

    // Deserialize actionFunc from index
    if (state.contains("actionFuncIndex")) {
        int funcIndex = state["actionFuncIndex"].get<int>();
        void* actionFunc = GetActionFuncByIndex(syncInfo, funcIndex);
        if (actionFunc != nullptr) {
            SetActorActionFunc(actor, syncInfo, actionFunc);
        }
    }

    // Deserialize jointTable (using actual offset from generated data)
    if (state.contains("jointTable") && state["jointTable"].is_array()) {
        Vec3s* jointTable = GetActorJointTable(actor, syncInfo);
        if (jointTable != nullptr) {
            const auto& joints = state["jointTable"];
            size_t count = std::min(joints.size(), (size_t)syncInfo->jointCount);
            for (size_t i = 0; i < count; i++) {
                if (joints[i].contains("x") && joints[i].contains("y") && joints[i].contains("z")) {
                    jointTable[i].x = joints[i]["x"].get<s16>();
                    jointTable[i].y = joints[i]["y"].get<s16>();
                    jointTable[i].z = joints[i]["z"].get<s16>();
                }
            }
        }
    }

    // Deserialize morphTable (using actual offset from generated data)
    if (state.contains("morphTable") && state["morphTable"].is_array()) {
        Vec3s* morphTable = GetActorMorphTable(actor, syncInfo);
        if (morphTable != nullptr) {
            const auto& morphs = state["morphTable"];
            size_t count = std::min(morphs.size(), (size_t)syncInfo->morphCount);
            for (size_t i = 0; i < count; i++) {
                if (morphs[i].contains("x") && morphs[i].contains("y") && morphs[i].contains("z")) {
                    morphTable[i].x = morphs[i]["x"].get<s16>();
                    morphTable[i].y = morphs[i]["y"].get<s16>();
                    morphTable[i].z = morphs[i]["z"].get<s16>();
                }
            }
        }
    }

    // Deserialize SkelAnime frame/speed
    SkelAnime* skelAnime = GetActorSkelAnime(actor, syncInfo);
    if (skelAnime != nullptr && skelAnime->animation != nullptr) {
        if (state.contains("animFrame")) {
            skelAnime->curFrame = state["animFrame"].get<f32>();
        }
        if (state.contains("animSpeed")) {
            skelAnime->playSpeed = state["animSpeed"].get<f32>();
        }
    }

    // Deserialize all syncable state fields
    if (syncInfo->stateFields != nullptr) {
        for (int i = 0; i < syncInfo->stateFieldCount; i++) {
            FieldDescriptor* field = &syncInfo->stateFields[i];
            if (field->name != nullptr) {
                DeserializeField(state, actor, field);
            }
        }
    }
}
