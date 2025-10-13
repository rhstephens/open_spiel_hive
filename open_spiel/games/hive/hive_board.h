// Copyright 2025 DeepMind Technologies Limited
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef OPEN_SPIEL_GAMES_HIVE_BOARD_H_
#define OPEN_SPIEL_GAMES_HIVE_BOARD_H_

#include <array>
#include <cstdint>
#include <cstdlib>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "open_spiel/abseil-cpp/absl/base/attributes.h"
#include "open_spiel/abseil-cpp/absl/container/flat_hash_map.h"
#include "open_spiel/abseil-cpp/absl/container/flat_hash_set.h"
#include "open_spiel/abseil-cpp/absl/strings/str_cat.h"
#include "open_spiel/games/hive/hive_parallel_bitboard.h"
#include "open_spiel/spiel_globals.h"
#include "open_spiel/spiel_utils.h"

namespace open_spiel {
namespace hive {

  // TODO: put this back in hive.h or a global header
inline constexpr int kNumDistinctActions = 5488 + 1;  // +1 for pass


enum class BugType : uint8_t {
  kQueen = 0,
  kAnt,
  kGrasshopper,
  kSpider,
  kBeetle,
  kMosquito,
  kLadybug,
  kPillbug,
  kNumBugTypes,
  kNone,
};

enum class Colour { kWhite, kBlack };

struct ExpansionInfo {
  bool uses_mosquito;
  bool uses_ladybug;
  bool uses_pillbug;

  bool HasAny() const { return uses_mosquito || uses_ladybug || uses_pillbug; }
  bool IsBugTypeEnabled(BugType type) const {
    switch (type) {
      case BugType::kQueen:
      case BugType::kAnt:
      case BugType::kGrasshopper:
      case BugType::kSpider:
      case BugType::kBeetle:
        return true;
      case BugType::kMosquito:
        return uses_mosquito;
      case BugType::kLadybug:
        return uses_ladybug;
      case BugType::kPillbug:
        return uses_pillbug;
      default:
        return false;
    }
  }
};

inline constexpr std::array<int, static_cast<int>(BugType::kNumBugTypes)>
    kBugCounts = {{1, 3, 3, 2, 2, 1, 1, 1}};
inline constexpr Player kPlayerWhite = 0;
inline constexpr Player kPlayerBlack = 1;

inline Player OtherPlayer(Player p) {
  SPIEL_DCHECK_TRUE(p != kInvalidPlayer);
  return p == kPlayerWhite ? kPlayerBlack : kPlayerWhite;
}

inline Colour OtherColour(Colour c) {
  return c == Colour::kWhite ? Colour::kBlack : Colour::kWhite;
}

inline Colour PlayerToColour(Player p) {
  SPIEL_DCHECK_TRUE(p != kInvalidPlayer);
  return p == kPlayerWhite ? Colour::kWhite : Colour::kBlack;
}

inline Direction OppositeDirection(uint8_t in) {
  SPIEL_DCHECK_TRUE(in < Direction::kNumCardinalDirections);
  return static_cast<Direction>((in + 3) % Direction::kNumCardinalDirections);
}

inline Direction ClockwiseDirection(uint8_t in) {
  SPIEL_DCHECK_TRUE(in < Direction::kNumCardinalDirections);
  return static_cast<Direction>((in + 1) % Direction::kNumCardinalDirections);
}

inline Direction CounterClockwiseDirection(uint8_t in) {
  SPIEL_DCHECK_TRUE(in < Direction::kNumCardinalDirections);
  return static_cast<Direction>((in + 5) % Direction::kNumCardinalDirections);
}

// Wrapper class that uses an enum to represent each unique physical tile.
// This would be similar to using a uint8_t with bit fields to encode
// colour/type/ordinal, but instead with the convenient features of a class
class HiveTile {
 public:
  // the Value enum is a ubiquitous list of physical tiles found in the game
  // using their corresponding UHP names
  enum Value : uint8_t {
    // white h
    wQ = 0,
    wA1,
    wA2,
    wA3,
    wG1,
    wG2,
    wG3,
    wS1,
    wS2,
    wB1,
    wB2,
    wM,
    wL,
    wP,
    // black tiles
    bQ,
    bA1,
    bA2,
    bA3,
    bG1,
    bG2,
    bG3,
    bS1,
    bS2,
    bB1,
    bB2,
    bM,
    bL,
    bP,
    // constants
    kNumTiles,
    kNoneTile = kNumTiles
  };

