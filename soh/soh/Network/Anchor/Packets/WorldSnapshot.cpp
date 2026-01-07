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

// Map of actor IDs to skelAnime EXTRA offsets beyond sizeof(Actor)
// Header files assume Actor is 0x14C bytes, but actual sizeof(Actor) differs!
// Real offset = sizeof(Actor) + extra_offset
//
// If header says 0x14C -> extra = 0
// If header says 0x0164 -> extra = 0x0164 - 0x014C = 0x18
// If header says 0x0170 -> extra = 0x0170 - 0x014C = 0x24
// etc.
static const std::map<s16, size_t> actorSkelAnimeExtraOffsets = {
    // Enemies - Header offset 0x014C -> extra = 0
    { ACTOR_EN_TITE, 0 },           // Tektite
    { ACTOR_EN_PEEHAT, 0 },         // Peahat
    { ACTOR_EN_DEKUNUTS, 0 },       // Deku Scrub
    { ACTOR_EN_DODONGO, 0 },        // Dodongo
    { ACTOR_EN_ST, 0 },             // Skulltula
    { ACTOR_EN_BB, 0 },             // Bubble
    { ACTOR_EN_POH, 0 },            // Poe
    { ACTOR_EN_OKUTA, 0 },          // Octorok
    { ACTOR_EN_WALLMAS, 0 },        // Wallmaster
    { ACTOR_EN_FLOORMAS, 0 },       // Floormaster
    { ACTOR_EN_ZF, 0 },             // Lizalfos
    { ACTOR_EN_VALI, 0 },           // Bari
    { ACTOR_EN_BILI, 0 },           // Biri
    { ACTOR_EN_DODOJR, 0 },         // Baby Dodongo
    { ACTOR_EN_BW, 0 },             // Torch Slug
    { ACTOR_EN_SW, 0 },             // Skullwalltula
    { ACTOR_EN_SB, 0 },             // Shell Blade
    { ACTOR_EN_WEIYER, 0 },         // Stinger
    { ACTOR_EN_IK, 0 },             // Iron Knuckle
    { ACTOR_EN_FW, 0 },             // Flare Dancer core
    { ACTOR_EN_FD, 0 },             // Flare Dancer
    { ACTOR_EN_DH, 0 },             // Dead Hand
    { ACTOR_EN_DHA, 0 },            // Dead Hand's Hand
    { ACTOR_EN_VM, 0 },             // Beamos
    { ACTOR_EN_SKB, 0 },            // Stalchild
    { ACTOR_EN_SKJ, 0 },            // Skull Kid
    { ACTOR_EN_KAREBABA, 0 },       // Withered Deku Baba
    { ACTOR_EN_ANUBICE, 0 },        // Anubis
    { ACTOR_EN_HINTNUTS, 0 },       // Hint Deku Scrub
    { ACTOR_EN_SHOPNUTS, 0 },       // Business Deku Scrub
    { ACTOR_EN_DNS, 0 },            // Deku Salesman
    { ACTOR_EN_BIGOKUTA, 0 },       // Big Octo
    { ACTOR_EN_PO_FIELD, 0 },       // Big Poe
    { ACTOR_EN_PO_DESERT, 0 },      // Desert Poe
    { ACTOR_EN_PO_SISTERS, 0 },     // Poe Sisters
    { ACTOR_EN_PO_RELAY, 0 },       // Poe Guide
    { ACTOR_EN_SSH, 0 },            // Cursed Spider
    { ACTOR_EN_GOMA, 0 },           // Gohma Larva
    { ACTOR_EN_BUBBLE, 0 },         // Shabom
    { ACTOR_EN_REEBA, 0 },          // Leever
    { ACTOR_EN_EIYER, 0 },          // Stinger water
    { ACTOR_EN_NY, 0 },             // Spike
    { ACTOR_EN_FZ, 0 },             // Freezard
    { ACTOR_EN_RR, 0 },             // Like-Like
    { ACTOR_EN_BA, 0 },             // Tentacle
    { ACTOR_EN_TORCH2, 0 },         // Dark Link

    // Header offset 0x0164 -> extra = 0x18
    { ACTOR_EN_AM, 0x18 },          // Armos
    { ACTOR_EN_BROB, 0x18 },        // Beamos different
    { ACTOR_EN_JJ, 0x18 },          // Jabu Jabu

    // Header offset 0x0170 -> extra = 0x24
    { ACTOR_EN_FIREFLY, 0x24 },     // Keese

    // Header offset 0x017C -> extra = 0x30
    { ACTOR_EN_DEKUBABA, 0x30 },    // Deku Baba
    { ACTOR_EN_CROW, 0x30 },        // Guay

    // Header offset 0x0188 -> extra = 0x3C
    { ACTOR_EN_TEST, 0x3C },        // Stalfos
    { ACTOR_EN_RD, 0x3C },          // ReDead/Gibdo
    { ACTOR_EN_WF, 0x3C },          // Wolfos
    { ACTOR_EN_GELDB, 0x3C },       // Gerudo Fighter

    // Header offset 0x018C -> extra = 0x40
    { ACTOR_EN_MB, 0x40 },          // Moblin

    // Bosses - Header offset 0x014C -> extra = 0
    { ACTOR_BOSS_GOMA, 0 },
    { ACTOR_BOSS_DODONGO, 0 },
    { ACTOR_BOSS_VA, 0 },
    { ACTOR_BOSS_GANONDROF, 0 },
    { ACTOR_BOSS_FD, 0 },
    { ACTOR_BOSS_FD2, 0 },
    { ACTOR_BOSS_SST, 0 },
    { ACTOR_BOSS_GANON2, 0 },
    { ACTOR_BOSS_GANON, 0x04 },     // Header 0x0150 -> extra = 0x04
    { ACTOR_BOSS_TW, 0x41C },       // Header 0x0568 -> extra = 0x41C

    // NPCs - Type A: skelAnime right after Actor (extra = 0)
    { ACTOR_EN_SA, 0 },             // Saria
    { ACTOR_EN_MD, 0 },             // Mido
    { ACTOR_EN_MA1, 0 },            // Malon child
    { ACTOR_EN_MA2, 0 },            // Malon adult
    { ACTOR_EN_MA3, 0 },            // Malon child castle
    { ACTOR_EN_ZL1, 0 },            // Zelda child
    { ACTOR_EN_ZL2, 0 },            // Zelda escape
    { ACTOR_EN_ZL3, 0 },            // Zelda adult
    { ACTOR_EN_ZL4, 0 },            // Impa and Zelda
    { ACTOR_EN_RU1, 0 },            // Ruto child
    { ACTOR_EN_RU2, 0 },            // Ruto adult
    { ACTOR_EN_NB, 0 },             // Nabooru
    { ACTOR_EN_TA, 0 },             // Talon
    { ACTOR_EN_IN, 0 },             // Ingo
    { ACTOR_EN_KZ, 0 },             // King Zora
    { ACTOR_EN_GO, 0 },             // Goron
    { ACTOR_EN_GO2, 0 },            // Goron rolling
    { ACTOR_EN_ZO, 0 },             // Zora
    { ACTOR_EN_KO, 0 },             // Kokiri
    { ACTOR_EN_TK, 0 },             // Dampe
    { ACTOR_EN_DAIKU, 0 },          // Carpenter
    { ACTOR_EN_DAIKU_KAKARIKO, 0 }, // Carpenter Kakariko
    { ACTOR_EN_TORYO, 0 },          // Carpenter Boss
    { ACTOR_EN_HEISHI1, 0 },        // Castle guard
    { ACTOR_EN_HEISHI2, 0 },        // Kakariko guard
    { ACTOR_EN_HEISHI3, 0 },        // Castle guard
    { ACTOR_EN_HEISHI4, 0 },        // Guard
    { ACTOR_EN_CS, 0 },             // Graveyard boy
    { ACTOR_EN_NIW, 0 },            // Cucco
    { ACTOR_EN_DOG, 0 },            // Dog
    { ACTOR_EN_FR, 0 },             // Frog

    // NPCs - Type B: skelAnime has 0x4C extra bytes (header 0x198 -> extra = 0x4C)
    { ACTOR_EN_OWL, 0x4C },         // Kaepora Gaebora
    { ACTOR_EN_GE1, 0x4C },         // Gerudo white
    { ACTOR_EN_GE2, 0x4C },         // Gerudo guard
    { ACTOR_EN_GE3, 0x4C },         // Gerudo purple
    { ACTOR_EN_HS, 0x4C },          // Grog
    { ACTOR_EN_HS2, 0x4C },         // Carpenter's son
};

