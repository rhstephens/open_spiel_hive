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
#include <string>
#include <vector>

#include "open_spiel/abseil-cpp/absl/container/flat_hash_map.h"
#include "open_spiel/abseil-cpp/absl/container/flat_hash_set.h"
#include "open_spiel/abseil-cpp/absl/strings/str_cat.h"
#include "open_spiel/abseil-cpp/absl/strings/str_join.h"
#include "open_spiel/games/hive/hive_parallel_bitboard.h"
#include "open_spiel/spiel_utils.h"

namespace open_spiel {
namespace hive {

// init static bitboards and vars
const std::array<HexBitboard32x32, HiveBoard::kNumCells> HiveBoard::kNeighbourMasks = [](){
  std::array<HexBitboard32x32, HiveBoard::kNumCells> result{};

  for (size_t pos = 0; pos < HiveBoard::kNumCells; ++pos) {
    result[pos] = HexBitboard32x32(pos).hex_adjacent_bits();
  }

  return result;
}();

const std::array<std::array<size_t, Direction::kNumCardinalDirections>, HiveBoard::kNumCells>
  HiveBoard::kNeighbourIterators = [](){
    std::array<std::array<size_t, Direction::kNumCardinalDirections>, HiveBoard::kNumCells> result{};

    for (size_t pos = 0; pos < HiveBoard::kNumCells; ++pos) {
      result[pos] = {pos + kNeighbourOffsets[kNE] < kNumCells ? pos + kNeighbourOffsets[kNE] : size_t(-1),
                     pos + kNeighbourOffsets[kE] < kNumCells ? pos + kNeighbourOffsets[kE] : size_t(-1),
                     pos + kNeighbourOffsets[kSE] < kNumCells ? pos + kNeighbourOffsets[kSE] : size_t(-1),
                     pos + kNeighbourOffsets[kSW] < kNumCells ? pos + kNeighbourOffsets[kSW] : size_t(-1),
                     pos + kNeighbourOffsets[kW] < kNumCells ? pos + kNeighbourOffsets[kW] : size_t(-1),
                     pos + kNeighbourOffsets[kNW] < kNumCells ? pos + kNeighbourOffsets[kNW] : size_t(-1)};
    }

    return result;
  }();

const HiveBoard::GateMask HiveBoard::kGateMasks = [](){
  GateMask result{};

  for (size_t pos = 0; pos < HiveBoard::kNumCells; ++pos) {
    for (uint8_t dir = 0; dir < Direction::kNumCardinalDirections; ++dir) {
      uint8_t cw_dir = ClockwiseDirection(dir);
      uint8_t ccw_dir = CounterClockwiseDirection(dir);
      
      size_t next_pos = pos + kNeighbourOffsets[dir];
      size_t cw_pos = pos + kNeighbourOffsets[cw_dir];
      size_t ccw_pos = pos + kNeighbourOffsets[ccw_dir];
      
      // Create a mask that indicates valid gate configurations
      HexBitboard32x32 mask;
      if (HexBitboard32x32::valid_index(next_pos) && 
          HexBitboard32x32::valid_index(cw_pos) && 
          HexBitboard32x32::valid_index(ccw_pos)) {
        mask.set(cw_pos);  // Set bit for clockwise position
        mask.set(ccw_pos); // Set bit for counterclockwise position
      }
      
      result[dir][pos] = mask;
    }
  }

  return result;
}();




////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

// ctor
HiveBoard::HiveBoard(ExpansionInfo expansions, bool fixed_orientation)
  : expansions_(expansions), fixed_orientation_(fixed_orientation) {

  tile_positions_.fill(-1);
  played_tiles_.reserve(HiveTile::kNumTiles);
}

bool HiveBoard::ApplyMove(Move move) {
  SPIEL_DCHECK_TRUE(move.from.HasValue());

  // special case: "move.to == move.from" -> this is the first move of the game,
  // so should be moving "from nowhere, to the start pos".
  size_t from_pos = move.to == move.from ?
    kNullPos : tile_positions_[move.from];
  size_t to_pos = move.to == move.from ?
    kStartPos : tile_positions_[move.to] + (move.direction == kAbove ? 0 : kNeighbourOffsets[move.direction]);
  
  if (!HexBitboard32x32::valid_index(to_pos)) {
    // attempt to recenter if we go off grid
    if (!RecenterBoard()) {
      // something has gone horribly wrong - end early
      return false;
    }
  }

  // save which tile is underneath me now
  HiveTile underneath = GetTileUnderneath(move.from);

  // check if tile already exists at target loc
  if (tile_grid_[to_pos].HasValue()) {
    // set new height of stack
    tile_stack_heights_[move.from] = tile_stack_heights_[tile_grid_[to_pos]] + 1;

    // => previous tile will be covered and placed into first non-empty slot
    for (int idx = 0; idx < covered_tiles_.size(); ++idx) {
      if (!covered_tiles_[idx].first.HasValue()) {
        covered_tiles_[idx] = {tile_grid_[to_pos], move.from};
        break;
      }
    }
  } else {
    // tile must be on ground level
    tile_stack_heights_[move.from] = 1;
  }

  //////////////////////////////////////////////////////////////////////////////
  // PERFORM MOVE
  //////////////////////////////////////////////////////////////////////////////

  // set new position
  occupied_.set(to_pos);
  tile_grid_[to_pos] = move.from;
  tile_positions_[move.from] = to_pos;
  last_moved_ = move.from;
  last_moved_from_ = from_pos;
  Player player = static_cast<int>(move.from.GetColour());
  player_positions_[player].set(to_pos);
  player_positions_[OtherPlayer(player)].clear(to_pos);

  // clear old position if this is NOT a placement move
  if (from_pos != kNullPos) {
    // tile underneath could be a valid bug or null
    tile_grid_[from_pos] = underneath;

    if (underneath.HasValue()) {
      OnTileUncovered(underneath);

      // tile found underneath; update player positions'
      Player player_under = static_cast<int>(underneath.GetColour());
      player_positions_[player_under].set(from_pos);
      player_positions_[OtherPlayer(player_under)].clear(from_pos);
    } else {
      // no tile underneath; fully clear position
      player_positions_[0].clear(from_pos);
      player_positions_[1].clear(from_pos);
      occupied_.clear(from_pos);
    }
  } else {
    // this is a placement move
    played_tiles_.push_back(move.from);
  }

  //////////////////////////////////////////////////////////////////////////////
  // END MOVE
  //////////////////////////////////////////////////////////////////////////////

  // always call immediately after move, as other operations may rely on it
  UpdateArticulationPoints();
  
  // success
  return true;
}

void HiveBoard::GenerateAllMoves(std::bitset<kNumDistinctActions>& out,
                                 Colour to_play, int move_num) const {
  // Placement positions
  HexBitboard32x32 valid_placements = PlaceablePositions(static_cast<Player>(to_play));
  const bool queen_placed = move_num >= 8 ||
        IsInPlay(to_play == Colour::kWhite ? HiveTile::wQ : HiveTile::bQ);

  // filter out which tiles are *actually* placeable right now
  std::vector<HiveTile> tiles_to_place{};
  for (HiveTile tile : HiveTile::GetTilesForColour(to_play)) {
    if (move_num == 0) {
      if (tile.GetBugType() == BugType::kQueen ||
          tile.GetOrdinal() > 1 ||
          !expansions_.IsBugTypeEnabled(tile.GetBugType())) {
        continue;
      }
    } else if (move_num == 1) {
      if (tile.GetBugType() == BugType::kQueen ||
          tile.GetOrdinal() > 1 ||
          !expansions_.IsBugTypeEnabled(tile.GetBugType())) {
        continue;
      }
    } else {
      if (!expansions_.IsBugTypeEnabled(tile.GetBugType()) ||
          IsInPlay(tile)) {
        continue;
      }

      // Queen *must* be played by each player's 4th turn (8 total moves).
      if ((move_num == 6 || move_num == 7) && !queen_placed &&
          tile.GetBugType() != BugType::kQueen) {
        continue;
      }

      // check if previous tile ordinal has been played
      HiveTile prev_ordinal_tile = 
        HiveTile::GetTileFrom(to_play, tile.GetBugType(), tile.GetOrdinal() - 1);
      if (tile.GetOrdinal() != 1 && !IsInPlay(prev_ordinal_tile)) {
          continue;
      }
    }

    tiles_to_place.push_back(tile);
  }

  // move 0: white must play a (non-queen) tile at the origin
  if (move_num == 0) { // TODO: cold path
    valid_placements.set(kStartPos);

    for (HiveTile tile : tiles_to_place) {
      // playing the first tile at the origin is encoded as a move where
      // a tile is placed "on top of itself"
      out.set(GenerateAction(tile, tile, Direction::kAbove));
    }
  } else if (move_num == 1) { // TODO: cold path
    // move 1: black must play a (non-queen) tile next to white's first tile.
    // this is the only time placing a tile next to an opponent's is allowed
    for (HiveTile tile : tiles_to_place) {
      for (int dir = 0; dir < Direction::kNumCardinalDirections; ++dir) {
        // a fixed starting orientation allows for the simplification of the
        // game's opening, reducing the branching factor five-fold
        if (fixed_orientation_ && static_cast<Direction>(dir) != Direction::kE) {
          continue;
        }

        out.set(
            GenerateAction(tile, played_tiles_[0], static_cast<Direction>(dir)));
      }
    }
  } else { // TODO: Hot path
    for (HiveTile ref_tile : played_tiles_) {
      if (ref_tile.GetColour() != to_play || IsCovered(ref_tile)) {
        continue;
      }

      // quick optimization check before running more nested loops
      HexBitboard32x32 adj_placements = kNeighbourMasks[GetPositionOf(ref_tile)] &
                                     valid_placements;
      if (adj_placements.none()) {
        continue;
      }

      for (uint8_t dir = 0; dir < Direction::kNumCardinalDirections; ++dir) {
        size_t to_test = GetPositionOf(ref_tile) + kNeighbourOffsets[dir];
        if (valid_placements.test(to_test)) {
          for (HiveTile tile : tiles_to_place) {
            out.set(GenerateAction(tile, ref_tile, static_cast<Direction>(dir)));
          }
        }
      }
    }
  }
  
  // Movements:
  if (!queen_placed) {
    return;
  }

  for (HiveTile moving_tile : played_tiles_) {
    // Can't use the last_moved_ tile (Pillbug special)
    if (moving_tile.GetColour() != to_play || moving_tile == last_moved_) {
      continue;
    }

    GenerateMovesFor(out, moving_tile, moving_tile.GetBugType(), to_play);
  }
}


void HiveBoard::GenerateMovesFor(std::bitset<kNumDistinctActions>& out, HiveTile to_move, BugType acting_type, Colour to_play) const {
  SPIEL_DCHECK_TRUE(expansions_.IsBugTypeEnabled(acting_type));
  HexBitboard32x32 valid_positions{};

  // covered tiles cannot perform any moves
  if (IsCovered(to_move)) {
    return;
  }

  // only Pillbugs/Mosquitos can perform moves while pinned
  if (IsPinned(to_move) &&
    !(acting_type == BugType::kPillbug || acting_type == BugType::kMosquito)) {
    return;
  }

  // using an explicitly provided acting BugType to account for the Mosquito
  switch (acting_type) {
    case BugType::kQueen:
      valid_positions |= ValidWalkOnePositions(to_move);
      break;
    case BugType::kAnt:
      valid_positions |= ValidAntPositions(to_move);
      break;
    case BugType::kGrasshopper:
      valid_positions |= ValidGrasshopperPositions(to_move);
      break;
    case BugType::kSpider:
      valid_positions |= ValidSpiderPositions(to_move);
      break;
    case BugType::kBeetle:
      valid_positions |= ValidWalkOnePositions(to_move);
      valid_positions |= ValidClimbPositions(to_move);
      break;
    case BugType::kMosquito:
      // acts only as a beetle when on top of the hive
      if (GetTileHeight(to_move) > 1) {
        valid_positions |= ValidClimbPositions(to_move);
      } else {
        // otherwise, generate moves for each unique neighbouring type
        std::array<bool, static_cast<size_t>(BugType::kNumBugTypes)> seen_types{};
        size_t pos = GetPositionOf(to_move);

        for (uint8_t dir = 0; dir < Direction::kNumCardinalDirections; ++dir) {
          HiveTile nb = GetTopTileAt(pos + kNeighbourOffsets[dir]);
          if (nb.HasValue() && nb.GetBugType() != BugType::kMosquito && 
             !seen_types[static_cast<size_t>(nb.GetBugType())]) {
            GenerateMovesFor(out, to_move, nb.GetBugType(), to_play);
            seen_types[static_cast<size_t>(nb.GetBugType())] = true;
          }
        }
      }
      break;
    case BugType::kLadybug:
      valid_positions |= ValidLadybugPositions(to_move);
      break;
    case BugType::kPillbug:
      if (!IsPinned(to_move)) {
        valid_positions |= ValidWalkOnePositions(to_move);
      }
      // always call this, regardless of pinned state
      GeneratePillbugSpecialMoves(out, GetPositionOf(to_move));
      break;
    default:
      SpielFatalError("Unrecognized BugType");
  }
  
  // actually generate a move for each valid destination pos
  for (size_t valid_pos : valid_positions.all_set_bits()) {
    GenerateMove(out, to_move, valid_pos);
  }
}


void HiveBoard::GenerateMove(std::bitset<kNumDistinctActions>& out, HiveTile from_tile, size_t to_pos) const {
  // find reference tiles to use in all 6 directions
  for (uint8_t dir = 0; dir < Direction::kNumCardinalDirections; ++dir) {
    // if this is a climb UP
    if (occupied_.test(to_pos)) {
      out.set(GenerateAction(from_tile, tile_grid_[to_pos], Direction::kAbove));
    } else {
      Direction opposite_dir = OppositeDirection(static_cast<Direction>(dir));
      size_t ref_pos = to_pos + kNeighbourOffsets[opposite_dir];
      HiveTile ref_tile = tile_grid_[ref_pos];

      // first check for potential beetle self-referential move
      // TODO: small improvement to just use tile underneath no matter what (as long as it exists)
      if (ref_tile == from_tile && ref_tile.GetBugType() == BugType::kBeetle &&
          GetTileHeight(from_tile) > 1 &&
          (occupied_ & kNeighbourMasks[ref_pos]).popcount() == 1) {
        // use the tile underneath as reference in this case
        ref_tile = GetTileUnderneath(ref_tile);    
      }

      if (!occupied_.test(ref_pos) || ref_tile == from_tile) {
        continue;
      }

      // Get the tile at the reference position
      out.set(GenerateAction(from_tile, ref_tile, static_cast<Direction>(dir)));
    }
  }
}


void HiveBoard::GeneratePillbugSpecialMoves(std::bitset<kNumDistinctActions>& out, size_t pillbug_pos) const {
  std::vector<size_t> valid_positions;
  valid_positions.reserve(Direction::kNumCardinalDirections);

  std::vector<HiveTile> valid_tiles;
  valid_tiles.reserve(Direction::kNumCardinalDirections);
  
  // find valid locations and tiles
  for (uint8_t dir = 0; dir < Direction::kNumCardinalDirections; ++dir) {
    size_t test_pos = pillbug_pos + kNeighbourOffsets[dir];
    HiveTile to_move = GetTopTileAt(test_pos);

    // find suitable landing locations that are empty and not beetle-gated
    if (!IsBeetleGated(pillbug_pos, static_cast<Direction>(dir))) {
      if (to_move.HasValue() && !IsPinned(to_move) && GetTileHeight(to_move) <= 1 && to_move != last_moved_) {
        valid_tiles.push_back(to_move);
      } else if (!to_move.HasValue()) {
        valid_positions.push_back(test_pos);
      }
    }
  }

  // generate the moves from these valid 
  for (HiveTile tile : valid_tiles) {
    for (size_t pos : valid_positions)
    GenerateMove(out, tile, pos);
  }
}


// to find valid placeable positions, we can AND these three masks together:
// 1) a mask of all positions adjacent to current player
// 2) a mask of all positions NOT-adjacent to the opponent
// 3) a mask of every unoccupied position
HexBitboard32x32 HiveBoard::PlaceablePositions(Player to_play) const {
  return (player_positions_[to_play].hex_adjacent_bits()) &
         (~player_positions_[OtherPlayer(to_play)].hex_adjacent_bits()) &
         (~occupied_);
}


// PRIVATE METHODS
//

void HiveBoard::UpdateArticulationPoints() {
  pinned_.reset();

  // the DFS graph algorithm is faster than any approach with bitboards
  std::bitset<HiveTile::kNumTiles> visited{};
  std::array<int, HiveTile::kNumTiles> entry_point{};
  std::array<int, HiveTile::kNumTiles> low_point{};

  // any arbitrary starting point would do, but the Queen is guaranteed to be
  // in play when generating moves
  // TODO: fix for queen?
  DFSArticulation(played_tiles_[0], kNullPos, true,
    visited, 0, entry_point, low_point);
}

void HiveBoard::DFSArticulation(HiveTile tile, size_t parent_pos, bool is_root,
    std::bitset<HiveTile::kNumTiles>& visited, int visit_order,
    std::array<int, HiveTile::kNumTiles>& entry_point,
    std::array<int, HiveTile::kNumTiles>& low_point) {

  size_t vertex = tile_positions_[tile];
  visited.set(tile);
  entry_point[tile] = low_point[tile] = visit_order;
  ++visit_order;

  int children = 0;
  for (int dir = 0; dir < Direction::kNumCardinalDirections; ++dir) {
    HiveTile neighbour = tile_grid_[vertex + kNeighbourOffsets[dir]];
    if (!neighbour.HasValue() || vertex == parent_pos) {
      continue;
    }

    if (visited.test(neighbour)) {
      low_point[tile] = std::min(low_point[tile], entry_point[neighbour]);
    } else {
      DFSArticulation(neighbour, vertex, false, visited,
        visit_order, entry_point, low_point);
      ++children;
      low_point[tile] = std::min(low_point[tile], low_point[neighbour]);
      if (low_point[neighbour] >= entry_point[tile] && !is_root) {
        pinned_.set(vertex);
      }
    }
  }

  if (is_root && children > 1) {
    if (tile.HasValue()) {
      pinned_.set(vertex);
    }
  }
}


void HiveBoard::OnTileUncovered(HiveTile tile) {
  for (int idx = covered_tiles_.size() - 1; idx >= 0; --idx) {
    if (covered_tiles_[idx].first == tile) {
      covered_tiles_[idx] = {HiveTile::kNoneTile, HiveTile::kNoneTile};

      // left-rotate the kNoneTile to the end of the covered_tiles_ array
      // to maintain height order
      std::rotate(covered_tiles_.begin() + idx, covered_tiles_.begin() + idx + 1,
                  covered_tiles_.end());
      break;
    }
  }
}

bool HiveBoard::RecenterBoard() {
  return true;
}

std::vector<size_t> HiveBoard::SlidingAdjacentNeighbours(size_t start_pos) const {
  std::vector<size_t> result{};
  result.reserve(kNumCardinalDirections);

  for (uint8_t dir = 0; dir < kNumCardinalDirections; ++dir) {
    size_t test_pos = kNeighbourIterators[start_pos][dir];

    if (HexBitboard32x32::valid_index(test_pos) && !IsGated(start_pos, (Direction)dir)) {
      result.push_back(test_pos);
    }
  }

  return result;
}

HexBitboard32x32 HiveBoard::ValidWalkOnePositions(HiveTile tile) const {
  size_t start_pos = GetPositionOf(tile);
  HexBitboard32x32 result{};

  // TODO: Maybe just use SlidingAdjacentNeighbours? perf test it
  for (uint8_t dir = 0; dir < Direction::kNumCardinalDirections; ++dir) {
    if (!IsGated(start_pos, (Direction)dir)) {
      result.set(start_pos + kNeighbourOffsets[dir]);
    }
  }

  return result;
}

HexBitboard32x32 HiveBoard::ValidSpiderPositions(HiveTile tile) const {
  size_t start_pos = GetPositionOf(tile);
  HexBitboard32x32 result{};
  HexBitboard32x32 visited{};

  // TODO: Maybe just use SlidingAdjacentNeighbours? perf test it
  // let compiler unroll this mess
  for (uint8_t dir1 = 0; dir1 < kNumCardinalDirections; ++dir1) {
    size_t pos1 = start_pos + kNeighbourOffsets[dir1];

    // check first slide
    if (!IsGated(start_pos, (Direction)dir1, start_pos)) {
      for (uint8_t dir2 = 0; dir2 < kNumCardinalDirections; ++dir2) {
        size_t pos2 = pos1 + kNeighbourOffsets[dir2];

        // check second slide
        if (pos2 != start_pos && !IsGated(pos1, (Direction)dir2, start_pos)) {
          for (uint8_t dir3 = 0; dir3 < kNumCardinalDirections; ++dir3) {
            size_t pos3 = pos2 + kNeighbourOffsets[dir3];

            // check third slide
            if (pos3 != start_pos && pos3 != pos1 && !visited.test(pos3) && !IsGated(pos2, (Direction)dir3, start_pos)) {
              result.set(pos3);
              visited.set(pos3);
            }
          }
        }
      }
    }
  }

  return result;
}

HexBitboard32x32 HiveBoard::ValidAntPositions(HiveTile tile) const {
  size_t start_pos = GetPositionOf(tile);
  HexBitboard32x32 result{};
  HexBitboard32x32 visited{};
  visited.set(start_pos);
  HexBitboard32x32 frontier{};
  frontier.set(start_pos);

  HexBitboard32x32 occ = occupied_;
  occ.clear(start_pos);

  // calculate gates once
  HexBitboard32x32 directional_gates[kNumCardinalDirections];
  directional_gates[0] = (occ.north_west() ^ occ.east()) & (~occ);
  directional_gates[1] = (occ.north_east() ^ occ.south_east()) & (~occ);
  directional_gates[2] = (occ.east() ^ occ.south_west()) & (~occ);

  // If I can slide from A to B by going North-East, I can also slide from
  // B to A going South-West. So we can save some redundant calls here
  directional_gates[3] = directional_gates[0].south_west() & (~occ);
  directional_gates[4] = directional_gates[1].west() & (~occ);
  directional_gates[5] = directional_gates[2].north_west() & (~occ);

  // Continue until no new positions are found
  while (frontier.any()) {
    HexBitboard32x32 new_positions{};
    
    // For each direction, compute all valid movements from all frontier positions at once
    new_positions |= frontier.north_east() & (~visited) & directional_gates[0];
    new_positions |= frontier.east() & (~visited) & directional_gates[1];
    new_positions |= frontier.south_east() & (~visited) & directional_gates[2];
    new_positions |= frontier.south_west() & (~visited) & directional_gates[3];
    new_positions |= frontier.west() & (~visited) & directional_gates[4];
    new_positions |= frontier.north_west() & (~visited) & directional_gates[5];

    result |= new_positions;
    visited |= new_positions;
    
    // Update frontier for next iteration
    frontier = new_positions;
  }

  return result;
}

HexBitboard32x32 HiveBoard::ValidGrasshopperPositions(HiveTile tile) const {
  size_t start_pos = GetPositionOf(tile);
  HexBitboard32x32 result{};
  HexBitboard32x32 not_occ = ~occupied_;

  // raycast every direction.
  // Anything that doesn't hit directly beside start_pos is valid
  for (uint8_t dir = 0; dir < kNumCardinalDirections; ++dir) {
    result |= not_occ.raycast(start_pos, (Direction)dir) &
              (~HexBitboard32x32{start_pos + kNeighbourOffsets[dir]});
  }

  return result;
}

HexBitboard32x32 HiveBoard::ValidClimbPositions(HiveTile tile) const {
  size_t start_pos = GetPositionOf(tile);
  HexBitboard32x32 result{};

  for (uint8_t dir = 0; dir < Direction::kNumCardinalDirections; ++dir) {
    int from_height = GetTileHeight(tile);
    int dest_height = 1 + GetTileHeight(GetTopTileAt(start_pos + kNeighbourOffsets[dir]));

    // only valid when we are climbing onto, or climbing off of the hive
    if ((from_height > 1 || dest_height > 1) && !IsBeetleGated(start_pos, (Direction)dir)) {
      result.set(start_pos + kNeighbourOffsets[dir]);
    }
  }

  return result;
}

HexBitboard32x32 HiveBoard::ValidLadybugPositions(HiveTile tile) const {
  size_t start_pos = GetPositionOf(tile);
  HexBitboard32x32 result{};
  HexBitboard32x32 visited{};

  // let compiler unroll this mess
  for (uint8_t dir1 = 0; dir1 < kNumCardinalDirections; ++dir1) {
    size_t pos1 = start_pos + kNeighbourOffsets[dir1];

    // check first climb up
    if (occupied_.test(pos1) && !IsBeetleGated(start_pos, (Direction)dir1)) {
      for (uint8_t dir2 = 0; dir2 < kNumCardinalDirections; ++dir2) {
        size_t pos2 = pos1 + kNeighbourOffsets[dir2];

        // check second climb across
        if (pos2 != start_pos && occupied_.test(pos2) && !IsBeetleGated(pos1, (Direction)dir2)) {
          for (uint8_t dir3 = 0; dir3 < kNumCardinalDirections; ++dir3) {
            size_t pos3 = pos2 + kNeighbourOffsets[dir3];

            // check third climb down
            if (pos3 != pos1 && !(occupied_.test(pos3)) && !visited.test(pos3) && !IsBeetleGated(pos2, (Direction)dir3)) {
              result.set(pos3);
              visited.set(pos3);
            }
          }
        }
      }
    }
  }

  return result;
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

std::string Move::ToUHP() {
  // special case: pass for when a player has no possible legal moves
  if (IsPass()) {
    return "pass";
  }

  // special case: for the first turn, to == from
  if (to == from) {
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
