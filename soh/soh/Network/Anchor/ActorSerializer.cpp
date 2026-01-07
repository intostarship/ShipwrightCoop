#include "ActorSerializer.h"
#include <libultraship/libultraship.h>
#include <cmath>

extern "C" {
#include "macros.h"
#include "z64animation.h"
}

// All offsets from ActorSyncGenerated.inc are computed at COMPILE-TIME via offsetof()
// No runtime conversion needed - offsets are already correct!

// ============================================================================
// JSON VALIDATION HELPERS
// Maximum protection against malformed/null JSON values
// ============================================================================

// Helper to safely check if a JSON value is a valid number (not null, not string, not object)
static bool isValidNumber(const nlohmann::json& v) {
    if (v.is_null() || v.is_object() || v.is_array() || v.is_string() || v.is_boolean()) {
        return false;
    }
    return v.is_number();
}

// Helper to check if a float value is sane (not NaN, not Infinity)
static bool isValidFloat(f32 value) {
    return !std::isnan(value) && !std::isinf(value);
}

// Helper to safely check if a JSON value is a valid array with elements
static bool isValidArray(const nlohmann::json& v) {
    return !v.is_null() && v.is_array() && !v.empty();
}

// Helper to safely check if a JSON value is a valid array element at index
static bool isValidArrayElement(const nlohmann::json& arr, size_t index) {
    if (!arr.is_array() || index >= arr.size()) return false;
    return !arr[index].is_null();
}

// Helper to safely check if a JSON value is a valid Vec3 object
static bool isValidVec3(const nlohmann::json& v) {
    if (v.is_null() || !v.is_object()) return false;
    if (!v.contains("x") || !v.contains("y") || !v.contains("z")) return false;

    const auto& x = v["x"];
    const auto& y = v["y"];
    const auto& z = v["z"];

    return isValidNumber(x) && isValidNumber(y) && isValidNumber(z);
}

// Helper to safely check if a JSON value is a valid Vec3f (float) with sane values
static bool isValidVec3f(const nlohmann::json& v) {
    if (!isValidVec3(v)) return false;

    f32 x = v["x"].get<f32>();
    f32 y = v["y"].get<f32>();
    f32 z = v["z"].get<f32>();

    return isValidFloat(x) && isValidFloat(y) && isValidFloat(z);
}

/**
 * Safely get a numeric value from JSON with type bounds checking.
 */
template<typename T>
static T safeGetNumber(const nlohmann::json& v, T defaultValue = 0) {
    if (!isValidNumber(v)) return defaultValue;
    try {
        return v.get<T>();
    } catch (...) {
        return defaultValue;
    }
}

/**
 * Safely get a float value from JSON with NaN/Inf protection.
 */
static f32 safeGetFloat(const nlohmann::json& v, f32 defaultValue = 0.0f) {
    if (!isValidNumber(v)) return defaultValue;
    try {
        f32 val = v.get<f32>();
        return isValidFloat(val) ? val : defaultValue;
    } catch (...) {
        return defaultValue;
    }
}

// ============================================================================
// ACTOR ACCESS HELPERS
// ============================================================================

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

// ============================================================================
// SERIALIZATION (Actor -> JSON)
// ============================================================================

/**
 * Serialize a field based on its type.
 * Includes protection against serializing invalid float values (NaN, Inf)
 */
