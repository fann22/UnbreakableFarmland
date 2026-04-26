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

// ============================================================
//  Chunk border state
// ============================================================

static std::atomic<uint64_t>                                         sNextShapeId{UINT64_MAX};
static std::unordered_map<unsigned long long, std::pair<int,int>>    gLastChunk;
static std::unordered_map<unsigned long long, std::vector<uint64_t>> gShapeIds;
static std::unordered_set<unsigned long long>                        ChunkBorderList;
static std::mutex                                                    gChunkBorderMtx;

// Counts players with chunk border active.
// The loop thread checks this — when it hits 0, the thread exits naturally.
static std::atomic<int>  gChunkBorderCount{0};

// FIX #1: flag to prevent spawning duplicate loop threads.
// compare_exchange_strong ensures only one thread can transition false→true.
static std::atomic<bool> gLoopRunning{false};

// sendDeletePacket — kirim packet hapus shapes ke client berdasarkan vector IDs.
// Tidak menyentuh gShapeIds sama sekali; ownership sepenuhnya di caller.
static void sendDeletePacket(Player& player, std::vector<uint64_t> const& ids) {
    if (ids.empty()) return;
    DebugDrawerPacket pkt;
    pkt.setSerializationMode(SerializationMode::CerealOnly);
    for (auto id : ids) {
        ShapeDataPayload shape;
        shape.mNetworkId = id;
        shape.mShapeType = std::nullopt; // nullopt = hapus shape di client
        pkt.mShapes->emplace_back(std::move(shape));
    }
    pkt.sendTo(player);
}

// removeChunkBorder — ambil IDs dari gShapeIds, kirim delete packet, lalu erase entry.
// Aman dipanggil dari server thread kapanpun (acquire lock sendiri).
void removeChunkBorder(Player& player) {
    auto guid = player.getNetworkIdentifier().mGuid.g;

    std::vector<uint64_t> ids;
    {
        std::lock_guard lock(gChunkBorderMtx);
        auto it = gShapeIds.find(guid);
        if (it == gShapeIds.end()) return;
        ids = std::move(it->second); // ambil ownership
        gShapeIds.erase(it);         // hapus dari map — sebelum unlock
    }
    // Lock sudah dilepas, aman untuk I/O
    sendDeletePacket(player, ids);
}

void updateChunkBorder(Player& player) {
    Vec3 pos    = player.getPosition();
    int  chunkX = (int)std::floor(pos.x / 16);
    int  chunkZ = (int)std::floor(pos.z / 16);
    auto guid   = player.getNetworkIdentifier().mGuid.g;

    // Cek apakah player masih di chunk yang sama.
    // Sekaligus ambil IDs lama untuk dihapus — semua dalam satu lock,
    // sehingga tidak ada window antara "cek chunk" dan "ambil IDs lama".
    std::vector<uint64_t> oldIds;
    {
        std::lock_guard lock(gChunkBorderMtx);
        auto& lastChunk = gLastChunk[guid];
        if (lastChunk == std::make_pair(chunkX, chunkZ)) return; // belum pindah chunk
        lastChunk = {chunkX, chunkZ};

        // Ambil ownership IDs lama sekarang, saat masih di bawah lock
        auto it = gShapeIds.find(guid);
        if (it != gShapeIds.end()) {
            oldIds = std::move(it->second);
            gShapeIds.erase(it);
        }
    }

    // Kirim delete packet shapes lama — lock sudah dilepas
    sendDeletePacket(player, oldIds);

    // Bangun shapes baru
    float minX = (chunkX * 16);
    float minZ = (chunkZ * 16);
    float maxX = (chunkX * 16) + 16.0f;
    float maxZ = (chunkZ * 16) + 16.0f;
    float minY = -64.0f;
    float maxY = 320.0f;

    auto dimId = player.getDimension().mId;

    DebugDrawerPacket pkt;
    pkt.setSerializationMode(SerializationMode::CerealOnly);
    std::vector<uint64_t> newIds;

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
        newIds.push_back(id);
    };

    // North & South walls (sweep X)
    for (float x = minX; x <= maxX; x += 2.0f) {
        addLine({x, minY, minZ}, {x, maxY, minZ});
        addLine({x, minY, maxZ}, {x, maxY, maxZ});
    }

    // West & East walls (sweep Z)
    for (float z = minZ; z <= maxZ; z += 2.0f) {
        addLine({minX, minY, z}, {minX, maxY, z});
        addLine({maxX, minY, z}, {maxX, maxY, z});
    }

    // Horizontal rings every 2 blocks
    for (float y = minY; y <= maxY; y += 2.0f) {
        addLine({minX, y, minZ}, {maxX, y, minZ}); // North
        addLine({minX, y, maxZ}, {maxX, y, maxZ}); // South
        addLine({minX, y, minZ}, {minX, y, maxZ}); // West
        addLine({maxX, y, minZ}, {maxX, y, maxZ}); // East
    }

    // Kirim shapes baru ke client dulu, baru simpan IDs-nya.
    // Urutan ini penting: kalau server crash setelah sendTo tapi sebelum store,
    // IDs hilang dan tidak ada leak di sisi client (shapes akan expire sendiri).
    pkt.sendTo(player);

    {
        std::lock_guard lock(gChunkBorderMtx);
        gShapeIds[guid] = std::move(newIds);
    }
}

