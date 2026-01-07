#include "soh/Network/Anchor/Anchor.h"
#include "soh/Network/Anchor/AnchorHelpers.h"
#include "soh/Network/Anchor/JsonConversions.hpp"
#include "soh/Network/Anchor/ActorSerializer.h"
#include "soh/ObjectExtension/ActorListIndex.h"
#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h>
#include <set>

extern "C" {
#include "macros.h"
#include "variables.h"
#include "functions.h"
#include "z64animation.h"
extern PlayState* gPlayState;
}

// Header files assume Actor is 0x14C bytes, but actual sizeof(Actor) differs at runtime!
// To convert header offset to real offset: sizeof(Actor) + (headerOffset - 0x14C)
static const size_t HEADER_ACTOR_SIZE = 0x14C;

// Actors that should NOT be synced (per-player actors)
static const std::set<s16> perPlayerActors = {
    ACTOR_EN_ELF,       // Navi - each player has their own fairy
};

/**
 * Check if a room is currently loaded.
 * A room is considered loaded if:
 * - It's the current room and has a valid segment
 * - It's the previous room and has a valid segment (during transitions)
 * - It's room -1 (global/all rooms)
 *
 * This is used to prevent spawning actors in unloaded rooms which would crash.
 */
static bool IsRoomLoaded(s8 roomNum) {
    if (gPlayState == nullptr) return false;

    // Room -1 means "all rooms" / global - always valid
    if (roomNum < 0) return true;

    RoomContext* roomCtx = &gPlayState->roomCtx;

    // Check if it's the current room
    if (roomCtx->curRoom.num == roomNum && roomCtx->curRoom.segment != nullptr) {
        return true;
    }

    // Check if it's the previous room (during room transitions both are loaded)
    if (roomCtx->prevRoom.num == roomNum && roomCtx->prevRoom.segment != nullptr) {
        return true;
    }

    return false;
}

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
 * Generate a DETERMINISTIC spawn ID for an actor.
 *
 * For scene actors (actorListIndex >= 0):
 *   ID = 0x40000000 | (sceneNum << 23) | (roomNum << 15) | actorListIndex
 *   Bit 30 is always set to ensure ID is never 0 (scene 0, room 0, spawnIdx 0 would be 0 otherwise)
 *   This is 100% unique and deterministic - same for all players!
 *   Each room has its own spawnIdx counter starting at 0, so we MUST include room.
 *
 * For dynamic actors (actorListIndex == -1):
 *   Fallback to hash of (scene, room, actorId, homePos) with bit 31 set.
 */
static u32 GetDeterministicSpawnId(s16 sceneNum, s16 actorListIndex, s8 roomNum, s16 actorId, Vec3f homePos) {
    // Scene actors have a perfect unique ID: scene + room + spawn index
    // IMPORTANT: Each room's spawnIdx starts at 0, so we must include room to avoid collisions!
    if (actorListIndex >= 0) {
        // Pack: bit 30 (scene actor marker), scene (7 bits), room (8 bits), spawnIdx (15 bits)
        // Bit 30 is ALWAYS set for scene actors to ensure ID is never 0
        // Bit 31 is reserved for dynamic actors (hash-based IDs)
        u8 room = (roomNum < 0) ? 0xFF : (u8)roomNum;  // -1 (global) maps to 255
        return 0x40000000u | ((u32)(sceneNum & 0x7F) << 23) | ((u32)room << 15) | (u32)(actorListIndex & 0x7FFF);
    }

    // Dynamic actors (projectiles, dropped items, etc.) - fallback to hash
    // Use FNV-1a hash for better distribution
    u32 hash = 2166136261u; // FNV offset basis

    // Mix in scene and room
    hash ^= (u32)sceneNum;
    hash *= 16777619u; // FNV prime
    hash ^= (u32)(roomNum & 0xFF);
    hash *= 16777619u;

    // Mix in actor type
    hash ^= (u32)actorId;
    hash *= 16777619u;

    // Mix in home position
    hash ^= (u32)(s32)homePos.x;
    hash *= 16777619u;
    hash ^= (u32)(s32)homePos.y;
    hash *= 16777619u;
    hash ^= (u32)(s32)homePos.z;
    hash *= 16777619u;

    // Set high bit to distinguish from scene actors (scene actors have index in low 16 bits)
    return hash | 0x80000000u;
}

/**
 * Generate a deterministic spawn ID for an actor.
 */
u32 Anchor::GetActorUniqueId(Actor* actor) {
    if (actor == nullptr || gPlayState == nullptr) return 0;

    s16 actorListIndex = GetActorListIndex(actor);

    return GetDeterministicSpawnId(
        gPlayState->sceneNum,
        actorListIndex,
        actor->room,
        actor->id,
        actor->home.pos
    );
}

/**
 * Get the networkActorId for an actor.
 * Now uses DETERMINISTIC spawn IDs based on scene data, so all players
 * compute the same ID for the same actor without needing to sync.
 *
 * The ID is based on (scene, room, actorId, homePos) - identical for all players.
 */
u32 Anchor::GetOrAssignNetworkId(Actor* actor) {
    if (actor == nullptr || gPlayState == nullptr) return 0;

    // Use deterministic spawn ID - same for all players!
    u32 spawnId = GetActorUniqueId(actor);

    // Cache the mapping for quick lookup
    if (actorToNetworkId.find(actor) == actorToNetworkId.end()) {
        actorToNetworkId[actor] = spawnId;
        networkIdToActor[spawnId] = actor;
    }

    return spawnId;
}

