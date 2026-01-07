#include "soh/Network/Anchor/Anchor.h"
#include "soh/Network/Anchor/JsonConversions.hpp"
#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h>

extern "C" {
#include "macros.h"
#include "variables.h"
#include "functions.h"
#include "z64animation.h"
extern PlayState* gPlayState;
}

// Map of actor IDs to skelAnime offsets within their struct
static const std::map<s16, size_t> actorSkelAnimeOffsets = {
    // Enemies - Offset 0x014C (most common)
    { ACTOR_EN_TITE, 0x014C },      // Tektite
    { ACTOR_EN_PEEHAT, 0x014C },    // Peahat
    { ACTOR_EN_DEKUNUTS, 0x014C },  // Deku Scrub
    { ACTOR_EN_DODONGO, 0x014C },   // Dodongo
    { ACTOR_EN_ST, 0x014C },        // Skulltula
    { ACTOR_EN_BB, 0x014C },        // Bubble
    { ACTOR_EN_POH, 0x014C },       // Poe
    { ACTOR_EN_OKUTA, 0x014C },     // Octorok
    { ACTOR_EN_WALLMAS, 0x014C },   // Wallmaster
    { ACTOR_EN_FLOORMAS, 0x014C },  // Floormaster
    { ACTOR_EN_ZF, 0x014C },        // Lizalfos
    { ACTOR_EN_VALI, 0x014C },      // Bari
    { ACTOR_EN_BILI, 0x014C },      // Biri
    { ACTOR_EN_DODOJR, 0x014C },    // Baby Dodongo
    { ACTOR_EN_BW, 0x014C },        // Torch Slug
    { ACTOR_EN_SW, 0x014C },        // Skullwalltula
    { ACTOR_EN_SB, 0x014C },        // Shell Blade
    { ACTOR_EN_WEIYER, 0x014C },    // Stinger
    { ACTOR_EN_IK, 0x014C },        // Iron Knuckle
    { ACTOR_EN_FW, 0x014C },        // Flare Dancer core
    { ACTOR_EN_FD, 0x014C },        // Flare Dancer
    { ACTOR_EN_DH, 0x014C },        // Dead Hand
    { ACTOR_EN_DHA, 0x014C },       // Dead Hand's Hand
    { ACTOR_EN_VM, 0x014C },        // Beamos
    { ACTOR_EN_SKB, 0x014C },       // Stalchild
    { ACTOR_EN_SKJ, 0x014C },       // Skull Kid
    { ACTOR_EN_KAREBABA, 0x014C },  // Withered Deku Baba
    { ACTOR_EN_ANUBICE, 0x014C },   // Anubis
    { ACTOR_EN_HINTNUTS, 0x014C },  // Hint Deku Scrub
    { ACTOR_EN_SHOPNUTS, 0x014C },  // Business Deku Scrub
    { ACTOR_EN_DNS, 0x014C },       // Deku Salesman
    { ACTOR_EN_BIGOKUTA, 0x014C },  // Big Octo
    { ACTOR_EN_PO_FIELD, 0x014C },  // Big Poe
    { ACTOR_EN_PO_DESERT, 0x014C }, // Desert Poe
    { ACTOR_EN_PO_SISTERS, 0x014C },// Poe Sisters
    { ACTOR_EN_PO_RELAY, 0x014C },  // Poe Guide
    { ACTOR_EN_SSH, 0x014C },       // Cursed Spider
    { ACTOR_EN_GOMA, 0x014C },      // Gohma Larva
    { ACTOR_EN_BUBBLE, 0x014C },    // Shabom
    { ACTOR_EN_REEBA, 0x014C },     // Leever
    { ACTOR_EN_EIYER, 0x014C },     // Stinger water
    { ACTOR_EN_NY, 0x014C },        // Spike
    { ACTOR_EN_FZ, 0x014C },        // Freezard
    { ACTOR_EN_RR, 0x014C },        // Like-Like
    { ACTOR_EN_BA, 0x014C },        // Tentacle
    { ACTOR_EN_TORCH2, 0x014C },    // Dark Link

    // Offset 0x0164
    { ACTOR_EN_AM, 0x0164 },        // Armos
    { ACTOR_EN_BROB, 0x0164 },      // Beamos different
    { ACTOR_EN_JJ, 0x0164 },        // Jabu Jabu

    // Offset 0x0170
    { ACTOR_EN_FIREFLY, 0x0170 },   // Keese

    // Offset 0x017C
    { ACTOR_EN_DEKUBABA, 0x017C },  // Deku Baba
    { ACTOR_EN_CROW, 0x017C },      // Guay

    // Offset 0x0188
    { ACTOR_EN_TEST, 0x0188 },      // Stalfos
    { ACTOR_EN_RD, 0x0188 },        // ReDead/Gibdo
    { ACTOR_EN_WF, 0x0188 },        // Wolfos
    { ACTOR_EN_GELDB, 0x0188 },     // Gerudo Fighter

    // Offset 0x018C
    { ACTOR_EN_MB, 0x018C },        // Moblin

    // Bosses
    { ACTOR_BOSS_GOMA, 0x014C },
    { ACTOR_BOSS_DODONGO, 0x014C },
    { ACTOR_BOSS_VA, 0x014C },
    { ACTOR_BOSS_GANONDROF, 0x014C },
    { ACTOR_BOSS_FD, 0x014C },
    { ACTOR_BOSS_FD2, 0x014C },
    { ACTOR_BOSS_SST, 0x014C },
    { ACTOR_BOSS_GANON2, 0x014C },
    { ACTOR_BOSS_GANON, 0x0150 },
    { ACTOR_BOSS_TW, 0x0568 },
};

