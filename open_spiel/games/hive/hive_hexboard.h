// Copyright 2024 DeepMind Technologies Limited
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

#ifndef OPEN_SPIEL_GAMES_HIVE_HEXBOARD_H_
#define OPEN_SPIEL_GAMES_HIVE_HEXBOARD_H_

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "open_spiel/abseil-cpp/absl/container/flat_hash_map.h"
#include "open_spiel/abseil-cpp/absl/container/flat_hash_set.h"
#include "open_spiel/spiel.h"


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
  kNumCardinalDirections = kAbove, // syntactic sugar for iterating below kAbove
  kNumAllDirections
};


// an overly verbose addition for the sake of clarity when it really matters
// if we are talking about the colour of tile vs the player using it
enum class Colour {
  kWhite,
  kBlack
};


// A regular hexagonal grid defined by using an Axial co-ordinate system (q,r)
// as well as a height to account for beetles/mosquitos on top of the hive
// https://www.redblobgames.com/grids/hexagons/#coordinates-axial
class HivePosition {
 public:
  // default initialization to kNullPosition
  constexpr HivePosition() : q_(0), r_(0), h_(-1) {}
  constexpr HivePosition(int8_t q, int8_t r, int8_t h = 0) 
    : q_(q), r_(r), h_(h) {}

  int8_t Q() const { return q_; }
  int8_t R() const { return r_; }

  // height above the hive, where 0 == "ground"
  int8_t H() const { return h_; }

  int DistanceTo(HivePosition other) const {
    HivePosition diff = *this - other;
    return (std::abs(diff.q_) + std::abs(diff.q_ + diff.r_) + std::abs(diff.r_)) / 2;
  }

  bool operator==(const HivePosition other) const {
    return q_ == other.q_ && r_ == other.r_ && h_ == other.h_; 
  }

  bool operator!=(const HivePosition other) const {
    return !operator==(other);
  }

  HivePosition operator+(const HivePosition other) const {
    return HivePosition(q_ + other.q_, r_ + other.r_, h_ + other.h_);
  }

  HivePosition operator-(const HivePosition other) const {
    return HivePosition(q_ - other.q_, r_ - other.r_, h_ - other.h_);
  }

  HivePosition& operator+=(const HivePosition other) {
    q_ += other.q_;
    r_ += other.r_;
    h_ += other.h_;

    return *this;
  }

  std::string ToString() const {
    return absl::StrCat("(", std::to_string(q_), ", ", std::to_string(r_), ", ", std::to_string(h_), ")");
  }

  std::array<HivePosition, Direction::kNumCardinalDirections> Neighbours() const {
    return {{{q_ + 1, r_ - 1},
             {q_ + 1, r_},
             {q_, r_ + 1},
             {q_ - 1, r_ + 1},
             {q_ - 1, r_},
             {q_, r_ - 1}
            }};
  }
  HivePosition NeighbourAt(Direction dir) const;

  HivePosition Grounded() const { return {q_, r_, 0}; }

  void SetQ(int8_t q) { q_ = q; }
  void SetR(int8_t r) { r_ = r; }
  void SetH(int8_t h) { h_ = h; }

 private:
  int8_t q_;
  int8_t r_;
  int8_t h_;

};

// All offsets starting at top-right neighbour, and then rotating clockwise,
// plus above for beetles/mosquitos
constexpr std::array<HivePosition, Direction::kNumAllDirections> kNeighbourOffsets = {
  //  NE       E      SE       SW       W       NW       Above
  {{1, -1}, {1, 0}, {0, 1}, {-1, 1}, {-1, 0}, {0, -1}, {0, 0, 1}}
};

inline HivePosition HivePosition::NeighbourAt(Direction dir) const {
  return *this + kNeighbourOffsets[dir];
}

inline std::ostream& operator<<(std::ostream& stream, HivePosition pos) {
  return stream << pos.ToString();
}

// support hashing for HivePosition
template <typename H>
H AbslHashValue(H state, HivePosition pos) {
  return H::combine(std::move(state), pos.Q(), pos.R(), pos.H());
}

