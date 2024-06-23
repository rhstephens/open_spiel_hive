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

#include "open_spiel/games/hive/hive_hexboard.h"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

//#include "open_spiel/abseil-cpp/absl/random/uniform_int_distribution.h"
#include "open_spiel/spiel.h"


namespace open_spiel {
namespace hive {


// Creates a regular hexagonal board with a default radius of 8 (excluding
// the center) The formula is: "3r^2 + 3r + 1" plus an extra tile for each 
// stackable piece
// 
HexBoard::HexBoard(const int board_radius, bool uses_mosquito, bool uses_ladybug, bool uses_pillbug)
    : hex_radius_(std::min(board_radius, kMaxBoardRadius)),
      num_tiles_(kMaxTileCount), // constant 28 for now?
      tile_grid_(SquareDimensions() * SquareDimensions()) { 


  // init grid with default values
  ;

}


// void HexBoard::CreateTile(const Player player, const BugType type, int ordinal) {
//   // allocate a HiveTile object here, and add to relevant mappings
//   HiveTilePtr tile = std::make_shared<HiveTile>(kNullPosition, player, type, ordinal);
//   tiles_.emplace_back(tile);

//   player_tiles_[player].emplace_back(tile);
// }


// void HexBoard::GetNeighbourTilePositions(std::vector<HivePosition>& in_vec,
//                                          const HivePosition pos) {
//   for (auto offset : kNeighbourOffsets) {
//     HivePosition new_pos = pos + offset;

//     // is there a tile at this position?
//     if (position_cache_.contains(new_pos)) {
//         in_vec.push_back(new_pos);
//     }
//   }
// }

// void HexBoard::GetNeighbourEmptyPositions(std::vector<HivePosition>& in_vec,
//                                           const HivePosition pos) {
//   for (auto& offset : kNeighbourOffsets) {
//     HivePosition new_pos = pos + offset;

//     // is there no tile at this position?
//     if (!position_cache_.contains(new_pos)) {
//         in_vec.push_back(new_pos);
//     }
//   }
// }


// A tile can be encoded by its position in an array of tiles
// laid out left-to-right containing every piece in the game.
// Conveniently, this is the order the tiles are created in
// int HexBoard::EncodeTile(HiveTilePtr& tile) const {
//   int encoding = -1;

//   auto it = std::find(tiles_.begin(), tiles_.end(), tile);
//   SPIEL_CHECK_TRUE(it != tiles_.end());
//   encoding = std::distance(tiles_.begin(), it);

//   return encoding;
// }

// const HiveTilePtr& HexBoard::DecodeTile(int encoding) const {
//   SPIEL_CHECK_TRUE(encoding >= 0 && encoding < num_tiles_);
//   return tiles_.at(encoding);
// }

// size_t HexBoard::AxialToIndex(HivePosition pos) const {
//   size_t idx = pos.Q() + Radius() + ((pos.R() + Radius()) * SquareDimensions());
//   SPIEL_DCHECK_GE(idx, 0);
//   SPIEL_DCHECK_LT(idx, tile_grid_.size());
//   return idx;
// }


void HexBoard::GenerateAllMoves(std::vector<Move>& out_vec, Colour to_move, int move_number) const {

  // find all HivePositions where player can place a tile from hand
  // and present them as Moves
  GeneratePlacementMoves(out_vec, to_move, move_number);

  // generate legal moves for tiles in play (Queen must also be in play)
  if (IsInPlay(to_move, BugType::kQueen)) {
    for (auto tile : played_tiles_) {
      if (tile.GetColour() == to_move && tile != last_moved_) {
        GenerateMovesFor(out_vec, tile, tile.GetBugType(), to_move);
      }
    }
  }

  // TODO: REMOVE
  // std::cout << "Articulation points:" << std::endl;
  // for (auto pos : articulation_points_) {
  //   std::cout << position_cache_.at(pos)->ToUHP() << ", ";
  // }
  // std::cout << std::endl;
}


void HexBoard::GeneratePlacementMoves(std::vector<Move>& out, Colour to_move, int move_number) const {
  // first two moves in a game have special placement rules
  // move 0: white must play a (non-queen) tile at the origin
  if (move_number == 0) {
    for (auto tile : NewHiveTile::GetTilesForColour(to_move)) {
      if (tile.GetBugType() == BugType::kQueen) {
        continue;
      }

      // playing the first tile at the origin is encoded as a move where
      // a tile is placed "on top of nothing", i.e. from=tile, to=nullptr
      out.emplace_back(Move{tile,
                            NewHiveTile::kNoneTile,
                            Direction::kAbove});
    }

  // move 1: black must play a (non-queen) tile next to white's first tile.
  // this is the only time placing a tile next to an opponent's is allowed
  } else if (move_number == 1) {
    for (auto tile : NewHiveTile::GetTilesForColour(to_move)) {
      if (tile.GetBugType() == BugType::kQueen) {
        continue;
      }

      for (int i = 0; i < Direction::kNumCardinalDirections; ++i) {
        out.emplace_back(Move{tile,
                              played_tiles_.front(),
                              static_cast<Direction>(i)});
      }
    }
  } else {
    // Queen *must* be played by each player's 4th turn (8 total moves). For
    // all other turns, find valid placement locations by computing a
    // set difference of the player's influence positions
    bool queen_placed = move_number >= 8 || IsInPlay(to_move == Colour::kWhite ? NewHiveTile::wQ : NewHiveTile::bQ);
    for (auto tile : NewHiveTile::GetTilesForColour(to_move)) {
      if (IsInPlay(tile)) {
        continue;
      }

      // Queen *must* be played by each player's 4th turn
      if ((move_number == 6 || move_number == 7) &&
            !queen_placed &&
            tile.GetBugType() != BugType::kQueen) {
        continue;
      }

      // check all positions for validity
      for (auto pos : colour_influence_[static_cast<int>(to_move)]) {
        // skip - there is already a tile here
        if (GetTopTileAt(pos).HasValue()) {
          continue;
        }

        // skip - other player's tile is next to this spot
        if (colour_influence_[static_cast<int>(OtherColour(to_move))].contains(pos)) {
          continue;
        }

        // for completeness, any neighbouring tile can be used as the reference.
        // would be nice to have an alternative action space to limit this down
        for (uint8_t i = 0; i < Direction::kNumCardinalDirections; ++i) {
          HivePosition to_pos = pos + kNeighbourOffsets[i];
          NewHiveTile neighbour = GetTopTileAt(to_pos);
          if (neighbour.HasValue()) {
            out.emplace_back(Move{tile,
                                  neighbour,
                                  OppositeDirection(i)});
          }
        }
      }
    }
  }
}


void HexBoard::GenerateMovesFor(std::vector<Move>& out, NewHiveTile tile, BugType acting_type, Colour to_move) const {
  //MaybeUpdateSlideCache();

  HivePosition start_pos = tile_positions_[tile];
  absl::flat_hash_set<HivePosition> positions;
  switch (acting_type) {
    case BugType::kQueen:
      GenerateValidSlides(positions, tile, start_pos, 1);
      break;

    case BugType::kAnt:
      GenerateValidSlides(positions, tile, start_pos, -1);
      break;

    case BugType::kGrasshopper:
      GenerateValidGrasshopperPositions(positions, tile, start_pos);
      break;

    case BugType::kSpider:
      GenerateValidSlides(positions, tile, start_pos, 3);
      break;

    case BugType::kBeetle:
      GenerateValidClimbs(positions, tile, start_pos);
      if (start_pos.H() == 0) {
        GenerateValidSlides(positions, tile, start_pos, 1);
      }
      break;

    case BugType::kMosquito:
      GenerateValidMosquitoPositions(out, tile, start_pos, to_move);
      break;

    case BugType::kLadybug:
      GenerateValidLadybugPositions(positions, tile, start_pos);
      break;

    case BugType::kPillbug:
      GenerateValidSlides(positions, tile, start_pos, 1);

      // pillbug special constructs its own moves
      GenerateValidPillbugSpecials(out, tile, start_pos);
      break;
  }


  // turn each position into moves by finding neighbouring tiles as reference
  for (auto to_pos : positions) {
    //std::cout << to_pos << ", ";
    if (to_pos.H() > 0) {
      // only generate kAbove moves when on top the hive
      out.emplace_back(Move{tile,
                            GetTopTileAt(to_pos),
                            Direction::kAbove});
    } else {
      // for (auto& tile_ref : NeighbourReferenceTiles(to_pos)) {
      //   if (tile != tile_ref.tile) {
      //     out.emplace_back(Move{tile,
      //                           tile_ref.tile,
      //                           tile_ref.dir});
      //   }
      // }

      // check for a valid reference tile in each cardinal direction
      for (uint8_t dir = 0; dir < Direction::kNumCardinalDirections; ++dir) {
        NewHiveTile neighbour = GetTopTileAt(to_pos + kNeighbourOffsets[dir]);
        if (neighbour.HasValue()) {
          if (start_pos.H() > 0 && neighbour == tile) {
            out.emplace_back(Move{tile,
                                  GetTileBelow(start_pos),
                                  OppositeDirection(dir)});
          } else if (neighbour != tile) {
            out.emplace_back(Move{tile,
                                  neighbour,
                                  OppositeDirection(dir)});
          }
        }
      }
    }
  }
  //std::cout << std::endl;
}


// In order for a tile to slide in direction D, the following must hold true:
// 1) The tile must not be "pinned" (i.e. at an articulation point)
// 2) The tile must not be covered by another tile
// 3) The tile must be able to physically slide into the position without
//    hitting other tiles. That is, when sliding in direction D, exactly one
//    of the two adjacent positions (D-1) (D+1) must be empty to physically
//    move in, and the other position must be occupied in order to remain
//    attached to the hive at all times (One-Hive rule)
void HexBoard::GenerateValidSlides(absl::flat_hash_set<HivePosition>& out, NewHiveTile tile, HivePosition start_pos, int distance) const {
  if (IsPinned(tile) || IsCovered(tile)) {
    return;
  }
  
  const bool unlimited_distance = distance < 0;
  absl::flat_hash_set<HivePosition> visited;
  //std::vector<std::pair<HivePosition,int>> node_stack;

  //node_stack.push_back({start_pos, 0});
  //visited.insert(start_pos);

  auto dfs = [&](auto& dfs, HivePosition pos, Direction from, int depth) -> void {
    if (visited.contains(pos) || (!unlimited_distance && depth > distance))  {
      return;
    }
    
    // validate positions breadth-first
    for (uint8_t dir = 0; dir < Direction::kNumCardinalDirections; ++dir) {
      HivePosition to_test = pos + kNeighbourOffsets[dir];
      NewHiveTile test_tile = GetTopTileAt(to_test);

      if (dir == from) {
        continue;
      }

      if (visited.contains(to_test)) {
        continue;
      }

      // all must be false to be a valid slide direction
      if (test_tile.HasValue() || IsGated(pos, static_cast<Direction>(dir), start_pos) || !IsConnected(to_test, start_pos)) {
        continue;
      }
      
      if (depth == distance || unlimited_distance) {
        out.insert(to_test);
      }
    }

    if (depth == distance) {
      return;
    }
    
    // traverse depth-first
    visited.insert(pos);
    for (uint8_t dir = 0; dir < Direction::kNumCardinalDirections; ++dir) {
      HivePosition to_test = pos + kNeighbourOffsets[dir];
      NewHiveTile test_tile = GetTopTileAt(to_test);

      if (dir == from) {
        continue;
      }

      if (visited.contains(to_test)) {
        continue;
      }

      // all must be false to be a valid slide direction
      if (test_tile.HasValue() || IsGated(pos, static_cast<Direction>(dir), start_pos) || !IsConnected(to_test, start_pos)) {
        continue;
      }
      
      if (depth == distance || unlimited_distance) {
        out.insert(to_test);
      }

      dfs(dfs, to_test, OppositeDirection(dir), depth + 1);

      if (!unlimited_distance) {
        visited.erase(to_test);
      }
    }
  };


  dfs(dfs, start_pos, Direction::kNumAllDirections, 1);
}


// A climb consists of a slide on top the hive laterally, with an optional
// vertical movement, in any non-gated direction. This slide is less
// restrictive than a ground-level slide as you do not require neighbours
// to remain connected to the hive
void HexBoard::GenerateValidClimbs(absl::flat_hash_set<HivePosition>& out, NewHiveTile tile, HivePosition start_pos) const {
  if (IsPinned(tile) || IsCovered(tile)) {
    return;
  }
  
  HivePosition ground_pos = start_pos.Grounded();

  // find the top tile, or an empty position, in each adjacent position
  for (uint8_t d = 0; d < Direction::kNumCardinalDirections; ++d) {
    NewHiveTile neighbour = GetTopTileAt(ground_pos + kNeighbourOffsets[d]);
    if (neighbour.HasValue()) {
      HivePosition to_pos = tile_positions_[neighbour].NeighbourAt(Direction::kAbove);

      // climbing up: check for a gate at the *target*'s height
      if (to_pos.H() > start_pos.H() && !IsGated({start_pos.Q(), start_pos.R(), to_pos.H()}, static_cast<Direction>(d))) {
        out.insert(to_pos);
      // climbing down or across: check for gate at *this* tile's height
      // TODO: VERIFY THESE CONDITIONS?
      } else if (to_pos.H() <= start_pos.H() && !IsGated(start_pos, static_cast<Direction>(d)) /*&& !position_cache_.contains(to_pos)*/) {
        out.insert(to_pos);
      }
    } else {
      HivePosition to_pos = ground_pos + kNeighbourOffsets[d];

      // climbing down to empty space: check for a gate at *this* tile's height
      // TODO: VERIFY THESE CONDITIONS?
      if (to_pos.H() < start_pos.H() && !IsGated(start_pos, static_cast<Direction>(d)) /*&& !position_cache_.contains(to_pos)*/) {
        out.insert(to_pos);
      }
    }
  }
}


void HexBoard::GenerateValidGrasshopperPositions(absl::flat_hash_set<HivePosition>& out, NewHiveTile tile, HivePosition start_pos) const {
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
      out.insert(to_test);
    }
  }
}


