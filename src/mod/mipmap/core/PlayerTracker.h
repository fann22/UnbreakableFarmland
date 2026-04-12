#pragma once
#include "HttpSender.h"
#include "mc/server/ServerPlayer.h"
#include "mc/world/level/Level.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/player/SerializedSkinImpl.h"
#include "mc/world/actor/player/SerializedSkinRef.h"
#include "mc/world/actor/player/SkinImage.h"
#include "mc/util/ThreadOwner.h"
#include <ll/api/service/Bedrock.h>


namespace mipmap {

class PlayerTracker {
public:
    explicit PlayerTracker(HttpSender& sender) : mSender(sender) {}

    void tick() {
        std::string playersJson = "[";
        bool first = true;

        ll::service::getLevel()->forEachPlayer([&](Player& player) -> bool {
            // ambil skin
            auto* sp = static_cast<ServerPlayer*>(&player);
            if (!sp->mSkin) return true;

            SerializedSkinRef& skinRef = *sp->mSkin;         // deref unique_ptr
            if (!skinRef.mSkinImpl) return true;
            SerializedSkinImpl& skin = skinRef.mSkinImpl->mObject;  // ThreadOwner->mObject
            SkinImage const& skinImg = skin.mSkinImage;

            // convert pixel bytes ke hex string
            std::string hex = bytesToHex(
                skinImg.mImageBytes.data(),
                skinImg.mImageBytes.size()
            );

            // skin shape: [height, width, channels]
            int channels = getChannels(skinImg.imageFormat);
            std::string shape = "["
                + std::to_string(skinImg.mHeight) + ","
                + std::to_string(skinImg.mWidth)  + ","
                + std::to_string(channels)         + "]";

            // posisi
            Vec3 pos = player.getPosition();

            // dimensi
            std::string dim = player.getDimension().mName;

            // nama & xuid
            std::string name = player.getRealName();
            std::string xuid = player.getXuid();

            if (!first) playersJson += ",";
            playersJson += "{"
                "\"name\":\""      + name                   + "\","
                "\"xuid\":\""      + xuid                   + "\","
                "\"skin\":\""      + hex                    + "\","
                "\"skinShape\":"   + shape                  + ","
                "\"dimension\":\"" + dim                    + "\","
                "\"x\":"           + std::to_string(pos.x)  + ","
                "\"y\":"           + std::to_string(pos.y)  + ","
                "\"z\":"           + std::to_string(pos.z)  +
            "}";
            first = false;

            return true;
        });

        playersJson += "]";

        std::string body = "{\"players\":" + playersJson + "}";
        mSender.sendPlayerData(std::move(body));
    }

private:
    HttpSender& mSender;

    static std::string bytesToHex(const uint8_t* data, size_t size) {
        static const char hex[] = "0123456789abcdef";
        std::string result;
        result.reserve(size * 2);
        for (size_t i = 0; i < size; i++) {
            result += hex[(data[i] >> 4) & 0xF];
            result += hex[data[i] & 0xF];
        }
        return result;
    }

    static int getChannels(mce::ImageFormat fmt) {
        switch (fmt) {
            case mce::ImageFormat::RGBA8Unorm:  return 4;
            case mce::ImageFormat::RGB8Unorm:   return 3;
            case mce::ImageFormat::RG8Unorm:    return 2;
            case mce::ImageFormat::R8Unorm:     return 1;
            default:                            return 4;
        }
    }
};

} // namespace mipmap