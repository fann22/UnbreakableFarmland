#include "mod/BDSE.h"

#include "freeCamera/FreeCamera.h"
#include "gsl/pointers"

#include "features/FastLeafDecay.h"

#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/io/LogLevel.h"
#include "ll/api/io/Logger.h"
#include "ll/api/mod/RegisterHelper.h"

#include "ll/api/memory/Hook.h"

#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"

#include "ll/api/event/entity/ActorHurtEvent.h"

#include "ll/api/event/world/BlockChangedEvent.h"

#include "ll/api/event/player/PlayerChatEvent.h"
#include "ll/api/event/player/PlayerConnectEvent.h"
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
#include "mc/world/level/BlockPos.h"
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

#include <ll/api/chrono/GameChrono.h>
#include <ll/api/thread/ServerThreadExecutor.h>

#include "mc/world/level/dimension/Dimension.h"
#include "mc/network/packet/DebugDrawerPacket.h"
#include "mc/network/packet/ShapeDataPayload.h"
#include "mc/network/packet/LineDataPayload.h"
#include "mc/scripting/modules/minecraft/debugdrawer/ScriptDebugShapeType.h"
#include "mc/network/packet/cerealize/core/SerializationMode.h"

ShapeDataPayload::ShapeDataPayload() { mNetworkId = 0; }