// List of actor categories to sync
static const std::set<s16> syncableCategories = {
    ACTORCAT_ENEMY,
    ACTORCAT_BOSS,
    ACTORCAT_NPC,
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
 * Uses sizeof(Actor) + extra_offset because header offsets assume wrong Actor size.
 */
SkelAnime* Anchor::GetActorSkelAnime(Actor* actor) {
    if (actor == nullptr) return nullptr;

    auto it = actorSkelAnimeExtraOffsets.find(actor->id);
    if (it == actorSkelAnimeExtraOffsets.end()) return nullptr;

    // Real offset = sizeof(Actor) + extra offset for this actor type
    size_t realOffset = sizeof(Actor) + it->second;
    return reinterpret_cast<SkelAnime*>(reinterpret_cast<uintptr_t>(actor) + realOffset);
}

/**
 * WORLD_SNAPSHOT
 *
 * Each client broadcasts the ENTIRE world state (all actors).
 * Receivers decide which state to apply based on proximity - the state
 * from the player closest to each actor wins.
 *
 * This is like parallel worlds merging at a frontier:
 * - Each player lives in their own world
 * - At the boundary where worlds collide, proximity determines truth
 * - The closest player's reality becomes the shared reality
 *
 * Scene entry delay:
 * - When entering a scene where others are already present, wait before broadcasting
 * - This allows receiving existing state before potentially overwriting it
 */
void Anchor::SendPacket_WorldSnapshot() {
    if (!IsSaveLoaded() || gPlayState == nullptr) return;

    // Detect scene change - reset sync state
    if (gPlayState->sceneNum != lastSnapshotSceneNum) {
        SPDLOG_INFO("[Anchor] Scene changed from {} to {}, resetting snapshot state",
                    lastSnapshotSceneNum, gPlayState->sceneNum);
        lastSnapshotSceneNum = gPlayState->sceneNum;
        hasReceivedSnapshotThisScene = false;
    }

    // Check if there are other players in the same scene
    bool hasOthersInScene = false;
    static int clientsLogCounter = 0;
    bool shouldLogClients = (++clientsLogCounter >= 180);  // Every 3 seconds
    if (shouldLogClients) {
        clientsLogCounter = 0;
        SPDLOG_INFO("[Anchor] My sceneNum={}, checking {} clients:", gPlayState->sceneNum, clients.size());
    }
    for (auto& [clientId, client] : clients) {
        if (shouldLogClients) {
            SPDLOG_INFO("[Anchor]   Client {}: name={}, sceneNum={}, online={}, isSaveLoaded={}, self={}",
                        clientId, client.name, client.sceneNum, client.online, client.isSaveLoaded, client.self);
        }
        if (client.sceneNum == gPlayState->sceneNum && client.online &&
            client.isSaveLoaded && !client.self) {
            hasOthersInScene = true;
        }
    }
    if (!hasOthersInScene) {
        return;
    }

    // Wait until we've received at least one snapshot from others before broadcasting
    // This ensures we don't overwrite existing state when entering a scene
    // EXCEPTION: If we have the lowest sessionId among players in this scene, we go first
    // This prevents deadlock when multiple players enter simultaneously
    if (!hasReceivedSnapshotThisScene) {
        bool hasLowerPriority = false;
        for (auto& [clientId, client] : clients) {
            if (client.sceneNum == gPlayState->sceneNum && client.online &&
                client.isSaveLoaded && !client.self) {
                // Use sessionId for priority - lower sessionId goes first
                if (client.sessionId != 0 && client.sessionId < sessionId) {
                    hasLowerPriority = true;
                    break;
                }
            }
        }
        // If someone with a lower sessionId is in the scene, wait for them to send first
        if (hasLowerPriority) {
            return;
        }
        // Otherwise, we have priority - mark as ready and start broadcasting
        SPDLOG_INFO("[Anchor] I have priority (sessionId={}), starting to broadcast", sessionId);
        hasReceivedSnapshotThisScene = true;  // Prevent re-checking priority every frame
    }

    // Rate limit: send at ~15Hz (every 4 frames at 60fps)
    snapshotSendCounter++;
    if (snapshotSendCounter < 4) return;
    snapshotSendCounter = 0;

    nlohmann::json payload;
    payload["type"] = WORLD_SNAPSHOT;
    payload["sceneNum"] = gPlayState->sceneNum;
    payload["actors"] = nlohmann::json::array();
    payload["quiet"] = true;  // Don't log these frequent packets

    // Broadcast ALL actors - receivers will decide which state to apply
    int enemyCount = 0, bossCount = 0, npcCount = 0;
    for (int cat : syncableCategories) {
        Actor* actor = gPlayState->actorCtx.actorLists[cat].head;
        while (actor != NULL) {
            if (cat == ACTORCAT_ENEMY) enemyCount++;
            if (cat == ACTORCAT_BOSS) bossCount++;
            if (cat == ACTORCAT_NPC) npcCount++;

            // Skip DummyPlayers (other network players)
            if (actor->id == ACTOR_EN_OE2 && actor->update == DummyPlayer_Update) {
                actor = actor->next;
                continue;
            }

            if (actor->update != NULL) {
                nlohmann::json actorJson;

                u32 uniqueId = GetActorUniqueId(actor);
                actorJson["uid"] = uniqueId;
                actorJson["id"] = actor->id;
                actorJson["params"] = actor->params;
                actorJson["pos"] = actor->world.pos;
                actorJson["rot"] = actor->shape.rot;
                actorJson["hp"] = actor->colChkInfo.health;
                actorJson["dead"] = 0;

                // Animation state if available and we have a known offset
                if (actorSkelAnimeExtraOffsets.count(actor->id) > 0) {
                    SkelAnime* skelAnime = GetActorSkelAnime(actor);
                    if (skelAnime != nullptr && skelAnime->animation != nullptr) {
                        actorJson["animFrame"] = skelAnime->curFrame;
                        actorJson["animSpeed"] = skelAnime->playSpeed;
                    }
                }

                payload["actors"].push_back(actorJson);
            }
            actor = actor->next;
        }
    }

    // Always send snapshot (even if empty) to unblock other players waiting for us
    SPDLOG_INFO("[Anchor] Sending WORLD_SNAPSHOT with {} actors (enemies={}, bosses={}, npcs={})",
                payload["actors"].size(), enemyCount, bossCount, npcCount);
    SendJsonToRemote(payload);
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

    // Mark that we've received a snapshot - we can now start broadcasting
    hasReceivedSnapshotThisScene = true;

    uint32_t senderClientId = payload["clientId"].get<uint32_t>();
    SPDLOG_INFO("[Anchor] Received WORLD_SNAPSHOT from client {} with {} actors",
                senderClientId, payload["actors"].size());

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

        // Apply animation if available and we have a known offset for this actor type
        if (actorJson.contains("animFrame") && actorSkelAnimeExtraOffsets.count(actor->id) > 0) {
            SkelAnime* skelAnime = GetActorSkelAnime(actor);
            if (skelAnime != nullptr && skelAnime->animation != nullptr) {
                f32 animFrame = actorJson["animFrame"].get<f32>();
                // Sanity check - animFrame should be reasonable (0 to 10000)
                if (animFrame >= 0.0f && animFrame < 10000.0f) {
                    skelAnime->curFrame = animFrame;
                    if (actorJson.contains("animSpeed")) {
                        f32 animSpeed = actorJson["animSpeed"].get<f32>();
                        if (animSpeed >= -10.0f && animSpeed <= 10.0f) {
                            skelAnime->playSpeed = animSpeed;
                        }
                    }
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
