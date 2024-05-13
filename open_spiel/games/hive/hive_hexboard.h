// Copyright 2019 DeepMind Technologies Limited
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
//#include "open_spiel/games/chess/chess_common.h"
#include "open_spiel/spiel.h"


namespace open_spiel {
namespace hive {


enum class BugType : int8_t {
  kNone = -1,
  kQueen = 0,
  kAnt,
  kGrasshopper,
  kSpider,
  kBeetle,
  kMosquito,
  kLadybug,
  kPillbug,
  kNumBugTypes
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

class HivePosition;
using HiveOffset = HivePosition;

// Defined by a regular hexagonal grid using an Axial co-ordinate system (q,r)
// as well as a height to account for beetles on top of the hive
// https://www.redblobgames.com/grids/hexagons/#coordinates-axial
class HivePosition {
 public:

  constexpr HivePosition(int8_t q = 0, int8_t r = 0, int8_t h = 0) 
    : q_(q), r_(r), s_(-q - r), h_(h) {}

  int8_t Q() const { return q_; }
  int8_t R() const { return r_; }

  // implicit third coordinate s = -q - r to satisfy constraint: q + r + s = 0
  // TODO: may not be needed???
  int8_t S() const { return s_; }

  // height above the hive, where 0 == "ground"
  int8_t H() const { return h_; }

  inline bool operator==(const HivePosition& other) const {
    return q_ == other.q_ && r_ == other.r_ && h_ == other.h_; 
  }

  inline bool operator!=(const HivePosition& other) const {
    return !operator==(other);
  }

  inline HivePosition operator+(const HivePosition& other) const {
    return HivePosition(q_ + other.q_, r_ + other.r_, h_ + other.h_);
  }

  inline HivePosition operator-(const HivePosition& other) const {
    return HivePosition(q_ - other.q_, r_ - other.r_, h_ - other.h_);
  }

  inline HivePosition& operator+=(const HivePosition& other) {
    q_ += other.q_;
    r_ += other.r_;
    h_ += other.h_;

    // recompute implicit S
    s_ = -q_ -r_;

    return *this;
  }

  std::string ToString() const {
    return absl::StrCat("(", std::to_string(q_), ", ", std::to_string(r_), ", ", std::to_string(h_), ")");
  }

 private:
  int8_t q_;
  int8_t r_;
  int8_t s_;
  int8_t h_;

};

inline std::ostream& operator<<(std::ostream& stream, const HivePosition& pos) {
  return stream << pos.ToString();
}

// support hashing for HivePosition
template <typename H>
H AbslHashValue(H state, const HivePosition& pos) {
  return H::combine(std::move(state), pos.Q(), pos.R(), pos.H());
}

inline constexpr Player kPlayerWhite = 0;
inline constexpr Player kPlayerBlack = 1;
inline constexpr HivePosition kOriginPosition { 0, 0, 0 };
inline constexpr HivePosition kNullPosition { 0, 0, -1 };

// All offsets starting at top-right neighbour, and then rotating clockwise,
// plus above for beetles/mosquitos
const std::array<HiveOffset, Direction::kNumAllDirections> kNeighbourOffsets = {
  //  NE       E      SE       SW       W       NW       Above
  {{1, -1}, {1, 0}, {0, 1}, {-1, 1}, {-1, 0}, {0, -1}, {0, 0, 1}}
};

// official names for each bug tile in the Universal Hive Protocol
// TODO: Change to constexpr switch function instead of map
const std::unordered_map<char, BugType> kUHPToBugType = {
  {'Q', BugType::kQueen}, {'q', BugType::kQueen},
  {'A', BugType::kAnt}, {'a', BugType::kAnt},
  {'G', BugType::kGrasshopper}, {'g', BugType::kGrasshopper},
  {'S', BugType::kSpider}, {'s', BugType::kSpider},
  {'B', BugType::kBeetle}, {'b', BugType::kBeetle},
  {'M', BugType::kMosquito}, {'m', BugType::kMosquito},
  {'L', BugType::kLadybug}, {'l', BugType::kLadybug},
  {'P', BugType::kPillbug}, {'p', BugType::kPillbug}
};


// A canonical representation of the physical tiles used in the game.
// There should only ever be 1 instance of a HiveTile per equivalent physical
// tile, and anything else should be a pointer to that HiveTile
class HiveTile {
 public:
  HiveTile(const HivePosition& pos, Player player, BugType type, uint8_t ord)
    : pos_(pos), player_(player), type_(type), type_ordinal_(ord) {}

