#pragma once
#include "core/HttpSender.h"
#include "core/ChunkCollector.h"
#include "core/PlayerTracker.h"
#include "ll/api/thread/ServerThreadExecutor.h"
#include "ll/api/chrono/GameChrono.h"
#include "ll/api/memory/Hook.h"
#include "mc/world/level/storage/DBChunkStorage.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include <memory>
#include <atomic>

namespace mipmap {

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

    void shutdown() { mRunning = false; }

    ChunkCollector& getCollector() { return *mCollector; }

private:
    void schedulePlayerTick() {
        if (!mRunning) return;
        ll::thread::ServerThreadExecutor::getDefault().executeAfter(
            [this]() {
                if (!mRunning) return;
                mPlayerTracker->tick();
                schedulePlayerTick();
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

// Hook di luar namespace, setelah class MipMap didefinisikan
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