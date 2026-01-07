#ifndef NETWORK_ANCHOR_H
#define NETWORK_ANCHOR_H
#ifdef __cplusplus

#include "soh/Network/Network.h"
#include <libultraship/libultraship.h>
#include <queue>
#include <mutex>
#include <chrono>

extern "C" {
#include "variables.h"
#include "z64.h"
}

void DummyPlayer_Init(Actor* actor, PlayState* play);
void DummyPlayer_Update(Actor* actor, PlayState* play);
void DummyPlayer_Draw(Actor* actor, PlayState* play);
void DummyPlayer_Destroy(Actor* actor, PlayState* play);

typedef struct {
    uint32_t clientId;
    std::string name;
    Color_RGB8 color;
    std::string clientVersion;
    std::string teamId;
    bool online;
    bool self;
    uint32_t seed;
    bool isSaveLoaded;
    bool isGameComplete;
    s16 sceneNum;
    s32 entranceIndex;
    uint64_t sessionId;  // Random ID generated at startup for priority tie-breaking

    // Only available in PLAYER_UPDATE packets
    s32 linkAge;
    PosRot posRot;

    // Animation tables - must match PLAYER_LIMB_BUF_COUNT (24)
    Vec3s jointTable[24];       // Lower body animation
    Vec3s morphTable[24];       // Lower body morph targets
    Vec3s blendTable[24];       // Animation blending
    Vec3s upperJointTable[24];  // Upper body animation (arms when holding items!)
    Vec3s upperMorphTable[24];  // Upper body morph targets
    u8 movementFlags;
    Vec3s prevTransl;

    // All Player state fields serialized as JSON
    // This replaces individual fields - use PlayerSerializer to deserialize
    nlohmann::json playerState;

    // Equipment button (needs gSaveContext hack)
    u8 buttonItem0;

    // Ocarina
    u8 ocarinaNote;
    f32 ocarinaModulator;
    s8 ocarinaBend;

    // Held actor (for rocks, bombs, etc.)
    u32 heldActorNetworkId;
    Vec3f heldActorPos;  // Position of held actor (calculated by Player_Draw)

    // Interpolation data for smooth movement (lerp toward target)
    Vec3f interpTargetPos;
    Vec3s interpTargetRot;
    bool interpHasData;

    // Ptr to the dummy player
    Player* player;
} AnchorClient;

// Actor state for world snapshot sync
typedef struct {
    u32 uniqueId;           // Unique ID based on actorId + params + spawn position
    s16 actorId;
    s16 params;
    Vec3f pos;
    Vec3s rot;
    s16 health;
    u8 isDead;
    // Animation state
    f32 animCurFrame;
    f32 animPlaySpeed;
    s16 animIndex;
} ActorState;

// Interpolation data for smooth actor movement
typedef struct {
    Vec3f prevPos;
    Vec3f targetPos;
    Vec3s prevRot;
    Vec3s targetRot;
    f32 prevAnimFrame;
    f32 targetAnimFrame;
    f32 animSpeed;
    f32 interpAlpha;
    u32 lastUpdateFrame;
    bool hasData;
    bool hasAnimData;
} ActorInterpData;

typedef struct {
    uint32_t ownerClientId;
    u8 pvpMode;           // 0 = off, 1 = on, 2 = on with friendly fire
    u8 showLocationsMode; // 0 = none, 1 = team, 2 = all
    u8 teleportMode;      // 0 = off, 1 = team, 2 = all
    u8 syncItemsAndFlags; // 0 = off, 1 = on
} RoomState;

class Anchor : public Network {
  private:
    uint32_t spawningDummyPlayerForClientId = 0;
    bool shouldRefreshActors = false;
    bool justLoadedSave = false;
    bool isHandlingUpdateTeamState = false;
    bool isProcessingIncomingPacket = false;
    std::queue<nlohmann::json> incomingPacketQueue;
    std::mutex incomingPacketQueueMutex;
    std::queue<nlohmann::json> outgoingPacketQueue;
    std::mutex outgoingPacketQueueMutex;
    uint64_t sessionId = 0;  // Random ID generated at startup for priority tie-breaking

    nlohmann::json PrepClientState();
    nlohmann::json PrepRoomState();
    void RegisterHooks();
    void RefreshClientActors();
    void SetDummyPlayerClientId(const Actor* actor, uint32_t clientId);