// A lady bug moves in *exactly* 3 distinct steps: a climb onto the hive, a
// slide or climb across the hive, and a climb down from the hive
void HexBoard::GenerateValidLadybugPositions(absl::flat_hash_set<HivePosition>& out, NewHiveTile tile, HivePosition start_pos) const {
  if (IsPinned(tile) || IsCovered(tile)) {
    return;
  }
  
  absl::flat_hash_set<HivePosition> intermediates1;
  absl::flat_hash_set<HivePosition> intermediates2;
  absl::flat_hash_set<HivePosition> intermediates3;

  // intermediates1.reserve(player_influence_[0].size());
  // intermediates2.reserve(player_influence_[0].size());
  // intermediates3.reserve(player_influence_[0].size());

  // step 1
  GenerateValidClimbs(intermediates1, tile, start_pos);

  // step 2
  for (auto pos : intermediates1) {
    GenerateValidClimbs(intermediates2, tile, pos);
  }

  // step 3
  for (auto pos : intermediates2) {
    // ensure on top of the hive and not on top of the original tile
    if (pos.H() == 0 || pos == start_pos + kNeighbourOffsets[Direction::kAbove]) {
      continue;
    }

    GenerateValidClimbs(intermediates3, tile, pos);
  }

  // dumb way to check for dupes?
  //TODO absl::flat_hash_set<HivePosition> end_positions(intermediates3.begin(), intermediates3.end());
  for (auto pos : absl::flat_hash_set<HivePosition>(intermediates3.begin(), intermediates3.end())) {
    if (pos.H() == 0) {
      out.insert(pos);
    }
  }
}


