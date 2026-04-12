#pragma once
#include "core/HttpSender.h"
#include "core/ChunkCollector.h"
#include "core/PlayerTracker.h"
#include "ll/api/thread/ServerThreadExecutor.h"
#include "ll/api/chrono/GameChrono.h"
#include "ll/api/memory/Hook.h"
#include <memory>
#include <atomic>

#include "mc/world/level/storage/DBChunkStorage.h"

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

        schedulePlayerTick();
    }

    void shutdown() {
        mRunning = false;
    }

    ChunkCollector& getCollector() { return *mCollector; }

private:
    void schedulePlayerTick() {
        if (!mRunning) return;

        // 5 detik = 5000ms
        ll::thread::ServerThreadExecutor::getDefault().executeAfter(
            [this]() {
                if (!mRunning) return;
                mPlayerTracker->tick();
                schedulePlayerTick(); // reschedule
            },
            std::chrono::milliseconds(5000)
        );
    }

    std::unique_ptr<HttpSender>     mSender;
    std::unique_ptr<ChunkCollector> mCollector;
    std::unique_ptr<PlayerTracker>  mPlayerTracker;
    std::atomic<bool>               mRunning{true};
};

} // namespace mipmap