// List of actor categories to sync
static const std::set<s16> syncableCategories = {
    ACTORCAT_ENEMY,
    ACTORCAT_BOSS,
};

/**
 * Generate a unique ID for an actor based on its initial spawn position and params.
 * This allows matching actors across clients without network-assigned IDs.
 */
u32 Anchor::GetActorUniqueId(Actor* actor) {
    if (actor == nullptr) return 0;

    // Use a hash of actor ID, params, and home position
    u32 hash = actor->id;
    hash ^= (actor->params << 16);
    hash ^= (u32)(actor->home.pos.x * 10.0f) << 8;
    hash ^= (u32)(actor->home.pos.y * 10.0f) << 4;
    hash ^= (u32)(actor->home.pos.z * 10.0f);

    return hash;
}

/**
 * Get the client ID of the player closest to an actor.
 * Used to determine ownership for state broadcasting.
 */
uint32_t Anchor::GetClosestPlayerToActor(Actor* actor) {
    if (actor == nullptr || gPlayState == nullptr) return ownClientId;

    Player* localPlayer = GET_PLAYER(gPlayState);
    if (localPlayer == nullptr) return ownClientId;

    // Start with local player distance
    f32 minDist = Actor_WorldDistXZToActor(actor, &localPlayer->actor);
    uint32_t closestClientId = ownClientId;

    // Check all DummyPlayers (other connected players)
    Actor* npcActor = gPlayState->actorCtx.actorLists[ACTORCAT_NPC].head;
    while (npcActor != NULL) {
        if (npcActor->id == ACTOR_EN_OE2 && npcActor->update == DummyPlayer_Update) {
            uint32_t dummyClientId = GetDummyPlayerClientId(npcActor);
            if (clients.contains(dummyClientId) && clients[dummyClientId].online) {
                f32 dist = Actor_WorldDistXZToActor(actor, npcActor);
                if (dist < minDist) {
                    minDist = dist;
                    closestClientId = dummyClientId;
                }
            }
        }
        npcActor = npcActor->next;
    }

    return closestClientId;
}

/**
 * Check if the local player is the owner (closest player) of an actor.
 */
bool Anchor::IsActorOwner(Actor* actor) {
    return GetClosestPlayerToActor(actor) == ownClientId;
}

/**
 * Find an actor by its unique ID in the current scene.
 */
