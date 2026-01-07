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

// List of actor categories to sync - everything with position/rotation
static const std::set<s16> syncableCategories = {
    ACTORCAT_SWITCH,
    ACTORCAT_BG,
    // ACTORCAT_PLAYER - handled separately, don't sync here
    ACTORCAT_EXPLOSIVE,
    ACTORCAT_NPC,
    ACTORCAT_ENEMY,
    ACTORCAT_PROP,
    ACTORCAT_ITEMACTION,
    ACTORCAT_MISC,
    ACTORCAT_BOSS,
    ACTORCAT_DOOR,
    ACTORCAT_CHEST,
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

    // Include scene flags - actors read these to determine their state
    // This is the most generic way to sync actor state (chests, switches, etc.)
    s16 sceneNum = gPlayState->sceneNum;
    if (sceneNum >= 0 && sceneNum < SCENE_ID_MAX) {
        payload["sceneFlags"] = {
            {"swch", gSaveContext.sceneFlags[sceneNum].swch},
            {"chest", gSaveContext.sceneFlags[sceneNum].chest},
            {"clear", gSaveContext.sceneFlags[sceneNum].clear},
            {"collect", gSaveContext.sceneFlags[sceneNum].collect},
            {"rooms", gSaveContext.sceneFlags[sceneNum].rooms}
        };
    }

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
                actorJson["cat"] = actor->category;
                actorJson["params"] = actor->params;

                // Position and rotation
                actorJson["pos"] = actor->world.pos;
                actorJson["wRot"] = actor->world.rot;   // World rotation
                actorJson["sRot"] = actor->shape.rot;   // Shape/visual rotation
                actorJson["home"] = actor->home.pos;    // Home position for identification

                // Movement
                actorJson["vel"] = actor->velocity;
                actorJson["spd"] = actor->speedXZ;
                actorJson["grav"] = actor->gravity;

                // State
                actorJson["hp"] = actor->colChkInfo.health;
                actorJson["flags"] = actor->flags;
                actorJson["freeze"] = actor->freezeTimer;

                // Scale
                actorJson["scale"] = actor->scale;

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
 * Spawn actors that exist in remote snapshot but not locally.
 * Despawn actors that we don't own and aren't in any owner's snapshot.
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

    // Apply scene flags from the snapshot - this syncs chest/switch/collectible state
    if (payload.contains("sceneFlags")) {
        auto& flags = payload["sceneFlags"];
        if (sceneNum >= 0 && sceneNum < SCENE_ID_MAX) {
            // Merge flags (OR them together) so we don't lose local progress
            if (flags.contains("swch")) {
                gSaveContext.sceneFlags[sceneNum].swch |= flags["swch"].get<u32>();
            }
            if (flags.contains("chest")) {
                gSaveContext.sceneFlags[sceneNum].chest |= flags["chest"].get<u32>();
            }
            if (flags.contains("clear")) {
                gSaveContext.sceneFlags[sceneNum].clear |= flags["clear"].get<u32>();
            }
            if (flags.contains("collect")) {
                gSaveContext.sceneFlags[sceneNum].collect |= flags["collect"].get<u32>();
            }
            if (flags.contains("rooms")) {
                gSaveContext.sceneFlags[sceneNum].rooms |= flags["rooms"].get<u32>();
            }
        }
    }

    // Track which actors we've seen from this sender (for despawn logic)
    std::set<u32> receivedActorUids;

    for (const auto& actorJson : payload["actors"]) {
        u32 uniqueId = actorJson["uid"].get<u32>();
        s16 actorId = actorJson["id"].get<s16>();
        s16 category = actorJson.contains("cat") ? actorJson["cat"].get<s16>() : -1;
        s16 params = actorJson["params"].get<s16>();

        receivedActorUids.insert(uniqueId);

        // Find the actor locally
        Actor* actor = FindActorByUniqueId(uniqueId);

        // If actor doesn't exist locally, spawn it
        if (actor == nullptr) {
            Vec3f pos = actorJson["pos"].get<Vec3f>();
            Vec3s rot = actorJson.contains("wRot") ? actorJson["wRot"].get<Vec3s>() : actorJson["sRot"].get<Vec3s>();

            // Only spawn if we have category info and it's a syncable category
            if (category >= 0 && syncableCategories.count(category) > 0) {
                SPDLOG_INFO("[Anchor] Spawning missing actor id={} params={} at ({}, {}, {})",
                            actorId, params, pos.x, pos.y, pos.z);
                actor = Actor_Spawn(&gPlayState->actorCtx, gPlayState, actorId,
                                    pos.x, pos.y, pos.z, rot.x, rot.y, rot.z, params, false);
                if (actor == nullptr) {
                    SPDLOG_WARN("[Anchor] Failed to spawn actor id={}", actorId);
                    continue;
                }
            } else {
                continue;
            }
        }

        // Verify actor ID matches (could have collision in uniqueId hash)
        if (actor->id != actorId) continue;

        // Verify sender is actually the owner (closest player to this actor)
        // This prevents conflicts if multiple clients think they own the same actor
        if (GetClosestPlayerToActor(actor) != senderClientId) continue;

        // Get target state
        Vec3f targetPos = actorJson["pos"].get<Vec3f>();
        Vec3s targetRot = actorJson.contains("sRot") ? actorJson["sRot"].get<Vec3s>() : actorJson["wRot"].get<Vec3s>();

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

        // Apply health
        if (actorJson.contains("hp")) {
            actor->colChkInfo.health = actorJson["hp"].get<s16>();
        }

        // Apply velocity
        if (actorJson.contains("vel")) {
            actor->velocity = actorJson["vel"].get<Vec3f>();
        }

        // Apply speed
        if (actorJson.contains("spd")) {
            actor->speedXZ = actorJson["spd"].get<f32>();
        }

        // Apply gravity
        if (actorJson.contains("grav")) {
            actor->gravity = actorJson["grav"].get<f32>();
        }

        // Apply world rotation
        if (actorJson.contains("wRot")) {
            actor->world.rot = actorJson["wRot"].get<Vec3s>();
        }

        // Apply flags (but preserve some local-only flags)
        if (actorJson.contains("flags")) {
            u32 remoteFlags = actorJson["flags"].get<u32>();
            // Preserve ACTOR_FLAG_SFX_FOR_PLAYER_BODY_HIT as it's set locally
            actor->flags = (remoteFlags & ~ACTOR_FLAG_SFX_FOR_PLAYER_BODY_HIT) |
                           (actor->flags & ACTOR_FLAG_SFX_FOR_PLAYER_BODY_HIT);
        }

        // Apply freeze timer
        if (actorJson.contains("freeze")) {
            actor->freezeTimer = actorJson["freeze"].get<u16>();
        }

        // Apply scale
        if (actorJson.contains("scale")) {
            actor->scale = actorJson["scale"].get<Vec3f>();
        }

        // Store animation interpolation data if available
        if (actorJson.contains("animFrame") && actorSkelAnimeExtraOffsets.count(actor->id) > 0) {
            SkelAnime* skelAnime = GetActorSkelAnime(actor);
            if (skelAnime != nullptr && skelAnime->animation != nullptr) {
                f32 targetAnimFrame = actorJson["animFrame"].get<f32>();
                f32 animSpeed = actorJson.contains("animSpeed") ? actorJson["animSpeed"].get<f32>() : 1.0f;

                // Sanity check
                if (targetAnimFrame >= 0.0f && targetAnimFrame < 10000.0f &&
                    animSpeed >= -10.0f && animSpeed <= 10.0f) {
                    std::lock_guard<std::mutex> lock(actorInterpMutex);
                    ActorInterpData& interp = actorInterpData[uniqueId];

                    if (!interp.hasAnimData) {
                        // First update - snap to frame
                        interp.prevAnimFrame = targetAnimFrame;
                        interp.targetAnimFrame = targetAnimFrame;
                        interp.animSpeed = animSpeed;
                        interp.hasAnimData = true;
                    } else {
                        // Subsequent updates - set up interpolation
                        interp.prevAnimFrame = skelAnime->curFrame;
                        interp.targetAnimFrame = targetAnimFrame;
                        interp.animSpeed = animSpeed;
                    }
                }
            }
        }

        // Handle death (health = 0 means actor should die)
        if (actor->colChkInfo.health <= 0 && actor->update != NULL) {
            // Let the actor die naturally by setting health to 0
            // The actor's own update logic should handle death
        }
    }

    // Despawn actors that exist locally but aren't in the owner's snapshot
    // Only despawn if the sender is the owner (closest player)
    for (int cat : syncableCategories) {
        Actor* actor = gPlayState->actorCtx.actorLists[cat].head;
        while (actor != NULL) {
            Actor* nextActor = actor->next;

            // Skip DummyPlayers
            if (actor->id == ACTOR_EN_OE2 && actor->update == DummyPlayer_Update) {
                actor = nextActor;
                continue;
            }

            u32 uid = GetActorUniqueId(actor);

            // If this sender is the owner of this actor, and the actor wasn't in their snapshot, despawn it
            if (GetClosestPlayerToActor(actor) == senderClientId) {
                if (receivedActorUids.find(uid) == receivedActorUids.end()) {
                    SPDLOG_INFO("[Anchor] Despawning actor id={} uid={} (not in owner's snapshot)",
                                actor->id, uid);
                    Actor_Kill(actor);
                }
            }

            actor = nextActor;
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

        // Interpolate animation frame if we have animation data
        if (interp.hasAnimData && actorSkelAnimeExtraOffsets.count(actor->id) > 0) {
            SkelAnime* skelAnime = GetActorSkelAnime(actor);
            if (skelAnime != nullptr && skelAnime->animation != nullptr) {
                // Interpolate frame, accounting for looping animations
                f32 maxFrame = Animation_GetLastFrame(skelAnime->animation);
                f32 frameDiff = interp.targetAnimFrame - interp.prevAnimFrame;

                // Handle animation looping - if the diff is too large, it wrapped around
                if (frameDiff > maxFrame / 2.0f) {
                    frameDiff -= maxFrame;
                } else if (frameDiff < -maxFrame / 2.0f) {
                    frameDiff += maxFrame;
                }

                f32 interpFrame = interp.prevAnimFrame + frameDiff * smooth;

                // Wrap the frame within valid range
                while (interpFrame < 0.0f) interpFrame += maxFrame;
                while (interpFrame >= maxFrame) interpFrame -= maxFrame;

                skelAnime->curFrame = interpFrame;
                skelAnime->playSpeed = interp.animSpeed;
            }
        }
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
