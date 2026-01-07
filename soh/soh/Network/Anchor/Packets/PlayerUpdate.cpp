#include "soh/Network/Anchor/Anchor.h"
#include "soh/Network/Anchor/JsonConversions.hpp"
#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h>

extern "C" {
#include "macros.h"
#include "variables.h"
extern PlayState* gPlayState;
}

/**
 * PLAYER_UPDATE
 *
 * Contains real-time data necessary to update other clients in the same scene as the player
 *
 * Sent every frame to other clients within the same scene
 *
 * Note: This packet is sent _a lot_, so please do not include any unnecessary data in it
 */

void Anchor::SendPacket_PlayerUpdate() {
    if (!IsSaveLoaded() || gPlayState == nullptr) {
        return;
    }

    Player* player = GET_PLAYER(gPlayState);
    if (player == nullptr) {
        return;
    }

    uint32_t currentPlayerCount = 0;
    for (auto& [clientId, client] : clients) {
        if (client.sceneNum == gPlayState->sceneNum && client.online && client.isSaveLoaded && !client.self) {
            currentPlayerCount++;
        }
    }
    if (currentPlayerCount == 0) {
        return;
    }
    nlohmann::json payload;

    payload["type"] = PLAYER_UPDATE;
    payload["sceneNum"] = gPlayState->sceneNum;
    payload["entranceIndex"] = gSaveContext.entranceIndex;
    payload["linkAge"] = gSaveContext.linkAge;
    payload["posRot"]["pos"] = player->actor.world.pos;
    payload["posRot"]["rot"] = player->actor.shape.rot;

    // Serialize all 5 animation tables (24 joints each)
    // This gives us 100% accurate animation sync including upper body (arms when holding items)
    auto serializeTable = [](Vec3s* table) -> std::vector<int> {
        std::vector<int> arr;
        arr.reserve(24 * 3);
        for (size_t i = 0; i < 24; i++) {
            arr.push_back(table[i].x);
            arr.push_back(table[i].y);
            arr.push_back(table[i].z);
        }
        return arr;
    };

    payload["jointTable"] = serializeTable(player->skelAnime.jointTable);
    payload["morphTable"] = serializeTable(player->skelAnime.morphTable);
    payload["blendTable"] = serializeTable(player->blendTable);
    payload["upperJointTable"] = serializeTable(player->upperSkelAnime.jointTable);
    payload["upperMorphTable"] = serializeTable(player->upperSkelAnime.morphTable);

    payload["prevTransl"] = player->skelAnime.prevTransl;
    payload["movementFlags"] = player->skelAnime.movementFlags;

    // Equipment and appearance
    payload["currentBoots"] = player->currentBoots;
    payload["currentShield"] = player->currentShield;
    payload["currentTunic"] = player->currentTunic;
    payload["currentSwordItemId"] = player->currentSwordItemId;
    payload["currentMask"] = player->currentMask;
    payload["modelGroup"] = player->modelGroup;
    payload["leftHandType"] = player->leftHandType;
    payload["rightHandType"] = player->rightHandType;
    payload["sheathType"] = player->sheathType;

    // State flags
    payload["stateFlags1"] = player->stateFlags1;
    payload["stateFlags2"] = player->stateFlags2 & ~PLAYER_STATE2_DISABLE_DRAW;
    payload["stateFlags3"] = player->stateFlags3;

    // Item state
    payload["buttonItem0"] = gSaveContext.equips.buttonItems[0];
    payload["itemAction"] = player->itemAction;
    payload["heldItemAction"] = player->heldItemAction;
    payload["heldItemId"] = player->heldItemId;

    // Combat state
    payload["meleeWeaponAnimation"] = player->meleeWeaponAnimation;
    payload["meleeWeaponState"] = player->meleeWeaponState;
    payload["invincibilityTimer"] = player->invincibilityTimer;

    // Movement
    payload["linearVelocity"] = player->linearVelocity;
    payload["yaw"] = player->yaw;

    // Upper body rotation
    payload["headLimbRot"] = player->headLimbRot;
    payload["upperLimbRot"] = player->upperLimbRot;

    // Action variables
    payload["actionVar1"] = player->av1.actionVar1;
    payload["actionVar2"] = player->av2.actionVar2;

    // Misc state
    payload["unk_85C"] = player->unk_85C;
    payload["unk_862"] = player->unk_862;
    payload["unk_860"] = player->unk_860;
    payload["unk_854"] = player->unk_854;
    payload["unk_858"] = player->unk_858;

    // Door state
    payload["doorType"] = player->doorType;
    payload["doorDirection"] = player->doorDirection;
    payload["doorTimer"] = player->doorTimer;

    // Body parts positions
    auto serializeBodyParts = [](Vec3f* parts) -> std::vector<float> {
        std::vector<float> arr;
        arr.reserve(18 * 3);
        for (size_t i = 0; i < 18; i++) {
            arr.push_back(parts[i].x);
            arr.push_back(parts[i].y);
            arr.push_back(parts[i].z);
        }
        return arr;
    };
    payload["bodyPartsPos"] = serializeBodyParts(player->bodyPartsPos);

    // Held actor (for rocks, bombs, etc.)
    u32 heldActorNetworkId = 0;
    if (player->heldActor != nullptr) {
        heldActorNetworkId = Anchor::Instance->GetOrAssignNetworkId(player->heldActor);
    }
    payload["heldActorNetworkId"] = heldActorNetworkId;

    payload["quiet"] = true;

    for (auto& [clientId, client] : clients) {
        if (client.sceneNum == gPlayState->sceneNum && client.online && client.isSaveLoaded && !client.self) {
            payload["targetClientId"] = clientId;
            SendJsonToRemote(payload);
        }
    }
}