Actor* Anchor::FindActorByUniqueId(u32 uniqueId) {
    if (gPlayState == nullptr) return nullptr;

    for (int cat : syncableCategories) {
        Actor* actor = gPlayState->actorCtx.actorLists[cat].head;
        while (actor != NULL) {
            if (GetActorUniqueId(actor) == uniqueId) {
                return actor;
            }
            actor = actor->next;
        }
    }

    return nullptr;
}

/**
 * Get the skelAnime pointer for an actor if it's a known type.
 */
SkelAnime* Anchor::GetActorSkelAnime(Actor* actor) {
    if (actor == nullptr) return nullptr;

    auto it = actorSkelAnimeOffsets.find(actor->id);
    if (it == actorSkelAnimeOffsets.end()) return nullptr;

    return reinterpret_cast<SkelAnime*>(reinterpret_cast<uintptr_t>(actor) + it->second);
}

/**
 * WORLD_SNAPSHOT
 *
 * Each client sends the state of actors they "own" (are closest to).
 * Other clients receive and apply these states with interpolation.
 *
 * Proximity-based ownership:
 * - The player closest to an actor is responsible for broadcasting its state
 * - All clients do the same distance calculation, so they agree on ownership
 * - If ownership changes (player moves), the new owner starts broadcasting
 */
void Anchor::SendPacket_WorldSnapshot() {
    if (!IsSaveLoaded() || gPlayState == nullptr) return;

    // Rate limit: send at ~15Hz (every 4 frames at 60fps)
    snapshotSendCounter++;
    if (snapshotSendCounter < 4) return;
    snapshotSendCounter = 0;

    // Check if there are other players in the same scene
    bool hasOthersInScene = false;
    for (auto& [clientId, client] : clients) {
        if (client.sceneNum == gPlayState->sceneNum && client.online &&
            client.isSaveLoaded && !client.self) {
            hasOthersInScene = true;
            break;
        }
    }
    if (!hasOthersInScene) return;

    nlohmann::json payload;
    payload["type"] = WORLD_SNAPSHOT;
    payload["sceneNum"] = gPlayState->sceneNum;
    payload["actors"] = nlohmann::json::array();
    payload["quiet"] = true;  // Don't log these frequent packets

    // Iterate through syncable actor categories
    for (int cat : syncableCategories) {
        Actor* actor = gPlayState->actorCtx.actorLists[cat].head;
        while (actor != NULL) {
            // Only send if we own this actor (are closest to it)
            if (actor->update != NULL && IsActorOwner(actor)) {
                nlohmann::json actorJson;

                u32 uniqueId = GetActorUniqueId(actor);
                actorJson["uid"] = uniqueId;
                actorJson["id"] = actor->id;
                actorJson["params"] = actor->params;
                actorJson["pos"] = actor->world.pos;
                actorJson["rot"] = actor->shape.rot;
                actorJson["hp"] = actor->colChkInfo.health;
                actorJson["dead"] = (actor->update == NULL) ? 1 : 0;

                // Animation state if available
                SkelAnime* skelAnime = GetActorSkelAnime(actor);
                if (skelAnime != nullptr) {
                    actorJson["animFrame"] = skelAnime->curFrame;
                    actorJson["animSpeed"] = skelAnime->playSpeed;
                }

                payload["actors"].push_back(actorJson);
            }
            actor = actor->next;
        }
    }

    // Only send if we have actors to sync
    if (!payload["actors"].empty()) {
        SendJsonToRemote(payload);
    }
}

/**
 * Handle incoming world snapshot from another player.
 * Apply states to actors they own (are closest to).
 */
