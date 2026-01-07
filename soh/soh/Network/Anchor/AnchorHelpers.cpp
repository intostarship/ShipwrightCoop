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
