#ifndef ACTOR_SERIALIZER_H
#define ACTOR_SERIALIZER_H

#include <nlohmann/json.hpp>

extern "C" {
#include "z64.h"
#include "z64actor.h"
}

// Include the auto-generated actor sync info
#include "ActorSyncGenerated.inc"

/**
 * Serialize an actor's AI state to JSON.
 * This includes:
 * - actionFunc converted to a stable index
 * - jointTable/morphTable for animation (if available)
 *
 * Position, rotation, health are synced separately.
 */
nlohmann::json SerializeActorState(Actor* actor);

/**
 * Deserialize an actor's AI state from JSON.
 * Restores:
 * - actionFunc from index
 * - jointTable/morphTable
 */
void DeserializeActorState(Actor* actor, const nlohmann::json& state);

/**
 * Check if an actor type is syncable (has serialization info).
 */
bool IsActorSyncable(s16 actorId);

/**
 * Get the action function index for an actor.
 * Returns -1 if not found or not syncable.
 */
int GetActorActionFuncIndex(Actor* actor);

/**
 * Set the action function for an actor by index.
 * Returns true if successful.
 */
bool SetActorActionFuncByIndex(Actor* actor, int index);

#endif // ACTOR_SERIALIZER_H
