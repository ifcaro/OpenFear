#include "test_framework.h"
#include "openfear/game/MapFile.h"
#include "openfear/assets/NativeAssetProvider.h"
#include <cstdint>

using namespace openfear::game;

// MAPFILE path relative to project root (tests must run from project root)
static const char* kMapfilePath = "assets/MAPFILE";
static openfear::NativeAssetProvider provider("");

static const uint16_t kExpectedStoneCounts[8] = {144, 138, 146, 148, 134, 138, 43, 72};
static const uint16_t kTotalStones = 963;

void RegisterMapfileTests(Tests& tests) {

    tests.AddCase("MAPFILE: Load returns true (file accessible)", []() {
        MapFile mf;
        ASSERT_TRUE(mf.Load(provider, kMapfilePath));
    });

    tests.AddCase("MAPFILE: Load returns false for nonexistent path", []() {
        MapFile mf;
        ASSERT_FALSE(mf.Load(provider, "nonexistent/MAPFILE"));
    });

    tests.AddCase("MAPFILE: Total stone count = 963 across 8 provinces", []() {
        MapFile mf;
        mf.Load(provider, kMapfilePath);
        uint16_t total = 0;
        for (int p = 0; p < 8; ++p) total += mf.GetStoneCount(p);
        ASSERT_EQ(kTotalStones, total);
    });

    for (int p = 0; p < 8; ++p) {
        uint16_t expected = kExpectedStoneCounts[p];
        tests.AddCase("MAPFILE: Province " + std::to_string(p) + " has " +
                          std::to_string(expected) + " stones",
                      [p, expected]() {
                          MapFile mf;
                          mf.Load(provider, kMapfilePath);
                          ASSERT_EQ(expected, mf.GetStoneCount(p));
                      });
    }

    tests.AddCase("MAPFILE: GetStone(0, 0) returns non-null", []() {
        MapFile mf;
        mf.Load(provider, kMapfilePath);
        ASSERT_TRUE(mf.GetStone(0, 0) != nullptr);
    });

    tests.AddCase("MAPFILE: GetStone(0, 143) valid (last stone in province 0)", []() {
        MapFile mf;
        mf.Load(provider, kMapfilePath);
        ASSERT_TRUE(mf.GetStone(0, 143) != nullptr);
    });

    tests.AddCase("MAPFILE: GetStone(8, 0) returns null (invalid province)", []() {
        MapFile mf;
        mf.Load(provider, kMapfilePath);
        ASSERT_TRUE(mf.GetStone(8, 0) == nullptr);
    });

    tests.AddCase("MAPFILE: GetStone(0, 144) returns null (out-of-bounds stone)", []() {
        MapFile mf;
        mf.Load(provider, kMapfilePath);
        ASSERT_TRUE(mf.GetStone(0, 144) == nullptr);
    });

    tests.AddCase("MAPFILE: GetStoneCount(8) returns 0 (invalid province)", []() {
        MapFile mf;
        mf.Load(provider, kMapfilePath);
        ASSERT_EQ(0, mf.GetStoneCount(8));
    });

    tests.AddCase("MAPFILE: MapStone is exactly 20 bytes in memory",
                  []() { ASSERT_EQ(20, static_cast<int>(sizeof(MapStone))); });

    // Province 0, Stone 0: type=6, connections=4, dest=[1,25,-2,24],
    // nearest_bh=29, frame=[0,1,2,3] — known values from the real MAPFILE
    // (see MapFile.h's format comment).
    tests.AddCase("MAPFILE: Stone(0,0) type = 6 (junction)", []() {
        MapFile mf;
        mf.Load(provider, kMapfilePath);
        const MapStone* s = mf.GetStone(0, 0);
        ASSERT_TRUE(s != nullptr);
        ASSERT_EQ(6, static_cast<int>(s->type));
    });

    tests.AddCase("MAPFILE: Stone(0,0) N_dest = 1", []() {
        MapFile mf;
        mf.Load(provider, kMapfilePath);
        const MapStone* s = mf.GetStone(0, 0);
        ASSERT_TRUE(s != nullptr);
        ASSERT_EQ(1, static_cast<int>(s->dest[0]));  // North
    });

    tests.AddCase("MAPFILE: Stone(0,0) S_dest = -2 (wall)", []() {
        MapFile mf;
        mf.Load(provider, kMapfilePath);
        const MapStone* s = mf.GetStone(0, 0);
        ASSERT_TRUE(s != nullptr);
        ASSERT_EQ(-2, static_cast<int>(s->dest[2]));  // South
    });

    tests.AddCase("MAPFILE: Stone(0,0) nearest_bh = 29 (sign-extended from 1 byte)", []() {
        MapFile mf;
        mf.Load(provider, kMapfilePath);
        const MapStone* s = mf.GetStone(0, 0);
        ASSERT_TRUE(s != nullptr);
        ASSERT_EQ(29, static_cast<int>(s->nearest_bh));
    });

    tests.AddCase("MAPFILE: Stone(0,0) N_frame = 0", []() {
        MapFile mf;
        mf.Load(provider, kMapfilePath);
        const MapStone* s = mf.GetStone(0, 0);
        ASSERT_TRUE(s != nullptr);
        ASSERT_EQ(0, static_cast<int>(s->frame[0]));  // North
    });

    tests.AddCase("MAPFILE: All frame values are 0-2171 or 0xFFFF across all provinces", []() {
        MapFile mf;
        mf.Load(provider, kMapfilePath);
        for (int p = 0; p < 8; ++p) {
            uint16_t count = mf.GetStoneCount(p);
            for (uint16_t si = 0; si < count; ++si) {
                const MapStone* s = mf.GetStone(p, si);
                for (int dir = 0; dir < 4; ++dir) {
                    uint16_t frame = s->frame[dir];
                    ASSERT_TRUE(frame <= 2171 || frame == 0xFFFF);
                }
            }
        }
    });

    // dest = -3 (0xFFFD) = gatekeeper portal. Appears 24 times on type=7
    // stones across provinces 0-6.
    tests.AddCase("MAPFILE: All dest values are >= -3 across all provinces", []() {
        MapFile mf;
        mf.Load(provider, kMapfilePath);
        for (int p = 0; p < 8; ++p) {
            uint16_t count = mf.GetStoneCount(p);
            for (uint16_t si = 0; si < count; ++si) {
                const MapStone* s = mf.GetStone(p, si);
                for (int dir = 0; dir < 4; ++dir) {
                    ASSERT_GE(static_cast<int>(s->dest[dir]), -3);
                }
            }
        }
    });

    tests.AddCase("MAPFILE: dest=-3 only occurs on type=7 stones (gatekeeper portal invariant)",
                  []() {
                      MapFile mf;
                      mf.Load(provider, kMapfilePath);
                      for (int p = 0; p < 8; ++p) {
                          uint16_t count = mf.GetStoneCount(p);
                          for (uint16_t si = 0; si < count; ++si) {
                              const MapStone* s = mf.GetStone(p, si);
                              for (int dir = 0; dir < 4; ++dir) {
                                  if (s->dest[dir] == -3) {
                                      ASSERT_EQ(7, static_cast<int>(s->type));
                                  }
                              }
                          }
                      }
                  });

    tests.AddCase("MAPFILE: GetFrame(0, 0, 0) = 0 (Province 0, Stone 0, North)", []() {
        MapFile mf;
        mf.Load(provider, kMapfilePath);
        ASSERT_EQ(0, static_cast<int>(mf.GetFrame(0, 0, 0)));
    });

    tests.AddCase("MAPFILE: GetFrame returns kFrameInvalid for invalid province", []() {
        MapFile mf;
        mf.Load(provider, kMapfilePath);
        ASSERT_EQ(static_cast<int>(kFrameInvalid), static_cast<int>(mf.GetFrame(8, 0, 0)));
    });

    tests.AddCase("MAPFILE: GetDest(0, 0, 0) = 1 (Province 0, Stone 0, North -> Stone 1)", []() {
        MapFile mf;
        mf.Load(provider, kMapfilePath);
        ASSERT_EQ(1, static_cast<int>(mf.GetDest(0, 0, 0)));
    });

    tests.AddCase("MAPFILE: GetDest(0, 0, 2) = -2 (Province 0, Stone 0, South = wall)", []() {
        MapFile mf;
        mf.Load(provider, kMapfilePath);
        ASSERT_EQ(-2, static_cast<int>(mf.GetDest(0, 0, 2)));
    });

    // Province-2 hardcoded WALL patch (see MapFile::Load) — not derivable
    // from the raw file alone. Isolates stone 4 (which the raw file itself
    // wires as a valid 0<->4<->13 corridor) as fully unreachable.
    tests.AddCase("MAPFILE: Province 2, Stone 0, South is hardcoded WALL (post-load patch)", []() {
        MapFile mf;
        mf.Load(provider, kMapfilePath);
        ASSERT_EQ(-2, static_cast<int>(mf.GetDest(2, 0, 2)));  // direction 2 = South
    });

    tests.AddCase("MAPFILE: Province 2, Stone 13, North is hardcoded WALL (post-load patch)", []() {
        MapFile mf;
        mf.Load(provider, kMapfilePath);
        ASSERT_EQ(-2, static_cast<int>(mf.GetDest(2, 13, 0)));  // direction 0 = North
    });

    tests.AddCase("MAPFILE: Province 2, Stone 4 raw dest unaffected by the patch (N=0, S=13)",
                  []() {
                      MapFile mf;
                      mf.Load(provider, kMapfilePath);
                      ASSERT_EQ(0, static_cast<int>(mf.GetDest(2, 4, 0)));   // North -> stone 0
                      ASSERT_EQ(13, static_cast<int>(mf.GetDest(2, 4, 2)));  // South -> stone 13
                  });
}
