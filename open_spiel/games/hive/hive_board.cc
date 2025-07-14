// Copyright 2025 DeepMind Technologies Limited
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

#include "open_spiel/games/hive/hive_board.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
//#include <queue>
#include <string>
#include <vector>

#include "open_spiel/abseil-cpp/absl/container/flat_hash_map.h"
#include "open_spiel/abseil-cpp/absl/container/flat_hash_set.h"
#include "open_spiel/abseil-cpp/absl/strings/str_cat.h"
#include "open_spiel/abseil-cpp/absl/strings/str_join.h"
#include "open_spiel/spiel_utils.h"

namespace open_spiel {
namespace hive {

HiveBoard::HiveBoard(int board_radius, ExpansionInfo expansions,
                     bool fixed_orientation)
    : hex_radius_(std::min(board_radius, kMaxBoardRadius)),
      expansions_(expansions),
      fixed_orientation_(fixed_orientation),
      square_dims_(2 * Radius() + 1),
      board_size_(square_dims_ * square_dims_),
      tile_grid_(board_size_),
      visited_slides_(board_size_, false),
      neighbours_grid_(board_size_) {}

void HiveBoard::GenerateAllMoves(std::vector<Move>* out_vec, Colour to_move,
                                 int move_number) const {
  GeneratePlacementMoves(out_vec, to_move, move_number);

  // generate legal moves for tiles in play (Queen must also be in play)
  if (IsInPlay(to_move, BugType::kQueen)) {
    for (auto tile : played_tiles_) {
      if (tile.GetColour() == to_move && tile != last_moved_) {
        GenerateMovesFor(out_vec, tile, tile.GetBugType(), to_move);
      }
    }
  }
}

void HiveBoard::GeneratePlacementMoves(std::vector<Move>* out, Colour to_move,
                                       int move_number) const {
  // first two moves in a game have special placement rules
  // move 0: white must play a (non-queen) tile at the origin
  if (move_number == 0) {
    for (auto tile : HiveTile::GetTilesForColour(to_move)) {
      if (tile.GetBugType() == BugType::kQueen || tile.GetOrdinal() > 1) {
        continue;
      }

      if (!expansions_.IsBugTypeEnabled(tile.GetBugType())) {
        continue;
      }

      // playing the first tile at the origin is encoded as a move where
      // a tile is placed "on top of nothing", i.e. from=tile, to=nullptr
      out->emplace_back(Move{tile, HiveTile::kNoneTile, Direction::kAbove});
    }

    // move 1: black must play a (non-queen) tile next to white's first tile.
    // this is the only time placing a tile next to an opponent's is allowed
  } else if (move_number == 1) {
    for (auto tile : HiveTile::GetTilesForColour(to_move)) {
      if (tile.GetBugType() == BugType::kQueen || tile.GetOrdinal() > 1) {
        continue;
      }

      if (!expansions_.IsBugTypeEnabled(tile.GetBugType())) {
        continue;
      }

      for (int i = 0; i < Direction::kNumCardinalDirections; ++i) {
        // a fixed starting orientation allows for the simplification of the
        // game's opening, reducing the branching factor five-fold
        if (fixed_orientation_ && static_cast<Direction>(i) != Direction::kE) {
          continue;
        }

        out->emplace_back(
            Move{tile, played_tiles_.front(), static_cast<Direction>(i)});
      }
    }
  } else {
    // Queen *must* be played by each player's 4th turn (8 total moves). For
    // all other turns, find valid placement locations by computing a
    // set difference of the player's influence positions
    bool queen_placed =
        move_number >= 8 ||
        IsInPlay(to_move == Colour::kWhite ? HiveTile::wQ : HiveTile::bQ);

    // for (auto ref_tile : played_tiles_) {
    //   if (ref_tile.GetColour() != to_move || IsCovered(ref_tile)) {
    //     continue;
    //   }

    //   // check the neighbours of every tile of our colour in play for placement
    //   for (uint8_t dir = 0; dir < Direction::kNumCardinalDirections; ++dir) {
    //     HivePosition to_test = GetPositionOf(ref_tile) + kNeighbourOffsets[dir];
    //     if (IsPlaceable(to_move, to_test)) {
    //       // once here, we have a valid location to place, so generate Moves
    //       for (auto tile_to_play : HiveTile::GetTilesForColour(to_move)) {
    //         if (IsInPlay(tile_to_play)) {
    //           continue;
    //         }

    //         if ((move_number == 6 || move_number == 7) && !queen_placed &&
    //              tile_to_play.GetBugType() != BugType::kQueen) {
    //           continue;
    //         }

    //         out->emplace_back(Move{tile_to_play, ref_tile, OppositeDirection(dir)});
    //       }
    //     }
    //   }
    // }

    for (auto ref_tile : played_tiles_) {
      if (ref_tile.GetColour() != to_move) {
        continue;
      }

      for (uint8_t dir = 0; dir < Direction::kNumCardinalDirections; ++dir) {
        HivePosition to_test = GetPositionOf(ref_tile) + kNeighbourOffsets[dir];
        if (IsPlaceable(to_move, to_test)) {
          for (auto tile_to_play : HiveTile::GetTilesForColour(to_move)) {
            if (!expansions_.IsBugTypeEnabled(tile_to_play.GetBugType()) ||
                IsInPlay(tile_to_play)) {
              continue;
            }

            if ((move_number == 6 || move_number == 7) && !queen_placed &&
                tile_to_play.GetBugType() != BugType::kQueen) {
              continue;
            }

            // check if previous tile ordinal has been played
            if (tile_to_play.GetOrdinal() != 1 &&
              !IsInPlay(to_move, tile_to_play.GetBugType(), tile_to_play.GetOrdinal() - 1)) {
                continue;
            }

            out->emplace_back(Move{tile_to_play, ref_tile, static_cast<Direction>(dir)});
          }
        }
      }
    }



    // for (auto tile : HiveTile::GetTilesForColour(to_move)) {
    //   if (!expansions_.IsBugTypeEnabled(tile.GetBugType())) {
    //     continue;
    //   }

    //   if (IsInPlay(tile)) {
    //     continue;
    //   }

    //   // Queen *must* be played by each player's 4th turn
      // if ((move_number == 6 || move_number == 7) && !queen_placed &&
      //     tile.GetBugType() != BugType::kQueen) {
      //   continue;
      // }

    //   // 
    //   for (auto tile : HiveTile::GetTilesForColour(to_move))
    //     for (auto nb : neighbours_grid_[]) {

    //     }

    //   // check all influence positions for validity
    //   for (auto pos : colour_influence_[static_cast<int>(to_move)]) {
    //     if (GetTopTileAt(pos).HasValue()) {
    //       continue;
    //     }

    //     // skip - other player's tile is next to this spot
    //     if (colour_influence_[static_cast<int>(OtherColour(to_move))].contains(
    //             pos)) {
    //       continue;
    //     }

    //     // for completeness, any neighbouring tile can be used as the reference.
    //     // would be nice to have an alternative action space to limit this down
    //     for (uint8_t i = 0; i < Direction::kNumCardinalDirections; ++i) {
    //       HivePosition to_pos = pos + kNeighbourOffsets[i];
    //       HiveTile neighbour = GetTopTileAt(to_pos);
    //       if (neighbour.HasValue()) {
    //         out->emplace_back(Move{tile, neighbour, OppositeDirection(i)});
    //       }
    //     }
    //   }
    // }
  }
}

void HiveBoard::GenerateMovesFor(std::vector<Move>* out, HiveTile tile,
                                 BugType acting_type, Colour to_move) const {
  SPIEL_DCHECK_TRUE(expansions_.IsBugTypeEnabled(acting_type));

  HivePosition start_pos = tile_positions_[tile];
  std::vector<HivePosition> positions;
  //positions.reserve(Direction::kNumCardinalDirections);

  // using an explicitly provided acting BugType to account for the Mosquito
  switch (acting_type) {
    case BugType::kQueen:
      GenerateExactValidSlides(&positions, tile, start_pos, 1);
      break;

    case BugType::kAnt:
      GenerateAllValidSlides(&positions, tile, start_pos);
      break;

    case BugType::kGrasshopper:
      GenerateValidGrasshopperPositions(&positions, tile, start_pos);
      break;

    case BugType::kSpider:
      GenerateExactValidSlides(&positions, tile, start_pos, 3);
      break;

    case BugType::kBeetle:
      GenerateValidClimbs(&positions, tile, start_pos);
      if (start_pos.H() == 0) {
        GenerateExactValidSlides(&positions, tile, start_pos, 1);
      }
      break;

    case BugType::kMosquito:
      GenerateValidMosquitoPositions(out, tile, start_pos, to_move);
      break;

    case BugType::kLadybug:
      GenerateValidLadybugPositions(&positions, tile, start_pos);
      break;

    case BugType::kPillbug:
      GenerateExactValidSlides(&positions, tile, start_pos, 1);
      GenerateValidPillbugSpecials(out, tile, start_pos);
      break;

    default:
      SpielFatalError("Unrecognized BugType");
  }

  // turn each position into moves by finding neighbouring tiles as reference
  for (auto to_pos : positions) {
    if (to_pos.H() > 0) {
      // only generate kAbove moves when on top the hive
      out->emplace_back(Move{tile, GetTopTileAt(to_pos), Direction::kAbove});
    } else {
      // check for a valid reference tile in each cardinal direction
      for (uint8_t dir = 0; dir < Direction::kNumCardinalDirections; ++dir) {
        HiveTile neighbour = GetTopTileAt(to_pos + kNeighbourOffsets[dir]);
        if (neighbour.HasValue()) {
          if (start_pos.H() > 0 && neighbour == tile) {
            // special case where the only neighbouring reference tile is itself
            // on top of the stack, so use the tile directly below it
            out->emplace_back(
                Move{tile, GetTileBelow(start_pos), OppositeDirection(dir)});
          } else if (neighbour != tile) {
            out->emplace_back(Move{tile, neighbour, OppositeDirection(dir)});
          }
        }
      }
    }
  }
}



// TODO: BIG PERF - unroll this recursive function into multiple loops
void HiveBoard::GenerateExactValidSlides(std::vector<HivePosition>* out,
                                    HiveTile tile, HivePosition start_pos,
                                    int distance) const {

  if (IsCovered(tile) || IsPinned(tile)) {
    return;
  }

  std::fill(visited_slides_.begin(), visited_slides_.end(), false);
  visited_slides_[AxialToIndex(start_pos)] = true;
  //std::queue<std::pair<HivePosition, int>> to_search{{{start_pos, 0}}};
  std::vector<std::pair<HivePosition, int>> to_search{{{start_pos, 0}}};

  // dfs
  std::function<void(HivePosition,int)> dfs = [&](HivePosition pos, int depth) {
    if (depth == distance || (depth > 0 && distance < 0)) {
      out->push_back(pos);
    }
  
    const NeighbourList& nbs = GetNeighboursOf(pos);
    for (uint8_t dir = 0; dir < Direction::kNumCardinalDirections; ++dir) {
      // if (nbs[dir].HasValue()) {
      //   continue;
      // }

      HivePosition to_pos = pos + kNeighbourOffsets[dir];
      size_t to_pos_idx = AxialToIndex(to_pos);

      // Check these 4 conditions, with IsGated at end due to perf
      if (visited_slides_[to_pos_idx] ||
          nbs[dir].HasValue() ||
          !IsInBounds(to_pos) ||
          IsGated(pos, (Direction)dir, start_pos)) {
        continue;
      }

      // once here, slide can be performed
      visited_slides_[to_pos_idx] = true;
      dfs(to_pos, depth + 1);

      // unmark this position from visited if an exact distance is needed
      // (to allow previous paths to be reused at earlier depths)
      if (distance > 0) {
        visited_slides_[to_pos_idx] = false;
      }
    }
  };

  dfs(start_pos, 0);

  // breadth-first search
  // while (!to_search.empty()) {
  //   auto [pos, depth] = to_search.back();
  //   to_search.pop_back();
  //   const NeighbourList& nbs = GetNeighboursOf(pos);

  //   for (uint8_t dir = 0; dir < Direction::kNumCardinalDirections; ++dir) {
  //     if (nbs[dir].HasValue()) {
  //       continue;
  //     }

  //     HivePosition to_pos = pos + kNeighbourOffsets[dir];
  //     size_t to_pos_idx = AxialToIndex(to_pos);

  //     if (visited_slides_[to_pos_idx] || !IsInBounds(to_pos)) {
  //       continue;
  //     }

  //     if (!IsGated(pos, (Direction)dir, start_pos)) {
  //       visited_slides_[to_pos_idx] = true;

  //       if (distance < 0 || depth + 1 == distance) {
  //         out->push_back(to_pos);
  //       }

  //       if (distance < 0 || depth + 1 < distance) {
  //         to_search.push_back({to_pos, depth + 1});
  //       }
  //     }
  //   }
  // }
}

void HiveBoard::GenerateAllValidSlides(std::vector<HivePosition>* out,
                                    HiveTile tile, HivePosition start_pos) const {

  std::fill(visited_slides_.begin(), visited_slides_.end(), false);
  visited_slides_[AxialToIndex(start_pos)] = true;
  //std::queue<std::pair<HivePosition, int>> to_search{{{start_pos, 0}}};
  std::vector<HivePosition> to_search{start_pos};

  if (IsCovered(tile) || IsPinned(tile)) {
    return;
  }

  // breadth-first search to avoid nested DFS recursion on inner loop
  while (!to_search.empty()) {
    HivePosition pos = to_search.back();
    to_search.pop_back();
    const NeighbourList& nbs = GetNeighboursOf(pos);

    for (uint8_t dir = 0; dir < Direction::kNumCardinalDirections; ++dir) {
      if (nbs[dir].HasValue()) {
        continue;
      }

      HivePosition to_pos = pos + kNeighbourOffsets[dir];
      size_t to_pos_idx = AxialToIndex(to_pos);

      if (visited_slides_[to_pos_idx] || !IsInBounds(to_pos)) {
        continue;
      }

      if (!IsGated(pos, (Direction)dir, start_pos)) {
        visited_slides_[to_pos_idx] = true;
        out->push_back(to_pos);
        to_search.push_back(to_pos);
      }
    }
  }
}

// void HiveBoard::GenerateValidSlides(std::vector<HivePosition>* out,
//                                     HiveTile tile, HivePosition start_pos,
//                                     int distance) const {
//   if (IsPinned(tile) || IsCovered(tile)) {
//     return;
//   }

//   int dupes = 0;
//   const bool unlimited_distance = distance < 0;
//   absl::flat_hash_set<HivePosition> visited;

//   auto dfs = [&](auto& dfs, HivePosition pos, Direction from,
//                  int depth) -> void {
//     if (visited.contains(pos) || (!unlimited_distance && depth > distance)) {
//       return;
//     }

//     // validate positions breadth-first
//     for (uint8_t dir = 0; dir < Direction::kNumCardinalDirections; ++dir) {
//       HivePosition to_test = pos + kNeighbourOffsets[dir];
//       HiveTile test_tile = GetTopTileAt(to_test);

//       if (dir == from) {
//         continue;
//       }

//       if (visited.contains(to_test)) {
//         continue;
//       }

//       // all must be false to be a valid slide direction
//       if (test_tile.HasValue() ||
//           IsGated(pos, static_cast<Direction>(dir), start_pos) || // #PERF IsGated()
//           !IsConnected(to_test, start_pos)) {                     // #PERF from NeighboursOf()
//         continue;
//       }

//       if (depth == distance || unlimited_distance) {
//         if (tile.GetBugType() == BugType::kAnt && out->contains(to_test)) {
//           ++dupes;
//         }
//         out->insert(to_test);
//       }
//     }

//     if (depth == distance) {
//       return;
//     }

//     visited.insert(pos);

//     // traverse depth-first
//     for (uint8_t dir = 0; dir < Direction::kNumCardinalDirections; ++dir) {
//       HivePosition to_test = pos + kNeighbourOffsets[dir];
//       HiveTile test_tile = GetTopTileAt(to_test);

//       if (dir == from) {
//         continue;
//       }

//       if (visited.contains(to_test)) {
//         continue;
//       }

//       // all must be false to be a valid slide direction
//       if (test_tile.HasValue() ||
//           IsGated(pos, static_cast<Direction>(dir), start_pos) ||
//           !IsConnected(to_test, start_pos)) {
//         continue;
//       }

//       if (depth == distance || unlimited_distance) {
//         if (tile.GetBugType() == BugType::kAnt && out->contains(to_test)) {
//           ++dupes;
//         }
//         out->insert(to_test);
//       }

//       dfs(dfs, to_test, OppositeDirection(dir), depth + 1);

//       if (!unlimited_distance) {
//         visited.erase(to_test);
//       }
//     }
//   };

//   dfs(dfs, start_pos, Direction::kNumAllDirections, 1);
//   if (tile.GetBugType() == BugType::kAnt) {
//     //std::cout << "Found " << dupes << " duplicate moves when generating slides for ant." << std ::endl;
//   }
// }

void HiveBoard::GenerateValidClimbs(std::vector<HivePosition>* out,
                                    HiveTile tile,
                                    HivePosition start_pos) const {
  if (IsCovered(tile) || (IsPinned(tile) && start_pos.H() == 0)) {
    return;
  }

  HivePosition ground_pos = start_pos.Grounded();

  // find the top tile, or an empty position, in each adjacent position
  for (uint8_t d = 0; d < Direction::kNumCardinalDirections; ++d) {
    HiveTile neighbour = GetTopTileAt(ground_pos + kNeighbourOffsets[d]);
    if (neighbour.HasValue()) {
      HivePosition to_pos =
          tile_positions_[neighbour].NeighbourAt(Direction::kAbove);
      // climbing UP: check for a gate at the *target*'s height
      if (to_pos.H() > start_pos.H() &&
          !IsGated({start_pos.Q(), start_pos.R(), to_pos.H()},
                   static_cast<Direction>(d))) {
        out->push_back(to_pos);
        // climbing DOWN or across: check for gate at *this* tile's height
      } else if (to_pos.H() <= start_pos.H() &&
                 !IsGated(start_pos,static_cast<Direction>(d))) {
        out->push_back(to_pos);
      }
    } else {
      HivePosition to_pos = ground_pos + kNeighbourOffsets[d];
      // climbing DOWN to empty space: check for a gate at *this* tile's height
      if (to_pos.H() < start_pos.H() &&
          !IsGated(start_pos,static_cast<Direction>(d))) {
        out->push_back(to_pos);
      }
    }
  }
}

void HiveBoard::GenerateValidGrasshopperPositions(
    std::vector<HivePosition>* out, HiveTile tile,
    HivePosition start_pos) const {
  if (IsPinned(tile) || IsCovered(tile)) {
    return;
  }

  // in each cardinal direction that contains a tile, jump over all tiles in
  // that direction until reaching an empty space to land
  for (uint8_t d = 0; d < Direction::kNumCardinalDirections; ++d) {
    bool found = false;
    HivePosition to_test = start_pos + kNeighbourOffsets[d];
    while (GetTopTileAt(to_test).HasValue()) {
      to_test += kNeighbourOffsets[d];
      found = true;
    }

    if (found) {
      out->push_back(to_test);
    }
  }
}

void HiveBoard::GenerateValidLadybugPositions(
    std::vector<HivePosition>* out, HiveTile tile,
    HivePosition start_pos) const {
  if (IsPinned(tile) || IsCovered(tile)) {
    return;
  }

  // A lady bug moves in *exactly* 3 distinct steps: a climb onto the hive,
  // a slide/climb across the hive, and a climb down from the hive
  std::vector<HivePosition> intermediates1;
  std::vector<HivePosition> intermediates2;
  std::vector<HivePosition> intermediates3;

  // step 1
  GenerateValidClimbs(&intermediates1, tile, start_pos);

  // step 2
  for (auto pos : intermediates1) {
    GenerateValidClimbs(&intermediates2, tile, pos);
  }

  // step 3
  for (auto pos : intermediates2) {
    // ensure destination is above the hive but not "above itself"
    if (pos.H() == 0 ||
        pos == start_pos + kNeighbourOffsets[Direction::kAbove]) {
      continue;
    }

    GenerateValidClimbs(&intermediates3, tile, pos);
  }

  // only consider moves that finish on ground level
  for (auto pos : intermediates3) {
    if (pos.H() == 0) {
      out->push_back(pos);
    }
  }
}

void HiveBoard::GenerateValidMosquitoPositions(std::vector<Move>* out,
                                               HiveTile tile,
                                               HivePosition start_pos,
                                               Colour to_move) const {
  // we defer IsPinned() check as the Mosquito could still use a Pillbug special
  if (IsCovered(tile)) {
    return;
  }

  // when on top of the hive, a Mosquito can only act as a Beetle
  if (start_pos.H() > 0) {
    GenerateMovesFor(out, tile, BugType::kBeetle, to_move);
    return;
  }

  // Mosquitos copy the movement capabilities of any adjacent bug type
  std::array<bool, static_cast<size_t>(BugType::kNumBugTypes)> types_seen{};
  for (auto neighbour : NeighboursOf(start_pos)) {
    BugType type = neighbour.GetBugType();

    if (!types_seen[static_cast<size_t>(type)]) {
      types_seen[static_cast<size_t>(type)] = true;

      if (type == BugType::kMosquito) {
        continue;
      }

      // Queen and Spider moves are strict subsets of an Ant's moves
      if ((type == BugType::kQueen || type == BugType::kSpider) &&
          types_seen[static_cast<size_t>(BugType::kAnt)]) {
        continue;
      }

      GenerateMovesFor(out, tile, type, to_move);
    }
  }
}

void HiveBoard::GenerateValidPillbugSpecials(std::vector<Move>* out,
                                             HiveTile tile,
                                             HivePosition start_pos) const {
  // Pillbug can still perform its special when Pinned
  if (IsCovered(tile)) {
    return;
  }

  std::vector<HiveTile> valid_targets;
  std::vector<HivePosition> valid_positions;

  for (uint8_t dir = 0; dir < Direction::kNumCardinalDirections; ++dir) {
    // ensure there is no "gate" blocking above for this direction
    if (IsGated(start_pos + kNeighbourOffsets[Direction::kAbove],
                static_cast<Direction>(dir))) {
      continue;
    }

    HivePosition test_pos = start_pos + kNeighbourOffsets[dir];
    HiveTile test_tile = GetTopTileAt(test_pos);
    if (test_tile.HasValue()) {
      // valid IFF the target tile is not: Pinned, Covered, the LastMovedTile,
      // or above the hive
      if (!IsPinned(test_tile) && !IsCovered(test_tile) &&
          test_tile != LastMovedTile() && GetPositionOf(test_tile).H() == 0) {
        valid_targets.push_back(test_tile);
      }
    } else {
      valid_positions.push_back(test_pos);
    }
  }

  // for every target_tile, add a move to every valid position by checking
  // that position for its neighbours
  for (auto target_tile : valid_targets) {
    for (auto target_pos : valid_positions) {
      for (uint8_t dir = 0; dir < Direction::kNumCardinalDirections; ++dir) {
        HiveTile ref_tile = GetTopTileAt(target_pos + kNeighbourOffsets[dir]);
        if (ref_tile.HasValue() && ref_tile != target_tile) {
          out->emplace_back(
              Move{target_tile, ref_tile,
                   OppositeDirection(static_cast<Direction>(dir))});
        }
      }
    }
  }
}

const NeighbourList& HiveBoard::GetNeighboursOf(HivePosition pos) const {
  return neighbours_grid_[AxialToIndex(pos)];
}

std::vector<HiveTile> HiveBoard::NeighboursOf(HivePosition pos,
                                              HivePosition to_ignore) const {
  std::vector<HiveTile> neighbours;
  for (auto neighbour : pos.Neighbours()) {
    auto tile = GetTopTileAt(neighbour);
    if (tile.HasValue()) {
      neighbours.push_back(tile);
    }
  }

  return neighbours;
}

bool HiveBoard::MoveTile(Move move) {
  SPIEL_DCHECK_TRUE(move.from.HasValue());

  // compute the final position from the reference tile + direction
  HivePosition new_pos;
  if (move.to.HasValue()) {
    new_pos = tile_positions_[move.to] + kNeighbourOffsets[move.direction];

    if (!IsInBounds(new_pos)) {
      if (RecenterBoard(new_pos)) {
        new_pos = tile_positions_[move.to] + kNeighbourOffsets[move.direction];
      } else {
        // if the new position is still out of bounds, force terminate the game
        // as a draw (possible with board_sizes smaller than kMaxBoardRadius)
        return false;
      }
    }

    // if the reference tile was higher on the hive, the new_pos may need to
    // "fall down" until it hits either the ground or another tile
    if (new_pos.H() > 0) {
      HiveTile top_tile = GetTopTileAt(new_pos);
      if (top_tile.HasValue()) {
        new_pos.SetH(tile_positions_[top_tile].H() + 1);
      } else {
        new_pos.SetH(0);
      }
    }

  } else {
    // having no "to" tile encodes the opening move at the origin
    new_pos = kOriginPosition;
  }

  HivePosition old_pos = tile_positions_[move.from];
  if (old_pos == kNullPosition) {
    played_tiles_.push_back(move.from);
  }

  if (new_pos != old_pos) {
    last_moved_from_ = old_pos;
  }

  size_t old_idx = AxialToIndex(old_pos);
  size_t new_idx = AxialToIndex(new_pos);

  // if a tile already exists at the new position, it's now condsidered covered
  if (tile_grid_[new_idx].HasValue()) {
    for (int i = 0; i < covered_tiles_.size(); ++i) {
      if (!covered_tiles_[i].HasValue()) {
        covered_tiles_[i] = tile_grid_[new_idx];
        break;
      }
    }
  }

  // perform the move
  tile_grid_[new_idx] = move.from;
  tile_positions_[move.from] = new_pos;
  last_moved_ = move.from;

  // potentially reinstate a covered tile at the old position
  if (old_pos.H() > 0) {
    // reverse iterating guarantees the first tile found has the next highest H
    for (int i = covered_tiles_.size() - 1; i >= 0; --i) {
      if (covered_tiles_[i] == HiveTile::kNoneTile) {
        continue;
      }

      if (old_pos.Grounded() == GetPositionOf(covered_tiles_[i]).Grounded()) {
        tile_grid_[old_idx] = covered_tiles_[i];
        covered_tiles_[i] = HiveTile::kNoneTile;

        // left-rotate the kNoneTile to the end of the covered_tiles_ array
        // to maintain height order
        std::rotate(covered_tiles_.begin() + i, covered_tiles_.begin() + i + 1,
                    covered_tiles_.end());
        break;
      }
    }
  } else if (old_pos != kNullPosition) {
    tile_grid_[old_idx] = HiveTile::kNoneTile;
  }

  // Board state has changed, re-calc cache(s)
  // Only need to recalc the neighbours of "to" and "from"s neighbours

  for (uint8_t dir = 0; dir < Direction::kNumCardinalDirections; ++dir) {
    // tell the neighbours of old_pos that they have lost a neighbour in the
    // opposing direction, unless tile played for first time
    if (old_pos != kNullPosition) {
      size_t old_neighbour_idx = AxialToIndex(old_pos.NeighbourAt(static_cast<Direction>(dir)));
      
      if (old_neighbour_idx < BoardSize() && old_neighbour_idx >= 0) {
        // tile_grid_[old_idx] is either kNoneTile, or the next highest tile in stack
        neighbours_grid_[old_neighbour_idx][OppositeDirection(dir)] = tile_grid_[old_idx];
      }
    }

    // tell the neighbours of new_pos that they have gained a neighbour in the
    // opposing direction
    size_t new_neighbour_idx = AxialToIndex(new_pos.NeighbourAt(static_cast<Direction>(dir)));
    if (new_neighbour_idx < BoardSize() && new_neighbour_idx >= 0) {
      neighbours_grid_[new_neighbour_idx][OppositeDirection(dir)] = move.from;
    }
  }

  UpdateArticulationPoints();

  return true;
}

bool HiveBoard::IsInBounds(HivePosition pos) const {
  return (pos.Q() >= -Radius() && pos.Q() <= Radius() &&
          pos.R() >= -Radius() && pos.R() <= Radius());
}

bool HiveBoard::RecenterBoard(HivePosition new_pos) {
  int8_t max_Q = 0;
  int8_t min_Q = 0;
  int8_t max_R = 0;
  int8_t min_R = 0;
  int8_t max_S = 0;
  int8_t min_S = 0;

  for (auto tile : played_tiles_) {
    HivePosition pos = GetPositionOf(tile);
    max_Q = std::max(max_Q, pos.Q());
    min_Q = std::min(min_Q, pos.Q());
    max_R = std::max(max_R, pos.R());
    min_R = std::min(min_R, pos.R());
    max_S = std::max(max_S, pos.S());
    min_S = std::min(min_S, pos.S());
  }

  // determine the new "center" by averaging each axis and round
  // to the nearest integer hex coordinate
  double avg_Q = (max_Q + min_Q) / 2.0;
  double avg_R = (max_R + min_R) / 2.0;
  double avg_S = (max_S + min_S) / 2.0;

  int8_t round_Q = std::round(avg_Q);
  int8_t round_R = std::round(avg_R);
  int8_t round_S = std::round(avg_S);

  double diff_Q = std::abs(round_Q - avg_Q);
  double diff_R = std::abs(round_R - avg_R);
  double diff_S = std::abs(round_S - avg_S);

  if (diff_Q > diff_R && diff_Q > diff_S) {
    round_Q = -round_R - round_S;
  } else if (diff_R > diff_S) {
    round_R = -round_Q - round_S;
  }

  HivePosition offset = HivePosition(-round_Q, -round_R);

  // there are no valid directions to reposition the board without going OOB
  if (offset == kOriginPosition || !IsInBounds(new_pos + offset)) {
    return false;
  }

  // apply this offset to each valid position
  bool oob = false;
  std::for_each(played_tiles_.begin(), played_tiles_.end(),
                [this, offset, &oob](HiveTile tile) {
                  tile_positions_[tile] += offset;

                  // this usually occurs when tiles exist at each axes' extremes
                  if (!IsInBounds(tile_positions_[tile])) {
                    oob = true;
                  }
                });

  if (oob) {
    return false;
  }

  // TODO: REMOVE
  has_recentered_ = true;
  // TODO: REMOVE

  // recalculate grid indices
  std::fill(tile_grid_.begin(), tile_grid_.end(), HiveTile::kNoneTile);
  for (uint8_t tile = HiveTile::wQ; tile < HiveTile::kNumTiles; ++tile) {
    if (IsInPlay(tile) && !IsCovered(tile)) {
      tile_grid_[AxialToIndex(GetPositionOf(tile))] = tile;
    }
  }

  // reset neigbhours
  for (auto nbs : neighbours_grid_) {
    std::fill(nbs.begin(), nbs.end(), HiveTile::kNoneTile);
  }

  // full neighbour re-calculation (expensive but very infrequent)
  for (uint8_t tile = HiveTile::wQ; tile < HiveTile::kNumTiles; ++tile) {
    if (IsInPlay(tile)) {
      HivePosition pos = tile_positions_[tile];
      for (uint8_t dir = 0; dir < Direction::kNumCardinalDirections; ++dir) {
        HivePosition nb_pos = pos + kNeighbourOffsets[dir];
        HiveTile nb_tile = GetTopTileAt(nb_pos);

        // bi-directional neighbour pair
        neighbours_grid_[AxialToIndex(pos)][dir] = nb_tile;
        if (IsInBounds(nb_pos)) {
          neighbours_grid_[AxialToIndex(nb_pos)][OppositeDirection(dir)] = GetTopTileAt(pos);
        }
      }
    }
  }

  // must be AFTER neighbour calculations
  UpdateArticulationPoints();

  return true;
}

void HiveBoard::Pass() {
  last_moved_ = HiveTile::kNoneTile;
  last_moved_from_ = kNullPosition;
}

bool HiveBoard::IsQueenSurrounded(Colour c) const {
  HiveTile queen = c == Colour::kWhite ? HiveTile::wQ : HiveTile::bQ;
  if (!IsInPlay(c, BugType::kQueen)) {
    return false;
  }

  for (auto neighbour_pos : tile_positions_[queen].Neighbours()) {
    if (GetTopTileAt(neighbour_pos) == HiveTile::kNoneTile) {
      return false;
    }
  }

  return true;
}

// tile accessor with bounds checking
HiveTile HiveBoard::GetTopTileAt(HivePosition pos) const {
  size_t idx = AxialToIndex(pos);
  if (idx < 0 || idx >= tile_grid_.size()) {
    return HiveTile::kNoneTile;
  }

  return tile_grid_[idx];
}

HiveTile HiveBoard::GetTileBelow(HivePosition pos) const {
  SPIEL_DCHECK_TRUE(pos.H() > 0);

  HivePosition below = pos - kNeighbourOffsets[Direction::kAbove];
  // first check the top tile at this axial position
  if (GetPositionOf(GetTopTileAt(below)) == below) {
    return GetTopTileAt(below);
  }

  // otherwise, check the covered_tiles_ list
  for (auto tile : covered_tiles_) {
    if (tile.HasValue() && tile_positions_[tile] == below) {
      return tile;
    }
  }

  return HiveTile::kNoneTile;
}

// IsGated verifies requirement (3) in GenerateValidSlides()
bool HiveBoard::IsGated(HivePosition pos, Direction d,
                        HivePosition to_ignore) const {
  HivePosition cw = pos + kNeighbourOffsets[ClockwiseDirection(d)];
  HivePosition ccw = pos + kNeighbourOffsets[CounterClockwiseDirection(d)];

  const bool cw_exists =
      cw != to_ignore && GetPositionOf(GetTopTileAt(cw)).H() >= pos.H();
  const bool ccw_exists =
      ccw != to_ignore && GetPositionOf(GetTopTileAt(ccw)).H() >= pos.H();
  return pos.H() == 0 ? cw_exists == ccw_exists : cw_exists && ccw_exists;
}

bool HiveBoard::IsConnected(HivePosition pos, HivePosition to_ignore) const {
  return !NeighboursOf(pos, to_ignore).empty();
}

bool HiveBoard::IsCovered(HivePosition pos) const {
  return std::find_if(
      covered_tiles_.begin(), covered_tiles_.end(),
      [this, pos](HiveTile tile) { return GetPositionOf(tile) == pos; });
}

bool HiveBoard::IsCovered(HiveTile tile) const {
  return tile.HasValue() &&
         std::find(covered_tiles_.begin(), covered_tiles_.end(), tile) !=
             covered_tiles_.end();
}

bool HiveBoard::IsPinned(HivePosition pos) const {
  return IsPinned(GetTopTileAt(pos));
}

bool HiveBoard::IsPinned(HiveTile tile) const {
  if (tile.HasValue()) {
    return pinned_tiles_[tile];
  }

  return false;
}

// #PERF Big one (.contains())
bool HiveBoard::IsPlaceable(Colour c, HivePosition pos) const {
  if (IsInPlay(GetTopTileAt(pos))) {
    return false;
  }

  bool friendly_nb = false;
  bool opposing_nb = false;
  for (auto nb : GetNeighboursOf(pos)) {
    if (nb.HasValue()) {
      friendly_nb |= nb.GetColour() == c;
      opposing_nb |= nb.GetColour() == OtherColour(c);
    }

    if (opposing_nb) {
      return false;
    }
  }

  return friendly_nb && !opposing_nb;
}

void HiveBoard::BoardUpdated(HivePosition old_pos, HivePosition new_pos) {
  UpdateNeighbours(old_pos, new_pos);
  UpdateArticulationPoints();
}

void HiveBoard::UpdateNeighbours(HivePosition old_pos, HivePosition new_pos) {

}


void HiveBoard::UpdateArticulationPoints() {
  pinned_tiles_.fill(false);

  int visit_order = 0;
  std::bitset<HiveTile::kNumTiles> visited;
  std::array<int, HiveTile::kNumTiles> entry_point{};
  std::array<int, HiveTile::kNumTiles> low_point{};

  auto dfs = [&, this/*TODO: CHANGE THIS*/](auto& dfs, HivePosition vertex, HiveTile parent_tile,
                 bool is_root) -> void {
    HiveTile tile = GetTopTileAt(vertex);

    // TODO DELEEETE
    if (!tile.HasValue()) {
      std::cout << "invalid articulation at idx " << this->AxialToIndex(vertex) << " with parent " << parent_tile.ToUHP() << std::endl;
      std::cout << absl::StrJoin(tile_grid_, ";") << std::endl;
      // SpielFatalError("RYAAAAN");
    }
    visited.set(tile);
    entry_point[tile] = low_point[tile] = visit_order;
    ++visit_order;
    if (visit_order > 50) {
      std::string line;
      std::getline(std::cin, line);
    }

    int children = 0;
    for (auto neighbour : GetNeighboursOf(vertex)) {
      if (!neighbour.HasValue()) {
        continue;
      }

      if (neighbour == parent_tile) {
        continue;
      }

      if (visited.test(neighbour)) {
        low_point[tile] = std::min(low_point[tile], entry_point[neighbour]);
      } else {
        dfs(dfs, tile_positions_[neighbour], tile, false);
        ++children;
        low_point[tile] = std::min(low_point[tile], low_point[neighbour]);
        if (low_point[neighbour] >= entry_point[tile] && !is_root) {
          pinned_tiles_[tile] = true;
        }
      }
    }

    // for (uint8_t dir = 0; dir < Direction::kNumCardinalDirections; ++dir) {
    //   HivePosition to_vertex = vertex + kNeighbourOffsets[dir];
    //   if (!GetTopTileAt(to_vertex).HasValue()) {
    //     continue;
    //   }

    //   if (to_vertex == parent_tile) {
    //     continue;
    //   }

    //   if (visited.contains(to_vertex)) {
    //     low_point[vertex] = std::min(low_point[vertex], entry_point[to_vertex]);
    //   } else {
    //     dfs(dfs, to_vertex, vertex, false);
    //     ++children;
    //     low_point[vertex] = std::min(low_point[vertex], low_point[to_vertex]);
    //     if (low_point[to_vertex] >= entry_point[vertex] && !is_root) {
    //       pinned_tiles_[tile] = true;
    //     }
    //   }
    // }

    if (is_root && children > 1) {
      if (tile.HasValue()) {
        pinned_tiles_[tile] = true;
      }
    }
  };

  // any arbitrary starting point would do, but the Queen is guaranteed to be
  // in play when generating moves
  dfs(dfs, tile_positions_[HiveTile::wQ], HiveTile::kNoneTile, true);
}

std::string HiveTile::ToUHP() const {
  SPIEL_DCHECK_TRUE(HasValue());
  std::string uhp = "";

  // colour
  GetColour() == Colour::kWhite ? absl::StrAppend(&uhp, "w")
                                : absl::StrAppend(&uhp, "b");

  // bug type
  BugType type = GetBugType();
  switch (type) {
    case BugType::kQueen:
      absl::StrAppend(&uhp, "Q");
      break;
    case BugType::kAnt:
      absl::StrAppend(&uhp, "A");
      break;
    case BugType::kGrasshopper:
      absl::StrAppend(&uhp, "G");
      break;
    case BugType::kSpider:
      absl::StrAppend(&uhp, "S");
      break;
    case BugType::kBeetle:
      absl::StrAppend(&uhp, "B");
      break;
    case BugType::kLadybug:
      absl::StrAppend(&uhp, "L");
      break;
    case BugType::kMosquito:
      absl::StrAppend(&uhp, "M");
      break;
    case BugType::kPillbug:
      absl::StrAppend(&uhp, "P");
      break;
    default:
      SpielFatalError("HiveTile::ToUHP() - HiveTile has an invalid bug type!");
  }

  // bug type ordinal (for bugs where there can be more than 1)
  if (type == BugType::kAnt || type == BugType::kGrasshopper ||
      type == BugType::kSpider || type == BugType::kBeetle) {
    absl::StrAppend(&uhp, GetOrdinal());
  }

  return uhp;
}

// UHP string representation of a move
std::string Move::ToUHP() {
  // special case: pass for when a player has no possible legal moves
  if (IsPass()) {
    return "pass";
  }

  // special case: for the first turn, there is no reference tile 'to'
  if (!to.HasValue()) {
    return from.ToUHP();
  }

  std::string reference_tile_uhp = to.ToUHP();
  std::string offset_formatted = "";

  // add a prefix or suffix depending on the relative position
  switch (direction) {
    case Direction::kNE:
      offset_formatted = reference_tile_uhp + "/";
      break;
    case Direction::kE:
      offset_formatted = reference_tile_uhp + "-";
      break;
    case Direction::kSE:
      offset_formatted = reference_tile_uhp + "\\";
      break;
    case Direction::kSW:
      offset_formatted = "/" + reference_tile_uhp;
      break;
    case Direction::kW:
      offset_formatted = "-" + reference_tile_uhp;
      break;
    case Direction::kNW:
      offset_formatted = "\\" + reference_tile_uhp;
      break;
    case Direction::kAbove:
      offset_formatted = reference_tile_uhp;
      break;
    default:
      SpielFatalError("Move::ToUHP() - Move has an invalid direction!");
  }

  return absl::StrCat(from.ToUHP(), " ", offset_formatted);
}

}  // namespace hive
}  // namespace open_spiel
