#pragma once
#include <vector>
#include <string>
#include <set>
#include "HttpSender.h"

#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/level/ChunkPos.h"
#include "mc/world/level/ChunkBlockPos.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/ChunkPos.h"
#include "mc/world/level/ChunkLocalHeight.h"

namespace mipmap {

// blacklist block yang di-skip (sama seperti Python)
static const std::set<std::string> kBlacklist = {
    "minecraft:oxidized_copper_door",
    "minecraft:waxed_exposed_copper_door",
    "minecraft:waxed_exposed_copper_trapdoor",
    "minecraft:waxed_oxidized_copper_trapdoor",
    "minecraft:waxed_oxidized_copper_door",
    "minecraft:sculk_vein",
    "minecraft:crimson_fungus",
    "minecraft:acacia_wood",
    "minecraft:warped_fungus",
    "minecraft:composter",
    "minecraft:cauldron",
    "minecraft:ender_chest",
    "minecraft:trapped_chest",
    "minecraft:chest",
    "minecraft:tuff_wall",
    "minecraft:polished_tuff_wall",
    "minecraft:small_amethyst_bud",
    "minecraft:tuff_brick_wall",
    "minecraft:weathered_copper_door",
    "minecraft:waxed_weathered_copper_trapdoor",
    "minecraft:waxed_weathered_copper_door",
    "minecraft:waxed_copper_trapdoor",
    "minecraft:waxed_copper_door",
    "minecraft:purple_stained_glass_pane",
    "minecraft:blue_stained_glass_pane",
    "minecraft:light_blue_stained_glass_pane",
    "minecraft:cyan_stained_glass_pane",
    "minecraft:green_stained_glass_pane",
    "minecraft:lime_stained_glass_pane",
    "minecraft:yellow_stained_glass_pane",
    "minecraft:orange_stained_glass_pane",
    "minecraft:magenta_stained_glass_pane",
    "minecraft:pink_stained_glass_pane",
    "minecraft:ladder",
    "minecraft:acacia_trapdoor",
    "minecraft:jungle_trapdoor",
    "minecraft:birch_trapdoor",
    "minecraft:bamboo_door",
    "minecraft:iron_door",
    "minecraft:crimson_door",
    "minecraft:warped_door",
    "minecraft:bamboo_fence",
    "minecraft:warped_fence_gate",
    "minecraft:crimson_trapdoor",
    "minecraft:nether_brick_fence",
    "minecraft:mangrove_door",
    "minecraft:crimson_fence",
    "minecraft:cherry_door",
    "minecraft:acacia_fence",
    "minecraft:cherry_fence_gate",
    "minecraft:cherry_trapdoor",
    "minecraft:dark_oak_fence",
    "minecraft:bamboo_trapdoor",
    "minecraft:mangrove_fence",
    "minecraft:bamboo_fence_gate",
    "minecraft:cherry_fence",
    "minecraft:crimson_fence_gate",
    "minecraft:cobblestone_wall",
    "minecraft:birch_fence_gate",
    "minecraft:spruce_fence_gate",
    "minecraft:birch_fence",
    "minecraft:fence_gate",
    "minecraft:jungle_fence",
    "minecraft:warped_fence",
    "minecraft:mangrove_trapdoor",
    "minecraft:dark_oak_fence_gate",
    "minecraft:acacia_fence_gate",
    "minecraft:jungle_fence_gate",
    "minecraft:mangrove_fence_gate",
    "minecraft:deepslate_tile_wall",
    "minecraft:dark_oak_door",
    "minecraft:polished_deepslate_wall",
    "minecraft:deepslate_brick_wall",
    "minecraft:iron_bars",
    "minecraft:mud_brick_wall",
    "minecraft:polished_blackstone_wall",
    "minecraft:birch_door",
    "minecraft:polished_blackstone_brick_wall",
    "minecraft:jungle_door",
    "minecraft:cobbled_deepslate_wall",
    "minecraft:acacia_door",
    "minecraft:wooden_door",
    "minecraft:spruce_fence",
    "minecraft:spruce_door",
    "minecraft:blackstone_wall",
    "minecraft:oak_fence",
    "minecraft:large_fern",
    "minecraft:deadbush",
    "minecraft:short_grass",
    "minecraft:fern",
    "minecraft:torchflower",
    "minecraft:azure_bluet",
    "minecraft:sunflower",
    "minecraft:lily_of_the_valley",
    "minecraft:wither_rose",
    "minecraft:rose_bush",
    "minecraft:pink_petals",
    "minecraft:sweet_berry_bush",
    "minecraft:pitcher_plant",
    "minecraft:tall_grass",
    "minecraft:peony",
    "minecraft:cornflower",
    "minecraft:oxeye_daisy",
    "minecraft:pink_tulip",
    "minecraft:white_tulip",
    "minecraft:big_dripleaf",
    "minecraft:poppy",
    "minecraft:twisting_vines",
    "minecraft:orange_tulip",
    "minecraft:lilac",
    "minecraft:yellow_flower",
    "minecraft:medium_amethyst_bud",
    "minecraft:seagrass",
    "minecraft:glow_lichen",
    "minecraft:reeds",
    "minecraft:bubble_column",
    "minecraft:pointed_dripstone",
    "minecraft:vine",
    "minecraft:air",
    "minecraft:cactus_flower",
    "minecraft:leaf_litter",
    "minecraft:red_tulip",
    "minecraft:bush",
    "minecraft:firefly_bush",
    "minecraft:tall_dry_grass",
    "minecraft:bamboo",
    "minecraft:short_dry_grass",
    "minecraft:dandelion",
    "minecraft:flower_pot.png",
    "minecraft:grindstone.png",
    "minecraft:moss_carpet.png",
    "minecraft:wheat.png",
    "minecraft:sea_pickle.png",
    "minecraft:kelp.png",
    "minecraft:wildflowers.png",
    "minecraft:allium.png",
    "minecraft:torch.png",
    "minecraft:melon_stem.png",
    "minecraft:sandstone_wall.png",
    "minecraft:tube_coral.png",
    "minecraft:jungle_button.png",
    "minecraft:brain_coral.png",
    "minecraft:beetroot.png",
    "minecraft:fire_coral.png",
    "minecraft:carrots.png",
    "minecraft:brain_coral_fan.png",
    "minecraft:bell.png",
    "minecraft:fire.png"
};

class ChunkCollector {
public:
    explicit ChunkCollector(HttpSender& sender) : mSender(sender) {}