void Anchor::HandlePacket_WorldSnapshot(nlohmann::json payload) {
    if (!IsSaveLoaded() || gPlayState == nullptr) return;

    // Ignore if not in the same scene
    s16 sceneNum = payload["sceneNum"].get<s16>();
    if (sceneNum != gPlayState->sceneNum) return;

    uint32_t senderClientId = payload["clientId"].get<uint32_t>();

    for (const auto& actorJson : payload["actors"]) {
        u32 uniqueId = actorJson["uid"].get<u32>();
        s16 actorId = actorJson["id"].get<s16>();

        // Find the actor locally
        Actor* actor = FindActorByUniqueId(uniqueId);
        if (actor == nullptr || actor->id != actorId) continue;

        // Verify sender is actually the owner (closest player to this actor)
        // This prevents conflicts if multiple clients think they own the same actor
        if (GetClosestPlayerToActor(actor) != senderClientId) continue;

        // Get target state
        Vec3f targetPos = actorJson["pos"].get<Vec3f>();
        Vec3s targetRot = actorJson["rot"].get<Vec3s>();
        s16 health = actorJson["hp"].get<s16>();

        // Store interpolation data
        {
            std::lock_guard<std::mutex> lock(actorInterpMutex);
            ActorInterpData& interp = actorInterpData[uniqueId];

            if (!interp.hasData) {
                // First update - snap to position
                interp.prevPos = targetPos;
                interp.targetPos = targetPos;
                interp.prevRot = targetRot;
                interp.targetRot = targetRot;
                interp.hasData = true;
            } else {
                // Subsequent updates - set up interpolation
                interp.prevPos = actor->world.pos;
                interp.targetPos = targetPos;
                interp.prevRot = actor->shape.rot;
                interp.targetRot = targetRot;
            }
            interp.interpAlpha = 0.0f;
            interp.lastUpdateFrame = gPlayState->state.frames;
        }

        // Apply health immediately
        actor->colChkInfo.health = health;

        // Apply animation if available
        if (actorJson.contains("animFrame")) {
            SkelAnime* skelAnime = GetActorSkelAnime(actor);
            if (skelAnime != nullptr) {
                skelAnime->curFrame = actorJson["animFrame"].get<f32>();
                if (actorJson.contains("animSpeed")) {
                    skelAnime->playSpeed = actorJson["animSpeed"].get<f32>();
                }
            }
        }

        // Handle death
        if (actorJson.contains("dead") && actorJson["dead"].get<u8>() == 1) {
            if (actor->update != NULL) {
                Actor_Kill(actor);
            }
        }
    }
}

/**
 * Apply smooth interpolation to actors we don't own.
 * Called every frame to smoothly move actors to their target positions.
 */
void Anchor::ApplyActorInterpolation() {
    if (!IsSaveLoaded() || gPlayState == nullptr) return;

    std::lock_guard<std::mutex> lock(actorInterpMutex);

    for (auto& [uniqueId, interp] : actorInterpData) {
        if (!interp.hasData) continue;

        Actor* actor = FindActorByUniqueId(uniqueId);
        if (actor == nullptr || actor->update == NULL) continue;

        // Don't interpolate actors we own
        if (IsActorOwner(actor)) continue;

        // Advance interpolation (complete in ~4 frames for 15Hz updates)
        interp.interpAlpha += 0.25f;
        if (interp.interpAlpha > 1.0f) interp.interpAlpha = 1.0f;

        // Smoothstep for smoother interpolation
        f32 t = interp.interpAlpha;
        f32 smooth = t * t * (3.0f - 2.0f * t);

        // Interpolate position
        actor->world.pos.x = interp.prevPos.x + (interp.targetPos.x - interp.prevPos.x) * smooth;
        actor->world.pos.y = interp.prevPos.y + (interp.targetPos.y - interp.prevPos.y) * smooth;
        actor->world.pos.z = interp.prevPos.z + (interp.targetPos.z - interp.prevPos.z) * smooth;

        // Interpolate rotation (simple lerp, could use slerp for better results)
        actor->shape.rot.x = interp.prevRot.x + (s16)((interp.targetRot.x - interp.prevRot.x) * smooth);
        actor->shape.rot.y = interp.prevRot.y + (s16)((interp.targetRot.y - interp.prevRot.y) * smooth);
        actor->shape.rot.z = interp.prevRot.z + (s16)((interp.targetRot.z - interp.prevRot.z) * smooth);
    }

    // Clean up old interpolation data (actors that no longer exist)
    auto it = actorInterpData.begin();
    while (it != actorInterpData.end()) {
        if (gPlayState->state.frames - it->second.lastUpdateFrame > 60) {
            it = actorInterpData.erase(it);
        } else {
            ++it;
        }
    }
}
