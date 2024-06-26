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
                             {"m", GameParameter(true)},
                             {"l", GameParameter(true)},
                             {"p", GameParameter(true)},
                         }};

std::shared_ptr<const Game> Factory(const GameParameters& params) {
  return std::shared_ptr<const Game>(new HiveGame(params));
}

REGISTER_SPIEL_GAME(kGameType, Factory);

RegisterSingleTensorObserver single_tensor(kGameType.short_name);

}  // namespace

HiveState::HiveState(std::shared_ptr<const Game> game, int board_size, ExpansionInfo expansions, int num_bug_types) :
  State(game),
  expansions_(expansions),
  board_(board_size, expansions.uses_mosquito, expansions.uses_ladybug, expansions.uses_pillbug),
  num_bug_types_(num_bug_types),
  force_terminal_(false) {
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
    return PassAction();
  }
  
  Move move;
  move.direction = Direction::kNumAllDirections;
  std::vector<std::string> bugs = absl::StrSplit(move_str, " ");
  SPIEL_CHECK_GT(bugs.size(), 0);
  SPIEL_CHECK_LE(bugs.size(), 2);

  // first bug should always be valid
  move.from = NewHiveTile::UHPToTile(bugs[0]);
  if (!move.from.HasValue()) {
    SpielFatalError("HiveState::StringToAction() - invalid move string: " + move_str);
  }

  // special case: if only one bug is provided, it is a 1st turn move
  if (bugs.size() == 1) {
    return MoveToAction(move);
  }

  // get second bug and its relative direction
  char c = bugs[1].front();
  if (c == '\\') {
    move.direction = Direction::kNW;
  } else if (c == '-') {
    move.direction = Direction::kW;
  } else if (c == '/') {
    move.direction = Direction::kSW;
  }

  // check last char if we haven't found a direction
  if (move.direction == Direction::kNumAllDirections) {
    c = bugs[1].back();
    if (c == '\\') {
      move.direction = Direction::kSE;
    } else if (c == '-') {
      move.direction = Direction::kE;
    } else if (c == '/') {
      move.direction = Direction::kNE;
    }
  }

  // if still no direction, it must be above
  if (move.direction == Direction::kNumAllDirections) {
    move.direction = Direction::kAbove;
  }
  
  // now extract just the bug + ordinal from string
  size_t start_index = bugs[1].find_first_not_of("\\-/");
  size_t end_index = bugs[1].find_last_not_of("\\-/");
  move.to = NewHiveTile::UHPToTile(bugs[1].substr(start_index, end_index - start_index + 1));

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
//   absl::optional<HiveTilePtr> x = Board().GetTileFromUHP(str);
//   return x.value();
// }



