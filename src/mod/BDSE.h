#pragma once

#include "ll/api/mod/NativeMod.h"

#include "mc/world/scores/Objective.h"
#include "mc/world/scores/Scoreboard.h"

namespace bds_essentials {

class BDSE {

public:
    static BDSE& getInstance();

    BDSE() : mSelf(*ll::mod::NativeMod::current()) {}

    [[nodiscard]] ll::mod::NativeMod& getSelf() const { return mSelf; }

    /// @return True if the mod is loaded successfully.
    bool load();

    /// @return True if the mod is enabled successfully.
    bool enable();

    /// @return True if the mod is disabled successfully.
    bool disable();

    // TODO: Implement this method if you need to unload the mod.
    // /// @return True if the mod is unloaded successfully.
    // bool unload();


private:
    ll::mod::NativeMod& mSelf;

    Scoreboard* mScoreboard      = nullptr;
    Objective*  mHealthObjective = nullptr;
    Objective*  mXPObjective     = nullptr;
};

} // namespace bds_essentials