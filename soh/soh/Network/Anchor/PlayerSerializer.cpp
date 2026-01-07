#include "PlayerSerializer.h"
#include <libultraship/libultraship.h>

/**
 * Serialize a Player field based on its type.
 */
static void SerializePlayerField(nlohmann::json& state, Player* player, PlayerFieldDescriptor* field) {
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
                state[field->name] = *reinterpret_cast<f32*>(ptr);
            } else {
                nlohmann::json arr = nlohmann::json::array();
                for (int i = 0; i < field->arraySize; i++) {
                    arr.push_back(reinterpret_cast<f32*>(ptr)[i]);
                }
                state[field->name] = arr;
            }
            break;
        case PLAYER_FIELD_TYPE_VEC3F:
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
    }
}

/**
 * Deserialize a Player field based on its type.
 */
static void DeserializePlayerField(const nlohmann::json& state, Player* player, PlayerFieldDescriptor* field) {
    if (!state.contains(field->name)) return;

    const auto& value = state[field->name];

    // Skip null values
    if (value.is_null()) return;

    uintptr_t ptr = reinterpret_cast<uintptr_t>(player) + field->offset;

    switch (field->type) {
        case PLAYER_FIELD_TYPE_U8:
            if (field->arraySize == 1) {
                *reinterpret_cast<u8*>(ptr) = value.get<u8>();
            } else if (value.is_array()) {
                size_t count = std::min(value.size(), (size_t)field->arraySize);
                for (size_t i = 0; i < count; i++) {
                    reinterpret_cast<u8*>(ptr)[i] = value[i].get<u8>();
                }
            }
            break;
        case PLAYER_FIELD_TYPE_S8:
            if (field->arraySize == 1) {
                *reinterpret_cast<s8*>(ptr) = value.get<s8>();
            } else if (value.is_array()) {
                size_t count = std::min(value.size(), (size_t)field->arraySize);
                for (size_t i = 0; i < count; i++) {
                    reinterpret_cast<s8*>(ptr)[i] = value[i].get<s8>();
                }
            }
            break;
        case PLAYER_FIELD_TYPE_U16:
            if (field->arraySize == 1) {
                *reinterpret_cast<u16*>(ptr) = value.get<u16>();
            } else if (value.is_array()) {
                size_t count = std::min(value.size(), (size_t)field->arraySize);
                for (size_t i = 0; i < count; i++) {
                    reinterpret_cast<u16*>(ptr)[i] = value[i].get<u16>();
                }
            }
            break;
        case PLAYER_FIELD_TYPE_S16:
            if (field->arraySize == 1) {
                *reinterpret_cast<s16*>(ptr) = value.get<s16>();
            } else if (value.is_array()) {
                size_t count = std::min(value.size(), (size_t)field->arraySize);
                for (size_t i = 0; i < count; i++) {
                    reinterpret_cast<s16*>(ptr)[i] = value[i].get<s16>();
                }
            }
            break;
        case PLAYER_FIELD_TYPE_U32:
            if (field->arraySize == 1) {
                *reinterpret_cast<u32*>(ptr) = value.get<u32>();
            } else if (value.is_array()) {
                size_t count = std::min(value.size(), (size_t)field->arraySize);
                for (size_t i = 0; i < count; i++) {
                    reinterpret_cast<u32*>(ptr)[i] = value[i].get<u32>();
                }
            }
            break;
        case PLAYER_FIELD_TYPE_S32:
            if (field->arraySize == 1) {
                *reinterpret_cast<s32*>(ptr) = value.get<s32>();
            } else if (value.is_array()) {
                size_t count = std::min(value.size(), (size_t)field->arraySize);
                for (size_t i = 0; i < count; i++) {
                    reinterpret_cast<s32*>(ptr)[i] = value[i].get<s32>();
                }
            }
            break;
        case PLAYER_FIELD_TYPE_F32:
            if (field->arraySize == 1) {
                *reinterpret_cast<f32*>(ptr) = value.get<f32>();
            } else if (value.is_array()) {
                size_t count = std::min(value.size(), (size_t)field->arraySize);
                for (size_t i = 0; i < count; i++) {
                    reinterpret_cast<f32*>(ptr)[i] = value[i].get<f32>();
                }
            }
            break;
        case PLAYER_FIELD_TYPE_VEC3F:
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
        case PLAYER_FIELD_TYPE_VEC3S:
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
    if (player == nullptr || state.empty()) return;

    // Deserialize all fields using the field descriptors
    for (int i = 0; i < PlayerSyncFieldCount; i++) {
        PlayerFieldDescriptor* field = &PlayerSyncFields[i];
        if (field->name != nullptr) {
            DeserializePlayerField(state, player, field);
        }
    }
}