inline constexpr int kMaxTileCount = 28;
inline constexpr int kMaxBoardRadius = 14;
inline constexpr int kDefaultBoardRadius = 8;
inline constexpr std::array<int, static_cast<int>(BugType::kNumBugTypes)> kBugCounts = {
  {1, 3, 3, 2, 2, 1, 1, 1}
};
inline constexpr Player kPlayerWhite = 0;
inline constexpr Player kPlayerBlack = 1;
inline constexpr HivePosition kOriginPosition { 0, 0, 0 };
inline constexpr HivePosition kNullPosition { 0, 0, -1 };


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
  return static_cast<Direction>((in + 3) % 6);
}

inline Direction ClockwiseDirection(uint8_t in) {
  SPIEL_DCHECK_TRUE(in < Direction::kNumCardinalDirections);
  return static_cast<Direction>((in + 1) % 6);
}

inline Direction CounterClockwiseDirection(uint8_t in) {
  SPIEL_DCHECK_TRUE(in < Direction::kNumCardinalDirections);
  return static_cast<Direction>((in + 5) % 6);
}


// A canonical representation of the physical tiles used in the game. There
// should only ever be 1 instance of a HiveTile per equivalent physical tile
// class HiveTile {
//  public:
//   constexpr HiveTile(HivePosition pos, Player player, BugType type, uint8_t ord)
//     : pos_(pos), player_(player), type_(type), type_ordinal_(ord) {}

//   HiveTile(const HiveTile& other) = delete;
//   HiveTile& operator=(const HiveTile& other) = delete;

//   // ignore position, there should never be two tiles in one spot
//   bool operator==(const HiveTile& other) const {
//     return player_ == other.player_
//            && type_ == other.type_
//            && type_ordinal_ == other.type_ordinal_;
//   }

//   BugType GetBugType() const { return type_; }
//   Player GetPlayer() const { return player_; }
//   HivePosition GetPosition() const { return pos_; }
//   uint8_t GetOrdinal() const { return type_ordinal_; }
//   void SetPosition(HivePosition other) { pos_ = other; }

//   inline bool IsInPlay() const { return pos_ != kNullPosition; }
  
//   // Tile names as defined by the Universal Hive Protocol:
//   // https://github.com/jonthysell/Mzinga/wiki/UniversalHiveProtocol#movestring
//   std::string ToUHP() const;

//   // cute bug emojis
//   std::string ToUnicode() const;

//  private:

//   HivePosition pos_ = kNullPosition;
//   const Player player_;
//   const BugType type_;
//   const uint8_t type_ordinal_; // is it the first/second/third piece of its type played?

// };

// Wrapper class that uses an enum to represent each unique physical tile.
// This would be equivalent to using a uint8_t with bit fields to encode
// colour/type/ordinal, but instead using a class for readability with no 
// extra memory overhead
class NewHiveTile {
 public:
  // a convenient identifier for each unique tile, consistent with UHP names
  // this enum is the ubiquitous list of physical tiles found in the game
  // TODO: LINK TO UHP DEFINITION?
  enum Value : uint8_t {
    // white tiles
    wQ = 0,
    wA1, wA2, wA3,
    wG1, wG2, wG3,
    wS1, wS2,
    wB1, wB2,
    wM,
    wL,
    wP,
    // black tiles
    bQ,
    bA1, bA2, bA3,
    bG1, bG2, bG3,
    bS1, bS2,
    bB1, bB2,
    bM,
    bL,
    bP,
    // ==========
    kNumTiles,
    kNoneTile = kNumTiles
  };

  constexpr NewHiveTile() : tile_name_(kNoneTile) {}
  constexpr NewHiveTile(Value val) : tile_name_(val) {}
  constexpr NewHiveTile(uint8_t val) : tile_name_(static_cast<Value>(val)) {}

  // evaluates to the Value enum when used in expressions
  constexpr operator Value() const { return tile_name_; }
  constexpr bool HasValue() const { return tile_name_ < kNoneTile; }

  // ???????????????
  // Value& operator=(const Value& other) {
  //  return tile_name_ = other;
  // }
  // ???????????????