// Can copy the movement capabilities of any adjacent bug type
void HexBoard::GenerateValidMosquitoPositions(std::vector<Move>& out, NewHiveTile tile, HivePosition start_pos, Colour to_move) const {
  // not checking IsPinned() as the Mosquito could use Pillbug special
  if (IsCovered(tile)) {
    return;
  }

  // when on top of the hive, a Mosquito can only act as a Beetle
  if (start_pos.H() > 0) {
    GenerateMovesFor(out, tile, BugType::kBeetle, to_move);
    return;
  }

  // otherwise, copy the types of adjacent tiles
  std::array<bool, static_cast<size_t>(BugType::kNumBugTypes)> types_seen{};
  for (auto neighbour : NeighboursOf(start_pos)) {
    BugType type = neighbour.GetBugType();
    //SPIEL_DCHECK_TRUE(type != BugType::kNone);

    if (!types_seen[static_cast<size_t>(type)]) {
      types_seen[static_cast<size_t>(type)] = true;

      if (type == BugType::kMosquito) {
        continue;
      }

      // Queen and Spider moves are strict subsets of an Ant's moves
      if ((type == BugType::kQueen || type == BugType::kSpider) && types_seen[static_cast<size_t>(BugType::kAnt)]) {
        continue;
      }

      GenerateMovesFor(out, tile, type, to_move);
    }
  }
}