// startChunkBorderLoop — dispatch tick ke server thread setiap 40ms.
// Tidak pakai future.wait() karena ServerThreadExecutor adalah single-threaded
// tick loop; memblok pool thread sambil nunggu server thread = deadlock.
// Solusi: pakai atomic flag gTickPending untuk skip dispatch kalau tick
// sebelumnya belum selesai — mencegah task numpuk tanpa blocking.
static std::atomic<bool> gTickPending{false};

void startChunkBorderLoop() {
    bool expected = false;
    if (!gLoopRunning.compare_exchange_strong(expected, true)) return;

    ll::thread::ThreadPoolExecutor::getDefault().execute([]() {
        while (gChunkBorderCount > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(40));

            // Skip kalau tick sebelumnya masih diproses server thread.
            // Ini mencegah task numpuk di queue tanpa perlu blocking.
            bool tickExpected = false;
            if (!gTickPending.compare_exchange_strong(tickExpected, true)) continue;

            ll::thread::ServerThreadExecutor::getDefault().execute([]() {
                std::vector<unsigned long long> active;
                {
                    std::lock_guard lock(gChunkBorderMtx);
                    active.assign(ChunkBorderList.begin(), ChunkBorderList.end());
                }
                for (auto guid : active) {
                    Player* player = ll::service::getLevel()->getPlayer(guid);
                    if (!player) continue;
                    try {
                        updateChunkBorder(*player);
                    } catch (std::exception& e) {
                        BDSE::getInstance().getSelf().getLogger().error("chunkBorder: {}", e.what());
                    }
                }
                gTickPending = false; // tandai tick selesai, boleh dispatch berikutnya
            });
        }
        gLoopRunning  = false;
        gTickPending  = false;
    });
}

// ============================================================
//  Hooks
// ============================================================

// Sync XP level to the sidebar scoreboard whenever a player gains levels.
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
    // using std::max() cuz the value might be negative, we definitely don't want that to happen.

    if (scoreboard && xpObjective) {
        ScoreboardId const& id = scoreboard->getScoreboardId(*this);
        if (id != ScoreboardId::INVALID()) {
            ScoreboardOperationResult result;
            scoreboard->modifyPlayerScore(result, id, *xpObjective, fixLvl, PlayerScoreSetFunction::Set);
        }
    }

    origin(lvl);
}

// Prevent achievements from being flagged as disabled on world load.
LL_TYPE_INSTANCE_HOOK(
    AchievementsWillBeDisabledHook,
    ll::memory::HookPriority::Normal,
    LevelData,
    &LevelData::achievementsWillBeDisabledOnLoad,
    bool
) {
    return false;
}

// Swallow the disable call entirely — achievements stay enabled.
LL_TYPE_INSTANCE_HOOK(
    DisableAchievementsHook,
    ll::memory::HookPriority::Normal,
    LevelData,
    &LevelData::disableAchievements,
    void
) {}

// ============================================================
//  Helpers
// ============================================================

// Returns (or creates) the ScoreboardId for a player.
const ScoreboardId* getOrCreateScoreboardId(Player& player) {
    Scoreboard *scoreboard = BDSE::getInstance().getScoreboard();
    if (!scoreboard) return nullptr;

    ScoreboardId const* id = &scoreboard->getScoreboardId(player);
    if (*id == ScoreboardId::INVALID())
        id = &scoreboard->createScoreboardId(player);

    return id;
}

// ============================================================
//  BDSE lifecycle
// ============================================================

BDSE& BDSE::getInstance() {
    static BDSE instance;
    return instance;
}

static std::vector<ll::event::ListenerPtr> gListeners;

// MOTD messages cycled on every server ping.
// NOTE: if you want per-ping randomization instead of sequential rotation,
// replace gMotdIndex logic with: rand() % gMotdMessages.size()
static std::vector<std::string> gMotdMessages = {
    "•> 𝗩𝗮𝗻𝗶𝗹𝗹𝗮 𝗦𝗲𝗿𝘃𝗲𝗿 <•",
    "•> play.anomaly.bond <•",
    "•> PORT: 25600 <•"
};
// Rotated by a background thread every 1.5 s — read on ping event (different thread),
// so atomic is the right tool here.
static std::atomic<int>  gMotdIndex{0};
static std::atomic<bool> gRunning{false};