  constexpr HiveTile() : tile_name_(kNoneTile) {}
  constexpr HiveTile(Value val) : tile_name_(val) {}
  constexpr HiveTile(uint8_t val) : tile_name_(static_cast<Value>(val)) {}

  // evaluates to the Value enum when used in expressions
  constexpr operator Value() const { return tile_name_; }

  constexpr bool HasValue() const { return tile_name_ < kNoneTile; }

  static constexpr std::array<HiveTile, bQ> GetTilesForColour(Colour c) {
    switch (c) {
      case Colour::kWhite:
        return {wQ,  wA1, wA2, wA3, wG1, wG2, wG3,
                wS1, wS2, wB1, wB2, wM,  wL,  wP};
      case Colour::kBlack:
        return {bQ,  bA1, bA2, bA3, bG1, bG2, bG3,
                bS1, bS2, bB1, bB2, bM,  bL,  bP};
      default:
        return {};
    }
  }

  static constexpr Value GetTileFrom(Colour c, BugType type,
                                     uint8_t ordinal = 1) {
    uint8_t retval = c == Colour::kWhite ? wQ : bQ;

    // sort of like reverse-iterating through an enum to determine its index
    switch (type) {
      case BugType::kPillbug:
        retval += kBugCounts[static_cast<int>(BugType::kLadybug)];
        ABSL_FALLTHROUGH_INTENDED;
      case BugType::kLadybug:
        retval += kBugCounts[static_cast<int>(BugType::kMosquito)];
        ABSL_FALLTHROUGH_INTENDED;
      case BugType::kMosquito:
        retval += kBugCounts[static_cast<int>(BugType::kBeetle)];
        ABSL_FALLTHROUGH_INTENDED;
      case BugType::kBeetle:
        retval += kBugCounts[static_cast<int>(BugType::kSpider)];
        ABSL_FALLTHROUGH_INTENDED;
      case BugType::kSpider:
        retval += kBugCounts[static_cast<int>(BugType::kGrasshopper)];
        ABSL_FALLTHROUGH_INTENDED;
      case BugType::kGrasshopper:
        retval += kBugCounts[static_cast<int>(BugType::kAnt)];
        ABSL_FALLTHROUGH_INTENDED;
      case BugType::kAnt:
        retval += kBugCounts[static_cast<int>(BugType::kQueen)];
        ABSL_FALLTHROUGH_INTENDED;
      default:
        // no-op
        break;
    }

    return static_cast<Value>(retval + ordinal - 1);
  }

  static Value UHPToTile(const std::string& uhp) {
    static absl::flat_hash_map<std::string, Value> string_mapping = {
        {"wQ", wQ},
        {"wA1", wA1},
        {"wA2", wA2},
        {"wA3", wA3},
        {"wG1", wG1},
        {"wG2", wG2},
        {"wG3", wG3},
        {"wS1", wS1},
        {"wS2", wS2},
        {"wB1", wB1},
        {"wB2", wB2},
        {"wM", wM},
        {"wL", wL},
        {"wP", wP},
        //
        {"bQ", bQ},
        {"bA1", bA1},
        {"bA2", bA2},
        {"bA3", bA3},
        {"bG1", bG1},
        {"bG2", bG2},
        {"bG3", bG3},
        {"bS1", bS1},
        {"bS2", bS2},
        {"bB1", bB1},
        {"bB2", bB2},
        {"bM", bM},
        {"bL", bL},
        {"bP", bP}};

    auto it = string_mapping.find(uhp);
    SPIEL_CHECK_TRUE(it != string_mapping.end());
    return it->second;
  }

