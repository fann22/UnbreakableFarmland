#pragma once
#include "core/HttpSender.h"
#include "core/ChunkCollector.h"
#include "core/PlayerTracker.h"
#include "ll/api/schedule/Scheduler.h"
#include "ll/api/memory/Hook.h"

#include "mc/world/level/storage/DBChunkStorage.h"

#include <memory>

namespace mipmap {

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

class MipMap {
public:
    static MipMap& getInstance() {
        static MipMap instance;
        return instance;
    }

    void init() {
        HttpSender::Config cfg;
        cfg.host = "127.0.0.1";
        cfg.port = 8080;

        mSender        = std::make_unique<HttpSender>(cfg);
        mCollector     = std::make_unique<ChunkCollector>(*mSender);
        mPlayerTracker = std::make_unique<PlayerTracker>(*mSender);

        // tick player tiap 5 detik (100 tick)
        mScheduler.add<ll::schedule::RepeatTask>(
            ll::chrono::ticks(100),
            [this]() {
                mPlayerTracker->tick();
            }
        );
    }

    ChunkCollector& getCollector() { return *mCollector; }

private:
    std::unique_ptr<HttpSender>     mSender;
    std::unique_ptr<ChunkCollector> mCollector;
    std::unique_ptr<PlayerTracker>  mPlayerTracker;
    ll::schedule::GameTickScheduler mScheduler;
};

} // namespace mipmap