  static constexpr std::array<NewHiveTile, bQ> GetTilesForColour(Colour c) {
    switch (c) {
      case Colour::kWhite:
        return {wQ, wA1, wA2, wA3, wG1, wG2, wG3, wS1, wS2, wB1, wB2, wM, wL, wP};
      case Colour::kBlack:
        return {bQ, bA1, bA2, bA3, bG1, bG2, bG3, bS1, bS2, bB1, bB2, bM, bL, bP};
    }
  }

  static constexpr Value GetTileFrom(Colour c, BugType type, uint8_t ordinal = 1) {
    uint8_t retval = c == Colour::kWhite ? wQ : bQ;

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
      case BugType::kQueen:
      default:
        // no-op
        break;
    }

    return static_cast<Value>(retval + ordinal - 1);
  }

  static Value UHPToTile(const std::string& uhp) {
    // kinda cheeky but idk
    static std::unordered_map<std::string, Value> string_mapping = {
      {"wQ",  wQ},
      {"wA1", wA1}, {"wA2", wA2}, {"wA3", wA3},
      {"wG1", wG1}, {"wG2", wG2}, {"wG3", wG3},
      {"wS1", wS1}, {"wS2", wS2},
      {"wB1", wB1}, {"wB2", wB2},
      {"wM",  wM},
      {"wL",  wL},
      {"wP",  wP},
      {"bQ",  bQ},
      {"bA1", bA1}, {"bA2", bA2}, {"bA3", bA3},
      {"bG1", bG1}, {"bG2", bG2}, {"bG3", bG3},
      {"bS1", bS1}, {"bS2", bS2},
      {"bB1", bB1}, {"bB2", bB2},
      {"bM",  bM},
      {"bL",  bL},
      {"bP",  bP}
    };

    auto it = string_mapping.find(uhp);
    SPIEL_CHECK_TRUE(it != string_mapping.end());
    return it->second;
  }

  static std::string TileToUHP(NewHiveTile tile) {
    // kinda cheeky but idk
    static std::unordered_map<Value, std::string> enum_mapping = {
      {wQ,  "wQ"},
      {wA1, "wA1"}, {wA2, "wA2"}, {wA3, "wA3"},
      {wG1, "wG1"}, {wG2, "wG2"}, {wG3, "wG3"},
      {wS1, "wS1"}, {wS2, "wS2"},
      {wB1, "wB1"}, {wB2, "wB2"},
      {wM,  "wM"},
      {wL,  "wL"},
      {wP,  "wP"},
      {bQ,  "bQ"},
      {bA1, "bA1"}, {bA2, "bA2"}, {bA3, "bA3"},
      {bG1, "bG1"}, {bG2, "bG2"}, {bG3, "bG3"},
      {bS1, "bS1"}, {bS2, "bS2"},
      {bB1, "bB1"}, {bB2, "bB2"},
      {bM,  "bM"},
      {bL,  "bL"},
      {bP,  "bP"}
    };

    auto it = enum_mapping.find(tile);
    SPIEL_CHECK_TRUE(it != enum_mapping.end());
    return it->second;
  }

  constexpr BugType GetBugType() const {
    switch(tile_name_) {
      case wQ:
      case bQ:
        return BugType::kQueen;
      case wA1 ... wA3:
      case bA1 ... bA3:
        return BugType::kAnt;
      case wG1 ... wG3:
      case bG1 ... bG3:
        return BugType::kGrasshopper;
      case wS1 ... wS2:
      case bS1 ... bS2:
        return BugType::kSpider;
      case wB1 ... wB2:
      case bB1 ... bB2:
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
    switch(tile_name_) {
      case wQ ... wP:
        return Colour::kWhite;
      case bQ ... bP:
        return Colour::kBlack;
      default:
        SpielFatalError("GetColour() - invalid enum value");
    }
  }

  constexpr uint8_t GetOrdinal() const {
    switch(tile_name_) {
      case kNoneTile:
        return 0;
      case wA2: case wG2: case wS2: case wB2:
      case bA2: case bG2: case bS2: case bB2:
        return 2;
      case wA3: case wG3:
      case bA3: case bG3:
        return 3;
      default:
        return 1;
    }
  }

  std::string ToUHP(bool use_emojis = false) const;

 private:
  Value tile_name_;

};


