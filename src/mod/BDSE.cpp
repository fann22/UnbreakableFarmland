#include "mod/BDSE.h"

#include "freeCamera/FreeCamera.h"
#include "gsl/pointers"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/io/LogLevel.h"
#include "ll/api/io/Logger.h"
#include "ll/api/mod/RegisterHelper.h"

#include "ll/api/memory/Hook.h"

#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"

#include "ll/api/event/entity/ActorHurtEvent.h"

#include "ll/api/event/player/PlayerChatEvent.h"
#include "ll/api/event/player/PlayerDieEvent.h"
#include "ll/api/event/player/PlayerDisconnectEvent.h"
#include "ll/api/event/player/PlayerJoinEvent.h"

#include "ll/api/thread/ThreadPoolExecutor.h"

#include "ll/api/service/Bedrock.h"

#include "ila/event/minecraft/server/ServerPongEvent.h"
#include "ila/event/minecraft/server/SendPacketEvent.h"

#include "ila/event/minecraft/world/actor/MobHealthChangeEvent.h"
#include "ila/event/minecraft/world/level/block/FarmDecayEvent.h"

#include "mc/platform/Result.h"
#include "mc/deps/core/utility/ReadOnlyBinaryStream.h"
#include "mc/deps/shared_types/legacy/actor/ActorDamageCause.h"

#include "mc/server/ServerInstance.h"
#include "mc/server/commands/CommandOutput.h"

#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/player/PlayerListEntry.h"
#include "mc/world/attribute/Attribute.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceRef.h"
#include "mc/world/attribute/BaseAttributeMap.h"

#include "mc/world/item/Item.h"
#include "mc/world/item/registry/ItemRegistryRef.h"
#include "mc/world/level/ILevel.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/registry/BlockTypeRegistry.h"
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

#include "mc/network/ServerNetworkHandler.h"
#include "mc/network/NetEventCallback.h"
#include "mc/network/NetworkIdentifier.h"
#include "mc/network/packet/ActorEventPacket.h"
#include "mc/network/packet/UpdateBlockPacket.h"
#include "mc/network/packet/UpdateBlockSyncedPacket.h"
#include "mc/network/packet/TextPacket.h"
#include "mc/network/packet/PlayerSkinPacket.h"
#include "mc/network/packet/PlaySoundPacket.h"
#include "mc/network/packet/PlaySoundPacketPayload.h"

namespace bds_essentials {

LL_AUTO_TYPE_INSTANCE_HOOK(
    NetEventCallbackHook,
    ll::memory::HookPriority::Normal,
    NetEventCallback,
    &NetEventCallback::$handle,
    void,
    NetworkIdentifier const& id,
    std::shared_ptr<::UpdateBlockPacket> pkt
) {
    if (pkt) {
        BDSE::getInstance().getSelf().getLogger().info("Sending packet with mRuntimeId: {}", pkt->mRuntimeId);
        BlockTypeRegistry* blockReg = ll::service::getLevel()->getBlockTypeRegistry().get();
        Block const& glass = blockReg->getDefaultBlockState("minecraft:glass");
        if (pkt->mRuntimeId == glass.computeRawSerializationIdHashForNetwork()) {
            Block const& glass = blockReg->getDefaultBlockState("minecraft:dirt");
            pkt->mRuntimeId = glass.computeRawSerializationIdHashForNetwork();
        }
    }

    origin(id, pkt);
}
LL_AUTO_TYPE_INSTANCE_HOOK(
    NetEventCallbackHook2,
    ll::memory::HookPriority::Normal,
    NetEventCallback,
    &NetEventCallback::$handle,
    void,
    NetworkIdentifier const& id,
    std::shared_ptr<::UpdateBlockSyncedPacket> pkt
) {
    if (pkt) {
        BDSE::getInstance().getSelf().getLogger().info("Sending packet with mRuntimeId: {}", pkt->mRuntimeId);
        BlockTypeRegistry* blockReg = ll::service::getLevel()->getBlockTypeRegistry().get();
        Block const& glass = blockReg->getDefaultBlockState("minecraft:glass");
        if (pkt->mRuntimeId == glass.computeRawSerializationIdHashForNetwork()) {
            Block const& glass = blockReg->getDefaultBlockState("minecraft:dirt");
            pkt->mRuntimeId = glass.computeRawSerializationIdHashForNetwork();
        }
    }

    origin(id, pkt);
}

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
    int                  fixLvl  = std::max(0, int(ref.mPtr->mCurrentValue) + lvl);
    // using std::max() cuz the value might be negative, we definitely don't want that to happend.