/**
 * Set the networkActorId for an actor (received from another player's snapshot).
 */
void Anchor::SetActorNetworkId(Actor* actor, u32 networkActorId) {
    if (actor == nullptr || networkActorId == 0) return;

    // Remove old mapping if exists
    auto it = actorToNetworkId.find(actor);
    if (it != actorToNetworkId.end()) {
        networkIdToActor.erase(it->second);
    }

    actorToNetworkId[actor] = networkActorId;
    networkIdToActor[networkActorId] = actor;

    // Keep nextNetworkId ahead of any received ID
    if (networkActorId >= nextNetworkId) {
        nextNetworkId = networkActorId + 1;
    }
}

/**
 * Find an actor by its networkActorId.
 * For scene actors (bit 30 set, bit 31 clear), extracts scene/room/spawnIdx and searches directly.
 * For dynamic actors (bit 31 set), uses map lookup.
 */
Actor* Anchor::FindActorByNetworkId(u32 networkActorId) {
    if (gPlayState == nullptr || networkActorId == 0) return nullptr;

    // Scene actors have bit 30 set, bit 31 clear
    // Format: 0x40000000 | (scene << 23) | (room << 15) | spawnIdx
    if ((networkActorId & 0xC0000000u) == 0x40000000u) {
        // Extract scene, room, and spawnIdx
        s16 expectedScene = (networkActorId >> 23) & 0x7F;
        u8 expectedRoom = (networkActorId >> 15) & 0xFF;
        s16 expectedSpawnIdx = networkActorId & 0x7FFF;

        // Only search in current scene
        if (expectedScene != gPlayState->sceneNum) return nullptr;

        // Search all syncable categories for actor with this room + spawnIdx
        for (int cat : syncableCategories) {
            Actor* actor = gPlayState->actorCtx.actorLists[cat].head;
            while (actor != NULL) {
                // Match by spawnIdx AND room
                u8 actorRoom = (actor->room < 0) ? 0xFF : (u8)actor->room;
                if (GetActorListIndex(actor) == expectedSpawnIdx && actorRoom == expectedRoom) {
                    // Found it! Cache the mapping for future lookups
                    actorToNetworkId[actor] = networkActorId;
                    networkIdToActor[networkActorId] = actor;
                    return actor;
                }
                actor = actor->next;
            }
        }
        return nullptr;
    }

    // Dynamic actor - use map lookup (fallback)
    auto it = networkIdToActor.find(networkActorId);
    if (it != networkIdToActor.end()) {
        Actor* actor = it->second;
        if (actor != nullptr && actor->update != nullptr) {
            return actor;
        }
        actorToNetworkId.erase(actor);
        networkIdToActor.erase(networkActorId);
    }
    return nullptr;
}

/**
 * Try to match a received actor to a local actor by type and position.
 * Used when we don't have a networkActorId mapping yet.
 */
Actor* Anchor::FindActorByTypeAndPosition(s16 actorId, s16 params, Vec3f homePos) {
    if (gPlayState == nullptr) return nullptr;

    f32 bestDist = 100.0f;  // Max distance to consider a match
    Actor* bestMatch = nullptr;

    for (int cat : syncableCategories) {
        Actor* actor = gPlayState->actorCtx.actorLists[cat].head;
        while (actor != NULL) {
            // Check if type matches (ignore params - they may differ due to randomizer)
            if (actor->id == actorId) {
                // Check home position distance
                f32 dx = actor->home.pos.x - homePos.x;
                f32 dy = actor->home.pos.y - homePos.y;
                f32 dz = actor->home.pos.z - homePos.z;
                f32 dist = sqrtf(dx*dx + dy*dy + dz*dz);

                if (dist < bestDist) {
                    bestDist = dist;
                    bestMatch = actor;
                }
            }
            actor = actor->next;
        }
    }

    return bestMatch;
}

/**
 * Clear all networkActorId mappings (called on scene change).
 */
void Anchor::ClearNetworkIds() {
    actorToNetworkId.clear();
    networkIdToActor.clear();
    networkIdToOwner.clear();
    ownerToNetworkIds.clear();
    actorsPendingKill.clear();
    destroyedActors.clear();
    lastSentActorInfo.clear();
    nextNetworkId = 1;
    hasAssignedNetworkIds = false;
}

/**
 * Process deferred actor kills.
 * Called from game thread to safely kill actors that were marked for deletion.
 */
void Anchor::ProcessPendingActorKills() {
    if (actorsPendingKill.empty()) return;

    for (Actor* actor : actorsPendingKill) {
        if (actor != nullptr && actor->update != nullptr) {
            SPDLOG_INFO("[Anchor] Killing actor id={} (owner stopped sending)", actor->id);
            Actor_Kill(actor);
        }
    }
    actorsPendingKill.clear();
}

/**
 * Check if the owner of an actor is still online.
 * Returns false if: no owner, owner not in clients list, or owner is offline.
 * Uses sessionId for identification to avoid clientId duplicates.
 */
bool Anchor::IsOwnerOnline(u32 networkActorId) {
    auto it = networkIdToOwner.find(networkActorId);
    if (it == networkIdToOwner.end()) {
        return false;  // No owner recorded
    }

    uint64_t ownerSessionId = it->second;

    // Find client by sessionId
    for (auto& [clientId, client] : clients) {
        if (client.sessionId == ownerSessionId) {
            return client.online;
        }
    }

    return false;  // Owner not found
}

