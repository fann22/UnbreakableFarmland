// from https://github.com/CoralFans-Dev/CoralFans
// from https://github.com/GroupMountain/FreeCamera

#include "FreeCamera.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/memory/Memory.h"
#include "ll/api/service/Bedrock.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/network/NetworkPeer.h"
#include "mc/network/ServerNetworkHandler.h"
#include "mc/network/packet/AddPlayerPacket.h"
#include "mc/network/packet/PlayerSkinPacket.h"
#include "mc/network/packet/RemoveActorPacket.h"
#include "mc/network/packet/UpdateAbilitiesPacket.h"
#include "mc/network/packet/UpdatePlayerGameTypePacket.h"
#include "mc/platform/UUID.h"
#include "mc/server/ServerPlayer.h"
#include "mc/world/actor/Actor.h "
#include "mc/world/actor/player/SerializedSkinRef.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Tick.h"

#include "gmlib/mc/network/BinaryStream.h"
#include "gmlib/mc/world/actor/Player.h"

namespace bds_essentials::freeCamera {

void EnableFreeCameraPacket(Player* pl) {
    auto pkt            = UpdatePlayerGameTypePacket();
    pkt.mPlayerGameType = GameType::Spectator;
    pkt.mTargetPlayer   = pl->getOrCreateUniqueID();
    pkt.mTick->mValue   = ll::service::getLevel()->getCurrentTick().tickID;
    pkt.sendTo(*pl);
}

void SendFakePlayerPacket(Player* pl) {
    auto pkt1             = AddPlayerPacket(*pl);
    pkt1.mEntityId->rawID = pkt1.mEntityId->rawID + 114514;
    auto randomUuid       = mce::UUID::random();
    pkt1.mUuid            = randomUuid;
    pl->sendNetworkPacket(pkt1);

    auto& ref = *pl->mSkin;
    if (!ref.mSkinImpl) return;

    auto& skinImpl = **ref.mSkinImpl;
    gmlib::GMBinaryStream bs;
    bs.writePacketHeader(MinecraftPacketIds::PlayerSkin);
    bs.writeUuid(randomUuid);
    bs.writeSkin(skinImpl);
    bs.writeString("");
    bs.writeString("");
    bs.writeBool(true);
    bs.sendTo(
        *(gmlib::GMPlayer*)pl,
        NetworkPeer::Reliability::ReliableOrdered,
        Compressibility::Compressible
    );
}

void DisableFreeCameraPacket(Player* pl) {
    auto pkt            = UpdatePlayerGameTypePacket();
    pkt.mPlayerGameType = pl->getPlayerGameType();
    pkt.mTargetPlayer   = pl->getOrCreateUniqueID();
    pkt.mTick->mValue   = ll::service::getLevel()->getCurrentTick().tickID;
    pkt.sendTo(*pl);

    auto uniqueId  = pl->getOrCreateUniqueID();
    uniqueId.rawID = uniqueId.rawID + 114514;

    auto pkt2      = RemoveActorPacket();
    pkt2.mEntityId = uniqueId;
    pkt2.sendTo(*pl);
    UpdateAbilitiesPacket(pl->getOrCreateUniqueID(), pl->getAbilities()).sendTo(*pl);
}

void FreeCameraManager::EnableFreeCamera(Player* pl) {
    FreeCameraManager::getInstance().FreeCamList.insert(pl->getNetworkIdentifier().mGuid.g);
    EnableFreeCameraPacket(pl);
    SendFakePlayerPacket(pl);
}

void FreeCameraManager::DisableFreeCamera(Player* pl) {
    auto pos   = pl->getFeetPos();
    auto dimid = pl->getDimensionId();

    FreeCameraManager::getInstance().FreeCamList.erase(pl->getNetworkIdentifier().mGuid.g);
    DisableFreeCameraPacket(pl);
    pl->teleport(pos, dimid);
}

LL_TYPE_INSTANCE_HOOK(
    ServerPlayerMoveHandleEvent,
    ll::memory::HookPriority::Normal,
    ServerNetworkHandler,
    &ServerNetworkHandler::$handle,
    void,
    NetworkIdentifier const&     id,
    PlayerAuthInputPacket const& pkt
) {
    if (FreeCameraManager::getInstance().FreeCamList.contains(id.mGuid.g)) {
        return;
    } else {
        origin(id, pkt);
    }
}

LL_TYPE_INSTANCE_HOOK(
    PlayerGamemodeChangeEvent,
    ll::memory::HookPriority::Normal,
    ServerPlayer,
    &ServerPlayer::$setPlayerGameType,
    void,
    ::GameType gamemode
) {
    origin(gamemode);
    if (FreeCameraManager::getInstance().FreeCamList.contains(getNetworkIdentifier().mGuid.g)) {
        FreeCameraManager::DisableFreeCamera(this);
    }
}

LL_TYPE_INSTANCE_HOOK(
    PlayerHurtEvent,
    ll::memory::HookPriority::Normal,
    Mob,
    &Mob::getDamageAfterResistanceEffect,
    float,
    class ActorDamageSource const& a1,
    float                          a2
) {
    auto res = origin(a1, a2);
    if (this->isType(ActorType::Player) && res != 0) {
        auto pl = (Player*)this;
        if ((pl->isSurvival() || pl->isAdventure())
            && FreeCameraManager::getInstance().FreeCamList.contains(pl->getNetworkIdentifier().mGuid.g)) {
            FreeCameraManager::DisableFreeCamera(pl);
        }
    }
    return res;
}

LL_TYPE_INSTANCE_HOOK(
    PlayerDieEvent,
    ll::memory::HookPriority::Normal,
    Player,
    &Player::$die,
    void,
    class ActorDamageSource const& a1
) {
    if (FreeCameraManager::getInstance().FreeCamList.contains(getNetworkIdentifier().mGuid.g)) {
        FreeCameraManager::DisableFreeCamera(this);
    }
    return origin(a1);
}

LL_TYPE_INSTANCE_HOOK(
    PlayerLeftEvent,
    ll::memory::HookPriority::Normal,
    ServerPlayer,
    &ServerPlayer::disconnect,
    void
) {
    FreeCameraManager::getInstance().FreeCamList.erase(getNetworkIdentifier().mGuid.g);
    return origin();
}

struct Impl {
    ll::memory::HookRegistrar<
        ServerPlayerMoveHandleEvent,
        PlayerGamemodeChangeEvent,
        PlayerHurtEvent,
        PlayerDieEvent,
        PlayerLeftEvent>
        r;
};

std::unique_ptr<Impl> impl;

void FreeCameraManager::freecameraHook(bool enable) {
    if (enable) {
        if (!impl) impl = std::make_unique<Impl>();
    } else {
        impl.reset();
    }
};

} // namespace bds_essentials::freeCamera