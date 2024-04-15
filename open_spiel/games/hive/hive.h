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


// Encodes a move as defined by the Universal Hive Protocol
// {INSERT LINK} //////
struct Move {
  Player player;                    // whose turn was it during this move
  std::shared_ptr<HiveTile> from;   // the tile that's being moved
  std::shared_ptr<HiveTile> to;     // the reference tile
  Direction direction;
  bool is_pass = false;

  std::string ToUHP();
  HivePosition StartPosition() { return from->GetPosition(); }
  HivePosition EndPosition() { return to->GetPosition() + kNeighbourOffsets[direction]; }
};


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
    return IsQueenSurrounded(kPlayerWhite) || IsQueenSurrounded(kPlayerBlack); 
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

  // custom
  Move ActionToMove(Action action) const;
  Action MoveToAction(Move& move) const;
  //HiveTilePtr& StringToTile(const std::string& str) const;

  bool IsQueenSurrounded(const Player& player) const;


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
    std::cout << "check3" << std::endl;
    return std::unique_ptr<State>(
        new HiveState(shared_from_this(), 8));
  }
  int NumPlayers() const override { return kNumPlayers; }
  double MinUtility() const override { return -1; }
  absl::optional<double> UtilitySum() const override { return 0; }
  double MaxUtility() const override { return 1; }
  std::vector<int> ObservationTensorShape() const override {
    return { 16 /*num bug types x num_players */ + 1 /* negative space plane */ + 1 /* articulation points */,
    2*kBoardRadius + 1, /*dimensions of a sq board from hex board is: (2*radius + 1)*/
    2*kBoardRadius + 1};
  }
  int MaxGameLength() const override { return 250; }

 private:
  const int kBoardRadius;
};


}  // namespace hive
}  // namespace open_spiel



/*
  // Represents a single cell on the board, as well as the structures needed for
  // groups of cells. Groups of cells are defined by a union-find structure
  // embedded in the array of cells. Following the `parent` indices will lead to
  // the group leader which has the up to date size, corner and edge
  // connectivity of that group. Size, corner and edge are not valid for any
  // cell that is not a group leader.
  struct Cell {
    // Who controls this cell.
    Player player;

    // Whether this cell is marked/visited in a ring search. Should always be
    // false except while running CheckRingDFS.
    bool mark;

    // A parent index to allow finding the group leader. It is the leader of the
    // group if it points to itself. Allows path compression to shorten the path
    // from a direct parent to the leader.
    uint16_t parent;

    // These three are only defined for the group leader's cell.
    uint16_t size;   // Size of this group of cells.
    uint8_t corner;  // A bitset of which corners this group is connected to.
    uint8_t edge;    // A bitset of which edges this group is connected to.

    Cell() {}
    Cell(Player player_, int parent_, int corner_, int edge_)
        : player(player_),
          mark(false),
          parent(parent_),
          size(1),
          corner(corner_),
          edge(edge_) {}

    // How many corner or edges this group of cell is connected to. Only defined
    // if called on the group leader.
    int NumCorners() const;
    int NumEdges() const;
  };

 public:
  HiveState(std::shared_ptr<const Game> game, int board_size,
                bool ansi_color_output = false, bool allow_swap = false);

  HiveState(const HiveState&) = default;

  Player CurrentPlayer() const override {
    return IsTerminal() ? kTerminalPlayerId : static_cast<int>(current_player_);
  }
  std::string ActionToString(Player player, Action action_id) const override;
  std::string ToString() const override;
  bool IsTerminal() const override { return outcome_ != kPlayerEmpty; }
  std::vector<double> Returns() const override;
  std::string InformationStateString(Player player) const override;
  std::string ObservationString(Player player) const override;

  // A 3d tensor, 3 player-relative one-hot 2d planes. The layers are: the
  // specified player, the other player, and empty.
  void ObservationTensor(Player player,
                         absl::Span<float> values) const override;
  std::unique_ptr<State> Clone() const override;
  std::vector<Action> LegalActions() const override;

 protected:
  void DoApplyAction(Action action) override;

  // Find the leader of the group. Not const due to union-find path compression.
  int FindGroupLeader(int cell);

  // Join the groups of two positions, propagating group size, and edge/corner
  // connections. Returns true if they were already the same group.
  bool JoinGroups(int cell_a, int cell_b);

  // Do a depth first search for a ring starting at `move`.
  // `left` and `right give the direction bounds for the search. A valid ring
  // won't take any sharp turns, only going in one of the 3 forward directions.
  // The only exception is the very beginning where we don't know the direction
  // and it's valid to search in all 6 directions. 4 is enough though, since any
  // valid ring can't start and end in the 2 next to each other while still
  // going through `move.`
  bool CheckRingDFS(const Move& move, int left, int right);

  // Turn an action id into a `Move` with an x,y.
  Move ActionToMove(Action action_id) const;

  bool AllowSwap() const;

 private:
  std::vector<Cell> board_;
  Player current_player_ = kPlayer1;
  Player outcome_ = kPlayerEmpty;
  const int board_size_;
  const int board_diameter_;
  const int valid_cells_;
  int moves_made_ = 0;
  Move last_move_ = kMoveNone;
  const NeighborList& neighbors_;
  const bool ansi_color_output_;
  const bool allow_swap_;
  */






  /*

  int NumDistinctActions() const override {
    // Really diameter^2 - size*(size-1), but that's harder to represent, so
    // the extra actions in the corners are never legal.
    return Diameter() * Diameter();
  }
  std::unique_ptr<State> NewInitialState() const override {
    return std::unique_ptr<State>(
        new HiveState(shared_from_this(), board_size_, ansi_color_output_,
                          allow_swap_));
  }
  int NumPlayers() const override { return kNumPlayers; }
  double MinUtility() const override { return -1; }
  absl::optional<double> UtilitySum() const override { return 0; }
  double MaxUtility() const override { return 1; }
  std::vector<int> ObservationTensorShape() const override {
    return {kCellStates, Diameter(), Diameter()};
  }
  int MaxGameLength() const override {
    // The true number of playable cells on the board.
    // No stones are removed, and it is possible to draw by filling the board.
    return Diameter() * Diameter() - board_size_ * (board_size_ - 1) +
           allow_swap_;
  }

 private:
  int Diameter() const { return board_size_ * 2 - 1; }
  const int board_size_;
  const bool ansi_color_output_ = false;
  const bool allow_swap_ = false;
  */

#endif  // OPEN_SPIEL_GAMES_HIVE_H_
