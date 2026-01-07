#ifndef PLAYER_SERIALIZER_H
#define PLAYER_SERIALIZER_H

#include <nlohmann/json.hpp>

extern "C" {
#include "z64.h"
#include "z64player.h"
}

// Use the same FieldType enum from ActorSyncGenerated.inc
typedef enum {
    PLAYER_FIELD_TYPE_U8,
    PLAYER_FIELD_TYPE_S8,
    PLAYER_FIELD_TYPE_U16,
    PLAYER_FIELD_TYPE_S16,
    PLAYER_FIELD_TYPE_U32,
    PLAYER_FIELD_TYPE_S32,
    PLAYER_FIELD_TYPE_F32,
    PLAYER_FIELD_TYPE_VEC3F,
    PLAYER_FIELD_TYPE_VEC3S,
} PlayerFieldType;

// Field descriptor for Player state variables
typedef struct {
    const char* name;
    size_t offset;
    PlayerFieldType type;
    u8 arraySize;  // 1 for non-arrays
} PlayerFieldDescriptor;

// All Player fields to sync (excluding pointers, colliders, DMA stuff)
// Computed at compile-time using offsetof()
static PlayerFieldDescriptor PlayerSyncFields[] = {
    // Equipment and appearance (0x014C - 0x015F)
    { "currentTunic", offsetof(Player, currentTunic), PLAYER_FIELD_TYPE_S8, 1 },
    { "currentSwordItemId", offsetof(Player, currentSwordItemId), PLAYER_FIELD_TYPE_S8, 1 },
    { "currentShield", offsetof(Player, currentShield), PLAYER_FIELD_TYPE_S8, 1 },
    { "currentBoots", offsetof(Player, currentBoots), PLAYER_FIELD_TYPE_S8, 1 },
    { "heldItemButton", offsetof(Player, heldItemButton), PLAYER_FIELD_TYPE_S8, 1 },
    { "heldItemAction", offsetof(Player, heldItemAction), PLAYER_FIELD_TYPE_S8, 1 },
    { "heldItemId", offsetof(Player, heldItemId), PLAYER_FIELD_TYPE_U8, 1 },
    { "prevBoots", offsetof(Player, prevBoots), PLAYER_FIELD_TYPE_S8, 1 },
    { "itemAction", offsetof(Player, itemAction), PLAYER_FIELD_TYPE_S8, 1 },
    { "modelGroup", offsetof(Player, modelGroup), PLAYER_FIELD_TYPE_U8, 1 },
    { "nextModelGroup", offsetof(Player, nextModelGroup), PLAYER_FIELD_TYPE_U8, 1 },
    { "itemChangeType", offsetof(Player, itemChangeType), PLAYER_FIELD_TYPE_S8, 1 },
    { "modelAnimType", offsetof(Player, modelAnimType), PLAYER_FIELD_TYPE_U8, 1 },
    { "leftHandType", offsetof(Player, leftHandType), PLAYER_FIELD_TYPE_U8, 1 },
    { "rightHandType", offsetof(Player, rightHandType), PLAYER_FIELD_TYPE_U8, 1 },
    { "sheathType", offsetof(Player, sheathType), PLAYER_FIELD_TYPE_U8, 1 },
    { "currentMask", offsetof(Player, currentMask), PLAYER_FIELD_TYPE_U8, 1 },

    // Left hand position
    { "leftHandPos", offsetof(Player, leftHandPos), PLAYER_FIELD_TYPE_VEC3F, 1 },
    { "unk_3BC", offsetof(Player, unk_3BC), PLAYER_FIELD_TYPE_VEC3S, 1 },
    { "unk_3C8", offsetof(Player, unk_3C8), PLAYER_FIELD_TYPE_VEC3F, 1 },

    // Door state (0x042C - 0x042F)
    { "doorType", offsetof(Player, doorType), PLAYER_FIELD_TYPE_S8, 1 },
    { "doorDirection", offsetof(Player, doorDirection), PLAYER_FIELD_TYPE_S8, 1 },
    { "doorTimer", offsetof(Player, doorTimer), PLAYER_FIELD_TYPE_S16, 1 },

    // Get item state
    { "getItemId", offsetof(Player, getItemId), PLAYER_FIELD_TYPE_S16, 1 },
    { "getItemDirection", offsetof(Player, getItemDirection), PLAYER_FIELD_TYPE_U16, 1 },

    // Mount/ride
    { "mountSide", offsetof(Player, mountSide), PLAYER_FIELD_TYPE_S8, 1 },

    // Cutscene state
    { "csAction", offsetof(Player, csAction), PLAYER_FIELD_TYPE_U8, 1 },
    { "prevCsAction", offsetof(Player, prevCsAction), PLAYER_FIELD_TYPE_U8, 1 },
    { "cueId", offsetof(Player, cueId), PLAYER_FIELD_TYPE_U8, 1 },
    { "unk_447", offsetof(Player, unk_447), PLAYER_FIELD_TYPE_U8, 1 },

    // Cutscene positions
    { "unk_450", offsetof(Player, unk_450), PLAYER_FIELD_TYPE_VEC3F, 1 },
    { "unk_45C", offsetof(Player, unk_45C), PLAYER_FIELD_TYPE_VEC3F, 1 },

    // State flags
    { "stateFlags1", offsetof(Player, stateFlags1), PLAYER_FIELD_TYPE_U32, 1 },
    { "stateFlags2", offsetof(Player, stateFlags2), PLAYER_FIELD_TYPE_U32, 1 },
    { "stateFlags3", offsetof(Player, stateFlags3), PLAYER_FIELD_TYPE_U8, 1 },

    // Exchange item
    { "exchangeItemId", offsetof(Player, exchangeItemId), PLAYER_FIELD_TYPE_S8, 1 },

    // Talk state
    { "talkActorDistance", offsetof(Player, talkActorDistance), PLAYER_FIELD_TYPE_F32, 1 },

    // Various floats
    { "unk_6A0", offsetof(Player, unk_6A0), PLAYER_FIELD_TYPE_F32, 1 },
    { "closestSecretDistSq", offsetof(Player, closestSecretDistSq), PLAYER_FIELD_TYPE_F32, 1 },

    // Idle state
    { "idleType", offsetof(Player, idleType), PLAYER_FIELD_TYPE_S8, 1 },
    { "unk_6AD", offsetof(Player, unk_6AD), PLAYER_FIELD_TYPE_U8, 1 },
    { "unk_6AE_rotFlags", offsetof(Player, unk_6AE_rotFlags), PLAYER_FIELD_TYPE_U16, 1 },

    // Upper limb rotation
    { "upperLimbYawSecondary", offsetof(Player, upperLimbYawSecondary), PLAYER_FIELD_TYPE_S16, 1 },
    { "headLimbRot", offsetof(Player, headLimbRot), PLAYER_FIELD_TYPE_VEC3S, 1 },
    { "upperLimbRot", offsetof(Player, upperLimbRot), PLAYER_FIELD_TYPE_VEC3S, 1 },
    { "unk_6C2", offsetof(Player, unk_6C2), PLAYER_FIELD_TYPE_S16, 1 },
    { "unk_6C4", offsetof(Player, unk_6C4), PLAYER_FIELD_TYPE_F32, 1 },

    // Upper anim state
    { "upperAnimInterpWeight", offsetof(Player, upperAnimInterpWeight), PLAYER_FIELD_TYPE_F32, 1 },
    { "unk_834", offsetof(Player, unk_834), PLAYER_FIELD_TYPE_S16, 1 },
    { "unk_836", offsetof(Player, unk_836), PLAYER_FIELD_TYPE_S8, 1 },
    { "putAwayCooldownTimer", offsetof(Player, putAwayCooldownTimer), PLAYER_FIELD_TYPE_U8, 1 },

    // Movement
    { "linearVelocity", offsetof(Player, linearVelocity), PLAYER_FIELD_TYPE_F32, 1 },
    { "yaw", offsetof(Player, yaw), PLAYER_FIELD_TYPE_S16, 1 },
    { "parallelYaw", offsetof(Player, parallelYaw), PLAYER_FIELD_TYPE_S16, 1 },
    { "underwaterTimer", offsetof(Player, underwaterTimer), PLAYER_FIELD_TYPE_U16, 1 },

    // Combat
    { "meleeWeaponAnimation", offsetof(Player, meleeWeaponAnimation), PLAYER_FIELD_TYPE_S8, 1 },
    { "meleeWeaponState", offsetof(Player, meleeWeaponState), PLAYER_FIELD_TYPE_S8, 1 },
    { "unk_844", offsetof(Player, unk_844), PLAYER_FIELD_TYPE_S8, 1 },
    { "unk_845", offsetof(Player, unk_845), PLAYER_FIELD_TYPE_U8, 1 },

    // Control stick data
    { "controlStickDataIndex", offsetof(Player, controlStickDataIndex), PLAYER_FIELD_TYPE_U8, 1 },
    { "controlStickSpinAngles", offsetof(Player, controlStickSpinAngles), PLAYER_FIELD_TYPE_S8, 4 },
    { "controlStickDirections", offsetof(Player, controlStickDirections), PLAYER_FIELD_TYPE_S8, 4 },

    // Action variables
    { "actionVar1", offsetof(Player, av1.actionVar1), PLAYER_FIELD_TYPE_S8, 1 },
    { "actionVar2", offsetof(Player, av2.actionVar2), PLAYER_FIELD_TYPE_S16, 1 },

    // Misc floats
    { "unk_854", offsetof(Player, unk_854), PLAYER_FIELD_TYPE_F32, 1 },
    { "unk_858", offsetof(Player, unk_858), PLAYER_FIELD_TYPE_F32, 1 },
    { "unk_85C", offsetof(Player, unk_85C), PLAYER_FIELD_TYPE_F32, 1 },
    { "unk_860", offsetof(Player, unk_860), PLAYER_FIELD_TYPE_S16, 1 },
    { "unk_862", offsetof(Player, unk_862), PLAYER_FIELD_TYPE_S16, 1 },
    { "unk_864", offsetof(Player, unk_864), PLAYER_FIELD_TYPE_F32, 1 },
    { "unk_868", offsetof(Player, unk_868), PLAYER_FIELD_TYPE_F32, 1 },
    { "unk_86C", offsetof(Player, unk_86C), PLAYER_FIELD_TYPE_F32, 1 },
    { "unk_870", offsetof(Player, unk_870), PLAYER_FIELD_TYPE_F32, 1 },
    { "unk_874", offsetof(Player, unk_874), PLAYER_FIELD_TYPE_F32, 1 },
    { "unk_878", offsetof(Player, unk_878), PLAYER_FIELD_TYPE_F32, 1 },
    { "unk_87C", offsetof(Player, unk_87C), PLAYER_FIELD_TYPE_S16, 1 },
    { "turnRate", offsetof(Player, turnRate), PLAYER_FIELD_TYPE_S16, 1 },
    { "unk_880", offsetof(Player, unk_880), PLAYER_FIELD_TYPE_F32, 1 },

    // Ledge climbing
    { "yDistToLedge", offsetof(Player, yDistToLedge), PLAYER_FIELD_TYPE_F32, 1 },
    { "distToInteractWall", offsetof(Player, distToInteractWall), PLAYER_FIELD_TYPE_F32, 1 },
    { "ledgeClimbType", offsetof(Player, ledgeClimbType), PLAYER_FIELD_TYPE_U8, 1 },
    { "ledgeClimbDelayTimer", offsetof(Player, ledgeClimbDelayTimer), PLAYER_FIELD_TYPE_U8, 1 },
    { "textboxBtnCooldownTimer", offsetof(Player, textboxBtnCooldownTimer), PLAYER_FIELD_TYPE_U8, 1 },
    { "damageFlickerAnimCounter", offsetof(Player, damageFlickerAnimCounter), PLAYER_FIELD_TYPE_U8, 1 },
    { "unk_890", offsetof(Player, unk_890), PLAYER_FIELD_TYPE_U8, 1 },
    { "bodyShockTimer", offsetof(Player, bodyShockTimer), PLAYER_FIELD_TYPE_U8, 1 },
    { "unk_892", offsetof(Player, unk_892), PLAYER_FIELD_TYPE_U8, 1 },
    { "hoverBootsTimer", offsetof(Player, hoverBootsTimer), PLAYER_FIELD_TYPE_U8, 1 },

    // Fall state
    { "fallStartHeight", offsetof(Player, fallStartHeight), PLAYER_FIELD_TYPE_S16, 1 },
    { "fallDistance", offsetof(Player, fallDistance), PLAYER_FIELD_TYPE_S16, 1 },
    { "floorPitch", offsetof(Player, floorPitch), PLAYER_FIELD_TYPE_S16, 1 },
    { "floorPitchAlt", offsetof(Player, floorPitchAlt), PLAYER_FIELD_TYPE_S16, 1 },
    { "unk_89C", offsetof(Player, unk_89C), PLAYER_FIELD_TYPE_S16, 1 },
    { "floorSfxOffset", offsetof(Player, floorSfxOffset), PLAYER_FIELD_TYPE_U16, 1 },

    // Knockback
    { "knockbackDamage", offsetof(Player, knockbackDamage), PLAYER_FIELD_TYPE_U8, 1 },
    { "knockbackType", offsetof(Player, knockbackType), PLAYER_FIELD_TYPE_U8, 1 },
    { "knockbackRot", offsetof(Player, knockbackRot), PLAYER_FIELD_TYPE_S16, 1 },
    { "knockbackSpeed", offsetof(Player, knockbackSpeed), PLAYER_FIELD_TYPE_F32, 1 },
    { "knockbackYVelocity", offsetof(Player, knockbackYVelocity), PLAYER_FIELD_TYPE_F32, 1 },

    // Pushed state
    { "pushedSpeed", offsetof(Player, pushedSpeed), PLAYER_FIELD_TYPE_F32, 1 },
    { "pushedYaw", offsetof(Player, pushedYaw), PLAYER_FIELD_TYPE_S16, 1 },

    // Body parts positions (for IK)
    { "bodyPartsPos", offsetof(Player, bodyPartsPos), PLAYER_FIELD_TYPE_VEC3F, PLAYER_BODYPART_MAX },

    // Fire state
    { "bodyIsBurning", offsetof(Player, bodyIsBurning), PLAYER_FIELD_TYPE_U8, 1 },
    { "bodyFlameTimers", offsetof(Player, bodyFlameTimers), PLAYER_FIELD_TYPE_U8, PLAYER_BODYPART_MAX },
    { "unk_A73", offsetof(Player, unk_A73), PLAYER_FIELD_TYPE_U8, 1 },

    // Invincibility
    { "invincibilityTimer", offsetof(Player, invincibilityTimer), PLAYER_FIELD_TYPE_S8, 1 },

    // Floor state
    { "floorTypeTimer", offsetof(Player, floorTypeTimer), PLAYER_FIELD_TYPE_U8, 1 },
    { "floorProperty", offsetof(Player, floorProperty), PLAYER_FIELD_TYPE_U8, 1 },
    { "prevFloorType", offsetof(Player, prevFloorType), PLAYER_FIELD_TYPE_U8, 1 },

    // Previous control stick
    { "prevControlStickMagnitude", offsetof(Player, prevControlStickMagnitude), PLAYER_FIELD_TYPE_F32, 1 },
    { "prevControlStickAngle", offsetof(Player, prevControlStickAngle), PLAYER_FIELD_TYPE_S16, 1 },
    { "prevFloorSfxOffset", offsetof(Player, prevFloorSfxOffset), PLAYER_FIELD_TYPE_U16, 1 },

    // More misc
    { "unk_A84", offsetof(Player, unk_A84), PLAYER_FIELD_TYPE_S16, 1 },
    { "unk_A86", offsetof(Player, unk_A86), PLAYER_FIELD_TYPE_S8, 1 },
    { "unk_A87", offsetof(Player, unk_A87), PLAYER_FIELD_TYPE_U8, 1 },
    { "unk_A88", offsetof(Player, unk_A88), PLAYER_FIELD_TYPE_VEC3F, 1 },

    // SoH specific
    { "boomerangQuickRecall", offsetof(Player, boomerangQuickRecall), PLAYER_FIELD_TYPE_U8, 1 },
    { "ivanFloating", offsetof(Player, ivanFloating), PLAYER_FIELD_TYPE_U8, 1 },
    { "ivanDamageMultiplier", offsetof(Player, ivanDamageMultiplier), PLAYER_FIELD_TYPE_U8, 1 },

    // Terminator
    { NULL, 0, PLAYER_FIELD_TYPE_U8, 0 }
};

// Count of fields (excluding terminator)
static const int PlayerSyncFieldCount = (sizeof(PlayerSyncFields) / sizeof(PlayerFieldDescriptor)) - 1;

/**
 * Serialize Player state fields to JSON.
 * Does NOT include animation tables (those are handled separately for efficiency).
 */
nlohmann::json SerializePlayerState(Player* player);

/**
 * Deserialize Player state fields from JSON.
 * Does NOT include animation tables (those are handled separately).
 */
void DeserializePlayerState(Player* player, const nlohmann::json& state);

#endif // PLAYER_SERIALIZER_H
