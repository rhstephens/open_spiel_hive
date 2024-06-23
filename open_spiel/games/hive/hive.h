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
inline constexpr int kDefaultNumBugTypes = 5;
inline constexpr const char* kDefaultUHPGameType = "Base+PLM";
inline constexpr const char* kUHPNotStarted = "NotStarted";
inline constexpr const char* kUHPInProgress = "InProgress";
inline constexpr const char* kUHPWhiteWins = "WhiteWins";
inline constexpr const char* kUHPBlackWins = "BlackWins";
inline constexpr const char* kUHPDraw = "Draw";


struct ExpansionInfo {
  bool uses_mosquito;
  bool uses_ladybug;
  bool uses_pillbug;
};


// State of an in-play game.
class HiveState : public State {
 public:

  explicit HiveState(std::shared_ptr<const Game> game, int board_size = kDefaultBoardRadius, ExpansionInfo expansions = {}, int num_bug_types = kDefaultNumBugTypes);

  HiveState(const HiveState&) = default;
  HiveState& operator=(const HiveState&) = default;

  // overrides

  inline Player CurrentPlayer() const override {
    return IsTerminal() ? kTerminalPlayerId : current_player_;
  }
  std::string ActionToString(Player player, Action action_id) const override;
  std::string ToString() const override;
  Action StringToAction(Player player,
                        const std::string& move_str) const override;
  bool IsTerminal() const override {
    return WinConditionMet(kPlayerWhite) ||
           WinConditionMet(kPlayerBlack) ||
           MoveNumber() >= game_->MaxGameLength() ||
           force_terminal_; 
  }
  std::vector<double> Returns() const override;
  std::string InformationStateString(Player player) const override;
  std::string ObservationString(Player player) const override;

  // A 3d-tensor where each binary 2d-plane represents the following features:
  // (0-7):  current player's bugs in play for each of the 8 bug types
  // (8-15): opposing player's bugs in play for each of the 8 bug types
  // (16):   current player's "pinned" bugs
  // (17):   opposing player's "pinned" bugs
  // (18):   current player's valid placement positions
  // (19):   opposing player's valid placement positions
  // (20):   current player's "covered" bugs
  // (21):   opposing player's "covered" bugs
  void ObservationTensor(Player player,
                         absl::Span<float> values) const override;
  std::unique_ptr<State> Clone() const override;
  std::vector<Action> LegalActions() const override;

  // LINK TO UHP DEFINITION OF:
  // GameTypeString;GameStateString;TurnString;MoveString1;...;MoveStringN
  std::string Serialize() const override;


  // non-overrides
  HexBoard& Board() { return board_; }
  const HexBoard& Board() const { return board_; }

  Move ActionToMove(Action action) const;
  Action MoveToAction(Move& move) const;
  Action PassAction() const { return NumDistinctActions() - 1; }
  std::string PrintBoard(NewHiveTile tile_to_move = NewHiveTile::kNoneTile) const;
  std::string ProgressString() const;
  std::string TurnString() const;
  std::string MovesString() const;
  inline bool WinConditionMet(Player player) const {
    return Board().IsQueenSurrounded(OtherColour(PlayerToColour(player)));
  }
  bool BugTypeIsEnabled(BugType type) const;

  /// TODO REMOVE
  absl::flat_hash_set<HivePosition>& GetPinned() { return board_.articulation_points_; }


 protected:
  void DoApplyAction(Action action) override;

 private:
  // allows any combination of expansion pieces to be used for the observation
  size_t BugTypeToTensorIndex(BugType type) const;

  void CreateBugTypePlane(BugType type, Player player, absl::Span<float>::iterator& it);
  void CreatePlacementPlane(Player player, absl::Span<float>::iterator& it);
  void CreateArticulationPlane(Player player, absl::Span<float>::iterator& it);
  void CreateCoveredPlane(Player player, absl::Span<float>::iterator& it);

  // an axial coordinate at position (q, r) is stored at index [r][q] after
  // translating the axial coordinate by the length of the radius
  inline std::array<int, 2> AxialToTensorIndex(HivePosition pos) const {
    return {pos.R() + Board().Radius(), pos.Q() + Board().Radius()};
  }

  Player current_player_ = kPlayerWhite;
  HexBoard board_;
  ExpansionInfo expansions_;
  int num_bug_types_;
  bool force_terminal_;


};


// Game object.
class HiveGame : public Game {
 public:

  explicit HiveGame(const GameParameters& params);
  
  std::array<int,3> ActionsShape() const { return {7,28,28}; }
  // TODO: change action-space depending on expansions used
  int NumDistinctActions() const override { return 5488 + 1; } // +1 for pass
  inline std::unique_ptr<State> NewInitialState() const override {
    return std::make_unique<HiveState>(shared_from_this(), board_radius_, expansions_, num_bug_types_);
  }
  int NumPlayers() const override { return kNumPlayers; }
  double MinUtility() const override { return -1; }
  absl::optional<double> UtilitySum() const override { return 0; }
  double MaxUtility() const override { return 1; }

  // TODO: change observation tensor depending on expansions used
  std::vector<int> ObservationTensorShape() const override {
    return { num_bug_types_ * kNumPlayers /*num bug types x num players */ + 2 /* articulation point planes */ + 2 /* placeability planes */ + 2 /* covered planes */,
    2 * board_radius_ + 1, /*dimensions of a sq board from hex board is: (2*radius + 1)*/
    2 * board_radius_ + 1};
  }

  int MaxGameLength() const override { return 1000; }
  std::unique_ptr<State> DeserializeState(const std::string& str) const override;
  
 private:
  int board_radius_;
  int num_bug_types_;
  ExpansionInfo expansions_;

};


}  // namespace hive
}  // namespace open_spiel

#endif  // OPEN_SPIEL_GAMES_HIVE_H_