// TODO: wtf
using TileIdx = NewHiveTile;


// Encodes a move as defined by the Universal Hive Protocol
// {INSERT LINK} //////
struct Move {
  //Player player;          // whose turn was it during this move
  NewHiveTile from;       // the tile that's being moved
  NewHiveTile to;         // the reference tile
  Direction direction;    // offset applied to the reference tile

  std::string ToUHP();
  bool IsPass() { return !from.HasValue(); }
  // HivePosition StartPosition() const { return from->GetPosition(); }
  // HivePosition EndPosition() const { return to->GetPosition() + kNeighbourOffsets[direction]; }
};


// HexBoard
//
// One of the most glaring problems to solve for Hive is how to represent an
// infinitely-sized board in a fixed-sized manner? This is especially the case
// when also needing an accurate 2D representation of the board state for use
// as an ObservationTensor.
//
// While the game logic could easily be implemented with a wrap-around grid big
// enough to account for all tiles (a 29x29 grid for all expansion pieces), the
// resulting ObservationTensor would be:
//   1) massively large in size (compared to the typical size of a Hive game)
//   2) be extremely sparse, which would negatively affect learning, and
//   3) unsuitable for 2D convolution in AlphaZero with no way to account for
//      hexagonal wrapping of the tensor (that I know of). And even if there
//      was a potential solution, a vast majority of playthroughs would be 
//      unlikely to ever reach this state.
//
// With all of that in mind, I have chosen the following board design:
//   - the board will be stored as a fixed-sized and flattened 2d array where
//     each index contains an enum describing either the existance of a
//     specific tile, or an empty space on the grid
//   - each tile enum can be used to index into fixed-sized arrays that store
//     information about that specific tile. e.g. tile_positions_[::wA2] stores
//     the HivePosition of white's 2nd Ant tile.
//   - all of the game logic is computed using the Axial coordinate system
//     (described above under HivePosition), then later translated to an index
//     when needed for the ObservationTensor. This helps with the maths and
//     allows for quick computation for rotational and reflectional symmetry
//
// Example board state with radius = 2 to illustrate:
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
class HexBoard {
 public:

  // Creates a regular hexagonal board with given radius from the center
  HexBoard(const int board_radius, bool uses_mosquito, bool uses_ladybug, bool uses_pillbug);

  //int NumUniqueTiles() const { return num_tiles_; }
  int Radius() const { return hex_radius_; }
  int SquareDimensions() const { return Radius() * 2 + 1; }

  // Axial position (Q,R) is stored at the 2d-index: 
  //   grid_[R + Radius()][Q + Radius()]
  // which translates to the flattened index:
  //   grid_[Q + Radius() + ((R + Radius()) * SqDims)]
  size_t AxialToIndex(HivePosition pos) const {
    return pos.Q() + Radius() + ((pos.R() + Radius()) * SquareDimensions());
  }

  // std::pair<size_t, size_t> AxialTo2DIndices(HivePosition pos) const {
  //   return {pos.R() + Radius(), pos.Q() + Radius()};
  // }

  NewHiveTile GetTopTileAt(HivePosition pos) const;
  // NewHiveTile GetTopTileAt(TileIdx idx) const;
  NewHiveTile GetTileAbove(HivePosition pos) const;
  NewHiveTile GetTileBelow(HivePosition pos) const;
  HivePosition GetPositionOf(NewHiveTile tile) const {
    return tile.HasValue() ? tile_positions_[tile] : kNullPosition;
  }
  // NewHiveTile GetTileFromUHP(const std::string& uhp) const;
  //int GetStackSizeAt(HivePosition pos) const;
  const std::vector<NewHiveTile>& GetPlayedTiles() const { return played_tiles_; }

  // int EncodeTile(NewHiveTile& tile) const;
  // NewHiveTile DecodeTile(int encoding) const;

