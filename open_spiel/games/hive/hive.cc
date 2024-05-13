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
  return PrintBoard();
}

// e.g. the string "wA2 /bQ" translates to: "Move White's 2nd Ant to the
// south-west of Black's Queen"
Action HiveState::StringToAction(Player player,
                        const std::string& move_str) const {

  // pass move?
  if (move_str == "pass") {
    return NumDistinctActions() - 1;
  }
  
  Move move;
  std::vector<std::string> bugs = absl::StrSplit(move_str, " ");
  SPIEL_CHECK_GT(bugs.size(), 0);
  SPIEL_CHECK_LE(bugs.size(), 2);

  /////////move.player = player;

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
  Direction direction = Direction::kNumAllDirections;
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
  if (direction == Direction::kNumAllDirections) {
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
  if (direction == Direction::kNumAllDirections) {
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


/////// TODO REMOVE THIS ////////
  actions.push_back(NumDistinctActions() - 1);
/////// FOR TESTING ONLY ////////

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
    m.is_pass = true;
    return m;
  }

  int num_tiles = board_->NumUniqueTiles();

  int direction = action % Direction::kNumAllDirections;
  int to = (action / Direction::kNumAllDirections) % num_tiles;
  int from = action / (num_tiles * Direction::kNumAllDirections);

  // special case: for the first turn actions, they are encoded as playing a
  // tile on top of itself. In this case, we want "to" to be null
  HiveTilePtr to_tile;
  if (from == to && direction == kAbove) {
    to_tile = nullptr;
  } else {
    to_tile = board_->DecodeTile(to);
  }

  return Move{board_->DecodeTile(from),
              to_tile,
              static_cast<Direction>(direction)};
}

Action HiveState::MoveToAction(Move& move) const {
  if (move.is_pass) {
    return NumDistinctActions() - 1;
  }

  int from = board_->EncodeTile(move.from);
  int num_tiles = board_->NumUniqueTiles();

  // if there is no second bug "to", then we have a special case for first turn
  if (!move.to) {
    return (from * (num_tiles * Direction::kNumAllDirections)) + (Direction::kNumAllDirections * from) + Direction::kAbove;
  }

  int to = board_->EncodeTile(move.to);

  // as if indexing into a 3d array with indices [from][to][direction]
  return (from * (num_tiles * Direction::kNumAllDirections)) + (Direction::kNumAllDirections * to) + move.direction;
}


 std::string HiveState::PrintBoard(HiveTilePtr tile_to_move) const {
  static std::string white = "\033[38;5;223m";
  static std::string red = "\033[1;31m";
  static std::string reset = "\033[1;39m";
  static int n = board_->Radius();
  static float indent_size = 2.5f;

  std::string string;
  std::vector<HiveTilePtr> top_tiles;
  // *5 for the number of chars at each grid index
  string.reserve((n * 2 + 1) * (n * 2 + 1) * 5);

  std::vector<Move> moves;
  std::vector<HivePosition> positions;
  if (tile_to_move != nullptr) {
    board_->GenerateMovesFor(moves, tile_to_move->GetPosition(), tile_to_move->GetBugType(), current_player_);
  }

  for (auto move : moves) {
    positions.push_back(move.EndPosition());
 }

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
      absl::optional<HiveTilePtr> tile = board_->GetTopTileAt({q, r, 0});

      // print the tile's UHP representation, or "-" otherwise, centered around
      // a padded 5 char long string
      std::ostringstream oss;
      if (tile.has_value()) {
        (*tile)->GetPlayer() == kPlayerWhite ? oss << white : oss << red;

        std::string uhp = (*tile)->ToUHP();
        if ((*tile)->GetPosition().H() > 0) {
          uhp = absl::StrCat("^", uhp);
          top_tiles.push_back(*tile);
        }
        int left_padding = (5 - uhp.size()) / 2;
        int right_padding = (5 - uhp.size()) - left_padding;
        for (int i = 0; i < left_padding; ++i) { oss << ' '; }
        oss << uhp;

        if (*tile == board_->LastStunnedTile()) {
          oss << "~";
          --right_padding;
        } else if (*tile == board_->LastMovedTile()) {
          oss << "*";
          --right_padding;
        }

        for (int i = 0; i < right_padding; ++i) { oss << ' '; }


        // or for fun:
        //oss << (*tile)->ToUnicode();
      } else {
        oss << reset;

        oss << ((positions.size() > 0 && std::find(positions.begin(), positions.end(), HivePosition(q, r, 0)) != positions.end()) ? "  X  " : "  -  ");

        // if (movable_positions.size() > 0) {
        //   auto it = std::find(movable_positions.begin(), movable_positions.end(), HivePosition(q, r, 0));
        //   if (it !=  movable_positions.end()) {
        //     oss << "  O  ";
        //   } else {
        //     oss << "  -  ";
        //   }
        // } else { 
        //   oss << "  -  ";
        // }
      }
      absl::StrAppend(&string, oss.str());
    }
    absl::StrAppend(&string, "\n\n");
  }

  // print bug stacks
  for (auto& tile : top_tiles) {
    HivePosition pos = tile->GetPosition();
    absl::StrAppend(&string, tile->ToUHP());
    while (absl::optional<HiveTilePtr> below = board_->GetTileBelow(pos)) {
      absl::StrAppend(&string, " > ", (*below)->ToUHP());
      pos += {0, 0, -1};
    }

    absl::StrAppend(&string, "\n");
  }


  return string;
}


// we assume the move is valid at this point and simply apply it
void HiveState::DoApplyAction(Action action) {
  if (action == NumDistinctActions() - 1) {
    board_->Pass();
  } else {
    Move move = ActionToMove(action);
    HivePosition target_pos = move.to ? move.EndPosition() : kOriginPosition;

    if (move.from && move.from->IsInPlay()) {
      board_->MoveTile(move.from, target_pos, current_player_);
    } else {
      board_->PlaceTile(move.from, target_pos);
    }
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


}  // namespace hive
}  // namespace open_spiel