void HexBoard::GenerateValidPillbugSpecials(std::vector<Move>& out, NewHiveTile tile, HivePosition start_pos) const {
  // Pillbug can still perform its special when Pinned
  if (IsCovered(tile)) {
    return;
  }

  std::vector<NewHiveTile> valid_targets;
  std::vector<HivePosition> valid_positions;

  for (uint8_t dir = 0; dir < Direction::kNumCardinalDirections; ++dir) {
    // ensure there is no "gate" blocking above for this direction
    if (IsGated(start_pos + kNeighbourOffsets[Direction::kAbove], static_cast<Direction>(dir))) {
      continue;
    }

    HivePosition test_pos = start_pos + kNeighbourOffsets[dir];
    NewHiveTile test_tile = GetTopTileAt(start_pos + kNeighbourOffsets[dir]);
    if (test_tile.HasValue()) {
      // valid IFF the target tile is not: Pinned, Covered, the LastMovedTile, or above the hive
      if (!IsPinned(test_tile) && !IsCovered(test_tile) && test_tile != LastMovedTile() && GetPositionOf(test_tile).H() == 0) {
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
        NewHiveTile ref_tile = GetTopTileAt(target_pos + kNeighbourOffsets[dir]);
        if (ref_tile.HasValue() && ref_tile != target_tile) {
          out.emplace_back(Move{target_tile,
                                ref_tile,
                                OppositeDirection(static_cast<Direction>(dir))});
        }
      }
    }
  }
}


std::vector<NewHiveTile> HexBoard::NeighboursOf(HivePosition pos, HivePosition to_ignore) const {
  std::vector<NewHiveTile> neighbours;
  for (auto neighbour : pos.Neighbours()) {
    auto tile = GetTopTileAt(neighbour);
    if (tile.HasValue()) {
      neighbours.push_back(tile);
    }
  }

  return neighbours;
}




// void HexBoard::PlaceTile(NewHiveTile tile, HivePosition new_pos) {
//   SPIEL_CHECK_TRUE(tile != NewHiveTile::kNoneTile);

//   // IMPORTANT: if the reference tile was higher on the hive, the new_pos
//   // may need to "fall down" until it hits ground or another tile
//   int8_t new_height = new_pos.H();
//   while (new_height > 0) {
//     if (!position_cache_.contains({new_pos.Q(), new_pos.R(), new_height - 1})) {
//       new_height--;
//     } else {
//       break;
//     }
//   }

//   new_pos = {new_pos.Q(), new_pos.R(), new_height};

//   // add tile to relevant containers
//   played_tiles_.push_back(tile);
//   position_cache_.insert({new_pos, tile});
//   tile->SetPosition(new_pos);
//   UpdateInfluence(tile->GetPlayer());
//   last_moved_ = tile;
//   last_moved_from_ = kNullPosition;
//   cache_valid_ = false;
// }