  HivePosition LastMovedFrom() const;
  NewHiveTile LastMovedTile() const;
  bool MoveTile(Move move);
  void Pass();
  std::vector<NewHiveTile> NeighboursOf(HivePosition pos, HivePosition to_ignore = kNullPosition) const;

  bool IsInPlay(NewHiveTile tile) const {
    return tile.HasValue() && tile_positions_[tile] != kNullPosition;
  }
  bool IsInPlay(Colour c, BugType type, int ordinal = 1) const {
    return IsInPlay(NewHiveTile::GetTileFrom(c, type, ordinal));
  }
  bool IsQueenSurrounded(Colour c) const;
  bool IsGated(HivePosition pos, Direction d, HivePosition to_ignore = kNullPosition) const;
  bool IsConnected(HivePosition pos, HivePosition to_ignore) const;
  bool IsCovered(HivePosition pos) const;
  bool IsCovered(NewHiveTile tile) const;
  bool IsPinned(HivePosition pos) const;
  bool IsPinned(NewHiveTile tile) const;
  bool IsPlaceable(Colour c, HivePosition pos) const {
    return colour_influence_[static_cast<int>(c)].contains(pos) &&
          !colour_influence_[static_cast<int>(OtherColour(c))].contains(pos) &&
          !IsInPlay(GetTopTileAt(pos));
  }

  void GenerateAllMoves(std::vector<Move>& out, Colour to_move, int move_number) const;

  // using an explicitly provided BugType for the Mosquito
  void GenerateMovesFor(std::vector<Move>& out, NewHiveTile tile, BugType acting_type, Colour to_move) const;
  
 private:

  void GeneratePlacementMoves(std::vector<Move>& out, Colour to_move, int move_number) const;
  void GenerateValidSlides(absl::flat_hash_set<HivePosition>& out, NewHiveTile tile, HivePosition pos, int distance) const;
  void GenerateValidClimbs(absl::flat_hash_set<HivePosition>& out, NewHiveTile tile, HivePosition pos) const;
  void GenerateValidGrasshopperPositions(absl::flat_hash_set<HivePosition>& out, NewHiveTile tile, HivePosition pos) const;
  void GenerateValidLadybugPositions(absl::flat_hash_set<HivePosition>& out, NewHiveTile tile, HivePosition pos) const;
  void GenerateValidMosquitoPositions(std::vector<Move>& out, NewHiveTile tile, HivePosition pos, Colour to_move) const;
  void GenerateValidPillbugSpecials(std::vector<Move>& out, NewHiveTile tile, HivePosition pos) const;

  // void MaybeUpdateSlideCache();
  void UpdateArticulationPoints();
  void UpdateInfluence(Colour col);
  // std::vector<TileReference> NeighbourReferenceTiles(HivePosition& pos);

  int hex_radius_;
  int num_tiles_;

  // see class description for explanation
  std::vector<NewHiveTile> tile_grid_;
  std::vector<NewHiveTile> played_tiles_;
  std::array<HivePosition, kMaxTileCount> tile_positions_;

  // there are max 6 tiles that can climb on the hive to cover a tile
  std::array<NewHiveTile, 7> covered_tiles_;

  //absl::flat_hash_map<HivePosition, NewHiveTile> position_cache_;

  // Every slide has a direction and a reference tile that it "slides around"
  // to maintain connectivity. A slide will be valid for any tile, except the
  // reference tile, as that tile is required to move in the given direction
  // absl::flat_hash_map<HivePosition, std::vector<TileReference>> slidability_cache_;

// TODO CHANGE DUH //
public:
  absl::flat_hash_set<HivePosition> articulation_points_; 
  int largest_radius = 0;
private:
/////////////////////
  NewHiveTile last_moved_;
  HivePosition last_moved_from_;


  // contains the positions surrounding played tiles. Used for placement rules
  std::array<absl::flat_hash_set<HivePosition>, 2> colour_influence_;

  

};

}  // namespace hive
}  // namespace open_spiel

#endif  // OPEN_SPIEL_GAMES_HIVE_HEXBOARD_H_