    void HandlePacket_AllClientState(nlohmann::json payload);
    void HandlePacket_ConsumeAdultTradeItem(nlohmann::json payload);
    void HandlePacket_DamagePlayer(nlohmann::json payload);
    void HandlePacket_DisableAnchor(nlohmann::json payload);
    void HandlePacket_EntranceDiscovered(nlohmann::json payload);
    void HandlePacket_GameComplete(nlohmann::json payload);
    void HandlePacket_GiveItem(nlohmann::json payload);
    void HandlePacket_OcarinaSfx(nlohmann::json payload);
    void HandlePacket_PlayerSfx(nlohmann::json payload);
    void HandlePacket_PlayerUpdate(nlohmann::json payload);
    void HandlePacket_RequestTeamState(nlohmann::json payload);
    void HandlePacket_RequestTeleport(nlohmann::json payload);
    void HandlePacket_ServerMessage(nlohmann::json payload);
    void HandlePacket_SetCheckStatus(nlohmann::json payload);
    void HandlePacket_SetFlag(nlohmann::json payload);
    void HandlePacket_TeleportTo(nlohmann::json payload);
    void HandlePacket_UnsetFlag(nlohmann::json payload);
    void HandlePacket_UpdateBeansCount(nlohmann::json payload);
    void HandlePacket_UpdateClientState(nlohmann::json payload);
    void HandlePacket_UpdateDungeonItems(nlohmann::json payload);
    void HandlePacket_UpdateRoomState(nlohmann::json payload);
    void HandlePacket_UpdateTeamState(nlohmann::json payload);
    void HandlePacket_WorldSnapshot(nlohmann::json payload);

    // World snapshot helpers
    std::map<u32, ActorInterpData> actorInterpData;
    std::mutex actorInterpMutex;
    u8 snapshotSendCounter = 0;
    s16 lastSnapshotSceneNum = -1;
    s8 lastSnapshotRoomNum = -1;
    int receivedSnapshotCount = 0;  // Wait for N snapshots before broadcasting (prevents overwriting existing state)

    // Network ID system - unique IDs assigned by first player in scene
    std::map<Actor*, u32> actorToNetworkId;      // Actor pointer -> network ID
    std::map<u32, Actor*> networkIdToActor;      // Network ID -> Actor pointer
    u32 nextNetworkId = 1;                        // Counter for next available ID
    bool hasAssignedNetworkIds = false;           // Have we assigned IDs as the first player?

    // Ownership tracking - which session owns which actors (for despawn detection)
    // Uses sessionId (uint64_t) instead of clientId to avoid duplicates
    std::map<u32, uint64_t> networkIdToOwner;    // Network ID -> owner sessionId
    std::map<uint64_t, std::set<u32>> ownerToNetworkIds;  // Owner sessionId -> set of network IDs they own
    std::vector<Actor*> actorsPendingKill;       // Actors to kill next frame (deferred for safety)

    u32 GetActorUniqueId(Actor* actor);           // Legacy hash-based ID (fallback)
    void SetActorNetworkId(Actor* actor, u32 networkActorId);  // Set networkActorId from snapshot
    Actor* FindActorByTypeAndPosition(s16 actorId, s16 params, Vec3f homePos);  // Match by type+position
    void ClearNetworkIds();                       // Clear all networkActorId mappings (on scene change)
    void ProcessPendingActorKills();              // Kill actors queued for deletion
    bool IsOwnerOnline(u32 networkActorId);       // Check if actor's owner is still online

    Actor* FindActorByUniqueId(u32 uniqueId);     // Legacy - keep for compatibility
    void ApplyActorInterpolation();
    SkelAnime* GetActorSkelAnime(Actor* actor);

  public:
    // Network ID public methods (needed by DummyPlayer and AnchorHelpers)
    u32 GetOrAssignNetworkId(Actor* actor);       // Get existing or assign new networkActorId
    Actor* FindActorByNetworkId(u32 networkActorId);   // Find actor by networkActorId
    uint64_t GetClosestPlayerToActor(Actor* actor);  // Returns sessionId of closest player
    bool IsActorOwner(Actor* actor);
    bool IsWaitingForInitialSync();               // Check if waiting for first snapshot in scene with others

    uint32_t ownClientId;
    uint64_t GetSessionId() const { return sessionId; }  // Unique ID for this session (no duplicates)
    inline static const std::string clientVersion = (char*)gBuildVersion;

