#include "global.h"
#include "soh/Network/Anchor/AnchorHelpers.h"

void TitleSetup_InitImpl(GameState* gameState) {
    osSyncPrintf("ゼルダ共通データ初期化\n"); // "Zelda common data initalization"
    Anchor_LogInfo("[TitleSetup] Anchor_LogInfo test from C code - if you see this, logging works!");
    SaveContext_Init();
    gameState->running = false;
    SET_NEXT_GAMESTATE(gameState, Title_Init, TitleContext);
}

void TitleSetup_Destroy(GameState* gameState) {
}

void TitleSetup_Init(GameState* gameState) {
    gameState->destroy = TitleSetup_Destroy;
    TitleSetup_InitImpl(gameState);
}
