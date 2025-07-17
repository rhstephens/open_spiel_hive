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
#include "open_spiel/spiel_globals.h"
#include "open_spiel/spiel_utils.h"

namespace open_spiel {
namespace hive {

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

enum Direction : uint8_t {
  kNE = 0,
  kE,
  kSE,
  kSW,
  kW,
  kNW,
  kAbove,
  kNumCardinalDirections = kAbove,  // syntactic sugar for iterating
  kNumAllDirections
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

// HivePosition
//
// Describes as position using the Axial coordinate system (q,r) as well as
// a height to account for beetles/mosquitos on top of the hive
// https://www.redblobgames.com/grids/hexagons/#coordinates-axial
class HivePosition {
 public:
  // default initialization to kNullPosition
  constexpr HivePosition() : q_(0), r_(0), h_(-1) {}
  constexpr HivePosition(int8_t q, int8_t r, int8_t h = 0)
      : q_(q), r_(r), h_(h) {}

  constexpr int8_t Q() const { return q_; }
  constexpr int8_t R() const { return r_; }

  // height above the hive, where 0 == "ground"
  constexpr int8_t H() const { return h_; }

  // implicit 3rd axial coordinate S to maintain constraint: q + r + s = 0
  constexpr int8_t S() const { return -q_ - r_; }

  constexpr operator size_t() {
    return HiveBoard::AxialToIndex(*this);
  }

  int DistanceTo(HivePosition other) const {
    return (std::abs(q_ - other.q_) +
            std::abs((q_ - other.q_) + (r_ - other.r_)) +
            std::abs(r_ - other.r_)) /
           2;
  }

  bool operator==(HivePosition other) const {
    return q_ == other.q_ && r_ == other.r_ && h_ == other.h_;
  }

  bool operator!=(HivePosition other) const { return !operator==(other); }

  HivePosition operator+(HivePosition other) const {
    return HivePosition(q_ + other.q_, r_ + other.r_, h_ + other.h_);
  }

  HivePosition operator-(HivePosition other) const {
    return HivePosition(q_ - other.q_, r_ - other.r_, h_ - other.h_);
  }

  HivePosition& operator+=(HivePosition other) {
    q_ += other.q_;
    r_ += other.r_;
    h_ += other.h_;

    return *this;
  }

  std::string ToString() const {
    return absl::StrCat("(", std::to_string(q_), ", ", std::to_string(r_), ", ",
                        std::to_string(h_), ")");
  }

  HivePosition NeighbourAt(Direction dir) const;
  constexpr HivePosition Grounded() const { return {q_, r_, 0}; }

  void SetQ(int8_t q) { q_ = q; }
  void SetR(int8_t r) { r_ = r; }
  void SetH(int8_t h) { h_ = h; }

 private:
  int8_t q_;
  int8_t r_;
  int8_t h_;
};

inline constexpr std::array<int, static_cast<int>(BugType::kNumBugTypes)>
    kBugCounts = {{1, 3, 3, 2, 2, 1, 1, 1}};
inline constexpr Player kPlayerWhite = 0;
inline constexpr Player kPlayerBlack = 1;
inline constexpr HivePosition kOriginPosition{0, 0, 0};
inline constexpr HivePosition kNullPosition{0, 0, -1};

// support hashing for HivePosition
template <typename H>
H AbslHashValue(H state, HivePosition pos) {
  return H::combine(std::move(state), pos.Q(), pos.R(), pos.H());
}

// All offsets starting at top-right neighbour, and then rotating clockwise,
// plus above for beetles/mosquitos
//  5  0
// 4    1
//  3  2
constexpr std::array<HivePosition, Direction::kNumAllDirections>
    kNeighbourOffsets = {
        //  NE       E      SE       SW       W       NW       Above
        {{1, -1}, {1, 0}, {0, 1}, {-1, 1}, {-1, 0}, {0, -1}, {0, 0, 1}}};

inline HivePosition HivePosition::NeighbourAt(Direction dir) const {
  return operator+(kNeighbourOffsets[dir]);
}

inline std::ostream& operator<<(std::ostream& stream, HivePosition pos) {
  return stream << pos.ToString();
}

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
    // white tiles
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

using NeighbourList = std::array<HiveTile, kNumCardinalDirections>;


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
  // static board size for perf
  static constexpr int kBoardDims = 32;
  static constexpr int kNumCells = kBoardDims * kBoardDims;
  static constexpr int kNumWords = kNumCells / 64;
  static constexpr int kDirections = 6;

  // spend the extra memory for extreme vectorization of slide calculations.
  // the grid masks at [word][n][dir] contains 1024 bits that represent the
  // neighbours or slideable gaps for that cell [n] in direction [dir]
  static uint64_t grid_nbr_mask_[kNumWords][kNumCells][kDirections];
  static uint64_t grid_left_adj_mask_[kNumWords][kNumCells][kDirections];
  static uint64_t grid_right_adj_mask_[kNumWords][kNumCells][kDirections];

  // Creates a regular hexagonal board with given radius from the center
  constexpr HiveBoard(ExpansionInfo expansions, bool fixed_orientation);

  // Axial position (Q,R) is stored at the 2d-index:
  //   grid_[R + Radius()][Q + Radius()]
  // which translates to the flattened index:
  //   grid_[Q + Radius() + ((R + Radius()) * SqDims)]
  static constexpr size_t AxialToIndex(HivePosition pos) {
    return pos.Q() + kBoardDims / 2 + ((pos.R() + kBoardDims / 2) * kBoardDims);
  }

  constexpr size_t BitIndex(size_t pos_idx) const {
    return pos_idx % 64;
  }