/**
 * Get the sessionId of the player closest to an actor.
 * Uses sessionId instead of clientId to avoid duplicates.
 * Used to determine ownership for state broadcasting.
 */
uint64_t Anchor::GetClosestPlayerToActor(Actor* actor) {
    if (actor == nullptr || gPlayState == nullptr) return sessionId;

    Player* localPlayer = GET_PLAYER(gPlayState);
    if (localPlayer == nullptr) return sessionId;

    // If actor is held by the local player, local player owns it
    if (localPlayer->heldActor == actor) {
        return sessionId;
    }

    // If actor is held by a DummyPlayer, that player owns it
    for (auto& [clientId, client] : clients) {
        if (client.self || !client.online) continue;
        if (client.heldActorNetworkId != 0) {
            Actor* heldActor = FindActorByNetworkId(client.heldActorNetworkId);
            if (heldActor == actor) {
                return client.sessionId;
            }
        }
    }

    // Start with local player distance
    f32 minDist = Actor_WorldDistXZToActor(actor, &localPlayer->actor);
    uint64_t closestSessionId = sessionId;

    // Check all DummyPlayers (other connected players)
    Actor* npcActor = gPlayState->actorCtx.actorLists[ACTORCAT_NPC].head;
    while (npcActor != NULL) {
        if (npcActor->id == ACTOR_EN_OE2 && npcActor->update == DummyPlayer_Update) {
            uint32_t dummyClientId = GetDummyPlayerClientId(npcActor);
            if (clients.contains(dummyClientId) && clients[dummyClientId].online) {
                f32 dist = Actor_WorldDistXZToActor(actor, npcActor);
                if (dist < minDist) {
                    minDist = dist;
                    closestSessionId = clients[dummyClientId].sessionId;
                }
            }
        }
        npcActor = npcActor->next;
    }

    return closestSessionId;
}

/**
 * Check if the local player is the owner (closest player) of an actor.
 */