  static std::string TileToUHP(HiveTile tile) {
    static absl::flat_hash_map<Value, std::string> enum_mapping = {{wQ, "wQ"},
                                                                   {wA1, "wA1"},
                                                                   {wA2, "wA2"},
                                                                   {wA3, "wA3"},
                                                                   {wG1, "wG1"},
                                                                   {wG2, "wG2"},
                                                                   {wG3, "wG3"},
                                                                   {wS1, "wS1"},
                                                                   {wS2, "wS2"},
                                                                   {wB1, "wB1"},
                                                                   {wB2, "wB2"},
                                                                   {wM, "wM"},
                                                                   {wL, "wL"},
                                                                   {wP, "wP"},
                                                                   //
                                                                   {bQ, "bQ"},
                                                                   {bA1, "bA1"},
                                                                   {bA2, "bA2"},
                                                                   {bA3, "bA3"},
                                                                   {bG1, "bG1"},
                                                                   {bG2, "bG2"},
                                                                   {bG3, "bG3"},
                                                                   {bS1, "bS1"},
                                                                   {bS2, "bS2"},
                                                                   {bB1, "bB1"},
                                                                   {bB2, "bB2"},
                                                                   {bM, "bM"},
                                                                   {bL, "bL"},
                                                                   {bP, "bP"}};

    auto it = enum_mapping.find(tile);
    SPIEL_CHECK_TRUE(it != enum_mapping.end());
    return it->second;
  }

  constexpr BugType GetBugType() const {
    switch (tile_name_) {
      case wQ:
      case bQ:
        return BugType::kQueen;
      case wA1:
      case wA2:
      case wA3:
      case bA1:
      case bA2:
      case bA3:
        return BugType::kAnt;
      case wG1:
      case wG2:
      case wG3:
      case bG1:
      case bG2:
      case bG3:
        return BugType::kGrasshopper;
      case wS1:
      case wS2:
      case bS1:
      case bS2:
        return BugType::kSpider;
      case wB1:
      case wB2:
      case bB1:
      case bB2:
        return BugType::kBeetle;
      case wM:
      case bM:
        return BugType::kMosquito;
      case wL:
      case bL:
        return BugType::kLadybug;
      case wP:
      case bP:
        return BugType::kPillbug;
      default:
        return BugType::kNone;
    }
  }

  constexpr Colour GetColour() const {
    switch (tile_name_) {
      case wQ:
      case wA1:
      case wA2:
      case wA3:
      case wG1:
      case wG2:
      case wG3:
      case wS1:
      case wS2:
      case wB1:
      case wB2:
      case wM:
      case wL:
      case wP:
        return Colour::kWhite;
      case bQ:
      case bA1:
      case bA2:
      case bA3:
      case bG1:
      case bG2:
      case bG3:
      case bS1:
      case bS2:
      case bB1:
      case bB2:
      case bM:
      case bL:
      case bP:
        return Colour::kBlack;
      default:
        SpielFatalError("GetColour() - invalid enum value");
    }
  }

  constexpr uint8_t GetOrdinal() const {
    switch (tile_name_) {
      case kNoneTile:
        return 0;
      case wA2:
      case wG2:
      case wS2:
      case wB2:
      case bA2:
      case bG2:
      case bS2:
      case bB2:
        return 2;
      case wA3:
      case wG3:
      case bA3:
      case bG3:
        return 3;
      default:
        return 1;
    }
  }

  std::string ToUHP() const;

 private:
  Value tile_name_;
};


inline Action GenerateAction(HiveTile from, HiveTile to, size_t dir) {
  return (from * HiveTile::kNumTiles * Direction::kNumAllDirections) +
         (to * Direction::kNumAllDirections) +
         dir;
}

// The in-game representation of an Action
struct Move {
  HiveTile from;        // the tile that's being moved
  HiveTile to;          // the reference tile
  Direction direction;  // offset applied to the reference tile

  std::string ToUHP();
  bool IsPass() const { return !from.HasValue(); }