    void onChunkLoaded(LevelChunk& lc) {
        ChunkPos const& pos = lc.mPosition;
        Dimension& dim = lc.mDimension;
        int chunkX = pos.x;
        int chunkZ = pos.z;
        std::string dimName = dim.mName;

        std::array<ChunkLocalHeight, 256> const& heightmap = lc.mHeightmap;
        DimensionHeightRange const& heightRange = dim.mHeightRange;
        short minY = heightRange.mMin;

        // build JSON manual (hindari dependency nlohmann/json)
        std::string blocksJson = "[";
        bool first = true;

        for (uchar lx = 0; lx < 16; lx++) {
            for (uchar lz = 0; lz < 16; lz++) {
                ChunkLocalHeight height = heightmap[lx + lz * 16];

                // scan dari atas ke bawah, skip blacklist
                std::string blockName;
                int worldY = 0;

                for (short y = height.mVal; y >= 0; y--) {
                    ChunkLocalHeight localY{y};
                    Block const& block = lc.getBlock(ChunkBlockPos(lx, localY, lz));
                    blockName = block.getTypeName();

                    if (kBlacklist.find(blockName) == kBlacklist.end()) {
                        worldY = (int)y + minY;
                        break;
                    }
                }

                if (blockName.empty()) continue;

                int worldX = chunkX * 16 + lx;
                int worldZ = chunkZ * 16 + lz;

                if (!first) blocksJson += ",";
                blocksJson += "{\"name\":\"" + blockName + "\","
                            + "\"coordinates\":[" 
                            + std::to_string(worldX) + ","
                            + std::to_string(worldY) + ","
                            + std::to_string(worldZ) + "]}";
                first = false;
            }
        }

        blocksJson += "]";

        std::string body = "{"
            "\"chunkX\":" + std::to_string(chunkX) + ","
            "\"chunkZ\":" + std::to_string(chunkZ) + ","
            "\"chunk\":{"
                "\"dimension\":\"" + dimName + "\","
                "\"blocks\":" + blocksJson +
            "}}";

        mSender.sendChunkData(std::move(body));
    }

private:
    HttpSender& mSender;
};

} // namespace mipmap