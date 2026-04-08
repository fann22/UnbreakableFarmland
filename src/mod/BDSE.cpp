#include "mod/BDSE.h"

#include "gsl/pointers"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/io/LogLevel.h"
#include "ll/api/io/Logger.h"
#include "ll/api/mod/RegisterHelper.h"

#include "ll/api/event/player/PlayerDisconnectEvent.h"
#include "ll/api/event/player/PlayerJoinEvent.h"
#include "ll/api/service/Bedrock.h"

#include "ila/event/minecraft/world/actor/MobHealthChangeEvent.h"
#include "ila/event/minecraft/world/level/block/FarmDecayEvent.h"

#include "mc/world/actor/Actor.h"
#include "mc/world/attribute/Attribute.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceRef.h"
#include "mc/world/attribute/BaseAttributeMap.h"

#include "mc/world/level/Level.h"

#include "mc/world/scores/DisplayObjective.h"
#include "mc/world/scores/Objective.h"
#include "mc/world/scores/ObjectiveCriteria.h"
#include "mc/world/scores/ObjectiveRenderType.h"
#include "mc/world/scores/ObjectiveSortOrder.h"
#include "mc/world/scores/PlayerScoreSetFunction.h"
#include "mc/world/scores/Scoreboard.h"
#include "mc/world/scores/ScoreboardId.h"
#include "mc/world/scores/ScoreboardOperationResult.h"

namespace bds_essentials {

BDSE& BDSE::getInstance() {
    static BDSE instance;
    return instance;
}

static std::vector<ll::event::ListenerPtr> gListeners;

bool BDSE::load() {
    // getSelf().getLogger().debug("Loading...");
    // Code for loading the mod goes here.
    return true;
}

bool BDSE::enable() {
    Level*      level      = ll::service::getLevel();
    Scoreboard& scoreboard = level->getScoreboard();

    auto objectives = scoreboard.getObjectives();
    for (auto* obj : objectives) {
        scoreboard.removeObjective(const_cast<Objective*>(obj));
    }

    ObjectiveCriteria* criteria = scoreboard.getCriteria(Scoreboard::DEFAULT_CRITERIA());
    if (!criteria) {
        criteria = const_cast<ObjectiveCriteria*>(
            &scoreboard.createObjectiveCriteria(Scoreboard::DEFAULT_CRITERIA(), false, ::ObjectiveRenderType::Integer)
        );
    }

    Objective* HealthObjective = scoreboard.getObjective("PlayerHealth");
    if (!HealthObjective) {
        HealthObjective = scoreboard.addObjective("PlayerHealth", "❤", *criteria);
        scoreboard
            .setDisplayObjective(Scoreboard::DISPLAY_SLOT_BELOWNAME(), *HealthObjective, ObjectiveSortOrder::Ascending);
    }

    Objective* XPObjective = scoreboard.getObjective("MostLVL");
    if (!XPObjective) {
        XPObjective = scoreboard.addObjective("MostLVL", "•> Most Level <•", *criteria);
        scoreboard.setDisplayObjective(Scoreboard::DISPLAY_SLOT_SIDEBAR(), *XPObjective, ObjectiveSortOrder::Ascending);
    }

    auto& bus = ll::event::EventBus::getInstance();

    gListeners.insert(
        gListeners.begin(),
        bus.emplaceListener<ila::mc::FarmDecayBeforeEvent>([](ila::mc::FarmDecayBeforeEvent& event) { event.cancel(); })
    );

    gListeners.insert(
        gListeners.begin(),
        bus.emplaceListener<ll::event::PlayerJoinEvent>(
            [&scoreboard, &HealthObjective, &XPObjective](ll::event::PlayerJoinEvent& event) {
                Player&              player  = event.self();
                auto&                attrMap = const_cast<BaseAttributeMap&>(*player.getAttributes());
                AttributeInstanceRef ref     = attrMap.getMutableInstance(Player::LEVEL().mIDValue);
                float                lvl     = ref.mPtr->mCurrentValue;

                ScoreboardId const* id = &scoreboard.getScoreboardId(player);
                if (*id == ScoreboardId::INVALID()) {
                    id = &scoreboard.createScoreboardId(player);
                }
                ScoreboardOperationResult result;
                scoreboard.modifyPlayerScore(result, *id, *HealthObjective, 0, PlayerScoreSetFunction::Set);
                scoreboard.modifyPlayerScore(result, *id, *XPObjective, int(lvl), PlayerScoreSetFunction::Set);
            }
        )
    );

    gListeners.insert(
        gListeners.begin(),
        bus.emplaceListener<ll::event::PlayerDisconnectEvent>(
            [&scoreboard](ll::event::PlayerDisconnectEvent& event) {
                ScoreboardId const& id = scoreboard.createScoreboardId(event.self());
                scoreboard.resetPlayerScore(id);
            }
        )
    );

    gListeners.insert(
        gListeners.begin(),
        bus.emplaceListener<ila::mc::MobHealthChangeAfterEvent>(
            [&scoreboard, &HealthObjective](ila::mc::MobHealthChangeAfterEvent& event) {
                if (!event.self().isPlayer() || event.newValue() > float(event.self().getMaxHealth())) return;
                BDSE::getInstance().getSelf().getLogger().info("{} -> {}", event.oldValue(), event.newValue());
                ScoreboardId const&       id = scoreboard.createScoreboardId(event.self());
                ScoreboardOperationResult result;
                scoreboard.modifyPlayerScore(result, id, *HealthObjective, int(event.newValue()), PlayerScoreSetFunction::Set);
            }
        )
    );

    /*
    gBlockPlaceListener = bus.emplaceListener<ll::event::PlayerPlacedBlockEvent>(
        [this](ll::event::PlayerPlacedBlockEvent& event) {
            getSelf().getLogger().info("Block placed by player: " + event.placedBlock().getTypeName());
            if (event.placedBlock().getTypeName() != "minecraft:glass") return;

            auto& registry  = BlockTypeRegistry::get();
            auto& dirtBlock = registry.getDefaultBlockState("minecraft:dirt");

            getSelf().getLogger().info(dirtBlock.getTypeName());

            UpdateBlockPacket pkt;
            pkt.mPos         = event.pos();
            pkt.mLayer       = 0;
            pkt.mUpdateFlags = 1;
            pkt.mRuntimeId   = dirtBlock.mSerializationIdHashForNetwork;

            auto& player = event.self();
            player.sendNetworkPacket(pkt);
        }
    );*/

    getSelf().getLogger().info("loaded.");
    return true;
}

bool BDSE::disable() {
    auto& bus = ll::event::EventBus::getInstance();

    for (auto& listener : gListeners) {
        bus.removeListener(listener);
        listener.reset();
    }

    return true;
}

} // namespace bds_essentials

LL_REGISTER_MOD(unbreakable_farmland::BDSE, unbreakable_farmland::BDSE::getInstance());