  constexpr size_t WordIndex(size_t pos_idx) const {
    return pos_idx / 64;
  }

  void SetOccupied(size_t pos_idx) {
    grid_occupancy_[WordIndex(pos_idx)] |= uint64_t(1) << BitIndex(pos_idx);
  }

  void SetEmpty(size_t pos_idx) {
    grid_occupancy_[WordIndex(pos_idx)] &= ~(uint64_t(1) << BitIndex(pos_idx));
  }

  HiveTile GetTopTileAt(HivePosition pos) const;
  HiveTile GetTileBelow(HivePosition pos) const;
  const std::vector<HiveTile>& GetPlayedTiles() const { return played_tiles_; }
  const NeighbourList& GetNeighboursOf(HivePosition pos) const;
  std::vector<HiveTile> NeighboursOf(
      HivePosition pos, HivePosition to_ignore = kNullPosition) const;
  HivePosition GetPositionOf(HiveTile tile) const {
    return tile.HasValue() ? tile_positions_[tile] : kNullPosition;
  }

  HivePosition LastMovedFrom() const { return last_moved_from_; }
  HiveTile LastMovedTile() const { return last_moved_; }

  // returns false if the move was unsuccessful
  bool MoveTile(Move move);
  void Pass();

  bool IsQueenSurrounded(Colour c) const;
  bool IsGated(HivePosition pos, Direction d,
               HivePosition to_ignore = kNullPosition) const;
  bool IsConnected(HivePosition pos, HivePosition to_ignore) const;
  bool IsCovered(HivePosition pos) const;
  bool IsCovered(HiveTile tile) const;
  bool IsInBounds(HivePosition pos) const;
  bool IsPinned(HivePosition pos) const;
  bool IsPinned(HiveTile tile) const;
  bool IsPlaceable(Colour c, HivePosition pos) const;
  bool IsInPlay(HiveTile tile) const {
    return tile.HasValue() && tile_positions_[tile] != kNullPosition;
  }
  bool IsInPlay(Colour c, BugType type, int ordinal = 1) const {
    return IsInPlay(HiveTile::GetTileFrom(c, type, ordinal));
  }

  void GenerateAllMoves(std::vector<Move>* out, Colour to_move,
                        int move_number) const;
  void GenerateMovesFor(std::vector<Move>* out, HiveTile tile,
                        BugType acting_type, Colour to_move) const;

 private:
  // creates moves where a player can place an unplayed-tile from hand
  void GeneratePlacementMoves(std::vector<Move>* out, Colour to_move,
                              int move_number) const;

  // In order for a tile to slide in direction D, the following must hold true:
  // 1) The tile must not be "pinned" (i.e. at an articulation point)
  // 2) The tile must not be covered by another tile
  // 3) The tile must be able to physically slide into the position without
  //    displacing other tiles. That is, when sliding in direction D, exactly
  //    one of the two adjacent positions (D-1) (D+1) must be empty to
  //    physically move in, and the other position must be occupied in order
  //    to remain attached to the hive at all times (One-Hive rule)
  void GenerateExactValidSlides(std::vector<size_t>* out,
                                HiveTile tile, size_t tile_idx,
                                int distance) const;

  // Generating all slides (i.e. Ant moves) is calculated differently for perf
  void GenerateAllValidSlides(std::vector<size_t>* out,
                                HiveTile tile, size_t tile_idx) const;

  // A climb consists of a slide on top the hive laterally, with an optional
  // vertical movement, in any non-gated direction. This slide is less
  // restrictive than a ground-level slide as you do not require neighbours
  // to remain connected to the hive
  void GenerateValidClimbs(std::vector<size_t>* out,
                           HiveTile tile, size_t tile_idx) const;

  void GenerateValidGrasshopperPositions(std::vector<size_t>* out,
                                         HiveTile tile, size_t tile_idx) const;
  void GenerateValidLadybugPositions(std::vector<size_t>* out,
                                     HiveTile tile, size_t tile_idx) const;
  void GenerateValidMosquitoPositions(std::vector<Move>* out, HiveTile tile,
                                      size_t tile_idx, Colour to_move) const;
  void GenerateValidPillbugSpecials(std::vector<Move>* out, HiveTile tile,
                                    size_t tile_idx) const;

  void BoardUpdated(HivePosition old_pos, HivePosition new_pos);
  void UpdateNeighbours(HivePosition old_pos, HivePosition new_pos);

  // moves all tiles closer to the center relative to the distance of each axis
  bool RecenterBoard(HivePosition new_pos);

  // Articulation points in a connected graph are vertices where, when removed,
  // separate the graph into multiple components that are no longer connected.
  // Tiles at an articulation point are considered "pinned" (and thus, can't be
  // moved) as it would split the hive in two and invalidate the "One-Hive" rule
  // https://en.wikipedia.org/wiki/Biconnected_component
  // https://cp-algorithms.com/graph/cutpoints.html
  void UpdateArticulationPoints();

  //
  uint64_t grid_occupancy_[kNumWords];
  size_t min_word_idx;
  size_t max_word_idx;

  std::vector<HiveTile> played_tiles_;
  std::array<HivePosition, HiveTile::kNumTiles> tile_positions_;

  // there are max 6 tiles that can climb on the hive to cover a tile
  std::array<HiveTile, 7> covered_tiles_;

  HiveTile last_moved_;
  HivePosition last_moved_from_;

  bool fixed_orientation_;
  ExpansionInfo expansions_;
};

}  // namespace hive
}  // namespace open_spiel

#endif  // OPEN_SPIEL_GAMES_HIVE_BOARD_H_
