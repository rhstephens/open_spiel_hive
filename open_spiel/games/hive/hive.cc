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

#include "open_spiel/games/hive/hive.h"

#include <algorithm>
#include <iomanip>
#include <memory>
#include <utility>
#include <vector>

#include "open_spiel/abseil-cpp/absl/algorithm/container.h"
#include "open_spiel/abseil-cpp/absl/strings/str_format.h"
#include "open_spiel/game_parameters.h"
#include "open_spiel/spiel_utils.h"
#include "open_spiel/utils/tensor_view.h"

namespace open_spiel {
namespace hive {
namespace {


// Facts about the game.
const GameType kGameType{/*short_name=*/"hive",
                         /*long_name=*/"Hive",
                         GameType::Dynamics::kSequential,
                         GameType::ChanceMode::kDeterministic,
                         GameType::Information::kPerfectInformation,
                         GameType::Utility::kZeroSum,
                         GameType::RewardModel::kTerminal,
                         /*max_num_players=*/2,
                         /*min_num_players=*/2,
                         /*provides_information_state_string=*/true,
                         /*provides_information_state_tensor=*/false,
                         /*provides_observation_string=*/true,
                         /*provides_observation_tensor=*/true,
                         /*parameter_specification=*/
                         {
                             {"board_size", GameParameter(kDefaultBoardRadius)},
                             {"l", GameParameter(true)},
                             {"m", GameParameter(true)},
                             {"p", GameParameter(true)},
                         }};

std::shared_ptr<const Game> Factory(const GameParameters& params) {
  return std::shared_ptr<const Game>(new HiveGame(params));
}

REGISTER_SPIEL_GAME(kGameType, Factory);

RegisterSingleTensorObserver single_tensor(kGameType.short_name);

}  // namespace

HiveState::HiveState(std::shared_ptr<const Game> game, int board_size) :
  State(game), board_(new HexBoard(game, board_size, kNumStackableTiles)) {

  // create a tile for each player + bug type + ordinal
  for (Player p = 0; p < kNumPlayers; ++p) {
    for (int ord = 1; ord <= BugTypeCount(BugType::kQueen); ++ord) {
      board_->CreateTile(p, BugType::kQueen, ord);
    }

    for (int ord = 1; ord <= BugTypeCount(BugType::kAnt); ++ord) {
      board_->CreateTile(p, BugType::kAnt, ord);
    }

    for (int ord = 1; ord <= BugTypeCount(BugType::kGrasshopper); ++ord) {
      board_->CreateTile(p, BugType::kGrasshopper, ord);
    }

    for (int ord = 1; ord <= BugTypeCount(BugType::kSpider); ++ord) {
      board_->CreateTile(p, BugType::kSpider, ord);
    }

    for (int ord = 1; ord <= BugTypeCount(BugType::kBeetle); ++ord) {
      board_->CreateTile(p, BugType::kBeetle, ord);
    }

    for (int ord = 1; ord <= BugTypeCount(BugType::kLadybug); ++ord) {
      board_->CreateTile(p, BugType::kLadybug, ord);
    }

    for (int ord = 1; ord <= BugTypeCount(BugType::kMosquito); ++ord) {
      board_->CreateTile(p, BugType::kMosquito, ord);
    }

    for (int ord = 1; ord <= BugTypeCount(BugType::kPillbug); ++ord) {
      board_->CreateTile(p, BugType::kPillbug, ord);
    }
  }
}


std::string HiveState::ActionToString(Player player, Action action_id) const {
  return ActionToMove(action_id).ToUHP();
}

std::string HiveState::ToString() const {
  static int n = board_->Radius();
  static float indent_size = 2.5f;

  std::string string;
  // *5 for the number of chars at each grid index
  string.reserve((n * 2 + 1) * (n * 2 + 1) * 5);

  // loop over Q, R, to generate a hexagon
  for (int r = -n; r <= n; ++r) {
    // indent based on which row we are on (r). Intentionally use float
    // to take the floor for odd numbered rows
    int num_spaces = std::abs(r) * indent_size;
    for (int i = 0; i < num_spaces; ++i) {
      absl::StrAppend(&string, " ");
    }

    // print each tile on row r by iterating valid q indices
    for (int q = std::max(-n, -r - n); q <= std::min(n, -r + n); ++q) {
      absl::optional<HiveTilePtr> tile = board_->GetTileAt(HivePosition(q, r), true);

      // print the tile's UHP representation, or "-" otherwise, centered around
      // a padded 5 char long string
      std::ostringstream oss;
      oss << std::right << std::setw(4);
      if (tile.has_value()) {
        oss << (*tile)->ToUHP();
        if (*tile == board_->LastMovedTile()) {
          oss << "*";
        }
        // or for fun:
        //oss << (*tile)->ToUnicode();
      } else {
        oss << " - ";
      }
      absl::StrAppendFormat(&string, " %s", oss.str());
    }
    absl::StrAppend(&string, "\n\n");
  }

  return string;

}

// e.g. the string "wA2 /bQ" translates to: "Move White's 2nd Ant to the
// south-west of Black's Queen"
Action HiveState::StringToAction(Player player,
                        const std::string& move_str) const {

  // pass move?
  if (move_str == "pass") {
    return NumDistinctActions() - 1;
  }
  
  std::vector<std::string> bugs = absl::StrSplit(move_str, " ");
  SPIEL_CHECK_GT(bugs.size(), 0);
  SPIEL_CHECK_LE(bugs.size(), 2);

  Move move;
  move.player = player;

  // first bug should always be valid
  absl::optional<HiveTilePtr> bug1 = board_->GetTileFromUHP(bugs[0]);
  if (bug1.has_value()) {
    move.from = bug1.value();
  } else {
    SpielFatalError("HiveState::StringToAction() - invalid move string: "
                    + move_str);
  }

  // special case: if only first bug is provided, it is a valid 1st turn move
  if (bugs.size() == 1) {
    return MoveToAction(move);
  }

  // get second bug and its relative direction
  bool is_prefix_direction = true;
  Direction direction = Direction::kNumDirections;
  BugType type = BugType::kNone;
  int ordinal = 0;

  // check first char for a direction
  char c = bugs[1].front();
  if (c == '\\') {
    direction = Direction::kNW;
  } else if (c == '-') {
    direction = Direction::kW;
  } else if (c == '/') {
    direction = Direction::kSW;
  }

  // check last char if we haven't found a direction
  if (direction == kNumDirections) {
    c = bugs[1].back();
    if (c == '\\') {
      direction = Direction::kSE;
    } else if (c == '-') {
      direction = Direction::kE;
    } else if (c == '/') {
      direction = Direction::kNE;
    }
  }

  // if still no direction, it must be above
  if (direction == kNumDirections) {
    direction = Direction::kAbove;
  }

  move.direction = direction;
  
  // now get bug + ord from string
  size_t ord_index = bugs[1].find_first_of("123");
  size_t start_index = bugs[1].find_first_not_of("\\-/");
  size_t end_index = bugs[1].find_last_not_of("\\-/");

  if (ord_index != std::string::npos) {
    ordinal = bugs[1].at(ord_index) - '0';
  }

  move.to = board_->GetTileFromUHP(bugs[1].substr(start_index, end_index - start_index + 1));
  // if (tile.has_value()) {
  //   move.to = tile.value();
  // } else {
  //   SpielFatalError("HiveState::StringToAction() - invalid UHP string: "
  //                   + bugs[1].substr(start_index, end_index - start_index + 1));
  // }

  return MoveToAction(move);

  // while (it != bugs[1].end()) {
  //   char c = *it;

  //   // direction is based on whether \-/ comes before or after the bug type
  //   if (c == '\\') {
  //     direction = is_prefix_direction ? Direction::kNW : Direction::kSE;
  //   } else if (c == '-') {
  //     direction = is_prefix_direction ? Direction::kW : Direction::kE;
  //   } else if (c == '/') {
  //     direction = is_prefix_direction ? Direction::kSW : Direction::kNE;
  //   // determine Player
  //   } else if (c == 'w') {
  //     is_prefix_direction = false;
  //     player = kPlayerWhite;
  //   } else if (c == 'b') {
  //     is_prefix_direction = false;
  //     player = kPlayerBlack;
  //   // if its a space, skip, otherwise it must be bug type + ordinal
  //   } else if (c != ' ') {
  //     type = kUHPToBugType.at(c);
  //     if (++it != bugs[1].end()) {
  //       c = *it;
  //       if (std::isdigit(c)) {
  //         ordinal = c - '0';
  //       }
  //     }
  //   }
    
  //   ++it;

  //   if (direction) {

  //   }
  // }
}


// e.g. the string "wA2" would return white's 2nd Ant tile
// HiveTilePtr& HiveState::StringToTile(const std::string& str) const {
//   Player player;
//   BugType type;


//   absl::string_view bug = absl::StripAsciiWhitespace(str);
//   absl::optional<HiveTilePtr> x = board_->GetTileFromUHP(str);
//   return x.value();
// }



std::vector<double> HiveState::Returns() const {
  return {1, -1};
}

std::string HiveState::InformationStateString(Player player) const {
  SPIEL_CHECK_GE(player, 0);
  SPIEL_CHECK_LT(player, num_players_);
  return HistoryString();
}

std::string HiveState::ObservationString(Player player) const {
  SPIEL_CHECK_GE(player, 0);
  SPIEL_CHECK_LT(player, num_players_);
  return ToString();
}


// A 3d tensor, 3 player-relative one-hot 2d planes. The layers are: the
// specified player, the other player, and empty.
void HiveState::ObservationTensor(Player player,
                        absl::Span<float> values) const {
  return;
}

std::unique_ptr<State> HiveState::Clone() const {
  return std::unique_ptr<State>(new HiveState(game_));
}

std::vector<Action> HiveState::LegalActions() const {
  std::vector<Move> moves;
  std::vector<Action> actions;

  board_->GenerateAllMoves(moves, current_player_, move_number_);
  actions.reserve(moves.size());
  std::transform(moves.begin(), moves.end(), std::back_inserter(actions), [this](Move& m) {
    return MoveToAction(m); 
  });

  // TODO: change from all actions
  std::vector<Action> dummy_actions(NumDistinctActions());
  for (int i = 0; i < NumDistinctActions(); ++i) {
    dummy_actions[i] = i;
  }

  std::sort(actions.begin(), actions.end());
  return actions;
}

std::string HiveState::Serialize() const {
  return "";
}


Move HiveState::ActionToMove(Action action) const {
  // pass action
  if (action == NumDistinctActions() - 1) {
    Move m;
    m.player = current_player_;
    m.is_pass = true;
    return m;
  }

  int num_tiles = board_->NumUniqueTiles();

  int direction = action % kNumDirections;
  int to = (action / kNumDirections) % num_tiles;
  int from = action / (num_tiles * kNumDirections);

  // special case: for the first turn actions, they are encoded as playing a
  // tile on top of itself. In this case, we want "to" to be null
  HiveTilePtr to_tile;
  if (from == to && direction == kAbove) {
    to_tile = nullptr;
  } else {
    to_tile = board_->DecodeTile(to, current_player_);
  }

  return Move{current_player_,
              board_->DecodeTile(from, current_player_),
              to_tile,
              static_cast<Direction>(direction)};
}

Action HiveState::MoveToAction(Move& move) const {
  if (move.is_pass) {
    return NumDistinctActions() - 1;
  }

  int num_tiles = board_->NumUniqueTiles();

  // using tiles encoded from the current player's perspective,
  int from = board_->EncodeTile(move.from, move.player);

  // if there is no second bug "to", then we have a special case for first turn
  if (!move.to) {
    return (from * (num_tiles * kNumDirections)) + (kNumDirections * from) + Direction::kAbove;
  }

  int to = board_->EncodeTile(move.to, move.player);

  // as if indexing into a 3d array with indices [from][to][direction]
  return (from * (num_tiles * kNumDirections)) + (kNumDirections * to) + move.direction;
}


void HiveState::DoApplyAction(Action action) {
  // pass action - intentionally skip changing current_player_
  if (action == NumDistinctActions() - 1) {
    return;
  }

  // we assume the move is valid at this point and simply apply it
  Move move = ActionToMove(action);
  HivePosition target_pos = move.to ? move.EndPosition() : kOriginPosition;

  if (move.from && move.from->IsInPlay()) {
    board_->MoveTile(move.from, target_pos);
  } else {
    board_->PlaceTile(move.from, target_pos);
  }

  current_player_ = (++current_player_) % kNumPlayers;
}







HiveGame::HiveGame(const GameParameters& params)
  : Game(kGameType, params),
    kBoardRadius(kDefaultBoardRadius) {

}

std::unique_ptr<State> HiveGame::DeserializeState(const std::string& str) const {
  return NewInitialState();
}


// // The board is represented as a flattened 2d array of the form:
// //   1 2 3
// // a 0 1 2    0 1       0 1
// // b 3 4 5 => 3 4 5 => 3 4 5
// // c 6 7 8      7 8     7 8
// //
// // Neighbors are laid out in this pattern:
// //   0   1
// // 5   X   2
// //   4   3

// // Direct neighbors of a cell, clockwise.
// constexpr std::array<Move, kMaxNeighbors> neighbor_offsets = {
//     Move(-1, -1, kMoveOffset), Move(0, -1, kMoveOffset),
//     Move(1, 0, kMoveOffset),   Move(1, 1, kMoveOffset),
//     Move(0, 1, kMoveOffset),   Move(-1, 0, kMoveOffset),
// };


// std::string Move::ToString() const {
//   if (xy == kMoveUnknown) return "unknown";
//   if (xy == kMoveNone) return "none";
//   return absl::StrCat(std::string(1, static_cast<char>('a' + x)), y + 1);
// }

// int HiveState::Cell::NumCorners() const { return kBitsSetTable64[corner]; }
// int HiveState::Cell::NumEdges() const { return kBitsSetTable64[edge]; }

// Move HiveState::ActionToMove(Action action_id) const {
//   return Move(action_id % board_diameter_, action_id / board_diameter_,
//               board_size_);
// }

// std::vector<Action> HiveState::LegalActions() const {
//   // Can move in any empty cell.
//   std::vector<Action> moves;
//   if (IsTerminal()) return {};
//   moves.reserve(board_.size() - moves_made_);
//   for (int cell = 0; cell < board_.size(); ++cell) {
//     if (board_[cell].player == kPlayerNone) {
//       moves.push_back(cell);
//     }
//   }
//   if (AllowSwap()) {  // The second move is allowed to replace the first one.
//     moves.push_back(last_move_.xy);
//     absl::c_sort(moves);
//   }
//   return moves;
// }

// std::string HiveState::ActionToString(Player player,
//                                           Action action_id) const {
//   return ActionToMove(action_id).ToString();
// }

// bool HiveState::AllowSwap() const {
//   return allow_swap_ && moves_made_ == 1 && current_player_ == kPlayer2;
// }


}  // namespace hive
}  // namespace open_spiel