namespace bds_essentials {

static std::atomic<uint64_t> sNextShapeId{UINT64_MAX};
static std::unordered_map<unsigned long long, std::pair<int,int>>        gLastChunk;
static std::unordered_map<unsigned long long, std::vector<uint64_t>>     gShapeIds;

void removeChunkBorder(Player& player) {
    auto guid = player.getNetworkIdentifier().mGuid.g;
    auto it   = gShapeIds.find(guid);
    if (it == gShapeIds.end()) return;

    DebugDrawerPacket pkt;
    pkt.setSerializationMode(SerializationMode::CerealOnly);

    for (auto& id : it->second) {
        ShapeDataPayload shape;
        shape.mNetworkId = id;
        shape.mShapeType = std::nullopt;
        pkt.mShapes->emplace_back(std::move(shape));
    }

    pkt.sendTo(player);
    gShapeIds.erase(guid);
}

void updateChunkBorder(Player& player) {
    Vec3 pos   = player.getPosition();
    int chunkX = (int)std::floor(pos.x / 16);
    int chunkZ = (int)std::floor(pos.z / 16);

    auto guid = player.getNetworkIdentifier().mGuid.g;

    auto it = gLastChunk.find(guid);
    if (it != gLastChunk.end() && it->second == std::make_pair(chunkX, chunkZ)) return;

    removeChunkBorder(player);
    gLastChunk[guid] = {chunkX, chunkZ};

    float minX = (chunkX * 16);
    float minZ = (chunkZ * 16);
    float maxX = (chunkX * 16) + 16.0f;
    float maxZ = (chunkZ * 16) + 16.0f;
    float minY = -64.0f;
    float maxY = 320.0f;

    auto dimId = player.getDimension().mId;

    DebugDrawerPacket pkt;
    pkt.setSerializationMode(SerializationMode::CerealOnly);

    auto addLine = [&](Vec3 const& begin, Vec3 const& end) {
        auto id = sNextShapeId.fetch_sub(1);
        ShapeDataPayload shape;
        shape.mNetworkId        = id;
        shape.mShapeType        = ScriptModuleDebugUtilities::ScriptDebugShapeType::Line;
        shape.mLocation         = begin;
        shape.mColor            = mce::Color::RED();
        shape.mDimensionId      = dimId;
        shape.mExtraDataPayload = LineDataPayload{.mEndLocation = end};
        pkt.mShapes->emplace_back(std::move(shape));
        gShapeIds[guid].push_back(id);
    };

    // North & South (sweep X)
    for (float x = minX; x <= maxX; x += 2.0f) {
        addLine({x, minY, minZ}, {x, maxY, minZ}); // North
        addLine({x, minY, maxZ}, {x, maxY, maxZ}); // South
    }

    // West & East (sweep Z)
    for (float z = minZ; z <= maxZ; z += 2.0f) {
        addLine({minX, minY, z}, {minX, maxY, z}); // West
        addLine({maxX, minY, z}, {maxX, maxY, z}); // East
    }

    // Horizontal rings every 2 block (full height)
    for (float y = minY; y <= maxY; y += 2.0f) {
        addLine({minX, y, minZ}, {maxX, y, minZ}); // North
        addLine({minX, y, maxZ}, {maxX, y, maxZ}); // South
        addLine({minX, y, minZ}, {minX, y, maxZ}); // West
        addLine({maxX, y, minZ}, {maxX, y, maxZ}); // East
    }

    pkt.sendTo(player);
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
    "•> 𝗩𝗮𝗻𝗶𝗹𝗹𝗮 𝗦𝗲𝗿𝘃𝗲𝗿 <•",
    "•> play.anomaly.bond <•",
    "•> PORT: 25600 <•"
};
static std::atomic<int>  gMotdIndex = 0;
static std::atomic<bool> gRunning   = false;
static std::unordered_set<unsigned long long> ChunkBorderList;

bool BDSE::load() {
    // getSelf().getLogger().debug("Loading...");
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
    ll::thread::ThreadPoolExecutor::getDefault().execute([]() {
        while (gRunning) {
            ll::thread::ServerThreadExecutor::getDefault().execute([]() {
                ll::service::getLevel()->forEachPlayer([](Player& player) -> bool {
                    try {
                        auto guid = player.getNetworkIdentifier().mGuid.g;
                        if (ChunkBorderList.count(guid)) updateChunkBorder(player);
                    } catch (std::exception& e) {
                        BDSE::getInstance().getSelf().getLogger().error("drawChunkGrid: {}", e.what());
                    }
                    return true;
                });
            });
    
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
        }
    });

    freeCamera::FreeCameraManager::freecameraHook(true);
    auto& cmd = ll::command::CommandRegistrar::getInstance(false).getOrCreateCommand("freecamera", "Toggle freecam.");
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
    auto& cmd2 = ll::command::CommandRegistrar::getInstance(false).getOrCreateCommand("chunkborder", "Toggle chunk border.");
    ll::service::getCommandRegistry()->registerAlias("chunkborder", "cb");
    cmd2.overload().execute([&](CommandOrigin const& origin, CommandOutput& output) {
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
            if (!ChunkBorderList.count(guid)) {
                ChunkBorderList.insert(guid);
                updateChunkBorder(*player);
                output.success("Chunk border enabled.");
            } else {
                ChunkBorderList.erase(guid);
                removeChunkBorder(*player);
                gLastChunk.erase(guid);
                gShapeIds.erase(guid);
                output.success("Chunk border disabled.");
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

    features::fast_leaf_decay::enable();
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
            auto guid = event.self().getNetworkIdentifier().mGuid.g;
            removeChunkBorder(event.self());
            gLastChunk.erase(guid);
            gShapeIds.erase(guid);
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
                    2.0f, // volume
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

    gListeners.insert(
        gListeners.begin(),
        bus.emplaceListener<ll::event::BlockChangedEvent>([](ll::event::BlockChangedEvent& event) {
            BlockPos const& pos      = event.pos();
            Block const&    newBlock = event.newBlock();

            if (newBlock.getTypeName() == "minecraft:glass") {
                auto const& replacement = Block::tryGetFromRegistry(std::string_view("glass:regular"));
                if (!replacement) return;

                ll::thread::ServerThreadExecutor::getDefault().executeAfter(
                    [pos, replacement](){
                        UpdateBlockPacket pkt;
                        pkt.mPos         = pos;
                        pkt.mLayer       = 0;
                        pkt.mUpdateFlags = 1;
                        pkt.mRuntimeId   = (*replacement).mNetworkId;
                        pkt.sendToClients();
                    },
                    ll::chrono::ticks(4)
                );
            }
        })
    );

    // getSelf().getLogger().info("loaded.");
    return true;
}

bool BDSE::disable() {
    gRunning = false;

    features::fast_leaf_decay::disable();
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