static void SerializeField(nlohmann::json& state, Actor* actor, FieldDescriptor* field) {
    // Sanity check field descriptor
    if (field == nullptr || field->name == nullptr || field->arraySize == 0 || field->arraySize > 100) {
        return;
    }

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
                f32 val = *reinterpret_cast<f32*>(ptr);
                // Don't serialize NaN or Infinity - use 0.0f instead
                state[field->name] = isValidFloat(val) ? val : 0.0f;
            } else {
                nlohmann::json arr = nlohmann::json::array();
                for (int i = 0; i < field->arraySize; i++) {
                    f32 val = reinterpret_cast<f32*>(ptr)[i];
                    arr.push_back(isValidFloat(val) ? val : 0.0f);
                }
                state[field->name] = arr;
            }
            break;
        case FIELD_TYPE_VEC3F:
            if (field->arraySize == 1) {
                Vec3f* v = reinterpret_cast<Vec3f*>(ptr);
                // Sanitize float values
                f32 x = isValidFloat(v->x) ? v->x : 0.0f;
                f32 y = isValidFloat(v->y) ? v->y : 0.0f;
                f32 z = isValidFloat(v->z) ? v->z : 0.0f;
                state[field->name] = {{"x", x}, {"y", y}, {"z", z}};
            } else {
                nlohmann::json arr = nlohmann::json::array();
                for (int i = 0; i < field->arraySize; i++) {
                    Vec3f* v = &reinterpret_cast<Vec3f*>(ptr)[i];
                    f32 x = isValidFloat(v->x) ? v->x : 0.0f;
                    f32 y = isValidFloat(v->y) ? v->y : 0.0f;
                    f32 z = isValidFloat(v->z) ? v->z : 0.0f;
                    arr.push_back({{"x", x}, {"y", y}, {"z", z}});
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
        default:
            // Unknown field type - skip
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
    if (jointTable != nullptr && syncInfo->jointCount > 0 && syncInfo->jointCount <= 100) {
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
    if (morphTable != nullptr && syncInfo->morphCount > 0 && syncInfo->morphCount <= 100) {
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

    // Serialize SkelAnime frame/speed (with NaN/Inf protection)
    SkelAnime* skelAnime = GetActorSkelAnime(actor, syncInfo);
    if (skelAnime != nullptr && skelAnime->animation != nullptr) {
        // Sanitize float values before serializing
        state["animFrame"] = isValidFloat(skelAnime->curFrame) ? skelAnime->curFrame : 0.0f;
        state["animSpeed"] = isValidFloat(skelAnime->playSpeed) ? skelAnime->playSpeed : 1.0f;
        state["animStart"] = isValidFloat(skelAnime->startFrame) ? skelAnime->startFrame : 0.0f;
        state["animEnd"] = isValidFloat(skelAnime->endFrame) ? skelAnime->endFrame : 0.0f;
    }

    // Serialize all syncable state fields
    if (syncInfo->stateFields != nullptr && syncInfo->stateFieldCount > 0) {
        for (int i = 0; i < syncInfo->stateFieldCount; i++) {
            FieldDescriptor* field = &syncInfo->stateFields[i];
            if (field != nullptr && field->name != nullptr) {
                try {
                    SerializeField(state, actor, field);
                } catch (const std::exception& e) {
                    // Silently skip fields that fail to serialize
                }
            }
        }
    }

    return state;
}

// ============================================================================
// DESERIALIZATION (JSON -> Actor)
// ============================================================================

/**
 * Deserialize a field based on its type.
 * Maximum protection against malformed JSON.
 */
static void DeserializeField(const nlohmann::json& state, Actor* actor, FieldDescriptor* field) {
    // Validate inputs
    if (field == nullptr || field->name == nullptr) return;
    if (!state.is_object()) return;
    if (!state.contains(field->name)) return;

    const auto& value = state[field->name];

    // Skip null values entirely
    if (value.is_null()) return;

    // Sanity check arraySize
    if (field->arraySize == 0 || field->arraySize > 100) return;

    uintptr_t ptr = reinterpret_cast<uintptr_t>(actor) + field->offset;

    switch (field->type) {
        case FIELD_TYPE_U8:
            if (field->arraySize == 1) {
                if (isValidNumber(value)) {
                    *reinterpret_cast<u8*>(ptr) = safeGetNumber<u8>(value);
                }
            } else if (isValidArray(value)) {
                size_t count = std::min(value.size(), (size_t)field->arraySize);
                for (size_t i = 0; i < count; i++) {
                    if (isValidArrayElement(value, i) && isValidNumber(value[i])) {
                        reinterpret_cast<u8*>(ptr)[i] = safeGetNumber<u8>(value[i]);
                    }
                }
            }
            break;

        case FIELD_TYPE_S8:
            if (field->arraySize == 1) {
                if (isValidNumber(value)) {
                    *reinterpret_cast<s8*>(ptr) = safeGetNumber<s8>(value);
                }
            } else if (isValidArray(value)) {
                size_t count = std::min(value.size(), (size_t)field->arraySize);
                for (size_t i = 0; i < count; i++) {
                    if (isValidArrayElement(value, i) && isValidNumber(value[i])) {
                        reinterpret_cast<s8*>(ptr)[i] = safeGetNumber<s8>(value[i]);
                    }
                }
            }
            break;

        case FIELD_TYPE_U16:
            if (field->arraySize == 1) {
                if (isValidNumber(value)) {
                    *reinterpret_cast<u16*>(ptr) = safeGetNumber<u16>(value);
                }
            } else if (isValidArray(value)) {
                size_t count = std::min(value.size(), (size_t)field->arraySize);
                for (size_t i = 0; i < count; i++) {
                    if (isValidArrayElement(value, i) && isValidNumber(value[i])) {
                        reinterpret_cast<u16*>(ptr)[i] = safeGetNumber<u16>(value[i]);
                    }
                }
            }
            break;

        case FIELD_TYPE_S16:
            if (field->arraySize == 1) {
                if (isValidNumber(value)) {
                    *reinterpret_cast<s16*>(ptr) = safeGetNumber<s16>(value);
                }
            } else if (isValidArray(value)) {
                size_t count = std::min(value.size(), (size_t)field->arraySize);
                for (size_t i = 0; i < count; i++) {
                    if (isValidArrayElement(value, i) && isValidNumber(value[i])) {
                        reinterpret_cast<s16*>(ptr)[i] = safeGetNumber<s16>(value[i]);
                    }
                }
            }
            break;

        case FIELD_TYPE_U32:
            if (field->arraySize == 1) {
                if (isValidNumber(value)) {
                    *reinterpret_cast<u32*>(ptr) = safeGetNumber<u32>(value);
                }
            } else if (isValidArray(value)) {
                size_t count = std::min(value.size(), (size_t)field->arraySize);
                for (size_t i = 0; i < count; i++) {
                    if (isValidArrayElement(value, i) && isValidNumber(value[i])) {
                        reinterpret_cast<u32*>(ptr)[i] = safeGetNumber<u32>(value[i]);
                    }
                }
            }
            break;

        case FIELD_TYPE_S32:
            if (field->arraySize == 1) {
                if (isValidNumber(value)) {
                    *reinterpret_cast<s32*>(ptr) = safeGetNumber<s32>(value);
                }
            } else if (isValidArray(value)) {
                size_t count = std::min(value.size(), (size_t)field->arraySize);
                for (size_t i = 0; i < count; i++) {
                    if (isValidArrayElement(value, i) && isValidNumber(value[i])) {
                        reinterpret_cast<s32*>(ptr)[i] = safeGetNumber<s32>(value[i]);
                    }
                }
            }
            break;

        case FIELD_TYPE_F32:
            if (field->arraySize == 1) {
                if (isValidNumber(value)) {
                    *reinterpret_cast<f32*>(ptr) = safeGetFloat(value);
                }
            } else if (isValidArray(value)) {
                size_t count = std::min(value.size(), (size_t)field->arraySize);
                for (size_t i = 0; i < count; i++) {
                    if (isValidArrayElement(value, i) && isValidNumber(value[i])) {
                        reinterpret_cast<f32*>(ptr)[i] = safeGetFloat(value[i]);
                    }
                }
            }
            break;

        case FIELD_TYPE_VEC3F:
            if (field->arraySize == 1) {
                if (isValidVec3f(value)) {
                    Vec3f* v = reinterpret_cast<Vec3f*>(ptr);
                    v->x = safeGetFloat(value["x"]);
                    v->y = safeGetFloat(value["y"]);
                    v->z = safeGetFloat(value["z"]);
                }
            } else if (isValidArray(value)) {
                size_t count = std::min(value.size(), (size_t)field->arraySize);
                for (size_t i = 0; i < count; i++) {
                    if (isValidArrayElement(value, i) && isValidVec3f(value[i])) {
                        Vec3f* v = &reinterpret_cast<Vec3f*>(ptr)[i];
                        v->x = safeGetFloat(value[i]["x"]);
                        v->y = safeGetFloat(value[i]["y"]);
                        v->z = safeGetFloat(value[i]["z"]);
                    }
                }
            }
            break;

        case FIELD_TYPE_VEC3S:
            if (field->arraySize == 1) {
                if (isValidVec3(value)) {
                    Vec3s* v = reinterpret_cast<Vec3s*>(ptr);
                    v->x = safeGetNumber<s16>(value["x"]);
                    v->y = safeGetNumber<s16>(value["y"]);
                    v->z = safeGetNumber<s16>(value["z"]);
                }
            } else if (isValidArray(value)) {
                size_t count = std::min(value.size(), (size_t)field->arraySize);
                for (size_t i = 0; i < count; i++) {
                    if (isValidArrayElement(value, i) && isValidVec3(value[i])) {
                        Vec3s* v = &reinterpret_cast<Vec3s*>(ptr)[i];
                        v->x = safeGetNumber<s16>(value[i]["x"]);
                        v->y = safeGetNumber<s16>(value[i]["y"]);
                        v->z = safeGetNumber<s16>(value[i]["z"]);
                    }
                }
            }
            break;

        default:
            // Unknown field type - skip
            break;
    }
}

void DeserializeActorState(Actor* actor, const nlohmann::json& state) {
    // Validate inputs
    if (actor == nullptr) return;
    if (state.empty() || !state.is_object()) return;

    ActorSyncInfo* syncInfo = GetActorSyncInfo(actor->id);
    if (syncInfo == nullptr) return;

    // Deserialize actionFunc from index (with protection)
    if (state.contains("actionFuncIndex")) {
        const auto& funcIndexValue = state["actionFuncIndex"];
        if (isValidNumber(funcIndexValue)) {
            int funcIndex = safeGetNumber<int>(funcIndexValue, -1);
            if (funcIndex >= 0) {
                void* actionFunc = GetActionFuncByIndex(syncInfo, funcIndex);
                if (actionFunc != nullptr) {
                    SetActorActionFunc(actor, syncInfo, actionFunc);
                }
            }
        }
    }

    // Deserialize jointTable (with full protection)
    if (state.contains("jointTable")) {
        const auto& joints = state["jointTable"];
        if (isValidArray(joints)) {
            Vec3s* jointTable = GetActorJointTable(actor, syncInfo);
            if (jointTable != nullptr && syncInfo->jointCount > 0) {
                size_t count = std::min(joints.size(), (size_t)syncInfo->jointCount);
                for (size_t i = 0; i < count; i++) {
                    if (isValidArrayElement(joints, i) && isValidVec3(joints[i])) {
                        jointTable[i].x = safeGetNumber<s16>(joints[i]["x"]);
                        jointTable[i].y = safeGetNumber<s16>(joints[i]["y"]);
                        jointTable[i].z = safeGetNumber<s16>(joints[i]["z"]);
                    }
                }
            }
        }
    }

    // Deserialize morphTable (with full protection)
    if (state.contains("morphTable")) {
        const auto& morphs = state["morphTable"];
        if (isValidArray(morphs)) {
            Vec3s* morphTable = GetActorMorphTable(actor, syncInfo);
            if (morphTable != nullptr && syncInfo->morphCount > 0) {
                size_t count = std::min(morphs.size(), (size_t)syncInfo->morphCount);
                for (size_t i = 0; i < count; i++) {
                    if (isValidArrayElement(morphs, i) && isValidVec3(morphs[i])) {
                        morphTable[i].x = safeGetNumber<s16>(morphs[i]["x"]);
                        morphTable[i].y = safeGetNumber<s16>(morphs[i]["y"]);
                        morphTable[i].z = safeGetNumber<s16>(morphs[i]["z"]);
                    }
                }
            }
        }
    }

    // Deserialize SkelAnime frame/speed (with NaN/Inf protection)
    SkelAnime* skelAnime = GetActorSkelAnime(actor, syncInfo);
    if (skelAnime != nullptr && skelAnime->animation != nullptr) {
        if (state.contains("animFrame")) {
            const auto& frameValue = state["animFrame"];
            if (isValidNumber(frameValue)) {
                f32 frame = safeGetFloat(frameValue);
                // Sanity check frame value
                if (frame >= 0.0f && frame < 10000.0f) {
                    skelAnime->curFrame = frame;
                }
            }
        }
        if (state.contains("animSpeed")) {
            const auto& speedValue = state["animSpeed"];
            if (isValidNumber(speedValue)) {
                f32 speed = safeGetFloat(speedValue);
                // Sanity check speed value (reasonable range)
                if (speed >= -10.0f && speed <= 10.0f) {
                    skelAnime->playSpeed = speed;
                }
            }
        }
    }

    // Deserialize all syncable state fields (with try-catch for each field)
    if (syncInfo->stateFields != nullptr && syncInfo->stateFieldCount > 0) {
        for (int i = 0; i < syncInfo->stateFieldCount; i++) {
            FieldDescriptor* field = &syncInfo->stateFields[i];
            if (field != nullptr && field->name != nullptr) {
                try {
                    DeserializeField(state, actor, field);
                } catch (const std::exception& e) {
                    // Silently skip fields that fail to deserialize
                    // This prevents crashes from malformed JSON
                }
            }
        }
    }
}