    if (scoreboard && xpObjective) {
        ScoreboardId const& id = scoreboard->getScoreboardId(*this); // *this == Player
        if (id != ScoreboardId::INVALID()) {
            ScoreboardOperationResult result;
            scoreboard->modifyPlayerScore(
                result,
                id,
                *xpObjective,
                fixLvl,
                PlayerScoreSetFunction::Set
            );
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
static std::vector<std::string> gMotdMessages = {
    "•> 𝗔𝗻𝗼𝗺𝗮𝗹𝘆 𝗦𝘂𝗿𝘃𝗶𝘃𝗮𝗹! <•",
    "•> 𝗗𝗘𝗔𝗗 = 𝗚𝗔𝗬 <•",
    "•> 𝗩𝗮𝗻𝗶𝗹𝗹𝗮 𝗦𝗲𝗿𝘃𝗲𝗿 <•",
    "•> 𝟭𝟬𝟰.𝟮𝟰𝟴.𝟭𝟱𝟰.𝟮𝟯𝟬 <•"
};
static std::atomic<int>  gMotdIndex = 0;
static std::atomic<bool> gRunning   = false;

bool BDSE::load() {
    // getSelf().getLogger().debug("Loading...");
    // Code for loading the mod goes here.
    return true;
}

bool BDSE::enable() {
    gRunning = true;
    ll::thread::ThreadPoolExecutor::getDefault().execute([]() {
        while (gRunning) {
            gMotdIndex = (gMotdIndex + 1) % gMotdMessages.size();
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        }
    });

    freeCamera::FreeCameraManager::freecameraHook(true);
    auto& cmd = ll::command::CommandRegistrar::getInstance(false).getOrCreateCommand("freecamera", "Toggle freecam");
    ll::service::getCommandRegistry()->registerAlias("freecamera", "fc");
    cmd.overload().execute([&](CommandOrigin const& origin, CommandOutput& output) {
            auto entity = origin.getEntity();
            if (entity == nullptr || !entity->isType(ActorType::Player)) {
                output.error("Only players can run this command.");
                return;
            }
            auto* player = ll::service::getLevel()->getPlayer(entity->getOrCreateUniqueID());
            if (!player) {
                output.error("Didn't found the target player.");
                return;
            }
            auto guid = player->getNetworkIdentifier().mGuid.g;
            if (!freeCamera::FreeCameraManager::getInstance().FreeCamList.count(guid)) {
                freeCamera::FreeCameraManager::EnableFreeCamera(player);
                output.success("FreeCamera enabled.");
            } else {
                freeCamera::FreeCameraManager::DisableFreeCamera(player);
                output.success("FreeCamera disabled.");
            }
            return;
        }
    );

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
        bus.emplaceListener<ila::mc::ServerPongBeforeEvent>(
            [](ila::mc::ServerPongBeforeEvent& event) {
                event.motd() = gMotdMessages[gMotdIndex];
            }
        )
    );

    gListeners.insert(
        gListeners.begin(),
        bus.emplaceListener<ll::event::PlayerJoinEvent>([this](ll::event::PlayerJoinEvent& event) {
            Player&              player  = event.self();
            BaseAttributeMap&    attrMap = const_cast<BaseAttributeMap&>(*player.getAttributes());
            AttributeInstanceRef ref     = attrMap.getMutableInstance(Player::LEVEL().mIDValue);
            int                  lvl     = static_cast<int>(ref.mPtr->mCurrentValue);

            ScoreboardId const* id = getOrCreateScoreboardId(player);

            ScoreboardOperationResult result;
            mScoreboard->modifyPlayerScore(result, *id, *mHealthObjective, player.getHealth(), PlayerScoreSetFunction::Set);
            mScoreboard->modifyPlayerScore(result, *id, *mXPObjective, lvl, PlayerScoreSetFunction::Set);
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
                static_cast<int>(std::ceil(event.newValue())), 
                PlayerScoreSetFunction::Set
            );
        })
    );

    gListeners.insert(
        gListeners.begin(),
        bus.emplaceListener<ll::event::ActorHurtEvent>([](ll::event::ActorHurtEvent& event) {
            if (event.source().mCause == SharedTypes::Legacy::ActorDamageCause::Projectile) {
                Player* player = ll::service::getLevel()->getPlayer(event.source().getEntityUniqueID());
                if (!player) return;

                PlaySoundPacket packet(PlaySoundPacketPayload(
                    "random.orb",
                    player->getPosition(),
                    0.8f, // volume
                    1.2f  // pitch
                ));
                player->sendNetworkPacket(packet);
            }
        })
    );

    gListeners.insert(
        gListeners.begin(),
        bus.emplaceListener<ll::event::PlayerChatEvent>([](ll::event::PlayerChatEvent& event) {
            BDSE::getInstance().getSelf().getLogger().info("{}: {}", event.self().getRealName(), event.message());
            auto message = "§b" + event.self().getRealName() + "§f: " + event.message();
            TextPacket::createRawMessage(message).sendToClients();
            event.cancel();
        })
    );

    // getSelf().getLogger().info("loaded.");
    return true;
}

bool BDSE::disable() {
    gRunning = false;

    freeCamera::FreeCameraManager::freecameraHook(false);
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