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

int counteroo = 0;

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

int PlayerRelative(Player current_player);





// UHP string representation of a move (there is exactly 1 move per player turn)
std::string Move::ToUHP() {
  if (is_pass) {
    return "pass";
  }

  std::string reference_tile_uhp = to->ToUHP();
  std::string offset_formatted = "";

  // add a prefix or suffix depending on the relative position
  switch(direction) {
    case Direction::kNE:
      offset_formatted = reference_tile_uhp + "/";
    case Direction::kE:
      offset_formatted = reference_tile_uhp + "-";
    case Direction::kSE:
      offset_formatted = reference_tile_uhp + "\\";
    case Direction::kSW:
      offset_formatted = "/" + reference_tile_uhp;
    case Direction::kW:
      offset_formatted = "-" + reference_tile_uhp;
    case Direction::kNW:
      offset_formatted = "\\" + reference_tile_uhp;
    case Direction::kAbove:
      offset_formatted = reference_tile_uhp;
    default:
      SpielFatalError("Move::ToUHP() - Move has an invalid direction!");
  }

  return absl::StrCat(from->ToUHP(), " ", offset_formatted);
}




HiveGame::HiveGame(const GameParameters& params)
  : Game(kGameType, params),
    kBoardRadius(kDefaultBoardRadius) {

}





std::string HiveState::ActionToString(Player player, Action action_id) const {
  return std::to_string(action_id);
}

std::string HiveState::ToString() const {
  return "Base+LMP";
}


std::vector<double> HiveState::Returns() const {
  return {1, -1};
}

std::string HiveState::InformationStateString(Player player) const {
  SPIEL_CHECK_GE(player, 0);
  SPIEL_CHECK_LT(player, num_players_);
  return HistoryString();
}

std::string HiveState::ObservationString(Player player) const {
  return "?";
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
  // TODO: change from all actions
  return std::vector<Action>(NumDistinctActions());
}

Move HiveState::ActionToMove(Action action) const {
  // pass action
  if (action == NumDistinctActions() - 1) {
    return PassMove();
  }

  int num_tiles = board_->NumUniqueTiles();

  int direction = action % kNumDirections;
  int to = (action / kNumDirections) % num_tiles;
  int from = action / (num_tiles * num_tiles);

  return Move{current_player_,
              board_->DecodeTile(from, current_player_),
              board_->DecodeTile(to, current_player_),
              static_cast<Direction>(direction),
              false};
}

Action HiveState::MoveToAction(Move& move) const {
  if (move.is_pass) {
    return NumDistinctActions() - 1;
  }

  int num_tiles = board_->NumUniqueTiles();

  // using tiles encoded from the current player's perspective,
  int from = board_->EncodeTile(move.from, move.player);
  int to = board_->EncodeTile(move.to, move.player);


  // as if indexing into a 3d array with indices [from][to][direction]
  return (from * (num_tiles * num_tiles)) + (kNumDirections * to) + move.direction;
}






void HiveState::DoApplyAction(Action action) {
  counteroo++;
}


