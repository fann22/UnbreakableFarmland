#include "mod/BDSE.h"

#include "gsl/pointers"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/io/LogLevel.h"
#include "ll/api/io/Logger.h"
#include "ll/api/mod/RegisterHelper.h"

#include "ll/api/memory/Hook.h"

// #include "ll/api/event/player/PlayerAttackEvent.h"
#include "ll/api/event/player/PlayerChatEvent.h"
#include "ll/api/event/player/PlayerDieEvent.h"
#include "ll/api/event/player/PlayerDisconnectEvent.h"
#include "ll/api/event/player/PlayerJoinEvent.h"

#include "ll/api/service/Bedrock.h"

#include "ila/event/minecraft/world/actor/MobHealthChangeEvent.h"
#include "ila/event/minecraft/world/level/block/FarmDecayEvent.h"

// #include "mc/deps/shared_types/legacy/LevelSoundEvent.h"
// #include "mc/deps/shared_types/legacy/actor/ActorDamageCause.h"

#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/PlayerListEntry.h"
#include "mc/world/attribute/Attribute.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceRef.h"
#include "mc/world/attribute/BaseAttributeMap.h"

#include "mc/world/level/ILevel.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/storage/LevelData.h"

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

LL_TYPE_INSTANCE_HOOK(
    PlayerAddLevelHook,
    ll::memory::HookPriority::Normal,
    Player,
    &Player::$addLevels,
    void,
    int lvl
) {
    BDSE&       bdse        = BDSE::getInstance();
    Scoreboard* scoreboard  = bdse.getScoreboard();
    Objective*  xpObjective = bdse.getXPObjective();
    
    BaseAttributeMap&    attrMap = const_cast<BaseAttributeMap&>(*this->getAttributes());
    AttributeInstanceRef ref     = attrMap.getMutableInstance(Player::LEVEL().mIDValue);
    int                  fixLvl  = lvl;
    if (fixLvl < 0) fixLvl = 0; // Might be negative, we don't want that.
    int                  newLvl  = int(ref.mPtr->mCurrentValue) + fixLvl;

    if (scoreboard && xpObjective) {
        ScoreboardId const& id = scoreboard->getScoreboardId(*this); // *this = Player
        if (id != ScoreboardId::INVALID()) {
            ScoreboardOperationResult result;
            scoreboard->modifyPlayerScore(result, id, *xpObjective, newLvl, PlayerScoreSetFunction::Add);
        }
    }

    origin(lvl);
}

LL_TYPE_INSTANCE_HOOK(
    AchievementsWillBeDisabledHook,
    ll::memory::HookPriority::Normal,
    LevelData,
    &LevelData::achievementsWillBeDisabledOnLoad,
    bool
) {
    return false; // fuck you.
}

LL_TYPE_INSTANCE_HOOK(
    DisableAchievementsHook,
    ll::memory::HookPriority::Normal,
    LevelData,
    &LevelData::disableAchievements,
    void
) {
    // 🦗...
}