  bool operator==(Move other) const {
    return from == other.from && to == other.to && direction == other.direction;
  }
};


// HiveBoard
//
// One of the most apparent problems to solve for Hive is how to represent an
// infinitely-sized board in a fixed-sized manner? This is especially the case
// when also needing an accurate 2D representation of the board state for use
// as an ObservationTensor.
//
// While the game logic could be implemented with a wrap-around grid big enough
// to account for all tiles (a 29x29 grid for all expansion pieces), the
// resulting ObservationTensor would be:
//   1) massively large in size (compared to the typical size of a Hive game)
//   2) be extremely sparse, which could negatively affect learning, and
//   3) unsuitable for 2D convolution in AlphaZero with no way to account for
//      hexagonal wrapping of the tensor (that I know of). And even if there
//      was a potential solution, a vast majority of playthroughs would be
//      unlikely to ever reach a state where wrapping is necessary
//
// With all of that in mind, I have chosen the following board design:
//   - the board will be stored as a fixed-sized and flattened 2d array where
//     each index contains an enum describing either the existance of a
//     specific tile, or an empty space on the grid
//   - each tile enum can be used to index into fixed-sized arrays that store
//     information about that specific tile. e.g. tile_positions_[::wA2] stores
//     the HivePosition of white's 2nd Ant tile
//   - most of the game logic is computed using the Axial coordinate system
//     (described above under HivePosition), then later translated to an index
//     when needed for the grid. This helps with the maths and allows for
//     quick computation of rotational and reflectional symmetry
//
// Example board state with radius == 2 to illustrate (X means empty):
//
//                                                  ___0____1____2____3____4__
//       X     bQ    X                            0 |    |    |    | bQ |    |
//                                                  |____|____|____|____|____|
//     X     X   bA1    X                         1 |    |    |    | bA1|    |
//                              AxialToIndex()      |____|____|____|____|____|
//   X   wQ    wL    X    X     ------------->    2 |    | wQ | wL |    |    |
//                                                  |____|____|____|____|____|
//     X    wG1   X     X                         3 |    | wG1|    |    |    |
//                                                  |____|____|____|____|____|
//       X     X     X                            4 |    |    |    |    |    |
//                                                  |____|____|____|____|____|
//

class HiveBoard {
 public:
  // need to support a hex grid that can contain all 28 tiles played in one
  // direction, plus an extra space to handle wrap-arounds.
  // A minimal hexagonal board with a radius of 15 would handle this.
  // n = 15
  // #cells = 3n^2 - 3n + 1 = 631 cells -> round up to pow of 2 = 1024 = 32x32
  static constexpr size_t kBoardDims = 32;
  static constexpr size_t kNumCells = kBoardDims * kBoardDims;

  // starting cell is at the center of the center-most Bitboard word
  static constexpr size_t kStartPos = (kNumCells / 2) + (kBoardDims / 2);
  static constexpr size_t kNullPos = size_t(-1);
  static constexpr std::array<int, Direction::kNumCardinalDirections>
    kNeighbourOffsets = {
      kBoardDims + 1,   // NE
      1,                //  E
      -kBoardDims,      // SE
      -kBoardDims - 1,  // SW
      -1,               //  W
      kBoardDims        // NW
    };

  // kGateOffsets[dir] return the two indice offsets that must be checked in
  // order to slide from the current position in direction dir
  static constexpr std::array<std::pair<int, int>, Direction::kNumCardinalDirections>
    kGateOffsets = {{
      {kNeighbourOffsets[kNW], kNeighbourOffsets[kE]},      // NE
      {kNeighbourOffsets[kNE], kNeighbourOffsets[kSE]},     // E
      {kNeighbourOffsets[kE], kNeighbourOffsets[kSW]},      // SE
      {kNeighbourOffsets[kSE], kNeighbourOffsets[kW]},      // SW
      {kNeighbourOffsets[kSW], kNeighbourOffsets[kNW]},     // W
      {kNeighbourOffsets[kW], kNeighbourOffsets[kNE]}       // NW
    }};

