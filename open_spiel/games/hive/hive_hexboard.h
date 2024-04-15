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


inline constexpr Player kPlayerWhite = 0;
inline constexpr Player kPlayerBlack = 1;

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
  kNumDirections
};

class HivePosition;
using HiveOffset = HivePosition;

// Defined by a regular hexagonal grid using an Axial co-ordinate system (q,r)
// as well as a height to account for beetles on top of the hive
// https://www.redblobgames.com/grids/hexagons/#coordinates-axial
class HivePosition {
 public:

  constexpr HivePosition(const int8_t q = 0, const int8_t r = 0, const int8_t h = 0) 
    : q_(q), r_(r), s_(-q - r), h_(h) {}

  int8_t Q() const { return q_; }
  int8_t R() const { return r_; }

  // implicit third coordinate s = -q - r to satisfy constraint: q + r + s = 0
  int8_t S() const { return s_; }

  // height above the hive, where 0 == "ground"
  int8_t H() const { return h_; }

  inline bool operator==(HivePosition& other) const {
    return q_ == other.q_ && r_ == other.r_ && h_ == other.h_; 
  } 
  inline bool operator==(const HivePosition& other) const {
    return q_ == other.q_ && r_ == other.r_ && h_ == other.h_; 
  }

  inline HivePosition operator+(const HivePosition& other) const {
    return HivePosition(q_ + other.q_, r_ + other.r_, h_ + other.h_);
  }

  HiveOffset DistanceTo(const HivePosition& other) const;

 private:
  int8_t q_;
  int8_t r_;
  int8_t s_;
  int8_t h_;

};

// support hashing for HivePosition
template <typename H>
H AbslHashValue(H state, const HivePosition& pos) {
  return H::combine(std::move(state), pos.Q(), pos.R(), pos.H());
}

inline constexpr HivePosition kOriginPosition { 0, 0, 0 };
inline constexpr HivePosition kNullPosition { 0, 0, -1 };

// All offsets starting at top-right neighbour, and then rotating clockwise,
// plus above for beetles/mosquitos
const std::array<HiveOffset,kNumDirections> kNeighbourOffsets = {
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
  HiveTile(const HivePosition& pos, Player player, BugType type, int ord)
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
  int GetOrdinal() const { return type_ordinal_; }
  void SetPosition(HivePosition& other) { pos_ = other; }
  void GetNeighbours(std::vector<std::shared_ptr<HiveTile>>& in_vec);
  void GetEmptyNeighbours(std::vector<HivePosition>& in_vec);
  void GetAllAbove(std::vector<std::shared_ptr<HiveTile>>& in_vec);
  void GetAllBelow(std::vector<std::shared_ptr<HiveTile>>& in_vec);



  constexpr bool IsInHand() const { return pos_ == kNullPosition; }
  
  // Tile names as defined by the Universal Hive Protocol:
  // https://github.com/jonthysell/Mzinga/wiki/UniversalHiveProtocol#movestring
  std::string ToUHP() const;

  // cute bug emojis
  std::string ToUnicode() const;

  //size_t HashValue() const;

 private:
  std::array<std::shared_ptr<HiveTile>,kNumDirections-1> neighbours_;
  std::vector<std::shared_ptr<HiveTile>> stack_; // (directly above or below)

  HivePosition pos_ = kNullPosition;
  const Player player_;
  const BugType type_;
  const int type_ordinal_; // is it the first/second/third piece of its type played?

};

using HiveTilePtr = std::shared_ptr<HiveTile>;
using HiveTileSet = absl::flat_hash_set<HiveTilePtr>;

// special tile under (0,0) to enforce the first tile location at the origin and
// to represent empty tiles
// HiveTile kEmptyTile = {
//   kNullPosition, kInvalidPlayer, BugType::kNone, 0
// };


// support heterogenous lookup for HiveTiles
// template <typename H>
// H AbslHashValue(H state, const HiveTile& tile) {
//   return H::combine(std::move(state), tile.player_, tile.type_, tile.type_ordinal_);
// }


class HexBoard {
 public:

  // Creates a regular hexagonal board with given radius (excluding the center)
  // The formula is: 3r^2 + 3r + 1 + an extra tile for each stackable piece
  // 
  HexBoard(std::shared_ptr<const Game> game, const int board_radius,
           const int num_stackable = 6);

  int NumUniqueTiles() const { return num_tiles_; }

  void CreateTile(const Player player, const BugType type, const int ordinal);

  absl::optional<HiveTilePtr> GetTileAt(const HivePosition& pos) const;
  absl::optional<HiveTilePtr> GetTileFromUHP(const std::string& uhp) const;

  void GetNeighbourTilePositions(std::vector<HivePosition>& in_vec, const HivePosition pos);
  void GetNeighbourEmptyPositions(std::vector<HivePosition>& in_vec, const HivePosition pos);

  HiveTile& GetTileAbove(const HiveTile& tile) const;

  int EncodeTile(HiveTilePtr& tile, const Player player) const;
  HiveTilePtr& DecodeTile(int encoding, const Player player);

  int Radius() const { return board_radius_; }

  
 private:
  void MoveTile(HiveTilePtr& tile, HivePosition new_position);

  const int board_radius_;
  const int num_tiles_;
  const int num_stackable_;
  std::shared_ptr<const Game> game_; // TODO: remove

  // this first vec contains the initial instantiation of each tile in memory,
  // while the other hashmaps are used for quick lookups based on other 
  // hashable features of the tile
  std::vector<HiveTilePtr> tiles_;
  HiveTileSet played_tiles_;
  absl::flat_hash_map<Player, HiveTileSet> player_tiles_;
  absl::flat_hash_map<BugType, HiveTileSet> type_tiles_;
  absl::flat_hash_map<HivePosition, HiveTilePtr> position_cache_;


  // using the zobrist hashing technique commonly used in chess programming
  // to uniquely hash the state of the board and any actions applied to it
  //uint64_t zobrist_hash_;


  // not needed?????
  // const std::unordered_map<HivePosition, std::string> offset_to_string = {
  //   {kNeighbourOffsets[0], "/"},
  //   {kNeighbourOffsets[1], "-"},
  //   {kNeighbourOffsets[2], "\\"},
  //   {kNeighbourOffsets[3], "/"},
  //   {kNeighbourOffsets[4], "-"},
  //   {kNeighbourOffsets[5], "\\"}
  // };

};

}  // namespace hive
}  // namespace open_spiel

#endif  // OPEN_SPIEL_GAMES_HIVE_HEXBOARD_H_