  // ignore position, there should never be two tiles in one spot
  bool operator==(const HiveTile& other) const {
    return player_ == other.player_
           && type_ == other.type_
           && type_ordinal_ == other.type_ordinal_;
  }

  BugType GetBugType() const { return type_; }
  Player GetPlayer() const { return player_; }
  HivePosition GetPosition() const { return pos_; }
  uint8_t GetOrdinal() const { return type_ordinal_; }
  void SetPosition(HivePosition other) { pos_ = other; }
  void GetNeighbours(std::vector<std::shared_ptr<HiveTile>>& in_vec);
  void GetEmptyNeighbours(std::vector<HivePosition>& in_vec);
  void GetAllAbove(std::vector<std::shared_ptr<HiveTile>>& in_vec);
  void GetAllBelow(std::vector<std::shared_ptr<HiveTile>>& in_vec);

  inline bool IsInPlay() const { return pos_ != kNullPosition; }
  
  // Tile names as defined by the Universal Hive Protocol:
  // https://github.com/jonthysell/Mzinga/wiki/UniversalHiveProtocol#movestring
  std::string ToUHP() const;

  // cute bug emojis
  std::string ToUnicode() const;

  //size_t HashValue() const;

 private:
  std::vector<std::shared_ptr<HiveTile>> stack_; // (directly above or below)

  HivePosition pos_ = kNullPosition;
  const Player player_;
  const BugType type_;
  const uint8_t type_ordinal_; // is it the first/second/third piece of its type played?

};

using HiveTilePtr = std::shared_ptr<HiveTile>;
using HiveTileList = std::vector<HiveTilePtr>;

// special tile under (0,0) to enforce the first tile location at the origin and
// to represent empty tiles
// HiveTile kEmptyTile = {
//   kNullPosition, kInvalidPlayer, BugType::kNone, 0
// };


// Encodes a move as defined by the Universal Hive Protocol
// {INSERT LINK} //////
struct Move {
  //Player player;          // whose turn was it during this move
  HiveTilePtr from;       // the tile that's being moved
  HiveTilePtr to;         // the reference tile
  Direction direction;    // offset applied to the reference tile
  bool is_pass = false;

  std::string ToUHP();
  HivePosition StartPosition() { return from->GetPosition(); }
  HivePosition EndPosition() { return to->GetPosition() + kNeighbourOffsets[direction]; }
};


class HexBoard {
 public:

  // Creates a regular hexagonal board with given radius from the center
  // The formula is: 3r^2 + 3r + 1 + an extra tile for each stackable piece
  HexBoard(std::shared_ptr<const Game> game, const int board_radius,
           const int num_stackable = 6);

  int NumUniqueTiles() const { return num_tiles_; }

  void CreateTile(const Player player, const BugType type, int ordinal);

  absl::optional<HiveTilePtr> GetTopTileAt(const HivePosition& pos);
  absl::optional<HiveTilePtr> GetTileAbove(const HivePosition& pos);
  absl::optional<HiveTilePtr> GetTileBelow(const HivePosition& pos);
  HiveTilePtr& GetTileFromUHP(const std::string& uhp);

  // void GetNeighbourTilePositions(std::vector<HivePosition>& in_vec, const HivePosition pos);
  // void GetNeighbourEmptyPositions(std::vector<HivePosition>& in_vec, const HivePosition pos);