const ScoreboardId* getOrCreateScoreboardId(Player& player) {
    Scoreboard *scoreboard = BDSE::getInstance().getScoreboard();
    if (!scoreboard) return nullptr;

    ScoreboardId const* id = &scoreboard->getScoreboardId(player);
    if (*id == ScoreboardId::INVALID()) {
        id = &scoreboard->createScoreboardId(player);
    }
    return id;
}

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
    ll::service::getLevel()->getLevelData().mAchievementsDisabled = false;
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
    mScoreboard->setDisplayObjective(Scoreboard::DISPLAY_SLOT_BELOWNAME(), *mHealthObjective, ObjectiveSortOrder::Descending);

    mXPObjective = mScoreboard->addObjective("MostLVL", "•> Most Level <•", *criteria);
    mScoreboard->setDisplayObjective(Scoreboard::DISPLAY_SLOT_SIDEBAR(), *mXPObjective, ObjectiveSortOrder::Descending);

    AchievementsWillBeDisabledHook::hook();
    DisableAchievementsHook::hook();
    PlayerAddLevelHook::hook();

    auto& bus = ll::event::EventBus::getInstance();

    gListeners.insert(
        gListeners.begin(),
        bus.emplaceListener<ila::mc::FarmDecayBeforeEvent>([](ila::mc::FarmDecayBeforeEvent& event) { event.cancel(); })
    );

    gListeners.insert(
        gListeners.begin(),
        bus.emplaceListener<ll::event::PlayerJoinEvent>([this](ll::event::PlayerJoinEvent& event) {
            Player&              player  = event.self();
            BaseAttributeMap&    attrMap = const_cast<BaseAttributeMap&>(*player.getAttributes());
            AttributeInstanceRef ref     = attrMap.getMutableInstance(Player::LEVEL().mIDValue);
            float                lvl     = ref.mPtr->mCurrentValue;

            ScoreboardId const* id = getOrCreateScoreboardId(player);

            ScoreboardOperationResult result;
            mScoreboard->modifyPlayerScore(result, *id, *mHealthObjective, player.getHealth(), PlayerScoreSetFunction::Set);
            mScoreboard->modifyPlayerScore(result, *id, *mXPObjective, int(lvl), PlayerScoreSetFunction::Set);
        })
    );

    gListeners.insert(
        gListeners.begin(),
        bus.emplaceListener<ll::event::PlayerDisconnectEvent>([this](ll::event::PlayerDisconnectEvent& event) {
            ScoreboardId const* id = getOrCreateScoreboardId(event.self());
            if (id != nullptr) mScoreboard->resetPlayerScore(*id);
        })
    );

    gListeners.insert(
        gListeners.begin(),
        bus.emplaceListener<ll::event::PlayerDieEvent>([this](ll::event::PlayerDieEvent& event) {
            ScoreboardId const* id = getOrCreateScoreboardId(event.self());
            if (id == nullptr) return;

            ScoreboardOperationResult result;
            mScoreboard->modifyPlayerScore(
                result, *id,
                *mXPObjective,
                0,
                PlayerScoreSetFunction::Set
            );
        })
    );

    gListeners.insert(
        gListeners.begin(),
        bus.emplaceListener<ila::mc::MobHealthChangeAfterEvent>([this](ila::mc::MobHealthChangeAfterEvent& event) {
            if (!event.self().isPlayer() || event.newValue() > float(event.self().getMaxHealth())) return;
            // BDSE::getInstance().getSelf().getLogger().info("{} -> {}", event.oldValue(), event.newValue());

            ScoreboardId const* id = getOrCreateScoreboardId(static_cast<Player&>(event.self()));
            if (id == nullptr) return;

            ScoreboardOperationResult result;
            mScoreboard->modifyPlayerScore(
                result,
                *id,
                *mHealthObjective,
                int((std::round(event.newValue() * 2) / 2) * 2), 
                PlayerScoreSetFunction::Set
            );
        })
    );

    /*gListeners.insert(
        gListeners.begin(),
        bus.emplaceListener<ll::event::PlayerAttackEvent>([](ll::event::PlayerAttackEvent& event) {
            BDSE::getInstance().getSelf().getLogger().info("Damage source: {}", event.cause());
            if (event.cause() == SharedTypes::Legacy::ActorDamageCause::Projectile) {
                Actor& player = event.self();
                player.playSynchronizedSound(SharedTypes::Legacy::LevelSoundEvent::LevelUp, player.getPosition(), 0, true);
            }
        })
    );*/

    gListeners.insert(
        gListeners.begin(),
        bus.emplaceListener<ll::event::PlayerChatEvent>([](ll::event::PlayerChatEvent& event) {
            BDSE::getInstance().getSelf().getLogger().info("{}: {}", event.self().getRealName(), event.message());
            for (auto& [uuid, entry] : ll::service::getLevel()->getPlayerList()) {
                Player* player = ll::service::getLevel()->getPlayer(uuid);
                if (player) {
                    auto message = "§b" + event.self().getRealName() + "§f: " + event.message();
                    player->sendMessage(message);
                }
            }
            event.cancel();
        })
    );

    getSelf().getLogger().info("loaded.");
    return true;
}

bool BDSE::disable() {
    AchievementsWillBeDisabledHook::unhook();
    DisableAchievementsHook::unhook();
    PlayerAddLevelHook::unhook();

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