std::vector<double> HiveState::Returns() const {
  bool white_winner = WinConditionMet(kPlayerWhite);
  bool black_winner = WinConditionMet(kPlayerBlack);

  if (white_winner ^ black_winner) {
    return {white_winner ? 1.f : -1.f, black_winner ? 1.f : -1.f};
  }

  return {0, 0};
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


void HiveState::ObservationTensor(Player player, absl::Span<float> values) const {
  SPIEL_CHECK_GE(player, 0);
  SPIEL_CHECK_LT(player, num_players_);

  // starting indices for each 2D feature plane, variable based on expansions
  static int bug_idx = 0;
  static int articulation_idx = num_bug_types_ * num_players_;
  static int placeable_idx = articulation_idx + 2;
  static int covered_idx = placeable_idx + 2;

  // Treat values as a 3d-tensor, where each feature plane has square dimensions
  // (radius * 2 + 1) x (radius * 2 + 1), and contains player-relative one-hot
  // encodings of the current board state
  TensorView<3> view(values, {game_->ObservationTensorShape()[0], Board().SquareDimensions(), Board().SquareDimensions()}, true);

  int plane_idx = 0;
  Colour my_colour = PlayerToColour(player);
  Colour opposing_colour = OtherColour(my_colour);

  // populate all planes that reference a tile in play
  for (auto tile : Board().GetPlayedTiles()) {
    HivePosition pos = Board().GetPositionOf(tile);
    std::array<int, 2> indices = AxialToTensorIndex(pos);
    bool is_opposing = tile.GetColour() == opposing_colour;

    // bug type planes
    plane_idx = bug_idx + BugTypeToTensorIndex(tile.GetBugType()) + (is_opposing ? static_cast<int>(BugType::kNumBugTypes) : 0);
    view[{plane_idx, indices[0], indices[1]}] = 1.0f;

    // pinned plane
    plane_idx = articulation_idx + (is_opposing ? 1 : 0);
    if (Board().IsPinned(pos)) {
      view[{plane_idx, indices[0], indices[1]}] = 1.0f;
    }

    // covered plane
    plane_idx = covered_idx + (is_opposing ? 1 : 0);
    if (Board().IsCovered(tile)) {
      view[{plane_idx, indices[0], indices[1]}] = 1.0f;
    }
  }

  // populate all planes that reference a specific position
  int radius = Board().Radius();
  for (int r = -radius; r <= radius; ++r) {
    for (int q = -radius; q <= radius; ++q) {
      HivePosition pos = {q, r, 0};
      std::array<int, 2> indices = AxialToTensorIndex(pos);

      // player and opponent's placeable positions
      if (Board().IsPlaceable(my_colour, pos)) {
        view[{placeable_idx, indices[0], indices[1]}] = 1.0f;
      } else if (Board().IsPlaceable(opposing_colour, pos)) {
        view[{placeable_idx + 1, indices[0], indices[1]}] = 1.0f;
      }
    }
  }

  return;
}


std::unique_ptr<State> HiveState::Clone() const {
  return std::unique_ptr<State>(new HiveState(*this));
}


std::vector<Action> HiveState::LegalActions() const {
  std::vector<Move> moves;
  std::set<Action> unique_actions;

  Board().GenerateAllMoves(moves, PlayerToColour(current_player_), move_number_);
  std::transform(moves.begin(), moves.end(), std::inserter(unique_actions, unique_actions.end()), [this](Move& m) {
    return MoveToAction(m); 
  });

  std::vector<Action> actions(unique_actions.begin(), unique_actions.end());

  // if a player has no legal actions, then they must pass
  if (actions.size() == 0) {
    actions.push_back(PassAction());
  }

  return actions;
}


std::string HiveState::Serialize() const {
  return absl::StrJoin({std::string(kDefaultUHPGameType), ProgressString(), TurnString(), MovesString()}, ";");
}


Move HiveState::ActionToMove(Action action) const {
  // pass action
  if (action == PassAction()) {
    Move m;
    m.from = NewHiveTile::kNoneTile;
    return m;
  }

  int64_t direction = action % Direction::kNumAllDirections;
  int64_t to = (action / Direction::kNumAllDirections) % kMaxTileCount;
  int64_t from = action / (kMaxTileCount * Direction::kNumAllDirections);

  // special case: for the first turn actions, they are encoded as playing a
  // tile on top of itself. In this case, we want "to" to be kNoneTile
  if (from == to && direction == Direction::kAbove) {
    to = NewHiveTile::kNoneTile;
  }

  return Move{from,
              to,
              static_cast<Direction>(direction)};
}


Action HiveState::MoveToAction(Move& move) const {
  // pass move encoded as "moving no tile"
  if (move.IsPass()) {
    return PassAction();
  }

  // upcast
  int64_t from = move.from;

  // if there is no second bug "to", then we have a special case for first turn
  if (!move.to.HasValue()) {
    return (from * (kMaxTileCount * Direction::kNumAllDirections)) + (from * Direction::kNumAllDirections) + Direction::kAbove;
  }

  int64_t to = move.to;

  // as if indexing into a 3d array with indices [from][to][direction]
  return (from * (kMaxTileCount * Direction::kNumAllDirections)) + (to * Direction::kNumAllDirections) + move.direction;
}


std::string HiveState::PrintBoard(NewHiveTile tile_to_move) const {
  static std::string white = "\033[38;5;223m";
  static std::string red = "\033[1;31m";
  static std::string reset = "\033[1;39m";
  static float indent_size = 2.5f;

  std::string string = "\n";
  string.reserve(Board().SquareDimensions() * Board().SquareDimensions() * 5);
  std::vector<NewHiveTile> top_tiles;

  // ===== For testing =====
  std::vector<Move> moves;
  std::vector<HivePosition> positions;
  if (tile_to_move.HasValue() && tile_to_move.GetColour() == PlayerToColour(current_player_)) {
    // Board().GenerateAllMoves(moves, PlayerToColour(current_player_), move_number_);
    Board().GenerateMovesFor(moves, tile_to_move, tile_to_move.GetBugType(), PlayerToColour(current_player_));
    // std::remove_if(moves.begin(), moves.end(), [&](Move m) {
    //   return m.from != tile_to_move;
    // });
  }

  for (auto move : moves) {
    positions.push_back(Board().GetPositionOf(move.to).NeighbourAt(move.direction));
  }
  // ===== For testing =====

  // loop over valid Q, R, to generate a hexagon
  int radius = Board().Radius();
  for (int r = -radius; r <= radius; ++r) {
    // indent based on which row we are on (r). Intentionally using float
    // to take the floor for odd numbered rows
    int num_spaces = std::abs(r) * indent_size;
    for (int i = 0; i < num_spaces; ++i) {
      absl::StrAppend(&string, " ");
    }

    // print each tile on row r by iterating valid q indices
    for (int q = std::max(-radius, -r - radius); q <= std::min(radius, -r + radius); ++q) {
      NewHiveTile tile = Board().GetTopTileAt({q, r, 0});

      // print the tile's UHP representation, or "-" otherwise, centered around
      // a padded 5 char long string
      std::ostringstream oss;
      if (tile.HasValue()) {
        tile.GetColour() == Colour::kWhite ? oss << white : oss << red;

        std::string uhp = tile.ToUHP();
        if (Board().GetPositionOf(tile).H() > 0) {
          uhp = absl::StrCat("^", uhp);
          top_tiles.push_back(tile);
        }

        int left_padding = (5 - uhp.size()) / 2;
        int right_padding = (5 - uhp.size()) - left_padding;
        for (int i = 0; i < left_padding; ++i) { oss << ' '; }

        oss << uhp;

        if (tile == Board().LastMovedTile()) {
          oss << "*";
          --right_padding;
        }

        for (int i = 0; i < right_padding; ++i) { oss << ' '; }

        // or for fun:
        //oss << (*tile)->ToUnicode();
      } else {
        if (Board().LastMovedTile().HasValue() && Board().LastMovedFrom() == HivePosition(q, r, 0)) {
          if (Board().LastMovedTile().GetColour() == Colour::kWhite) {
            oss << white;
          } else {
            oss << red;
          }
          oss << "  *  ";
          oss << reset;
        } else {
          oss << reset;
          oss << ((positions.size() > 0 && std::find(positions.begin(), positions.end(), HivePosition(q, r, 0)) != positions.end()) ? "  X  " : "  -  ");
        }
      }
      oss << reset;
      absl::StrAppend(&string, oss.str());
    }
    absl::StrAppend(&string, "\n\n");
  }

  // print bug stacks
  for (auto tile : top_tiles) {
    HivePosition pos = Board().GetPositionOf(tile);
    absl::StrAppend(&string, tile.ToUHP());
    NewHiveTile below = Board().GetTileBelow(pos);
    while (below.HasValue()) {
      absl::StrAppend(&string, " > ", below.ToUHP());
      pos += {0, 0, -1};
      if (pos.H() <= 0) {
        break;
      }

      below = Board().GetTileBelow(pos);
    }

    absl::StrAppend(&string, "\n");
  }

  return string;
}


std::string HiveState::ProgressString() const {
  if (move_number_ == 0) {
    return kUHPNotStarted;
  }

  if (move_number_ > game_->MaxGameLength()) {
    return kUHPDraw;
  }

  bool white_winner = WinConditionMet(kPlayerWhite);
  bool black_winner = WinConditionMet(kPlayerBlack);
  if (white_winner ^ black_winner) {
    return white_winner ? kUHPWhiteWins : kUHPBlackWins;
  } else if (white_winner && black_winner) {
    return kUHPDraw;
  }

  return kUHPInProgress;
}


std::string HiveState::TurnString() const {

  return absl::StrFormat("%s[%d]", current_player_ == kPlayerWhite ? "White" : "Black", (move_number_ + 2) / 2);
}


std::string HiveState::MovesString() const {
  return absl::StrJoin(ActionsToStrings(*this, History()), ";");
}


bool HiveState::BugTypeIsEnabled(BugType type) const {
  switch (type) {
    case BugType::kQueen:
    case BugType::kAnt:
    case BugType::kGrasshopper:
    case BugType::kSpider:
    case BugType::kBeetle:
      return true;
    case BugType::kMosquito:
      return expansions_.uses_mosquito;
    case BugType::kLadybug:
      return expansions_.uses_ladybug;
    case BugType::kPillbug:
      return expansions_.uses_pillbug;
    default:
      return false;
  }
}


size_t HiveState::BugTypeToTensorIndex(BugType type) const {
  size_t index = 0;
  for (uint8_t i = 0; i < static_cast<int>(BugType::kNumBugTypes); ++i) {
    if (BugTypeIsEnabled(static_cast<BugType>(i))) {
      if (type == static_cast<BugType>(i)) {
        return index;
      }

      ++index;
    }
  }

  return -1;
}


void HiveState::CreateBugTypePlane(BugType type, Player player, absl::Span<float>::iterator& it) {
  for (uint8_t q = 0; q < Board().SquareDimensions(); ++q) {
    for (uint8_t r = 0; r < Board().SquareDimensions(); ++r) {
      // auto tile = Board().GetTopTileAt({q, r, 0});
      // if (tile.has_value() && (*tile)->GetBugType() == type && (*tile)->GetPlayer() == player) {
      //   *it++ = 1.0f;
      // } else {
      //   *it++ = 0.0f;
      // }
    }
  }
}


void HiveState::CreatePlacementPlane(Player player, absl::Span<float>::iterator& it) {
  for (uint8_t q = 0; q < Board().SquareDimensions(); ++q) {
    for (uint8_t r = 0; r < Board().SquareDimensions(); ++r) {
      //*it++ = Board().IsPlaceable(player, {q, r, 0}) ? 1.0f : 0.0f;
    }
  }
}


void HiveState::CreateArticulationPlane(Player player, absl::Span<float>::iterator& it) {
  for (uint8_t q = 0; q < Board().SquareDimensions(); ++q) {
    for (uint8_t r = 0; r < Board().SquareDimensions(); ++r) {
      //*it++ = Board().IsPlaceable(player, {q, r, 0}) ? 1.0f : 0.0f;
    }
  }
}


void HiveState::CreateCoveredPlane(Player player, absl::Span<float>::iterator& it) {
  for (uint8_t q = 0; q < Board().SquareDimensions(); ++q) {
    for (uint8_t r = 0; r < Board().SquareDimensions(); ++r) {
      //*it++ = Board().IsPlaceable(player, {q, r, 0}) ? 1.0f : 0.0f;
    }
  }
}


// we assume the move is valid at this point and simply apply it
void HiveState::DoApplyAction(Action action) {
  if (action == PassAction()) {
    Board().Pass();
  } else {
    // Move move = ActionToMove(action);
    // HivePosition target_pos = move.to ? move.EndPosition() : kOriginPosition;

    // if (Board().IsInPlay(move.from)) {
    //   Board().MoveTile(move.from, move.to, move.direction);
    // } else {
    //   Board().PlaceTile(move.from, target_pos);
    // }

    bool success = Board().MoveTile(ActionToMove(action));

    // something has gone wrong, force end the game (likely as a draw)
    if (!success) {
      force_terminal_ = true;
    }
  }

  current_player_ = (++current_player_) % kNumPlayers;
}











HiveGame::HiveGame(const GameParameters& params)
  : Game(kGameType, params),
    board_radius_(kDefaultBoardRadius),
    expansions_({
      ParameterValue<bool>("m"),
      ParameterValue<bool>("l"),
      ParameterValue<bool>("p")
    }) {
  num_bug_types_ = kDefaultNumBugTypes;
  if (expansions_.uses_mosquito) {
    ++num_bug_types_;
  }

  if (expansions_.uses_ladybug) {
    ++num_bug_types_;
  }

  if (expansions_.uses_pillbug) {
    ++num_bug_types_;
  }
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
