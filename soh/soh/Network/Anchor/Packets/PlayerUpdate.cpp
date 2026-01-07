#include "soh/Network/Anchor/Anchor.h"
#include "soh/Network/Anchor/JsonConversions.hpp"
#include "soh/Network/Anchor/PlayerSerializer.h"
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

    // Serialize ALL Player state fields using PlayerSerializer
    // This automatically handles ~100 fields including equipment, state flags,
    // combat, movement, animation, body parts, etc.
    payload["playerState"] = SerializePlayerState(player);

    // Special handling for stateFlags2 - mask out DISABLE_DRAW
    payload["playerState"]["stateFlags2"] = player->stateFlags2 & ~PLAYER_STATE2_DISABLE_DRAW;

    // Equipment button (needs gSaveContext)
    payload["buttonItem0"] = gSaveContext.equips.buttonItems[0];

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

        // Store all Player state fields as JSON
        // DummyPlayer will deserialize this directly to the dummy player
        if (payload.contains("playerState")) {
            client.playerState = payload["playerState"];
        }

        // Equipment button (needs gSaveContext hack in DummyPlayer)
        client.buttonItem0 = payload["buttonItem0"].get<u8>();

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