void Anchor::HandlePacket_PlayerUpdate(nlohmann::json payload) {
    try {
        if (!payload.contains("clientId")) return;
        uint32_t clientId = payload["clientId"].get<uint32_t>();

        if (!clients.contains(clientId)) return;
        auto& client = clients[clientId];

        // Check required fields exist before accessing
        if (!payload.contains("linkAge") || !payload.contains("sceneNum") ||
            !payload.contains("entranceIndex") || !payload.contains("posRot")) {
            return;
        }

        if (client.linkAge != payload["linkAge"].get<s32>()) {
            shouldRefreshActors = true;
        }

        client.sceneNum = payload["sceneNum"].get<s16>();
        client.entranceIndex = payload["entranceIndex"].get<s32>();
        client.linkAge = payload["linkAge"].get<s32>();
        client.posRot = payload["posRot"].get<PosRot>();

        // Deserialize all 5 animation tables
        auto deserializeTable = [](const nlohmann::json& arr, Vec3s* table) {
            std::vector<int> data = arr.get<std::vector<int>>();
            for (int i = 0; i < 24 && i * 3 + 2 < (int)data.size(); i++) {
                table[i].x = data[i * 3];
                table[i].y = data[i * 3 + 1];
                table[i].z = data[i * 3 + 2];
            }
        };

        if (payload.contains("jointTable")) deserializeTable(payload["jointTable"], client.jointTable);
        if (payload.contains("morphTable")) deserializeTable(payload["morphTable"], client.morphTable);
        if (payload.contains("blendTable")) deserializeTable(payload["blendTable"], client.blendTable);
        if (payload.contains("upperJointTable")) deserializeTable(payload["upperJointTable"], client.upperJointTable);
        if (payload.contains("upperMorphTable")) deserializeTable(payload["upperMorphTable"], client.upperMorphTable);

        client.movementFlags = payload["movementFlags"].get<u8>();
        client.prevTransl = payload["prevTransl"].get<Vec3s>();

        // Equipment and appearance
        client.currentBoots = payload["currentBoots"].get<s8>();
        client.currentShield = payload["currentShield"].get<s8>();
        client.currentTunic = payload["currentTunic"].get<s8>();
        if (payload.contains("currentSwordItemId")) client.currentSwordItemId = payload["currentSwordItemId"].get<s8>();
        if (payload.contains("currentMask")) client.currentMask = payload["currentMask"].get<u8>();
        client.modelGroup = payload["modelGroup"].get<u8>();
        if (payload.contains("leftHandType")) client.leftHandType = payload["leftHandType"].get<u8>();
        if (payload.contains("rightHandType")) client.rightHandType = payload["rightHandType"].get<u8>();
        if (payload.contains("sheathType")) client.sheathType = payload["sheathType"].get<u8>();

        // State flags
        client.stateFlags1 = payload["stateFlags1"].get<u32>();
        client.stateFlags2 = payload["stateFlags2"].get<u32>();
        if (payload.contains("stateFlags3")) client.stateFlags3 = payload["stateFlags3"].get<u8>();

        // Item state
        client.buttonItem0 = payload["buttonItem0"].get<u8>();
        client.itemAction = payload["itemAction"].get<s8>();
        client.heldItemAction = payload["heldItemAction"].get<s8>();
        if (payload.contains("heldItemId")) client.heldItemId = payload["heldItemId"].get<u8>();

        // Combat state
        if (payload.contains("meleeWeaponAnimation")) client.meleeWeaponAnimation = payload["meleeWeaponAnimation"].get<s8>();
        if (payload.contains("meleeWeaponState")) client.meleeWeaponState = payload["meleeWeaponState"].get<s8>();
        client.invincibilityTimer = payload["invincibilityTimer"].get<s8>();

        // Movement
        if (payload.contains("linearVelocity")) client.linearVelocity = payload["linearVelocity"].get<f32>();
        if (payload.contains("yaw")) client.yaw = payload["yaw"].get<s16>();

        // Upper body rotation
        if (payload.contains("headLimbRot")) client.headLimbRot = payload["headLimbRot"].get<Vec3s>();
        if (payload.contains("upperLimbRot")) client.upperLimbRot = payload["upperLimbRot"].get<Vec3s>();

        // Action variables
        client.actionVar1 = payload["actionVar1"].get<s8>();
        if (payload.contains("actionVar2")) client.actionVar2 = payload["actionVar2"].get<s16>();

        // Misc state
        client.unk_85C = payload["unk_85C"].get<f32>();
        client.unk_862 = payload["unk_862"].get<s16>();
        if (payload.contains("unk_860")) client.unk_860 = payload["unk_860"].get<s16>();
        if (payload.contains("unk_854")) client.unk_854 = payload["unk_854"].get<f32>();
        if (payload.contains("unk_858")) client.unk_858 = payload["unk_858"].get<f32>();

        // Door state
        if (payload.contains("doorType")) client.doorType = payload["doorType"].get<s8>();
        if (payload.contains("doorDirection")) client.doorDirection = payload["doorDirection"].get<s8>();
        if (payload.contains("doorTimer")) client.doorTimer = payload["doorTimer"].get<s16>();

        // Body parts positions
        if (payload.contains("bodyPartsPos")) {
            auto deserializeBodyParts = [](const nlohmann::json& arr, Vec3f* parts) {
                std::vector<float> data = arr.get<std::vector<float>>();
                for (int i = 0; i < 18 && i * 3 + 2 < (int)data.size(); i++) {
                    parts[i].x = data[i * 3];
                    parts[i].y = data[i * 3 + 1];
                    parts[i].z = data[i * 3 + 2];
                }
            };
            deserializeBodyParts(payload["bodyPartsPos"], client.bodyPartsPos);
        }

        // Held actor
        if (payload.contains("heldActorNetworkId")) {
            client.heldActorNetworkId = payload["heldActorNetworkId"].get<u32>();
        } else {
            client.heldActorNetworkId = 0;
        }
    } catch (const std::exception& e) {
        SPDLOG_ERROR("[Anchor] Error in HandlePacket_PlayerUpdate: {}", e.what());
    }
}