bool Anchor::IsActorOwner(Actor* actor) {
    return GetClosestPlayerToActor(actor) == sessionId;
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
 * Get the skelAnime pointer for an actor using ActorSyncGenerated.inc data.
 * Converts header offset to real offset: sizeof(Actor) + (headerOffset - 0x14C)
 */
SkelAnime* Anchor::GetActorSkelAnime(Actor* actor) {
    if (actor == nullptr) return nullptr;

    ActorSyncInfo* info = GetActorSyncInfo(actor->id);
    if (info == nullptr || info->skelAnimeOffset == 0) return nullptr;

    // Convert header offset to real runtime offset
    size_t realOffset = sizeof(Actor) + (info->skelAnimeOffset - HEADER_ACTOR_SIZE);
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

    // Detect scene or room change - reset sync state
    s8 curRoom = gPlayState->roomCtx.curRoom.num;
    if (gPlayState->sceneNum != lastSnapshotSceneNum || curRoom != lastSnapshotRoomNum) {
        SPDLOG_INFO("[Anchor] Scene/room changed from {}/{} to {}/{}, resetting snapshot state",
                    lastSnapshotSceneNum, lastSnapshotRoomNum, gPlayState->sceneNum, curRoom);
        lastSnapshotSceneNum = gPlayState->sceneNum;
        lastSnapshotRoomNum = curRoom;
        receivedSnapshotCount = 0;
        snapshotSeqCounter = 0;  // Reset our sequence counter
        // Reset all clients' lastSnapshotSeq so we accept their first packet in new room
        for (auto& [clientId, client] : clients) {
            client.lastSnapshotSeq = 0;
        }
        ClearNetworkIds();  // Clear actor network IDs on scene/room change
    }

    // Check if there are other players in the same scene AND room
    // We only sync with players in the exact same room - ignore players in other rooms
    bool hasOthersInRoom = false;
    static int clientsLogCounter = 0;
    bool shouldLogClients = (++clientsLogCounter >= 180);  // Every 3 seconds
    if (shouldLogClients) {
        clientsLogCounter = 0;
        SPDLOG_INFO("[Anchor] My scene={} room={}, checking {} clients:", gPlayState->sceneNum, curRoom, clients.size());
    }
    for (auto& [clientId, client] : clients) {
        if (shouldLogClients) {
            SPDLOG_INFO("[Anchor]   Client {}: name={}, sceneNum={}, online={}, isSaveLoaded={}, self={}",
                        clientId, client.name, client.sceneNum, client.online, client.isSaveLoaded, client.self);
        }
        // Note: We can't check client's room here because we don't have it in the client struct
        // The room check happens when we receive their snapshot
        if (client.sceneNum == gPlayState->sceneNum && client.online &&
            client.isSaveLoaded && !client.self) {
            hasOthersInRoom = true;
        }
    }
    if (!hasOthersInRoom) {
        return;
    }

    // Wait until we've received at least 4 snapshots from others before broadcasting
    // This ensures we properly receive the world state before sending our own
    // EXCEPTION: If we have the lowest sessionId among players in this scene, we go first
    // This prevents deadlock when multiple players enter simultaneously
    const int REQUIRED_SNAPSHOTS = 4;
    if (receivedSnapshotCount < REQUIRED_SNAPSHOTS) {
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
        receivedSnapshotCount = REQUIRED_SNAPSHOTS;  // Skip waiting
    }

    nlohmann::json payload;
    payload["type"] = WORLD_SNAPSHOT;
    payload["seq"] = ++snapshotSeqCounter;  // Incrementing sequence for stale packet detection
    payload["sceneNum"] = gPlayState->sceneNum;
    payload["roomNum"] = gPlayState->roomCtx.curRoom.num;  // Current room for room-based filtering
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

    // Include global event flags - NPCs like Mido check these to determine their behavior
    // Convert arrays to JSON arrays for transmission
    nlohmann::json eventChkInf = nlohmann::json::array();
    for (int i = 0; i < ARRAY_COUNT(gSaveContext.eventChkInf); i++) {
        eventChkInf.push_back(gSaveContext.eventChkInf[i]);
    }
    payload["eventChkInf"] = eventChkInf;

    nlohmann::json infTable = nlohmann::json::array();
    for (int i = 0; i < ARRAY_COUNT(gSaveContext.infTable); i++) {
        infTable.push_back(gSaveContext.infTable[i]);
    }
    payload["infTable"] = infTable;

    // Include time of day and weather - synced from host (lowest sessionId)
    // This ensures all players see the same day/night cycle and weather
    payload["dayTime"] = gSaveContext.dayTime;
    payload["nightFlag"] = gSaveContext.nightFlag;
    payload["totalDays"] = gSaveContext.totalDays;
    payload["weatherMode"] = gWeatherMode;

    // Kill any actors that respawned but are in our destroyed list
    // This handles the case where you leave a room and come back - game spawns actors fresh
    // but we know they should be dead from our destroyedActors tracking
    if (!destroyedActors.empty()) {
        for (auto& [networkId, info] : destroyedActors) {
            if (!IsRoomLoaded(info.room)) continue;

            Actor* actor = FindActorByNetworkId(networkId);
            if (actor == nullptr) {
                actor = FindActorByTypeAndPosition(info.actorId, info.params, info.homePos);
            }
            if (actor != nullptr && actor->update != nullptr) {
                SPDLOG_INFO("[Anchor] Killing respawned destroyed actor id={} networkId={}",
                            actor->id, networkId);
                Actor_Kill(actor);
            }
        }
    }

    // Track what we're sending this frame to detect deaths
    std::map<u32, DestroyedActorInfo> currentSentActorInfo;

    // Send only actors we OWN (we're the closest player to them)
    // Receivers will apply this state because we're the authority
    int totalSyncable = 0, ownedCount = 0, skippedNotOwned = 0;
    for (int cat : syncableCategories) {
        Actor* actor = gPlayState->actorCtx.actorLists[cat].head;
        while (actor != NULL) {
            // Skip DummyPlayers (other network players)
            if (actor->id == ACTOR_EN_OE2 && actor->update == DummyPlayer_Update) {
                actor = actor->next;
                continue;
            }

            // Skip per-player actors (each player has their own instance)
            if (perPlayerActors.count(actor->id) > 0) {
                actor = actor->next;
                continue;
            }

            // Get spawn index for networkId generation (scene actors vs dynamic actors)
            s16 spawnIdx = GetActorListIndex(actor);

            // Only send actors in LOADED rooms (curRoom + prevRoom during transitions)
            // This allows syncing actors from both rooms during room transitions!
            if (!IsRoomLoaded(actor->room)) {
                actor = actor->next;
                continue;
            }

            // Count AFTER filtering out non-syncable actors
            totalSyncable++;

            // SIMPLE OWNERSHIP: only send actors we're closest to
            // With deterministic IDs, we don't need complex tracking - just proximity
            if (!IsActorOwner(actor)) {
                skippedNotOwned++;
                actor = actor->next;
                continue;
            }

            ownedCount++;
            if (actor->update != NULL) {
                nlohmann::json actorJson;

                // Get or assign a networkActorId for this actor
                // If we get here without an ID, we're claiming this actor
                u32 networkActorId = GetOrAssignNetworkId(actor);

                // Mark ourselves as the owner if we're claiming/sending this actor
                // This prevents the other client from thinking we "stopped sending" when we're the authority
                // Uses sessionId to avoid clientId duplicates
                networkIdToOwner[networkActorId] = sessionId;
                ownerToNetworkIds[sessionId].insert(networkActorId);
                actorJson["networkActorId"] = networkActorId;
                actorJson["id"] = actor->id;
                actorJson["cat"] = actor->category;
                actorJson["params"] = actor->params;
                actorJson["room"] = actor->room;
                actorJson["spawnIdx"] = GetActorListIndex(actor);  // Scene spawn index (-1 for dynamic)

                // Position and rotation (always sync these)
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

                // Mark if this actor is held by a player (let Player_Draw handle position)
                // Uses sessionId to avoid clientId duplicates
                Player* localPlayer = GET_PLAYER(gPlayState);
                if (localPlayer != nullptr && localPlayer->heldActor == actor) {
                    actorJson["heldBy"] = sessionId;
                }

                // Animation state if available (GetActorSkelAnime uses ActorSyncGenerated.inc)
                SkelAnime* skelAnime = GetActorSkelAnime(actor);
                if (skelAnime != nullptr && skelAnime->animation != nullptr) {
                    actorJson["animFrame"] = skelAnime->curFrame;
                    actorJson["animSpeed"] = skelAnime->playSpeed;
                }

                // Full AI state serialization (actionFunc, state variables, jointTable, morphTable)
                // This gives us 100% sync for actors with serialization info
                if (IsActorSyncable(actor->id)) {
                    nlohmann::json aiState = SerializeActorState(actor);
                    if (!aiState.empty()) {
                        actorJson["aiState"] = aiState;
                    }
                }

                payload["actors"].push_back(actorJson);

                // Track this actor for death detection next frame
                currentSentActorInfo[networkActorId] = {
                    networkActorId,
                    actor->id,
                    actor->params,
                    (s8)actor->room,
                    actor->home.pos
                };
            }
            actor = actor->next;
        }
    }

    // Detect destroyed actors: were in lastSentActorInfo but not in currentSentActorInfo
    // IMPORTANT: Only mark as destroyed if the actor is ACTUALLY dead (doesn't exist anymore)
    // Not just if we stopped owning it (ownership can transfer to another player)
    for (auto& [networkId, info] : lastSentActorInfo) {
        if (currentSentActorInfo.find(networkId) == currentSentActorInfo.end()) {
            // We stopped sending this actor - check if it's ACTUALLY dead or just ownership changed
            Actor* actor = FindActorByNetworkId(networkId);
            if (actor == nullptr) {
                // Try to find by type + position
                actor = FindActorByTypeAndPosition(info.actorId, info.params, info.homePos);
            }

            // Only mark as destroyed if actor doesn't exist OR is dead (update == NULL)
            bool isActuallyDead = (actor == nullptr || actor->update == nullptr);

            if (isActuallyDead && destroyedActors.find(networkId) == destroyedActors.end()) {
                destroyedActors[networkId] = info;
                SPDLOG_INFO("[Anchor] Detected destroyed actor: id={} networkId={} room={}",
                            info.actorId, networkId, info.room);
            }
        }
    }

    // Update lastSentActorInfo for next frame
    lastSentActorInfo = currentSentActorInfo;

    // Add destroyed actors to payload so other players don't respawn them
    if (!destroyedActors.empty()) {
        payload["destroyed"] = nlohmann::json::array();
        for (auto& [networkId, info] : destroyedActors) {
            // Only send destroyed actors in loaded rooms
            if (IsRoomLoaded(info.room)) {
                nlohmann::json destroyedJson;
                destroyedJson["networkActorId"] = info.networkActorId;
                destroyedJson["id"] = info.actorId;
                destroyedJson["params"] = info.params;
                destroyedJson["room"] = info.room;
                destroyedJson["home"] = info.homePos;
                payload["destroyed"].push_back(destroyedJson);
            }
        }
    }

    // Always send snapshot (even if empty) to unblock other players waiting for us
    SPDLOG_INFO("[Anchor] Sending WORLD_SNAPSHOT: owned={}/{} syncable (other player owns {})",
                ownedCount, totalSyncable, skippedNotOwned);
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

    try {
        // Check required fields exist
        if (!payload.contains("sceneNum") || !payload.contains("clientId") || !payload.contains("actors")) {
            return;
        }

        // Ignore if not in the same scene
        s16 sceneNum = payload["sceneNum"].get<s16>();
        if (sceneNum != gPlayState->sceneNum) return;

        // Get sender's room (if provided)
        s8 senderRoom = payload.contains("roomNum") ? payload["roomNum"].get<s8>() : -1;
        s8 myRoom = gPlayState->roomCtx.curRoom.num;

        // Count received snapshots - we wait for 4 before broadcasting our own
        // This ensures we properly receive the world state before sending our own
        const int REQUIRED_SNAPSHOTS = 4;
        receivedSnapshotCount++;
        if (receivedSnapshotCount == REQUIRED_SNAPSHOTS) {
            SPDLOG_INFO("[Anchor] Received {} snapshots, now ready to broadcast", REQUIRED_SNAPSHOTS);
        }

        uint32_t senderClientId = payload["clientId"].get<uint32_t>();
        // Get sender's sessionId for ownership tracking (avoids clientId duplicates)
        uint64_t senderSessionId = 0;
        if (clients.contains(senderClientId)) {
            senderSessionId = clients[senderClientId].sessionId;
        }
        if (senderSessionId == 0) {
            SPDLOG_WARN("[Anchor] Received WORLD_SNAPSHOT from client {} but couldn't find sessionId", senderClientId);
            return;
        }

        // Check sequence number to drop stale packets
        // This prevents lag accumulation - if we're behind, drop old packets to catch up
        u32 seq = payload.contains("seq") ? payload["seq"].get<u32>() : 0;
        if (seq > 0 && clients.contains(senderClientId)) {
            u32 lastSeq = clients[senderClientId].lastSnapshotSeq;
            if (seq <= lastSeq) {
                // Old or duplicate packet - skip it to catch up
                SPDLOG_DEBUG("[Anchor] Dropping stale WORLD_SNAPSHOT seq={} (last={})", seq, lastSeq);
                return;
            }
            clients[senderClientId].lastSnapshotSeq = seq;
        }

        SPDLOG_INFO("[Anchor] Received WORLD_SNAPSHOT from client {} (session {}) with {} actors (seq={}, snapshot {}/{})",
                    senderClientId, senderSessionId, payload["actors"].size(), seq, receivedSnapshotCount, REQUIRED_SNAPSHOTS);

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

    // Apply global event flags - these control NPC behavior like Mido
    // Merge flags (OR them together) so we don't lose local progress
    if (payload.contains("eventChkInf")) {
        auto& eventFlags = payload["eventChkInf"];
        for (size_t i = 0; i < eventFlags.size() && i < ARRAY_COUNT(gSaveContext.eventChkInf); i++) {
            gSaveContext.eventChkInf[i] |= eventFlags[i].get<u16>();
        }
    }

    if (payload.contains("infTable")) {
        auto& infFlags = payload["infTable"];
        for (size_t i = 0; i < infFlags.size() && i < ARRAY_COUNT(gSaveContext.infTable); i++) {
            gSaveContext.infTable[i] |= infFlags[i].get<u16>();
        }
    }

    // Apply time of day and weather ONLY if sender is the host (lowest sessionId)
    // This ensures everyone sees the same day/night cycle and weather
    if (payload.contains("dayTime")) {
        // Check if sender has the lowest sessionId among all online clients in this scene
        bool senderIsHost = true;
        uint64_t senderSessionId = 0;

        // Get sender's sessionId
        if (clients.contains(senderClientId)) {
            senderSessionId = clients[senderClientId].sessionId;
        }

        // Check if any other client (including us) has a lower sessionId
        for (auto& [clientId, client] : clients) {
            if (client.sceneNum == gPlayState->sceneNum && client.online && client.isSaveLoaded) {
                if (client.sessionId != 0 && client.sessionId < senderSessionId) {
                    senderIsHost = false;
                    break;
                }
            }
        }

        // Also check our own sessionId
        if (sessionId != 0 && sessionId < senderSessionId) {
            senderIsHost = false;
        }

        if (senderIsHost && senderSessionId != 0) {
            gSaveContext.dayTime = payload["dayTime"].get<u16>();
            if (payload.contains("nightFlag")) {
                gSaveContext.nightFlag = payload["nightFlag"].get<s32>();
            }
            if (payload.contains("totalDays")) {
                gSaveContext.totalDays = payload["totalDays"].get<s32>();
            }
            if (payload.contains("weatherMode")) {
                gWeatherMode = payload["weatherMode"].get<u8>();
            }
        }
    }

    // Process destroyed actors - kill them locally if they exist
    if (payload.contains("destroyed")) {
        for (const auto& destroyedJson : payload["destroyed"]) {
            if (!destroyedJson.contains("networkActorId")) continue;

            u32 networkActorId = destroyedJson["networkActorId"].get<u32>();
            s16 actorId = destroyedJson.contains("id") ? destroyedJson["id"].get<s16>() : 0;
            s16 params = destroyedJson.contains("params") ? destroyedJson["params"].get<s16>() : 0;
            s8 room = destroyedJson.contains("room") ? destroyedJson["room"].get<s8>() : -1;
            Vec3f homePos = destroyedJson.contains("home") ? destroyedJson["home"].get<Vec3f>() : Vec3f{0,0,0};

            // Add to our local destroyed list
            if (destroyedActors.find(networkActorId) == destroyedActors.end()) {
                destroyedActors[networkActorId] = {networkActorId, actorId, params, room, homePos};
                SPDLOG_INFO("[Anchor] Received destroyed actor: id={} networkId={} room={}",
                            actorId, networkActorId, room);
            }

            // Kill the local actor if it exists
            Actor* actor = FindActorByNetworkId(networkActorId);
            if (actor == nullptr) {
                // Try to find by type + position
                actor = FindActorByTypeAndPosition(actorId, params, homePos);
            }
            if (actor != nullptr && actor->update != nullptr) {
                SPDLOG_INFO("[Anchor] Killing local actor id={} networkId={} (received destroyed)",
                            actor->id, networkActorId);
                Actor_Kill(actor);
            }
        }
    }

    // Track which networkActorIds we receive from this sender
    std::set<u32> receivedNetworkActorIds;

    // Get the set of networkActorIds this sender previously owned (uses sessionId)
    std::set<u32> previouslyOwnedByThisSender;
    if (ownerToNetworkIds.count(senderSessionId) > 0) {
        previouslyOwnedByThisSender = ownerToNetworkIds[senderSessionId];
    }

    for (const auto& actorJson : payload["actors"]) {
        // Skip actors with missing required fields
        if (!actorJson.contains("id") || !actorJson.contains("params") || !actorJson.contains("pos")) {
            continue;
        }

        s16 actorId = actorJson["id"].get<s16>();
        s16 category = actorJson.contains("cat") ? actorJson["cat"].get<s16>() : -1;
        s16 params = actorJson["params"].get<s16>();
        Vec3f homePos = actorJson.contains("home") ? actorJson["home"].get<Vec3f>() : Vec3f{0,0,0};
        s8 actorRoom = actorJson.contains("room") ? actorJson["room"].get<s8>() : -1;
        s16 spawnIdx = actorJson.contains("spawnIdx") ? actorJson["spawnIdx"].get<s16>() : -1;

        // Compute the DETERMINISTIC spawn ID locally - should match sender's ID
        // For scene actors (spawnIdx >= 0): ID includes scene/room/spawnIdx
        // For dynamic actors (spawnIdx < 0): ID is hash of actor type + position
        u32 networkActorId = GetDeterministicSpawnId(sceneNum, spawnIdx, actorRoom, actorId, homePos);

        // Skip per-player actors (each player has their own instance, don't sync)
        if (perPlayerActors.count(actorId) > 0) {
            continue;
        }

        receivedNetworkActorIds.insert(networkActorId);

        // IMPORTANT: Record ownership IMMEDIATELY when we receive this actor in a snapshot
        // This must happen BEFORE any early returns, otherwise IsOwnerOnline() will return false
        // because we never recorded who owns this actor
        // Uses sessionId to avoid clientId duplicates
        if (networkActorId != 0) {
            auto existingOwner = networkIdToOwner.find(networkActorId);
            if (existingOwner == networkIdToOwner.end() || existingOwner->second != sessionId) {
                // We're not the owner - record that the sender owns it
                networkIdToOwner[networkActorId] = senderSessionId;
            }
        }

        // Step 1: Try to find actor by networkActorId
        Actor* actor = FindActorByNetworkId(networkActorId);

        // Step 2: If not found, try to match by type + position (for initial sync)
        if (actor == nullptr && networkActorId != 0) {
            actor = FindActorByTypeAndPosition(actorId, params, homePos);
            if (actor != nullptr) {
                // Found a match! Assign the networkActorId from the snapshot
                SetActorNetworkId(actor, networkActorId);
                SPDLOG_INFO("[Anchor] Matched actor id={} to networkActorId={}", actorId, networkActorId);
            }
        }

        // Step 3: If actor doesn't exist locally, spawn it (only if room is loaded!)
        bool justSpawned = false;
        if (actor == nullptr) {
            // CRITICAL: Only spawn if the actor's room is currently loaded
            // Spawning in an unloaded room crashes because object banks aren't in memory
            if (!IsRoomLoaded(actorRoom)) {
                continue;  // Skip actors in unloaded rooms
            }

            // Actor doesn't exist locally - spawn it from network data
            Vec3f pos = actorJson["pos"].get<Vec3f>();
            Vec3s rot = actorJson.contains("wRot") ? actorJson["wRot"].get<Vec3s>() : Vec3s{0, 0, 0};

            actor = Actor_Spawn(&gPlayState->actorCtx, gPlayState, actorId, pos.x, pos.y, pos.z,
                                rot.x, rot.y, rot.z, params, false);

            if (actor != nullptr) {
                justSpawned = true;
                SetActorNetworkId(actor, networkActorId);
                SPDLOG_INFO("[Anchor] Spawned actor id={} networkActorId={} from network", actorId, networkActorId);
            } else {
                SPDLOG_WARN("[Anchor] Failed to spawn actor id={} networkActorId={}", actorId, networkActorId);
                continue;
            }
        }

        // Skip if actor is still null (couldn't spawn it)
        if (actor == nullptr) {
            continue;
        }

        // Verify actor ID matches
        if (actor->id != actorId) continue;

        // Check ownership - only apply remote state if we're NOT the owner
        // If we're the owner (closest to this actor), we keep our local state
        if (IsActorOwner(actor) && !justSpawned) {
            // We're closer to this actor than the sender - we keep our state
            // (justSpawned actors always get remote state to initialize properly)
            continue;
        }

        // Check if this actor is held by another player - skip position/movement sync
        // Player_Draw will handle positioning for held actors
        bool isHeldByOther = actorJson.contains("heldBy");

        // If held by another player, CLEAR interpolation data to prevent flickering
        // Otherwise the old interpolation target fights with Player_Draw positioning
        if (isHeldByOther) {
            std::lock_guard<std::mutex> lock(actorInterpMutex);
            actorInterpData.erase(networkActorId);
        }

        // Only sync position/rotation/movement for non-held actors
        if (!isHeldByOther) {
            // Get target state
            Vec3f targetPos = actorJson["pos"].get<Vec3f>();
            Vec3s targetRot = actorJson.contains("sRot") ? actorJson["sRot"].get<Vec3s>() : actorJson["wRot"].get<Vec3s>();

            // Store interpolation data
            {
                std::lock_guard<std::mutex> lock(actorInterpMutex);
                ActorInterpData& interp = actorInterpData[networkActorId];

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
        }

        // Apply health (always sync, even for held actors)
        if (actorJson.contains("hp")) {
            actor->colChkInfo.health = actorJson["hp"].get<s16>();
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
        SkelAnime* skelAnime = actorJson.contains("animFrame") ? GetActorSkelAnime(actor) : nullptr;
        if (skelAnime != nullptr && skelAnime->animation != nullptr) {
            f32 targetAnimFrame = actorJson["animFrame"].get<f32>();
            f32 animSpeed = actorJson.contains("animSpeed") ? actorJson["animSpeed"].get<f32>() : 1.0f;

            // Sanity check
            if (targetAnimFrame >= 0.0f && targetAnimFrame < 10000.0f &&
                animSpeed >= -10.0f && animSpeed <= 10.0f) {
                std::lock_guard<std::mutex> lock(actorInterpMutex);
                ActorInterpData& interp = actorInterpData[networkActorId];

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

        // Apply full AI state if available (actionFunc, state variables, jointTable, morphTable)
        // This gives us 100% sync for actors with serialization info
        if (actorJson.contains("aiState") && IsActorSyncable(actor->id)) {
            DeserializeActorState(actor, actorJson["aiState"]);
        }

        // Handle death (health = 0 means actor should die)
        if (actor->colChkInfo.health <= 0 && actor->update != NULL) {
            // Let the actor die naturally by setting health to 0
            // The actor's own update logic should handle death
        }

        // Note: Ownership is recorded at the TOP of the loop (before any early returns)
        // to ensure IsOwnerOnline() works correctly for all actors we receive
    }

    // Update the full set of actors owned by this sender (uses sessionId)
    ownerToNetworkIds[senderSessionId] = receivedNetworkActorIds;

    // NOTE: We NO LONGER despawn actors just because someone "stopped sending" them.
    // This was causing bugs like Saria disappearing when no player is close to her.
    //
    // Instead, we ONLY kill actors that are explicitly in the "destroyed" list.
    // The destroyed list is populated when:
    // 1. An actor we were sending dies (detected by comparing lastSentActorInfo)
    // 2. We receive a destroyed list from another player
    //
    // This is simpler and more robust: actors stay alive unless explicitly killed.
    } catch (const std::exception& e) {
        SPDLOG_ERROR("[Anchor] Error in HandlePacket_WorldSnapshot: {}", e.what());
    }
}

/**
 * Apply smooth interpolation to actors.
 * Called every frame to smoothly move actors to their target positions.
 */
void Anchor::ApplyActorInterpolation() {
    if (!IsSaveLoaded() || gPlayState == nullptr) return;

    std::lock_guard<std::mutex> lock(actorInterpMutex);

    // Build a set of networkIds for actors held by DummyPlayers (skip interpolation for these)
    // Use networkIds directly to avoid FindActorByNetworkId lookup issues
    std::set<u32> heldNetworkIds;
    for (auto& [clientId, client] : clients) {
        if (client.self || !client.online) continue;
        if (client.heldActorNetworkId != 0) {
            heldNetworkIds.insert(client.heldActorNetworkId);
        }
    }

    for (auto& [networkActorId, interp] : actorInterpData) {
        if (!interp.hasData) continue;

        // Skip interpolation for actors held by DummyPlayers
        // Player_Draw will position them at the holder's hands
        if (heldNetworkIds.count(networkActorId) > 0) continue;

        Actor* actor = FindActorByNetworkId(networkActorId);
        if (actor == nullptr || actor->update == NULL) continue;

        // Apply interpolation to all synced actors
        // Use "lerp toward target" approach for smooth movement
        // This is more robust than discrete prev->target interpolation

        const f32 LERP_FACTOR = 0.3f;  // Lower = smoother, higher = more responsive

        // Check for teleport (large distance = snap instead of lerp)
        f32 dx = interp.targetPos.x - actor->world.pos.x;
        f32 dy = interp.targetPos.y - actor->world.pos.y;
        f32 dz = interp.targetPos.z - actor->world.pos.z;
        f32 distSq = dx*dx + dy*dy + dz*dz;
        const f32 TELEPORT_THRESHOLD_SQ = 300.0f * 300.0f;

        if (distSq > TELEPORT_THRESHOLD_SQ) {
            // Teleport - snap to target
            actor->world.pos = interp.targetPos;
            actor->shape.rot = interp.targetRot;
        } else {
            // Smoothly lerp toward target position
            actor->world.pos.x += dx * LERP_FACTOR;
            actor->world.pos.y += dy * LERP_FACTOR;
            actor->world.pos.z += dz * LERP_FACTOR;

            // Smoothly lerp toward target rotation (handles s16 wraparound)
            s16 rotDiffX = interp.targetRot.x - actor->shape.rot.x;
            s16 rotDiffY = interp.targetRot.y - actor->shape.rot.y;
            s16 rotDiffZ = interp.targetRot.z - actor->shape.rot.z;
            actor->shape.rot.x += (s16)(rotDiffX * LERP_FACTOR);
            actor->shape.rot.y += (s16)(rotDiffY * LERP_FACTOR);
            actor->shape.rot.z += (s16)(rotDiffZ * LERP_FACTOR);
        }

        // Interpolate animation frame if we have animation data
        if (interp.hasAnimData) {
            SkelAnime* skelAnime = GetActorSkelAnime(actor);
            if (skelAnime != nullptr && skelAnime->animation != nullptr) {
                // Use lerp-toward-target for animation frames too
                f32 maxFrame = Animation_GetLastFrame(skelAnime->animation);
                f32 frameDiff = interp.targetAnimFrame - skelAnime->curFrame;

                // Handle animation looping - if the diff is too large, it wrapped around
                if (frameDiff > maxFrame / 2.0f) {
                    frameDiff -= maxFrame;
                } else if (frameDiff < -maxFrame / 2.0f) {
                    frameDiff += maxFrame;
                }

                skelAnime->curFrame += frameDiff * LERP_FACTOR;

                // Wrap the frame within valid range
                while (skelAnime->curFrame < 0.0f) skelAnime->curFrame += maxFrame;
                while (skelAnime->curFrame >= maxFrame) skelAnime->curFrame -= maxFrame;

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

/**
 * Check if we're waiting for the initial sync in a room with other players.
 *
 * With room-based sync, we can't reliably know if someone is in our room
 * (we only track scene in client state, not room). So we don't block spawns.
 *
 * Instead, the priority system (sessionId) handles who broadcasts first,
 * and room filtering ensures we only sync with players in our room.
 */
bool Anchor::IsWaitingForInitialSync() {
    // Don't block spawns - let the priority system and room filtering handle sync
    return false;
}