// returns true if the move was successful, and false otherwise
bool HexBoard::MoveTile(Move move) {
  SPIEL_CHECK_TRUE(move.from.HasValue());

  // auto handle = position_cache_.extract(old_pos);
  // if (handle.empty()) {
  //   SpielFatalError("HexBoard::MoveTile() - Tile not found in position cache");
  // }

  // IMPORTANT: if the reference tile was higher on the hive, the new_pos
  // may need to "fall down" until it hits ground or another tile
  // int8_t new_height = new_pos.H();
  // while (new_height > 0) {
  //   if (!position_cache_.contains({new_pos.Q(), new_pos.R(), new_height - 1})) {
  //     new_height--;
  //   } else {
  //     break;
  //   }
  // }


  // compute the final position from the reference tile + direction
  HivePosition new_pos;
  if (move.to.HasValue()) {
    new_pos = tile_positions_[move.to] + kNeighbourOffsets[move.direction];

    // if the reference tile was higher on the hive, the new_pos may need to
    // "fall down" until it hits either the ground or another tile
    if (new_pos.H() > 0) {
      NewHiveTile top_tile = GetTopTileAt(new_pos);
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


  // TODO:
  // Here is where you put code to check in new_pos is out of bounds. If so,
  // "the board" must be re-positioned if possible. If not possible, TERMINATE
  // TODO:  
  int dist = new_pos.DistanceTo(kOriginPosition);
  largest_radius = std::max(largest_radius, dist);
  if (dist > hex_radius_) {
    
    return false;
  }

  HivePosition old_pos = tile_positions_[move.from];
  if (old_pos == kNullPosition) {
    played_tiles_.push_back(move.from);
  }

  // TODO REMOVE THIS SHIT ???
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
  NewHiveTile old_tile = GetTopTileAt(old_pos);
  if (old_pos.H() > 0) {
    // reverse iterating guarantees the first tile found is the next highest H()
    for (int i = covered_tiles_.size() - 1; i >= 0; --i) {
      if (covered_tiles_[i] == NewHiveTile::kNoneTile) {
        continue;
      }

      if (old_pos.Grounded() == GetPositionOf(covered_tiles_[i]).Grounded()) {
        tile_grid_[old_idx] = covered_tiles_[i];
        covered_tiles_[i] = NewHiveTile::kNoneTile;

        // left-rotate the kNoneTile to the end of the covered_tiles_ array
        // to maintain height order
        std::rotate(covered_tiles_.begin() + i, covered_tiles_.begin() + i + 1, covered_tiles_.end());
        break;
      }
    }
  } else if (old_pos != kNullPosition) {
    tile_grid_[old_idx] = NewHiveTile::kNoneTile;
  }

  // update influence of the moved tile's colour. Potentially have to update
  // both influences if the moved tile was part of a stack
  UpdateInfluence(move.from.GetColour());
  if (old_pos.H() > 0 || new_pos.H() > 0) {
    UpdateInfluence(OtherColour(move.from.GetColour()));
  }
  UpdateArticulationPoints();

  return true;
}


// reset any turn-dependent variables
void HexBoard::Pass() {
  last_moved_ = NewHiveTile::kNoneTile;
  last_moved_from_ = kNullPosition;
}


NewHiveTile HexBoard::LastMovedTile() const {
  return last_moved_;
}


HivePosition HexBoard::LastMovedFrom() const {
  return last_moved_from_;
}


// bool HexBoard::IsInPlay(Colour c, BugType type, int ordinal /*= 1*/) const {
// }

// bool HexBoard::IsInPlay(TileIdx idx) const {
// }


bool HexBoard::IsQueenSurrounded(Colour c) const {
  NewHiveTile queen = c == Colour::kWhite ? NewHiveTile::wQ : NewHiveTile::bQ;
  if (!IsInPlay(queen)) {
    return false;
  }

  for (auto neighbour_pos : tile_positions_[queen].Neighbours()) {
    if (GetTopTileAt(neighbour_pos) == NewHiveTile::kNoneTile) {
      return false;
    }
  }

  return true;
}


// tile accessor with bounds checking
NewHiveTile HexBoard::GetTopTileAt(HivePosition pos) const {
  if (pos.DistanceTo(kOriginPosition) > Radius()) {
    return NewHiveTile::kNoneTile;
  }

  SPIEL_DCHECK_GE(AxialToIndex(pos), 0);
  SPIEL_DCHECK_LT(AxialToIndex(pos), tile_grid_.size());
  return tile_grid_[AxialToIndex(pos)];
}


// tile accessor with bounds checking
// NewHiveTile HexBoard::GetTopTileAt(TileIdx idx) const {
//   if (!idx.HasValue()) {
//     return NewHiveTile::kNoneTile;
//   }

//   SPIEL_DCHECK_GE(idx, 0);
//   SPIEL_DCHECK_LT(idx, tile_grid_.size());
//   return tile_grid_[idx];
// }


NewHiveTile HexBoard::GetTileAbove(HivePosition pos) const {
  // HivePosition to_test = {pos.Q(), pos.R(), pos.H() + 1};
  // if (position_cache_.contains(to_test)) {
  //   return position_cache_.at(to_test);
  // }
  // return absl::nullopt;
}


NewHiveTile HexBoard::GetTileBelow(HivePosition pos) const {
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

  return NewHiveTile::kNoneTile;
}


// Expects a string of a specific player's tile in UHP form, with no extra chars
// NewHiveTile HexBoard::GetTileFromUHP(const std::string& uhp) const {
//   // kinda cheeky but idk
//   const static std::unordered_map<std::string,NewHiveTile> index_mapping = {
//     {"wQ",   NewHiveTile::wQ},
//     {"wA1",  NewHiveTile::wA1},
//     {"wA2",  NewHiveTile::wA2},
//     {"wA3",  NewHiveTile::wA3},
//     {"wG1",  NewHiveTile::wG1},
//     {"wG2",  NewHiveTile::wG2},
//     {"wG3",  NewHiveTile::wG3},
//     {"wS1",  NewHiveTile::wS1},
//     {"wS2",  NewHiveTile::wS2},
//     {"wB1",  NewHiveTile::wB1},
//     {"wB2",  NewHiveTile::wB2},
//     {"wM",   NewHiveTile::wM},
//     {"wL",   NewHiveTile::wL},
//     {"wP",   NewHiveTile::wP},
//     {"bQ",   NewHiveTile::bQ},
//     {"bA1",  NewHiveTile::bA1},
//     {"bA2",  NewHiveTile::bA2},
//     {"bA3",  NewHiveTile::bA3},
//     {"bG1",  NewHiveTile::bG1},
//     {"bG2",  NewHiveTile::bG2},
//     {"bG3",  NewHiveTile::bG3},
//     {"bS1",  NewHiveTile::bS1},
//     {"bS2",  NewHiveTile::bS2},
//     {"bB1",  NewHiveTile::bB1},
//     {"bB2",  NewHiveTile::bB2},
//     {"bM",   NewHiveTile::bM},
//     {"bL",   NewHiveTile::bL},
//     {"bP",   NewHiveTile::bP}
//   };

//   auto it = index_mapping.find(uhp);
//   SPIEL_CHECK_TRUE(it != index_mapping.end());
//   return it->second;
// }


// int HexBoard::GetStackSizeAt(HivePosition pos) const {
//   NewHiveTile tile = at(pos);
//   if (tile.HasValue()) {
//     int stack_size = 1;
//     for (auto covered_tile : covered_tiles_) {
//       if (tile_positions_[covered_tile].Lateral() == pos.Lateral());
//     }
//   } else {
//     return 0;
//   }
// }


// IsGated verifies requirement (3) in GenerateValidSlides()
bool HexBoard::IsGated(HivePosition pos, Direction d, HivePosition to_ignore) const {
  HivePosition cw = pos + kNeighbourOffsets[ClockwiseDirection(d)];
  HivePosition ccw = pos + kNeighbourOffsets[CounterClockwiseDirection(d)];

  bool cw_exists = cw != to_ignore && GetPositionOf(GetTopTileAt(cw)).H() >= pos.H();
  bool ccw_exists = ccw != to_ignore && GetPositionOf(GetTopTileAt(ccw)).H() >= pos.H();
  return pos.H() == 0 ? cw_exists == ccw_exists : cw_exists && ccw_exists;
}

  
bool HexBoard::IsConnected(HivePosition pos, HivePosition to_ignore) const {
  // if (pos.H() > 0) {
  //   return position_cache_.contains(pos - kNeighbourOffsets[Direction::kAbove]);
  // }

  // for (uint8_t dir = 0; dir < Direction::kNumCardinalDirections; ++dir) {
  //   if (position_cache_.contains(pos + kNeighbourOffsets[dir]) && pos + kNeighbourOffsets[dir] != to_ignore) {
  //     return true;
  //   }
  // }

  // return false;
  return NeighboursOf(pos, to_ignore).size() > 0;
}


bool HexBoard::IsCovered(HivePosition pos) const {
  return std::find_if(covered_tiles_.begin(), covered_tiles_.end(), [this, pos](const NewHiveTile tile) {
    return GetPositionOf(tile) == pos;
  });
}


bool HexBoard::IsCovered(NewHiveTile tile) const {
  return tile.HasValue() && std::find(covered_tiles_.begin(), covered_tiles_.end(), tile) != covered_tiles_.end();
}


bool HexBoard::IsPinned(HivePosition pos) const {
  return articulation_points_.contains(pos);
}


bool HexBoard::IsPinned(NewHiveTile tile) const {
  return tile.HasValue() && IsPinned(tile_positions_[tile]);
}


// clear and recalculate this tile's player's influence range
void HexBoard::UpdateInfluence(Colour c) {
  colour_influence_[static_cast<int>(c)].clear();
  for (auto tile : played_tiles_) {
    if (tile.GetColour() != c) {
      continue;
    }

    // if a tile is covered, it has no influence
    if (IsCovered(tile)) {
      continue;
    }

    // exert influence on all neighbouring positions
    for (auto pos : tile_positions_[tile].Neighbours()) {
      // 0 out the height, so that stacked tiles influence the ground tiles
      // around them, not tiles floating in air
      colour_influence_[static_cast<int>(c)].insert(pos.Grounded());
    }
  }
}




// void HexBoard::MaybeUpdateSlideCache() {
//   // We need to calculate the valid slidable directions for every HivePosition
//   // that is currently reachable. This accounts for every tile in play, as
//   // well as all positions surrounding those tiles in play

//   if (cache_valid_) {
//     return;
//   }

//   // TODO: make sure cache is calculated properly for heights higher than 0??
//   // clear old cached slides
//   for (auto& slides : slidability_cache_) {
//     slides.second.clear();
//   }

//   absl::flat_hash_set<HivePosition> visited;
//   visited.reserve(player_influence_[kPlayerWhite].size() + player_influence_[kPlayerBlack].size() + played_tiles_.size());
//   auto dfs = [&](auto& dfs, HivePosition pos, Direction from) -> void {
//     if (visited.contains(pos) || IsCovered(pos)) {
//       return;
//     }
//     visited.insert(pos);

//     // make sure we aren't calculating a position that is currently unreachable
//     HiveTileList my_neighbours = NeighboursOf(pos);
//     if ((pos.H() == 0 && my_neighbours.size() == 0) ||
//         (pos.H() > 0 && !position_cache_.contains(pos - kNeighbourOffsets[Direction::kAbove]))) {
//       return;
//     }

//     // check each direction for slidability. Can never slide above/upwards
//     for (uint8_t i = 0; i < Direction::kNumCardinalDirections; ++i) {
//       // can't slide into another tile
//       HivePosition to_test = pos + kNeighbourOffsets[i];
//       if (position_cache_.contains(to_test)) {
//         continue;
//       }

//       // IsGated verifies requirement (3) in GenerateValidSlides()
//       if (IsGated(pos, static_cast<Direction>(i))) {
//         continue;
//       }

//       // if on top of the hive, there must also be a tile directly underneath
//       //TODO: this is redundant? already covered in ValidClimbs maybe?
//       if (to_test.H() > 0) {
//         auto it = position_cache_.find({to_test.Q(), to_test.R(), to_test.H() - 1});
//         if (it != position_cache_.end()) {
//           slidability_cache_[pos].push_back({it->second, static_cast<Direction>(i)});
//         }

//         // regardless of outcome, we are done
//         continue;
//       }

//       // must have at least one neighbour
//       HiveTileList to_neighbours = NeighboursOf(to_test);
//       if (to_test.H() > 0 && to_neighbours.size() == 0) {
//         continue;
//       }

//       // the reference tile for this slide is the neighbour of both positions
//       bool found = false;
//       for (auto& tile : my_neighbours) {
//         auto it = std::find(to_neighbours.begin(), to_neighbours.end(), tile);

//         if (it != to_neighbours.end() && (*it)->GetPosition() != pos) {
//           // if here, sliding around "tile" from "pos" in direction "i" is valid
//           slidability_cache_[pos].push_back({tile, static_cast<Direction>(i)});
//           found = true;
//         }
//       }

//       if (!found) {
//         SpielFatalError(absl::StrCat("HexBoard::MaybeUpdateSlideCache() - "
//                         "Can't find reference tile for a slide at position ",
//                         pos.ToString(),
//                         " and direction ",
//                         std::to_string(i)));
//       }
        
//     }

//     // perform search for all neighbours
//     // TODO: fix for kAbove
//     for (uint8_t i = 0; i < Direction::kNumCardinalDirections; ++i) {
//       if (i == from) {
//         continue;
//       }

//       dfs(dfs, pos + kNeighbourOffsets[i], OppositeDirection(i));
//     }
//   };

//   // traverse across the hive starting at an arbitrary tile
//   for (auto& tile : played_tiles_) {
//     dfs(dfs, tile->GetPosition(), Direction::kNumAllDirections);
//   }

//   // std::cout << "\nSlide cache:" << std::endl;
//   // std::cout << "  size: " << slidability_cache_.size() << std::endl;
//   // for (auto& item : slidability_cache_) {
//   //   if (item.second.size() == 0) {
//   //     continue;
//   //   }
//   //   std::cout << item.first << ": [" << std::flush;
//   //   for (auto& slide : item.second) {
//   //     std::cout << std::to_string(slide.dir) << "->" << slide.ref_tile->ToUHP() << ", " << std::flush;
//   //   }
//   //   std::cout << "]\n" << std::endl;
//   // }
// }




// Articulation points in a connected graph are vertices where, when removed,
// separate the graph into multiple components that are no longer connected.
// Tiles at an articulation point are considered "pinned" (and thus, can't be
// moved) as it would separate the hive and invalidate the "One-Hive" rule
// https://en.wikipedia.org/wiki/Biconnected_component
// https://cp-algorithms.com/graph/cutpoints.html
void HexBoard::UpdateArticulationPoints() {
  articulation_points_.clear();

  int visit_order = 0;
  absl::flat_hash_set<HivePosition> visited;
  absl::flat_hash_map<HivePosition,int> entry_point;
  absl::flat_hash_map<HivePosition,int> low_point;

  auto dfs = [&](auto& dfs, HivePosition vertex, HivePosition parent, bool is_root) -> void {
    visited.insert(vertex);
    entry_point[vertex] = low_point[vertex] = visit_order;
    ++visit_order;

    int children = 0;
    for (uint8_t dir = 0; dir < Direction::kNumCardinalDirections; ++dir) {
      HivePosition to_vertex = vertex + kNeighbourOffsets[dir];
      if (!GetTopTileAt(to_vertex).HasValue()) {
        continue;
      }

      if (to_vertex == parent) {
        continue;
      }

      if (visited.contains(to_vertex)) {
        low_point[vertex] = std::min(low_point[vertex], entry_point[to_vertex]);
      } else {
        dfs(dfs, to_vertex, vertex, false);
        ++children;
        low_point[vertex] = std::min(low_point[vertex], low_point[to_vertex]);
        if (low_point[to_vertex] >= entry_point[vertex] && !is_root) {
          articulation_points_.insert(vertex);
        }
      }
    }

    if (is_root && children > 1) {
      articulation_points_.insert(vertex);
    }
  };

  // any arbitrary starting point would do, but the Queen is guaranteed to be
  // in play when generating moves
  dfs(dfs, tile_positions_[NewHiveTile::wQ], kNullPosition, true);
}


// the tile(s) used as reference for a Move, from a top-down view.
// tiles on the hive will also reference the tile underneath
// std::vector<HexBoard::TileReference> HexBoard::NeighbourReferenceTiles(HivePosition& pos) {
//   std::vector<HexBoard::TileReference> refs;
//   for (uint8_t d = 0; d < Direction::kNumCardinalDirections; ++d) {
//     auto tile = GetTopTileAt(pos + kNeighbourOffsets[d]);
//     if (tile.HasValue()) {
//       refs.push_back({tile, OppositeDirection(d)});
//     }
//   }

//   return refs;
// }


std::string NewHiveTile::ToUHP(bool use_emojis /*= false*/) const {
  SPIEL_CHECK_TRUE(HasValue());
  std::string uhp = "";

  // colour
  GetColour() == Colour::kWhite ? absl::StrAppend(&uhp, "w") : absl::StrAppend(&uhp, "b");

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
  }

  // bug type ordinal (for bugs where there can be more than 1)
  if (type == BugType::kAnt || type == BugType::kGrasshopper ||
      type == BugType::kSpider || type == BugType::kBeetle) {
    absl::StrAppend(&uhp, GetOrdinal());
  }
  
  return uhp;
}

// std::string NewHiveTile::ToUnicode() const {
//   SPIEL_CHECK_TRUE(player_ != kInvalidPlayer && type_ != BugType::kNone);
//   std::string unicode = "";

//   // colour
//   player_ == kPlayerWhite ? absl::StrAppend(&unicode, "w") : absl::StrAppend(&unicode, "b");

//   // bug type
//   switch (type_) {
//     case BugType::kQueen:
//         absl::StrAppend(&unicode, "🐝");
//         break;
//     case BugType::kAnt:
//         absl::StrAppend(&unicode, "🐜");
//         break;
//     case BugType::kGrasshopper:
//         absl::StrAppend(&unicode, "🦗");
//         break;
//     case BugType::kSpider:
//         absl::StrAppend(&unicode, "🕷️");
//         break;
//     case BugType::kBeetle:
//         absl::StrAppend(&unicode, "🪲");
//         break;
//     case BugType::kLadybug:
//         absl::StrAppend(&unicode, "🐞");
//         break;
//     case BugType::kMosquito:
//         absl::StrAppend(&unicode, "🦟");
//         break;
//     case BugType::kPillbug:
//         absl::StrAppend(&unicode, "🐛");
//         break;
//   }

//   // bug type ordinal (for bugs where there can be more than 1)
//   if (type_ == BugType::kAnt || type_ == BugType::kGrasshopper ||
//       type_ == BugType::kSpider || type_ == BugType::kBeetle) {
//     absl::StrAppend(&unicode, type_ordinal_);
//   }

//   return unicode;
// }


// UHP string representation of a move (there is exactly 1 move per player turn)
std::string Move::ToUHP() {
  // special case: pass for when a player has no possible legal moves 
  if (IsPass()) {
    return "pass";
  }

  // special case: for the first turn, there is no reference tile
  if (!to.HasValue()) {
    return from.ToUHP();
  }

  std::string reference_tile_uhp = to.ToUHP();
  std::string offset_formatted = "";

  // add a prefix or suffix depending on the relative position
  switch(direction) {
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



} // namespace hive
} // namespace open_spiel
