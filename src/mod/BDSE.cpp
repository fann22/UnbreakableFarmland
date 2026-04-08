#include "mod/BDSE.h"

#include "gsl/pointers"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/io/LogLevel.h"
#include "ll/api/io/Logger.h"
#include "ll/api/mod/RegisterHelper.h"

#include "ll/api/event/player/PlayerAttackEvent.h"
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

// #include "mc/deps/shared_types/legacy/actor/ActorDamageCause.h"
// #include "mc/deps/shared_types/legacy/LevelSoundEvent.h"

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
    mScoreboard = &ll::service::getLevel()->getScoreboard();

    auto objectives = mScoreboard->getObjectives();
    for (auto* obj : objectives) {
        mScoreboard->removeObjective(const_cast<Objective*>(obj));
    }

    ObjectiveCriteria* criteria = mScoreboard->getCriteria(Scoreboard::DEFAULT_CRITERIA());
    if (!criteria) {
        criteria = const_cast<ObjectiveCriteria*>(
            &mScoreboard->createObjectiveCriteria(Scoreboard::DEFAULT_CRITERIA(), false, ::ObjectiveRenderType::Integer)
        );
    }

    mHealthObjective = mScoreboard->addObjective("PlayerHealth", "❤", *criteria);
    mScoreboard->setDisplayObjective(Scoreboard::DISPLAY_SLOT_BELOWNAME(), *mHealthObjective, ObjectiveSortOrder::Ascending);

    mXPObjective = mScoreboard->addObjective("MostLVL", "•> Most Level <•", *criteria);
    mScoreboard->setDisplayObjective(Scoreboard::DISPLAY_SLOT_SIDEBAR(), *mXPObjective, ObjectiveSortOrder::Ascending);

    auto& bus = ll::event::EventBus::getInstance();

    gListeners.insert(
        gListeners.begin(),
        bus.emplaceListener<ila::mc::FarmDecayBeforeEvent>([](ila::mc::FarmDecayBeforeEvent& event) { event.cancel(); })
    );

    gListeners.insert(
        gListeners.begin(),
        bus.emplaceListener<ll::event::PlayerJoinEvent>(
            [this](ll::event::PlayerJoinEvent& event) {
                Player&              player  = event.self();
                auto&                attrMap = const_cast<BaseAttributeMap&>(*player.getAttributes());
                AttributeInstanceRef ref     = attrMap.getMutableInstance(Player::LEVEL().mIDValue);
                float                lvl     = ref.mPtr->mCurrentValue;

                ScoreboardId const* id = &mScoreboard->getScoreboardId(player);
                if (*id == ScoreboardId::INVALID()) {
                    id = &mScoreboard->createScoreboardId(player);
                }
                ScoreboardOperationResult result;
                mScoreboard->modifyPlayerScore(result, *id, *mHealthObjective, player.getHealth(), PlayerScoreSetFunction::Set);
                mScoreboard->modifyPlayerScore(result, *id, *mXPObjective, int(lvl), PlayerScoreSetFunction::Set);
            }
        )
    );

    gListeners.insert(
        gListeners.begin(),
        bus.emplaceListener<ll::event::PlayerDisconnectEvent>(
            [this](ll::event::PlayerDisconnectEvent& event) {
                ScoreboardId const* id = &mScoreboard->getScoreboardId(event.self());
                if (*id == ScoreboardId::INVALID()) {
                    id = &mScoreboard->createScoreboardId(event.self());
                }
                mScoreboard->resetPlayerScore(*id);
            }
        )
    );

    gListeners.insert(
        gListeners.begin(),
        bus.emplaceListener<ila::mc::MobHealthChangeAfterEvent>(
            [this](ila::mc::MobHealthChangeAfterEvent& event) {
                if (!event.self().isPlayer() || event.newValue() > float(event.self().getMaxHealth())) return;
                BDSE::getInstance().getSelf().getLogger().info("{} -> {}", event.oldValue(), event.newValue());
                ScoreboardId const&       id = mScoreboard->createScoreboardId(event.self());
                ScoreboardOperationResult result;
                mScoreboard->modifyPlayerScore(result, id, *mHealthObjective, int(event.newValue()), PlayerScoreSetFunction::Set);
            }
        )
    );
/*
    gListeners.insert(
        gListeners.begin(),
        bus.emplaceListener<ll::event::PlayerAttackEvent>(
            [](ll::event::PlayerAttackEvent& event) {
                if (event.cause() == ActorDamageCause::Projectile) {
                    Player& player = event.self();
                    player.playSound(LevelSoundEvent::Boost, player.getPosition(), 1);
                }
            }
        )
    );
*/

    getSelf().getLogger().info("loaded.");
    return true;
}

bool BDSE::disable() {
    auto& bus = ll::event::EventBus::getInstance();

    for (auto& listener : gListeners) {
        bus.removeListener(listener);
        listener.reset();
    }

    gListeners.clear();

    mScoreboard      = nullptr;
    mHealthObjective = nullptr;
    mXPObjective     = nullptr;

    return true;
}

} // namespace bds_essentials

LL_REGISTER_MOD(bds_essentials::BDSE, bds_essentials::BDSE::getInstance());