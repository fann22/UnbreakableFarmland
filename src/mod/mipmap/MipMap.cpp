#include "MipMap.h"
#include "mc/world/level/storage/DBChunkStorage.h"
#include "ll/api/memory/Hook.h"

LL_TYPE_INSTANCE_HOOK(
    MipMapChunkLoadHook,
    ll::memory::HookPriority::Normal,
    DBChunkStorage,
    &DBChunkStorage::$loadChunk,
    void,
    LevelChunk& lc,
    bool forceImmediateReplacementDataLoad
) {
    origin(lc, forceImmediateReplacementDataLoad);
    mipmap::MipMap::getInstance().getCollector().onChunkLoaded(lc);
}