  int EncodeTile(HiveTilePtr& tile) const;
  HiveTilePtr& DecodeTile(int encoding);

  int Radius() const { return board_radius_; }

  const HiveTilePtr& LastMovedTile() const;
  const HiveTilePtr& LastStunnedTile() const;
  HiveTileList NeighboursOf(HivePosition pos);
  void PlaceTile(HiveTilePtr& tile, HivePosition new_pos);
  void MoveTile(HiveTilePtr& tile, HivePosition new_pos, Player player);
  bool IsInPlay(Player player, BugType type, int ordinal = -1) const;
  bool IsQueenSurrounded(Player player);

  void GenerateAllMoves(std::vector<Move>& out, Player player, int move_number);
  void GenerateMovesFor(std::vector<Move>& out, const HivePosition& pos, BugType type, Player player);
  void Pass();
  
 private:
  struct TileReference {
    HiveTilePtr tile;
    Direction dir;
  };

  void GeneratePlacementMoves(std::vector<Move>& out, Player player, int move_number);

  void GenerateValidSlides(std::vector<HivePosition>& out, const HivePosition& pos, int distance);
  void GenerateValidClimbs(std::vector<HivePosition>& out, const HivePosition& pos);
  void GenerateValidGrasshopperPositions(std::vector<HivePosition>& out, const HivePosition& pos);
  void GenerateValidLadybugPositions(std::vector<HivePosition>& out, const HivePosition& pos);
  void GenerateValidMosquitoPositions(std::vector<HivePosition>& out, const HivePosition& pos);
  void GenerateValidPillbugSpecials(std::vector<Move>& out, const HivePosition& pos);

  bool IsGated(const HivePosition& pos, Direction d) const;
  bool IsCovered(const HivePosition& pos) const;
  bool IsPinned(const HivePosition& pos) const {
    return articulation_points_.contains(pos);
  }

  void MaybeUpdateSlideCache();
  void MaybeUpdateArticulationPoints();
  void UpdateInfluence(Player player);
  std::vector<TileReference> NeighbourReferenceTiles(HivePosition& pos);

  const int board_radius_;
  const int num_tiles_;
  const int num_stackable_;
  std::shared_ptr<const Game> game_; // TODO: remove

  // this first vec contains the initial instantiation of each tile in memory,
  // while the other hashmaps are used for quick lookups based on other 
  // hashable features of the tile
  HiveTileList tiles_;
  HiveTileList played_tiles_;
  std::array<HiveTileList, 2> player_tiles_;
  absl::flat_hash_map<BugType, HiveTileList> type_tiles_;
  absl::flat_hash_map<HivePosition, HiveTilePtr> position_cache_;

  // minimal graph representation of the hive used for quick game logic
  // stores the encoded tile
  // struct Node {
  //   int tile;
  //   int above = -1;
  //   int below = -1;
  //   std::array<int, Direction::kNumCardinalDirections> neighbours = { -1 };

  //   void ClearNeighbours() { neighbours.fill(-1); above = -1; below = -1; }
  // };

  
  

  // Every slide has a direction and a reference tile that it "slides around"
  // to maintain connectivity. A slide will be valid for any tile, except the
  // reference tile, as that tile is required to move in the given direction
  absl::flat_hash_map<HivePosition, std::vector<TileReference>> slidability_cache_;

  absl::flat_hash_set<HivePosition> articulation_points_;
  absl::flat_hash_map<HivePosition, std::vector<Direction>> climbability_cache_;
  HiveTilePtr last_moved_;
  HiveTilePtr last_stunned_;
  bool cache_valid_;
  
  // contains the positions surrounding played tiles. Used for placement rules
  std::array<absl::flat_hash_set<HivePosition>, 2> player_influence_;
};

}  // namespace hive
}  // namespace open_spiel

#endif  // OPEN_SPIEL_GAMES_HIVE_HEXBOARD_H_
