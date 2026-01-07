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

#ifdef __cplusplus
}
#endif

#endif // ANCHOR_HELPERS_H