bool HiveState::IsQueenSurrounded(const Player& player) const {
  return counteroo >= 5;
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

// std::string HiveState::ToString() const {
//   // Generates something like:
//   //        a b c d e
//   //     1 @ O . @ O f
//   //    2 . O O @ O @ g
//   //   3 . @ @ . . @ O h
//   //  4 . @ @ . . . O O i
//   // 5 @ . . O . @ @ O .
//   //  6 @ O . O O @ @[O]
//   //   7 . @ O . O O O
//   //    8 @ O @ O O O
//   //     9 @ O @ @ @


//   std::string white = "O";
//   std::string black = "@";
//   std::string empty = ".";
//   std::string coord = "";
//   std::string reset = "";
//   if (ansi_color_output_) {
//     std::string esc = "\033";
//     reset = esc + "[0m";
//     coord = esc + "[1;37m";  // bright white
//     empty = reset + ".";
//     white = esc + "[1;33m" + "@";  // bright yellow
//     black = esc + "[1;34m" + "@";  // bright blue
//   }

//   std::ostringstream out;

//   // Top x coords.
//   out << std::string(board_size_ + 3, ' ');
//   for (int x = 0; x < board_size_; x++) {
//     out << ' ' << coord << static_cast<char>('a' + x);
//   }
//   out << '\n';

//   for (int y = 0; y < board_diameter_; y++) {
//     out << std::string(abs(board_size_ - 1 - y) + 1 + ((y + 1) < 10), ' ');
//     out << coord << (y + 1);  // Leading y coord.

//     bool found_last = false;
//     int start_x = (y < board_size_ ? 0 : y - board_size_ + 1);
//     int end_x = (y < board_size_ ? board_size_ + y : board_diameter_);
//     for (int x = start_x; x < end_x; x++) {
//       Move pos(x, y, board_size_);

//       // Spacing and last-move highlight.
//       if (found_last) {
//         out << coord << ']';
//         found_last = false;
//       } else if (last_move_ == pos) {
//         out << coord << '[';
//         found_last = true;
//       } else {
//         out << ' ';
//       }

//       // Actual piece.
//       Player p = board_[pos.xy].player;
//       if (p == kPlayerNone) out << empty;
//       if (p == kPlayer1) out << white;
//       if (p == kPlayer2) out << black;
//     }
//     if (found_last) {
//       out << coord << ']';
//     }
//     if (y < board_size_ - 1) {  // Trailing x coord.
//       out << ' ' << coord << static_cast<char>('a' + board_size_ + y);
//     }
//     out << '\n';
//   }
//   out << reset;
//   return out.str();
// }

// std::vector<double> HiveState::Returns() const {
//   if (outcome_ == kPlayer1) return {1, -1};
//   if (outcome_ == kPlayer2) return {-1, 1};
//   if (outcome_ == kPlayerDraw) return {0, 0};
//   return {0, 0};  // Unfinished
// }

// std::string HiveState::InformationStateString(Player player) const {
//   SPIEL_CHECK_GE(player, 0);
//   SPIEL_CHECK_LT(player, num_players_);
//   return HistoryString();
// }

// std::string HiveState::ObservationString(Player player) const {
//   SPIEL_CHECK_GE(player, 0);
//   SPIEL_CHECK_LT(player, num_players_);
//   return ToString();
// }

// int PlayerRelative(Player state, Player current) {
//   switch (state) {
//     case kPlayerWhite:
//       return current == 0 ? 0 : 1;
//     case kPlayer2:
//       return current == 1 ? 0 : 1;
//     case kPlayerNone:
//       return 2;
//     default:
//       SpielFatalError("Unknown player type.");
//   }
// }

// void HiveState::ObservationTensor(Player player,
//                                       absl::Span<float> values) const {
//   SPIEL_CHECK_GE(player, 0);
//   SPIEL_CHECK_LT(player, num_players_);

//   TensorView<2> view(values, {kCellStates, static_cast<int>(board_.size())},
//                      true);
//   for (int i = 0; i < board_.size(); ++i) {
//     if (board_[i].player < kCellStates) {
//       view[{PlayerRelative(board_[i].player, player), i}] = 1.0;
//     }
//   }
// }

// void HiveState::DoApplyAction(Action action) {
//   //SPIEL_CHECK_EQ(outcome_, kPlayerNone);


//   current_player_ = (current_player_ == kPlayer1 ? kPlayer2 : kPlayer1);
// }

// int HiveState::FindGroupLeader(int cell) {
//   int parent = board_[cell].parent;
//   if (parent != cell) {
//     do {  // Follow the parent chain up to the group leader.
//       parent = board_[parent].parent;
//     } while (parent != board_[parent].parent);
//     // Do path compression, but only the current one to avoid recursion.
//     board_[cell].parent = parent;
//   }
//   return parent;
// }

// bool HiveState::JoinGroups(int cell_a, int cell_b) {
//   int leader_a = FindGroupLeader(cell_a);
//   int leader_b = FindGroupLeader(cell_b);

//   if (leader_a == leader_b)  // Already the same group.
//     return true;

//   if (board_[leader_a].size < board_[leader_b].size) {
//     // Force group a's subtree to be bigger.
//     std::swap(leader_a, leader_b);
//   }

//   // Group b joins group a.
//   board_[leader_b].parent = leader_a;
//   board_[leader_a].size += board_[leader_b].size;
//   board_[leader_a].corner |= board_[leader_b].corner;
//   board_[leader_a].edge |= board_[leader_b].edge;

//   return false;
// }

// bool HiveState::CheckRingDFS(const Move& move, int left, int right) {
//   if (!move.OnBoard()) return false;

//   Cell& c = board_[move.xy];
//   if (current_player_ != c.player) return false;
//   if (c.mark) return true;  // Found a ring!

//   c.mark = true;
//   bool success = false;
//   for (int i = left; !success && i <= right; i++) {
//     int dir = (i + 6) % 6;  // Normalize.
//     success = CheckRingDFS(neighbors_[move.xy][dir], dir - 1, dir + 1);
//   }
//   c.mark = false;
//   return success;
// }

// std::unique_ptr<State> HiveState::Clone() const {
//   return std::unique_ptr<State>(new HiveState(*this));
// }

// HiveGame::HiveGame(const GameParameters& params)
//     : Game(kGameType, params),
//       board_size_(ParameterValue<int>("board_size")),
//       ansi_color_output_(ParameterValue<bool>("ansi_color_output")),
//       allow_swap_(ParameterValue<bool>("swap")) {}

}  // namespace hive
}  // namespace open_spiel
