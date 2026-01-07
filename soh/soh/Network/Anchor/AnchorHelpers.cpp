#include "AnchorHelpers.h"
#include "Anchor.h"

extern "C" {
#include "macros.h"
#include "functions.h"
extern PlayState* gPlayState;
}

extern "C" Actor* Anchor_GetClosestPlayerActor(PlayState* play, Actor* fromActor) {
    // Safety check
    if (play == nullptr || fromActor == nullptr) {
        return nullptr;
    }

    // If Anchor is not active, return the normal player
    if (Anchor::Instance == nullptr || !Anchor::Instance->isConnected) {
        Player* player = GET_PLAYER(play);
        return player != nullptr ? &player->actor : nullptr;
    }

    Actor* closestPlayer = nullptr;
    f32 minDistSq = 1e20f; // Large initial value

    // 1. Check the local player
    Player* localPlayer = GET_PLAYER(play);
    if (localPlayer != nullptr && localPlayer->actor.update != NULL) {
        f32 xzDist = Actor_WorldDistXZToActor(fromActor, &localPlayer->actor);
        f32 yDist = Actor_HeightDiff(fromActor, &localPlayer->actor);
        f32 distSq = SQ(xzDist) + SQ(yDist);
        if (distSq < minDistSq) {
            minDistSq = distSq;
            closestPlayer = &localPlayer->actor;
        }
    }

    // 2. Check DummyPlayers (in ACTORCAT_NPC)
    Actor* actor = play->actorCtx.actorLists[ACTORCAT_NPC].head;
    while (actor != NULL) {
        Actor* nextActor = actor->next;
        // Check if it's a DummyPlayer by checking if update function matches
        if (actor->id == ACTOR_EN_OE2 && actor->update == DummyPlayer_Update) {
            uint32_t clientId = Anchor::Instance->GetDummyPlayerClientId(actor);
            if (clientId != 0 && Anchor::Instance->clients.contains(clientId)) {
                AnchorClient& client = Anchor::Instance->clients[clientId];
                // Only consider online players in the same scene with a loaded save
                if (client.online && client.isSaveLoaded && client.sceneNum == play->sceneNum) {
                    f32 xzDist = Actor_WorldDistXZToActor(fromActor, actor);
                    f32 yDist = Actor_HeightDiff(fromActor, actor);
                    f32 distSq = SQ(xzDist) + SQ(yDist);
                    if (distSq < minDistSq) {
                        minDistSq = distSq;
                        closestPlayer = actor;
                    }
                }
            }
        }
        actor = nextActor;
    }

    // Fallback to local player if nothing found
    if (closestPlayer == nullptr && localPlayer != nullptr) {
        return &localPlayer->actor;
    }
    return closestPlayer;
}

extern "C" bool Anchor_IsEnabled(void) {
    return Anchor::Instance != nullptr && Anchor::Instance->isConnected;
}

extern "C" bool Anchor_IsActorOwner(Actor* actor) {
    // If Anchor is not active, we own everything
    if (Anchor::Instance == nullptr || !Anchor::Instance->isConnected) {
        return true;
    }

    // Safety check
    if (actor == nullptr || gPlayState == nullptr) {
        return true;
    }

    // Use the Anchor class method to check ownership
    return Anchor::Instance->IsActorOwner(actor);
}

extern "C" bool Anchor_IsPositionOwner(Vec3f* pos) {
    // If Anchor is not active, we own everything
    if (Anchor::Instance == nullptr || !Anchor::Instance->isConnected) {
        return true;
    }

    // Safety check
    if (pos == nullptr || gPlayState == nullptr) {
        return true;
    }

    Player* localPlayer = GET_PLAYER(gPlayState);
    if (localPlayer == nullptr) {
        return true;
    }

    // Calculate distance from local player to position
    f32 dx = localPlayer->actor.world.pos.x - pos->x;
    f32 dz = localPlayer->actor.world.pos.z - pos->z;
    f32 localDistSq = dx * dx + dz * dz;

    // Check all DummyPlayers (other connected players)
    Actor* actor = gPlayState->actorCtx.actorLists[ACTORCAT_NPC].head;
    while (actor != NULL) {
        if (actor->id == ACTOR_EN_OE2 && actor->update == DummyPlayer_Update) {
            uint32_t clientId = Anchor::Instance->GetDummyPlayerClientId(actor);
            if (clientId != 0 && Anchor::Instance->clients.contains(clientId)) {
                AnchorClient& client = Anchor::Instance->clients[clientId];
                if (client.online && client.isSaveLoaded && client.sceneNum == gPlayState->sceneNum) {
                    f32 otherDx = actor->world.pos.x - pos->x;
                    f32 otherDz = actor->world.pos.z - pos->z;
                    f32 otherDistSq = otherDx * otherDx + otherDz * otherDz;
                    if (otherDistSq < localDistSq) {
                        // Another player is closer - they own this position
                        return false;
                    }
                }
            }
        }
        actor = actor->next;
    }

    // We are the closest - we own this position
    return true;
}

extern "C" bool Anchor_IsWaitingForInitialSync(void) {
    if (Anchor::Instance == nullptr || !Anchor::Instance->isConnected) {
        return false;
    }
    return Anchor::Instance->IsWaitingForInitialSync();
}