  using GateMask = std::array<std::array<HexBitboard32x32, HiveBoard::kNumCells>,
                              Direction::kNumCardinalDirections>;

  // kNeighbourMasks at index [pos] contains a bitboard with the neighbours of pos
  static const std::array<HexBitboard32x32, kNumCells> kNeighbourMasks;
  static const std::array<std::array<size_t, Direction::kNumCardinalDirections>, kNumCells>
    kNeighbourIterators;


  // TODO: use this inside of IsGated()
  // kGateMasks at index [dir][pos] contains a bitboard with the clockwise and
  // counterclockwise adjacent neighbours of pos in direction dir
  static const GateMask kGateMasks;

  struct Offset {
    int row_offset;
    int col_offset;

    constexpr Offset(int row, int col) : row_offset(row), col_offset(col) {}

    // TODO: consider not having this magic type deduction
    constexpr operator size_t() {
      return col_offset + row_offset * kBoardDims;
    }
  };

  // pairs a covered tile with the height its at on the stack (0 == ground)
  struct CoveredTile {
    HiveTile tile{};
    int stack_height{};

    // searchable by tile only

  };

  // ctor
  HiveBoard(ExpansionInfo expansions, bool fixed_orientation);

  // state altering methods
  bool ApplyMove(Move move);

  // public methods
  void GenerateAllMoves(std::bitset<kNumDistinctActions>& out, Colour to_play, int move_num) const;
  // this is separated out to allow pillbug/mosquito to directly generate Moves
  void GenerateMovesFor(std::bitset<kNumDistinctActions>& out, HiveTile to_move, BugType acting_type, Colour to_play) const;

  size_t GetPositionOf(HiveTile tile) const { return tile_positions_[tile]; }
  HiveTile GetTopTileAt(size_t pos) const { return tile_grid_[pos]; }
  int GetTileHeight(HiveTile tile) const { return tile_stack_heights_[tile]; }
  HiveTile GetTileUnderneath(HiveTile tile) const { 
    for (int i = covered_tiles_.size() - 1; i >= 0; --i) {
      if (tile == covered_tiles_[i].second) {
        return covered_tiles_[i].first;
      }
    }

    return HiveTile::kNoneTile;
  }
  HiveTile LastMovedTile() const { return last_moved_; }
  size_t LastMovedFrom() const { return last_moved_from_; }
  
  // Checks gate and occupancy for a single sliding direction
  bool IsGated(size_t pos, Direction dir, size_t to_ignore = 0) const {
    HexBitboard32x32 occ = occupied_;
    occ.clear(to_ignore);

    return (occ.test(pos + kNeighbourOffsets[dir]) ||
           (occ.test(pos + kNeighbourOffsets[ClockwiseDirection(dir)]) ==
            occ.test(pos + kNeighbourOffsets[CounterClockwiseDirection(dir)])));
  }

  // Check if a Beetle can climb from top of position pos in direction dir
  bool IsBeetleGated(size_t pos, Direction dir) const {
    int from_stack_height = GetTileHeight(GetTopTileAt(pos));
    int to_stack_height = 1 + GetTileHeight(GetTopTileAt(pos + kNeighbourOffsets[dir]));
    int cw_stack_height = GetTileHeight(GetTopTileAt(pos + kNeighbourOffsets[ClockwiseDirection(dir)]));
    int ccw_stack_height = GetTileHeight(GetTopTileAt(pos + kNeighbourOffsets[CounterClockwiseDirection(dir)]));

    // take the max of "from" and "to", and check for a gate at that level
    int compare_height = std::max(from_stack_height, to_stack_height);
    return compare_height > 1 &&
           cw_stack_height >= compare_height &&
           ccw_stack_height >= compare_height;
  }

