#include "PlayerSerializer.h"
#include <libultraship/libultraship.h>
#include <cmath>

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

// Helper to safely get a float and validate it
static bool isValidJsonFloat(const nlohmann::json& v) {
    if (!isValidNumber(v)) return false;
    f32 val = v.get<f32>();
    return isValidFloat(val);
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

    // Check each component exists and is a valid number
    const auto& x = v["x"];
    const auto& y = v["y"];
    const auto& z = v["z"];

    return isValidNumber(x) && isValidNumber(y) && isValidNumber(z);
}

// Helper to safely check if a JSON value is a valid Vec3f (float) with sane values
static bool isValidVec3f(const nlohmann::json& v) {
    if (!isValidVec3(v)) return false;

    // Additional float sanity checks
    f32 x = v["x"].get<f32>();
    f32 y = v["y"].get<f32>();
    f32 z = v["z"].get<f32>();

    return isValidFloat(x) && isValidFloat(y) && isValidFloat(z);
}

// ============================================================================
// SERIALIZATION (Player -> JSON)
// ============================================================================

/**
 * Serialize a Player field based on its type.
 * Includes protection against serializing invalid float values (NaN, Inf)
 */
static void SerializePlayerField(nlohmann::json& state, Player* player, PlayerFieldDescriptor* field) {
    // Sanity check field descriptor
    if (field == nullptr || field->name == nullptr || field->arraySize == 0 || field->arraySize > 100) {
        return;
    }

    uintptr_t ptr = reinterpret_cast<uintptr_t>(player) + field->offset;

    switch (field->type) {
        case PLAYER_FIELD_TYPE_U8:
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
        case PLAYER_FIELD_TYPE_S8:
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
        case PLAYER_FIELD_TYPE_U16:
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
        case PLAYER_FIELD_TYPE_S16:
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
        case PLAYER_FIELD_TYPE_U32:
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
        case PLAYER_FIELD_TYPE_S32:
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
        case PLAYER_FIELD_TYPE_F32:
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
        case PLAYER_FIELD_TYPE_VEC3F:
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
        case PLAYER_FIELD_TYPE_VEC3S:
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

// ============================================================================
// DESERIALIZATION (JSON -> Player)
// ============================================================================

/**
 * Safely get a numeric value from JSON with type bounds checking.
 * Returns default value if conversion fails or value is out of range.
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

/**
 * Deserialize a Player field based on its type.
 * Maximum protection against malformed JSON.
 */
static void DeserializePlayerField(const nlohmann::json& state, Player* player, PlayerFieldDescriptor* field) {
    // Validate inputs
    if (field == nullptr || field->name == nullptr) return;
    if (!state.is_object()) return;
    if (!state.contains(field->name)) return;

    const auto& value = state[field->name];

    // Skip null values entirely
    if (value.is_null()) return;

    // Sanity check arraySize
    if (field->arraySize == 0 || field->arraySize > 100) return;

    uintptr_t ptr = reinterpret_cast<uintptr_t>(player) + field->offset;

    switch (field->type) {
        case PLAYER_FIELD_TYPE_U8:
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

        case PLAYER_FIELD_TYPE_S8:
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

        case PLAYER_FIELD_TYPE_U16:
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

        case PLAYER_FIELD_TYPE_S16:
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

        case PLAYER_FIELD_TYPE_U32:
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

        case PLAYER_FIELD_TYPE_S32:
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

        case PLAYER_FIELD_TYPE_F32:
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

        case PLAYER_FIELD_TYPE_VEC3F:
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

        case PLAYER_FIELD_TYPE_VEC3S:
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

nlohmann::json SerializePlayerState(Player* player) {
    nlohmann::json state;

    if (player == nullptr) return state;

    // Serialize all fields using the field descriptors
    for (int i = 0; i < PlayerSyncFieldCount; i++) {
        PlayerFieldDescriptor* field = &PlayerSyncFields[i];
        if (field->name != nullptr) {
            SerializePlayerField(state, player, field);
        }
    }

    return state;
}

void DeserializePlayerState(Player* player, const nlohmann::json& state) {
    if (player == nullptr || state.empty() || !state.is_object()) return;

    // Deserialize all fields using the field descriptors
    for (int i = 0; i < PlayerSyncFieldCount; i++) {
        PlayerFieldDescriptor* field = &PlayerSyncFields[i];
        if (field->name != nullptr) {
            try {
                DeserializePlayerField(state, player, field);
            } catch (const std::exception& e) {
                // Silently skip fields that fail to deserialize
                // This prevents crashes from malformed JSON
            }
        }
    }
}
