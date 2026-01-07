#ifndef ANCHOR_HELPERS_H
#define ANCHOR_HELPERS_H

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Finds the closest player (real Player or DummyPlayer) to the given actor.
 * Used by enemies/NPCs to target the nearest player in multiplayer.
 */
Actor* Anchor_GetClosestPlayerActor(PlayState* play, Actor* fromActor);

/**
 * Checks if Anchor networking is currently enabled and connected.
 */
bool Anchor_IsEnabled(void);

/**
 * Checks if the local player is the owner (closest player) of an actor.
 * Owner is responsible for running Update() and sending state.
 * Non-owners should skip Update() and apply received state instead.
 */
bool Anchor_IsActorOwner(Actor* actor);

/**
 * Checks if the local player is the owner of a position.
 * Used to determine if we should spawn actors at that position.
 */
bool Anchor_IsPositionOwner(Vec3f* pos);

/**
 * Sets network spawn mode.
 * When true, Actor_Spawn bypasses ownership check (used for spawning from world snapshot).
 */
void Anchor_SetNetworkSpawnMode(bool enabled);

/**
 * Checks if we're waiting for initial sync in a scene with other players.
 * Used to block local spawns until we receive the first world snapshot.
 */
bool Anchor_IsWaitingForInitialSync(void);

/**
 * Checks if an actor category should have its Update() skipped for non-owners.
 * Returns true for ENEMY, BOSS, NPC - categories with AI that uses proximity triggers.
 */
bool Anchor_ShouldSkipUpdateForNonOwner(s16 category);

#ifdef __cplusplus
}
#endif

#endif // ANCHOR_HELPERS_H