    // Packet types //
    inline static const std::string ALL_CLIENT_STATE = "ALL_CLIENT_STATE";
    inline static const std::string DAMAGE_PLAYER = "DAMAGE_PLAYER";
    inline static const std::string DISABLE_ANCHOR = "DISABLE_ANCHOR";
    inline static const std::string ENTRANCE_DISCOVERED = "ENTRANCE_DISCOVERED";
    inline static const std::string GAME_COMPLETE = "GAME_COMPLETE";
    inline static const std::string GIVE_ITEM = "GIVE_ITEM";
    inline static const std::string HANDSHAKE = "HANDSHAKE";
    inline static const std::string OCARINA_SFX = "OCARINA_SFX";
    inline static const std::string PLAYER_SFX = "PLAYER_SFX";
    inline static const std::string PLAYER_UPDATE = "PLAYER_UPDATE";
    inline static const std::string REQUEST_TEAM_STATE = "REQUEST_TEAM_STATE";
    inline static const std::string REQUEST_TELEPORT = "REQUEST_TELEPORT";
    inline static const std::string SERVER_MESSAGE = "SERVER_MESSAGE";
    inline static const std::string SET_CHECK_STATUS = "SET_CHECK_STATUS";
    inline static const std::string SET_FLAG = "SET_FLAG";
    inline static const std::string TELEPORT_TO = "TELEPORT_TO";
    inline static const std::string UNSET_FLAG = "UNSET_FLAG";
    inline static const std::string UPDATE_BEANS_COUNT = "UPDATE_BEANS_COUNT";
    inline static const std::string UPDATE_CLIENT_STATE = "UPDATE_CLIENT_STATE";
    inline static const std::string UPDATE_DUNGEON_ITEMS = "UPDATE_DUNGEON_ITEMS";
    inline static const std::string UPDATE_ROOM_STATE = "UPDATE_ROOM_STATE";
    inline static const std::string UPDATE_TEAM_STATE = "UPDATE_TEAM_STATE";
    inline static const std::string WORLD_SNAPSHOT = "WORLD_SNAPSHOT";

    static Anchor* Instance;
    std::map<uint32_t, AnchorClient> clients;
    RoomState roomState;

    void Enable();
    void Disable();
    void OnIncomingJson(nlohmann::json payload);
    void OnConnected();
    void OnDisconnected();
    void ProcessOutgoingPackets();
    void DrawMenu();
    void ProcessIncomingPacketQueue();
    void SendJsonToRemote(nlohmann::json packet);
    bool IsSaveLoaded();
    bool CanTeleportTo(uint32_t clientId);
    uint32_t GetDummyPlayerClientId(const Actor* actor);

    void SendPacket_ClearTeamState(std::string teamId);
    void SendPacket_DamagePlayer(u32 clientId, u8 damageEffect, u8 damage);
    void SendPacket_EntranceDiscovered(u16 entranceIndex);
    void SendPacket_GameComplete();
    void SendPacket_GiveItem(u16 modId, s16 getItemId);
    void SendPacket_Handshake();
    void SendPacket_OcarinaSfx(uint8_t note, float modulator, int8_t bend);
    void SendPacket_PlayerSfx(u16 sfxId);
    void SendPacket_PlayerUpdate();
    void SendPacket_RequestTeamState();
    void SendPacket_RequestTeleport(u32 clientId);
    void SendPacket_SetCheckStatus(RandomizerCheck rc);
    void SendPacket_SetFlag(s16 sceneNum, s16 flagType, s16 flag);
    void SendPacket_TeleportTo(u32 clientId);
    void SendPacket_UnsetFlag(s16 sceneNum, s16 flagType, s16 flag);
    void SendPacket_UpdateBeansCount();
    void SendPacket_UpdateClientState();
    void SendPacket_UpdateDungeonItems();
    void SendPacket_UpdateRoomState();
    void SendPacket_UpdateTeamState();
    void SendPacket_WorldSnapshot();
};

typedef enum {
    // Starting at 5 to continue from the last value in the PlayerDamageResponseType enum
    DUMMY_PLAYER_HIT_RESPONSE_STUN = 5,
    DUMMY_PLAYER_HIT_RESPONSE_FIRE,
    DUMMY_PLAYER_HIT_RESPONSE_NORMAL,
} DummyPlayerDamageResponseType;

class AnchorRoomWindow : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;

    void InitElement() override{};
    void DrawElement() override;
    void Draw() override;
    void UpdateElement() override{};
};

#endif // __cplusplus
#endif // NETWORK_ANCHOR_H
