#include "mod/UFM.h"

#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/io/Logger.h"
#include "ll/api/mod/RegisterHelper.h"

#include "ila/event/minecraft/world/level/block/FarmDecayEvent.h"

namespace unbreakable_farmland {

UFM& UFM::getInstance() {
    static UFM instance;
    return instance;
}

static ll::event::ListenerPtr gFarmDecayListener;

bool UFM::load() {
    // getSelf().getLogger().debug("Loading...");
    // Code for loading the mod goes here.
    return true;
}

bool UFM::enable() {
    auto& bus    = ll::event::EventBus::getInstance();

    gFarmDecayListener = bus.emplaceListener<ila::mc::FarmDecayBeforeEvent>(
        [](ila::mc::FarmDecayBeforeEvent& event) {
            event.cancel();
        }
    );

    getSelf().getLogger().info("UnbreakableFarmland active.");
    return true;
}

bool UFM::disable() {
    auto& bus = ll::event::EventBus::getInstance();

    if (gFarmDecayListener) {
        bus.removeListener(gFarmDecayListener);
        gFarmDecayListener.reset();
    }

    return true;
}

} // namespace unbreakable_farmland