  bool IsCovered(HiveTile tile) const {
    return std::find_if(covered_tiles_.begin(), covered_tiles_.end(),
      [tile](const std::pair<HiveTile,int>& pair) {
        return pair.first == tile;
      }) != covered_tiles_.end();
  }
  bool IsInBounds(size_t pos) const { return pos < kNumCells; }
  bool IsInPlay(HiveTile tile) const {
    return std::find(played_tiles_.begin(), played_tiles_.end(), tile) != played_tiles_.end();
  }
  bool IsPinned(HiveTile tile) const { return IsPinned(GetPositionOf(tile)) && GetTileHeight(tile) <= 1; }
  bool IsPinned(size_t pos) const { return pinned_.test(pos); }
  bool IsSurrounded(size_t pos) const {
    return (kNeighbourMasks[pos] & occupied_) == kNeighbourMasks[pos];
  }

  bool WinConditionMet(Player player) const {
    HiveTile other_queen = HiveTile::GetTileFrom(OtherColour(PlayerToColour(player)), BugType::kQueen);

    return IsInPlay(other_queen) && IsSurrounded(GetPositionOf(other_queen));
  }

  std::string PrintBoard() const { return occupied_.to_string(); }

  void Pass() const {}

  HexBitboard32x32 PlaceablePositions(Player to_play) const;

 private:
  // Articulation points in a connected graph are vertices where, when removed,
  // separate the graph into multiple components that are no longer connected.
  // Tiles at an articulation point are considered "pinned" (and thus, can't be
  // moved) as it would split the hive in two and invalidate the "One-Hive" rule
  // https://en.wikipedia.org/wiki/Biconnected_component
  // https://cp-algorithms.com/graph/cutpoints.html
  void UpdateArticulationPoints();
  void DFSArticulation(HiveTile tile, size_t parent_pos, bool is_root,
    std::bitset<HiveTile::kNumTiles>& visited, int visit_order,
    std::array<int, HiveTile::kNumTiles>& entry_point,
    std::array<int, HiveTile::kNumTiles>& low_point);

  HexBitboard32x32 GeneratePositionsFor(HiveTile tile, 
                                        BugType acting_type,
                                        Colour to_move) const;

  void GenerateMove(std::bitset<kNumDistinctActions>& out, HiveTile from_tile, size_t to_pos) const;
  void GeneratePillbugSpecialMoves(std::bitset<kNumDistinctActions>& out, size_t pillbug_pos) const;

  // Checks and returns the next top-most bug at position pos, or kNoneTile
  void OnTileUncovered(HiveTile tile);
  bool RecenterBoard();

  std::vector<size_t> SlidingAdjacentNeighbours(size_t start_pos) const;

  HexBitboard32x32 ValidWalkOnePositions(HiveTile tile) const;
  HexBitboard32x32 ValidSpiderPositions(HiveTile tile) const;
  HexBitboard32x32 ValidAntPositions(HiveTile tile) const;
  HexBitboard32x32 ValidGrasshopperPositions(HiveTile tile) const;
  HexBitboard32x32 ValidClimbPositions(HiveTile tile) const;
  HexBitboard32x32 ValidLadybugPositions(HiveTile tile) const;

  // Bitboards are used for things accessed frequently in the hot-path
  HexBitboard32x32 occupied_;
  HexBitboard32x32 pinned_;
  HexBitboard32x32 player_positions_[2];

  // mail-box approach for queries that aren't as frequently called
  std::array<HiveTile, kNumCells> tile_grid_;
  std::array<size_t, HiveTile::kNumTiles> tile_positions_;

  // the current height of a given tile within its stack 
  // (0 = not played, 1 = first level, 2 = second level and implicitly on top of at
  // least one other tile)
  std::array<size_t, HiveTile::kNumTiles + 1> tile_stack_heights_{};

  std::array<std::pair</*me*/ HiveTile, /*above me*/ HiveTile>, 7> covered_tiles_;
  std::vector<HiveTile> played_tiles_;

  HiveTile last_moved_;
  size_t last_moved_from_;

  // game params
  bool fixed_orientation_;
  ExpansionInfo expansions_;
};


}  // namespace hive
}  // namespace open_spiel

#endif  // OPEN_SPIEL_GAMES_HIVE_BOARD_H_
