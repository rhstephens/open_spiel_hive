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


// Defined by a regular hexagonal grid using an Axial co-ordinate system (q,r)
// as well as a height to account for beetles on top of the hive
// https://www.redblobgames.com/grids/hexagons/#coordinates-axial
class HivePosition {
 public:

  constexpr HivePosition(const int8_t q = 0, const int8_t r = 0, const int8_t h = 0) 
    : q_(q), r_(r), h_(h), s_(-q - r) {}

  int8_t Q() const { return q_; }
  int8_t R() const { return r_; }

  // height above the hive, where 0 == "ground"
  int8_t H() const { return h_; }

  // implicit third coordinate s = -q - r to satisfy constraint: q + r + s = 0
  int8_t S() const { return s_; }

  bool operator==(HivePosition& other) const {
    return q_ == other.q_ && r_ == other.r_ && h_ == other.h_; 
  } 
  bool operator==(const HivePosition& other) const {
    return q_ == other.q_ && r_ == other.r_ && h_ == other.h_; 
  }

  inline HivePosition operator+(const HivePosition& other) {
    return HivePosition(q_ + other.q_, r_ + other.r_, h_ + other.h_);
  }

  HiveOffset DistanceTo(const HivePosition& other) const;

 private:
  int8_t q_{};
  int8_t r_{};
  int8_t s_{};
  int8_t h_{};

};


using HiveOffset = HivePosition;

inline constexpr int kPlayerWhite = 0;
inline constexpr int kPlayerBlack = 1;
inline constexpr HivePosition kOriginPosition { 0, 0, 0 };
inline constexpr HivePosition kNullPosition { 0, 0, -1 };

// All offsets starting at top-right neighbour, and then rotating clockwise
const std::array<HiveOffset,6> kNeighbourOffsets = {
  {{1,-1}, {1,0}, {0,1}, {-1,1}, {-1,0}, {0,-1}}
};

// official names for each bug tile in the Universal Hive Protocol
const std::unordered_map<BugType, const char*> kBugTypeToUHP = {
  {BugType::kQueen,       "Q"},
  {BugType::kAnt,         "A"},
  {BugType::kGrasshopper, "G"},
  {BugType::kSpider,      "S"},
  {BugType::kBeetle,      "B"},
  {BugType::kMosquito,    "M"},
  {BugType::kLadybug,     "L"},
  {BugType::kPillbug,     "P"}
};


class HiveTile {

 public:
  HiveTile(const HivePosition& pos, BugType type, Player player)
    : pos_(pos), type_(type), player_(player), type_ordinal_(0) {}

  bool operator==(const HiveTile& other) const {
    return pos_ == other.pos_ && type_ == other.type_ && player_ == other.player_;
  }

  constexpr bool IsInHand() const { return type_ordinal_ == 0; }
  
  // Tile names as defined by the Universal Hive Protocol:
  // https://github.com/jonthysell/Mzinga/wiki/UniversalHiveProtocol#movestring
  std::string ToUHP() const;

  size_t HashValue() const;

 private:
  HivePosition pos_;
  BugType type_ = BugType::kNone;
  Player player_ = kInvalidPlayer;
  int type_ordinal_;

};

class HiveGame;

class HexBoard {
  
 public:

  // Creates a regular hexagonal board with a radius of 8 (excluding the center)
  // The formula is: 3r^2 + 3r + 1 + an extra tile for each stackable piece
  // 
  HexBoard(std::shared_ptr<const HiveGame> game, const int board_radius,
           const int num_stackable = 6);

  HiveTile& GetTileAt(const HivePosition& pos) const;

  HiveTile& GetTileFromUHP(const std::string& uhp) const;

  HiveTile& GetNeighbourFrom(const HivePosition& pos, const HiveOffset& offset) const;

  HiveTile& GetTileAbove(const HiveTile& tile) const;
  
 private:
  const int board_radius_;
  const int num_tiles_;
  const int num_stackable_;
  std::shared_ptr<const HiveGame> game_;
  absl::flat_hash_set<HiveTile> all_tiles_;
  absl::flat_hash_set<HiveTile> player_tiles_;

  // using the zobrist hashing technique commonly used in chess programming
  // to uniquely hash the state of the board and any actions applied to it
  //uint64_t zobrist_hash_;

  const std::unordered_map<std::string, HiveOffset> string_to_offset_ = {
    {"/", kNeighbourOffsets[0]},
    {"-", kNeighbourOffsets[1]},
    {"\\", kNeighbourOffsets[2]},
    {"/", kNeighbourOffsets[3]},
    {"-", kNeighbourOffsets[4]},
    {"\\", kNeighbourOffsets[5]}
  };

};

}  // namespace hive
}  // namespace open_spiel

#endif  // OPEN_SPIEL_GAMES_HIVE_HEXBOARD_H_
