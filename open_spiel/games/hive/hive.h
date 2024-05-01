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

#ifndef OPEN_SPIEL_GAMES_HIVE_H_
#define OPEN_SPIEL_GAMES_HIVE_H_

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "open_spiel/abseil-cpp/absl/container/flat_hash_set.h"
#include "open_spiel/games/hive/hive_hexboard.h"
#include "open_spiel/spiel.h"

// https://en.wikipedia.org/wiki/Hive_(game)
//
// Parameters:
//   "board_size"        int     radius of the board   (default = 8)
//   "swap"              bool    Whether to allow the swap rule.
//   "ansi_color_output" bool    Whether to color the output for a terminal.

namespace open_spiel {
namespace hive {

inline constexpr int kNumPlayers = 2;
inline constexpr int kDefaultBoardRadius = 8; // Assumes a regular Hexagonal layout
inline constexpr int kNumStackableTiles = 6; // number of beetles/mosquitos
inline constexpr int kNumNeighbours = 6; // ???????????????????????????????


//typedef std::array<int, 2> Direction__;
//struct Direction { int8_t q_offset{}; int8_t r_offset{}; int8_t h_offset{}; };


// List of neighbors of a cell: [cell][direction]
//typedef std::vector<std::array<Move, kNumNeighbours>> NeighborList;

// State of an in-play game.
class HiveState : public State {
 public:

  // returns the total amount of tiles each player starts with per BugType
  // (may need to modify if allowing less expansions than the full game...?)
  static constexpr uint8_t BugTypeCount(BugType type) {
    switch (type) {
      case BugType::kQueen:
        return 1;
      case BugType::kAnt:
        return 3;
      case BugType::kGrasshopper:
        return 3;
      case BugType::kSpider:
        return 2;
      case BugType::kBeetle:
        return 2;
      case BugType::kLadybug:
        return 1;
      case BugType::kMosquito:
        return 1;
      case BugType::kPillbug:
        return 1;
      default:
        return 0;
    }
  };

  HiveState(std::shared_ptr<const Game> game, int board_size = kDefaultBoardRadius);

  HiveState(const HiveState&) = default;


  // overrides

  inline Player CurrentPlayer() const override {
    return IsTerminal() ? kTerminalPlayerId : current_player_;
  }
  std::string ActionToString(Player player, Action action_id) const override;
  std::string ToString() const override;
  Action StringToAction(Player player,
                        const std::string& move_str) const override;
  bool IsTerminal() const override {
    return board_->IsQueenSurrounded(kPlayerWhite) ||
           board_->IsQueenSurrounded(kPlayerBlack); 
  }
  std::vector<double> Returns() const override;
  std::string InformationStateString(Player player) const override;
  std::string ObservationString(Player player) const override;

  // A 3d tensor, 3 player-relative one-hot 2d planes. The layers are: the
  // specified player, the other player, and empty.
  void ObservationTensor(Player player,
                         absl::Span<float> values) const override;
  std::unique_ptr<State> Clone() const override;
  std::vector<Action> LegalActions() const override;

  // LINK TO UHP DEFINITION OF:
  // GameTypeString;GameStateString;TurnString;MoveString1;MoveString2;MoveStringN
  std::string Serialize() const override;


  // custom
  Move ActionToMove(Action action) const;
  Action MoveToAction(Move& move) const;
  //HiveTilePtr& StringToTile(const std::string& str) const;


 protected:
  void DoApplyAction(Action action) override;

 private:

  inline void BugPlayed(BugType type) { ++type_played_counts_.at(type); }

  Player current_player_ = kPlayerWhite;
  std::unique_ptr<HexBoard> board_;
  std::unordered_map<BugType,uint8_t> type_played_counts_ = {
    {BugType::kQueen,       0},
    {BugType::kAnt,         0},
    {BugType::kGrasshopper, 0},
    {BugType::kSpider,      0},
    {BugType::kBeetle,      0},
    {BugType::kMosquito,    0},
    {BugType::kLadybug,     0},
    {BugType::kPillbug,     0}
  };
};


// Game object.
class HiveGame : public Game {
 public:
  explicit HiveGame(const GameParameters& params);

  
  std::array<int,3> ActionsShape() const { return {7,28,28}; }
  int NumDistinctActions() const override { return 5488 + 1; } // +1 for pass
  inline std::unique_ptr<State> NewInitialState() const override {
    return std::unique_ptr<State>(
        new HiveState(shared_from_this(), 8));
  }
  int NumPlayers() const override { return kNumPlayers; }
  double MinUtility() const override { return -1; }
  absl::optional<double> UtilitySum() const override { return 0; }
  double MaxUtility() const override { return 1; }
  std::vector<int> ObservationTensorShape() const override {
    return { 16 /*num bug types x num_players */ + 2 /* player influence plane */ + 1 /* articulation points */,
    2*kBoardRadius + 1, /*dimensions of a sq board from hex board is: (2*radius + 1)*/
    2*kBoardRadius + 1};
  }
  int MaxGameLength() const override { return 250; }

  std::unique_ptr<State> DeserializeState(const std::string& str) const override;
  
 private:
  const int kBoardRadius;
};


}  // namespace hive
}  // namespace open_spiel

#endif  // OPEN_SPIEL_GAMES_HIVE_H_
