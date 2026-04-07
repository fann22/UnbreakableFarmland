#include "mod/UFM.h"

#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/io/Logger.h"
#include "ll/api/mod/RegisterHelper.h"

#include "ila/event/minecraft/world/level/block/FarmDecayEvent.h"

#include "ll/api/event/player/PlayerPlaceBlockEvent.h"
#include "mc/network/packet/UpdateBlockPacket.h"
#include "mc/world/level/block/registry/BlockTypeRegistry.h"

namespace unbreakable_farmland {

UFM& UFM::getInstance() {
    static UFM instance;
    return instance;
}

static ll::event::ListenerPtr gFarmDecayListener;
static ll::event::ListenerPtr gBlockPlaceListener;

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

    gBlockPlaceListener = bus.emplaceListener<ll::event::PlayerPlacedBlockEvent>(
        [](ll::event::PlayerPlacedBlockEvent& event) {
            if (event.placedBlock().getTypeName() != "minecraft:glass") return;

            // Ambil Block dirt via registry
            auto& registry  = BlockTypeRegistry::get();
            auto& dirtBlock = registry.getDefaultBlockState("minecraft:dirt");

            UpdateBlockPacket pkt;

            // Field payload langsung ada di pkt karena multiple inheritance
            pkt.mPos         = event.pos();
            pkt.mLayer       = 0;
            pkt.mUpdateFlags = 1;
            pkt.mRuntimeId   = dirtBlock.mNetworkId;

            event.self().sendNetworkPacket(pkt);
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

    if (gBlockPlaceListener) {
        bus.removeListener(gBlockPlaceListener);
        gBlockPlaceListener.reset();
    }

    return true;
}

} // namespace unbreakable_farmland;