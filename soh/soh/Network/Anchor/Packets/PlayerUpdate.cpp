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
    payload["currentBoots"] = player->currentBoots;
    payload["currentShield"] = player->currentShield;
    payload["currentTunic"] = player->currentTunic;
    payload["stateFlags1"] = player->stateFlags1;
    payload["stateFlags2"] = player->stateFlags2 & ~PLAYER_STATE2_DISABLE_DRAW;
    payload["buttonItem0"] = gSaveContext.equips.buttonItems[0];
    payload["itemAction"] = player->itemAction;
    payload["heldItemAction"] = player->heldItemAction;
    payload["modelGroup"] = player->modelGroup;
    payload["invincibilityTimer"] = player->invincibilityTimer;
    payload["unk_862"] = player->unk_862;
    payload["unk_85C"] = player->unk_85C;
    payload["actionVar1"] = player->av1.actionVar1;

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
        client.currentBoots = payload["currentBoots"].get<s8>();
        client.currentShield = payload["currentShield"].get<s8>();
        client.currentTunic = payload["currentTunic"].get<s8>();
        client.stateFlags1 = payload["stateFlags1"].get<u32>();
        client.stateFlags2 = payload["stateFlags2"].get<u32>();
        client.buttonItem0 = payload["buttonItem0"].get<u8>();
        client.itemAction = payload["itemAction"].get<s8>();
        client.heldItemAction = payload["heldItemAction"].get<s8>();
        client.modelGroup = payload["modelGroup"].get<u8>();
        client.invincibilityTimer = payload["invincibilityTimer"].get<s8>();
        client.unk_862 = payload["unk_862"].get<s16>();
        client.unk_85C = payload["unk_85C"].get<f32>();
        client.actionVar1 = payload["actionVar1"].get<s8>();

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