bool BDSE::load() {
    return true;
}

bool BDSE::enable() {
    gRunning = true;

    // Background: rotate MOTD index every 1.5 s.
    // The actual string is only read on ServerPongBeforeEvent (server thread),
    // so only the index needs to be atomic — the vector itself is never mutated at runtime.
    ll::thread::ThreadPoolExecutor::getDefault().execute([]() {
        while (gRunning) {
            gMotdIndex = (gMotdIndex + 1) % (int)gMotdMessages.size();
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        }
    });

    // ── Commands ─────────────────────────────────────────────

    freeCamera::FreeCameraManager::freecameraHook(true);
    auto& cmd = ll::command::CommandRegistrar::getInstance(false).getOrCreateCommand("freecamera", "Toggle freecam.");
    ll::service::getCommandRegistry()->registerAlias("freecamera", "fc");
    cmd.overload().execute([&](CommandOrigin const& origin, CommandOutput& output) {
        auto entity = origin.getEntity();
        if (entity == nullptr || !entity->isType(ActorType::Player)) {
            output.error("§4Only players can run this command.");
            return;
        }
        auto* player = ll::service::getLevel()->getPlayer(entity->getOrCreateUniqueID());
        if (!player) {
            output.error("§4Didn't found the target player.");
            return;
        }
        auto guid = player->getNetworkIdentifier().mGuid.g;
        if (!freeCamera::FreeCameraManager::getInstance().FreeCamList.count(guid)) {
            freeCamera::FreeCameraManager::EnableFreeCamera(player);
            output.success("§nFree Camera§r §aenabled.");
        } else {
            freeCamera::FreeCameraManager::DisableFreeCamera(player);
            output.success("§nFree Camera§r §4disabled.");
        }
        return;
    });

    auto& cmd2 = ll::command::CommandRegistrar::getInstance(false).getOrCreateCommand("chunkborder", "Toggle chunk border.");
    ll::service::getCommandRegistry()->registerAlias("chunkborder", "cb");
    cmd2.overload().execute([&](CommandOrigin const& origin, CommandOutput& output) {
        auto entity = origin.getEntity();
        if (entity == nullptr || !entity->isType(ActorType::Player)) {
            output.error("§4Only players can run this command.");
            return;
        }
        auto* player = ll::service::getLevel()->getPlayer(entity->getOrCreateUniqueID());
        if (!player) {
            output.error("§4Didn't found the target player.");
            return;
        }
        auto guid = player->getNetworkIdentifier().mGuid.g;
        if (!ChunkBorderList.count(guid)) {
            {
                std::lock_guard lock(gChunkBorderMtx);
                ChunkBorderList.insert(guid);
            }
            if (++gChunkBorderCount == 1) startChunkBorderLoop();
            updateChunkBorder(*player);
            output.success("§nChunk border§r §aenabled.");
        } else {
            // FIX #2: call removeChunkBorder BEFORE erasing gShapeIds,
            // otherwise removeChunkBorder finds an empty map and sends no
            // delete packet — leaving the lines visible on the client.
            removeChunkBorder(*player); // sends delete packet & erases gShapeIds[guid]
            {
                std::lock_guard lock(gChunkBorderMtx);
                ChunkBorderList.erase(guid);
                gLastChunk.erase(guid);
                // gShapeIds[guid] already erased inside removeChunkBorder
            }
            --gChunkBorderCount;
            output.success("§nChunk border§r §4disabled.");
        }
        return;
    });

    // ── Scoreboard setup ─────────────────────────────────────

    ll::service::getLevel()->getLevelData().mAchievementsDisabled = false;
    mScoreboard = &ll::service::getLevel()->getScoreboard();

    // Clear any pre-existing objectives to start fresh.
    auto objectives = mScoreboard->getObjectives();
    for (auto* obj : objectives)
        mScoreboard->removeObjective(const_cast<Objective*>(obj));

    ObjectiveCriteria* criteria = mScoreboard->getCriteria(Scoreboard::DEFAULT_CRITERIA());
    if (!criteria) {
        criteria = const_cast<ObjectiveCriteria*>(
            &mScoreboard->createObjectiveCriteria(
                Scoreboard::DEFAULT_CRITERIA(), false, ::ObjectiveRenderType::Integer));
    }

    mHealthObjective = mScoreboard->addObjective("PlayerHealth", "❤", *criteria);
    mScoreboard->setDisplayObjective(
        Scoreboard::DISPLAY_SLOT_BELOWNAME(), *mHealthObjective, ObjectiveSortOrder::Descending);

    mXPObjective = mScoreboard->addObjective("MostLVL", "•> Most Level <•", *criteria);
    mScoreboard->setDisplayObjective(
        Scoreboard::DISPLAY_SLOT_SIDEBAR(), *mXPObjective, ObjectiveSortOrder::Descending);

    // ── Feature hooks ─────────────────────────────────────────

    features::fast_leaf_decay::enable();
    AchievementsWillBeDisabledHook::hook();
    DisableAchievementsHook::hook();
    PlayerAddLevelHook::hook();

    // ── Event listeners ───────────────────────────────────────

    auto& bus = ll::event::EventBus::getInstance();

    // Prevent farmland from decaying.
    gListeners.insert(
        gListeners.begin(),
        bus.emplaceListener<ila::mc::FarmDecayBeforeEvent>(
            [](ila::mc::FarmDecayBeforeEvent& event) { event.cancel(); }));

    // Rotate MOTD text.
    gListeners.insert(
        gListeners.begin(),
        bus.emplaceListener<ila::mc::ServerPongBeforeEvent>(
            [](ila::mc::ServerPongBeforeEvent& event) {
                event.motd() = gMotdMessages[gMotdIndex];
            }));

    // On join: populate health and XP scores.
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
            mScoreboard->modifyPlayerScore(result, *id, *mXPObjective,     lvl,                PlayerScoreSetFunction::Set);
        }));

    // On disconnect: remove scores and clean up chunk border state.
    gListeners.insert(
        gListeners.begin(),
        bus.emplaceListener<ll::event::PlayerDisconnectEvent>([this](ll::event::PlayerDisconnectEvent& event) {
            ScoreboardId const* id = getOrCreateScoreboardId(event.self());
            if (id != nullptr) mScoreboard->resetPlayerScore(*id);

            auto guid = event.self().getNetworkIdentifier().mGuid.g;

            // FIX #2: removeChunkBorder first (needs gShapeIds intact),
            // then erase bookkeeping data.
            bool hadBorder = false;
            {
                std::lock_guard lock(gChunkBorderMtx);
                hadBorder = ChunkBorderList.erase(guid) > 0;
                gLastChunk.erase(guid);
            }
            if (hadBorder) {
                removeChunkBorder(event.self()); // erases gShapeIds[guid] internally
                --gChunkBorderCount;
            }
        }));

    // On death: reset XP score to 0.
    gListeners.insert(
        gListeners.begin(),
        bus.emplaceListener<ll::event::PlayerDieEvent>([this](ll::event::PlayerDieEvent& event) {
            ScoreboardId const* id = getOrCreateScoreboardId(event.self());
            if (id == nullptr) return;

            ScoreboardOperationResult result;
            mScoreboard->modifyPlayerScore(result, *id, *mXPObjective, 0, PlayerScoreSetFunction::Set);
        }));

    // Keep health score in sync whenever HP changes.
    gListeners.insert(
        gListeners.begin(),
        bus.emplaceListener<ila::mc::MobHealthChangeAfterEvent>([this](ila::mc::MobHealthChangeAfterEvent& event) {
            if (!event.self().isPlayer() || event.newValue() > float(event.self().getMaxHealth())) return;

            ScoreboardId const* id = getOrCreateScoreboardId(static_cast<Player&>(event.self()));
            if (id == nullptr) return;

            ScoreboardOperationResult result;
            mScoreboard->modifyPlayerScore(
                result, *id, *mHealthObjective,
                static_cast<int>(std::ceil(event.newValue())),
                PlayerScoreSetFunction::Set);
        }));

    // Play a sound for the shooter when their projectile hits something.
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
        }));

    // Intercept chat: log, reformat with colour, and broadcast.
    gListeners.insert(
        gListeners.begin(),
        bus.emplaceListener<ll::event::PlayerChatEvent>([](ll::event::PlayerChatEvent& event) {
            BDSE::getInstance().getSelf().getLogger().info("{}: {}", event.self().getRealName(), event.message());
            auto message = "§b" + event.self().getRealName() + "§f: " + event.message();
            TextPacket::createRawMessage(message).sendToClients();
            event.cancel();
        }));

    return true;
}

bool BDSE::disable() {
    gRunning = false;

    // Force chunk border count to 0 so the loop thread exits on next iteration.
    gChunkBorderCount = 0;
    {
        std::lock_guard lock(gChunkBorderMtx);
        ChunkBorderList.clear();
        gLastChunk.clear();
        gShapeIds.clear();
    }

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