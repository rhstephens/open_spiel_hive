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
#include <queue>
#include <string>
#include <vector>
#include <immintrin.h>

#include "open_spiel/abseil-cpp/absl/container/flat_hash_map.h"
#include "open_spiel/abseil-cpp/absl/container/flat_hash_set.h"
#include "open_spiel/abseil-cpp/absl/strings/str_cat.h"
#include "open_spiel/abseil-cpp/absl/strings/str_join.h"
#include "open_spiel/games/hive/hive_parallel_bitboard.h"
#include "open_spiel/spiel_utils.h"

namespace open_spiel {
namespace hive {

////////////////////////////////////
// init static bitboards and vars //
////////////////////////////////////

// lookup table that maps adjacency bits to valid movement directions
alignas(64) constexpr std::array<uint8_t, 64> kValidMovesLUT = []() {
    std::array<uint8_t, 64> lut{};

    for (int shape = 0; shape < 64; ++shape) {
        uint8_t valid_moves = 0;

        for (int dir = 0; dir < 6; ++dir) {
            // Check if this direction is empty
            if (shape & (1 << dir)) continue;

            // Check the two adjacent directions (one must be occupied, one empty)
            int prev_dir = (dir + 5) % 6;  // (dir - 1 + 6) % 6
            int next_dir = (dir + 1) % 6;

            bool prev_occupied = shape & (1 << prev_dir);
            bool next_occupied = shape & (1 << next_dir);

            // XOR: exactly one must be occupied
            if (prev_occupied != next_occupied) {
                valid_moves |= (1 << dir);
            }
        }

        lut[shape] = valid_moves;
    }

    return lut;
}();


const std::array<HexBitboard32x32, HiveBoard::kNumCells> HiveBoard::kNeighbourMasks = [](){
  std::array<HexBitboard32x32, HiveBoard::kNumCells> result{};

  for (size_t pos = 0; pos < HiveBoard::kNumCells; ++pos) {
    result[pos] = HexBitboard32x32(pos).hex_adjacent_bits();
  }

  return result;
}();

const std::array<std::array<std::array<size_t, Direction::kNumAllDirections>, HiveTile::kNumTiles>, HiveTile::kNumTiles> kActions = [](){
  std::array<std::array<std::array<size_t, Direction::kNumAllDirections>, HiveTile::kNumTiles>, HiveTile::kNumTiles> result{};
  for (int from = 0; from < HiveTile::kNumTiles; ++from) {
    for (int to = 0; to < HiveTile::kNumTiles; ++to) {
      for (int dir = 0; dir < Direction::kNumAllDirections; ++dir) {
        result[from][to][dir] =
        (from * HiveTile::kNumTiles * Direction::kNumAllDirections) +
        (to * Direction::kNumAllDirections) +
        dir;
      }
    }
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
      SpielFatalError("HIVE forced to go off grid...");
      return false;
    }
  }

  // save which tile is underneath me now
  HiveTile underneath = GetTileUnderneath(move.from);

  // check if tile already exists at target loc
  if (tile_grid_[to_pos].HasValue()) {
    // set new height of stack
    SetStackHeight(move.from, 1 + GetStackHeight(tile_grid_[to_pos]));
    // tile_stack_heights_[move.from] = tile_stack_heights_[tile_grid_[to_pos]] + 1;

    // => previous tile will be covered and placed into first non-empty slot
    for (int idx = 0; idx < covered_tiles_.size(); ++idx) {
      if (!covered_tiles_[idx].first.HasValue()) {
        covered_tiles_[idx] = {tile_grid_[to_pos], move.from};
        SetCovered(tile_grid_[to_pos], true);
        break;
      }
    }
  } else {
    // tile must be on ground level
    // tile_stack_heights_[move.from] = 1;
    SetStackHeight(move.from, 1);
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
    SetInPlay(move.from);
  }

  //////////////////////////////////////////////////////////////////////////////
  // END MOVE
  //////////////////////////////////////////////////////////////////////////////

  // always call immediately after move, as other operations may rely on it
  UpdateArticulationPoints();

  // re-calculate the adjacencies of all positions next to "from" and "to"
  for (int dir = 0; dir < Direction::kNumCardinalDirections; ++dir) {
    if (from_pos != kNullPos) {
      adjacency_grid_[from_pos + kNeighbourOffsets[dir]] = CalculateAdjacency(from_pos + kNeighbourOffsets[dir]);
    }
    adjacency_grid_[to_pos + kNeighbourOffsets[dir]] = CalculateAdjacency(to_pos + kNeighbourOffsets[dir]);
  }
  
  // success
  return true;
}

void HiveBoard::GenerateAllMoves(std::bitset<kNumDistinctActions>& out,
                                 Colour to_play, int move_num) const {

  constexpr std::array<HiveTile, 7> first_move_tiles = {
    HiveTile::wA1, HiveTile::wG1, HiveTile::wS1, HiveTile::wB1,
    HiveTile::wM, HiveTile::wL, HiveTile::wP
  };

  constexpr std::array<HiveTile, 7> second_move_tiles = {
    HiveTile::bA1, HiveTile::bG1, HiveTile::bS1, HiveTile::bB1,
    HiveTile::bM, HiveTile::bL, HiveTile::bP
  };

  // ===========================================================================
  // PLACEMENT MOVES
  // ===========================================================================
  // separating first two moves into inline helpers to minimize branching impact

  // Special case: move 0 (white's first move)
  if (move_num == 0) {
    // for (HiveTile tile : first_move_tiles) {
    //   if (expansions_.IsBugTypeEnabled(tile.GetBugType())) {
    //     out.set(kActions[tile][tile][Direction::kAbove]);
    //   }
    // }
    GenerateFirstTurnPlacements(out);
    return;
  }
  
  // Special case: move 1 (black's response)
  if (move_num == 1) {
    // const int dir_start = fixed_orientation_ ? Direction::kE : 0;
    // const int dir_end = fixed_orientation_ ? Direction::kE + 1 : Direction::kNumCardinalDirections;
    
    // for (HiveTile tile : second_move_tiles) {
    //   if (expansions_.IsBugTypeEnabled(tile.GetBugType())) {
    //     for (int dir = dir_start; dir < dir_end; ++dir) {
    //       out.set(kActions[tile][played_tiles_[0]][dir]);
    //     }
    //   }
    // }
    GenerateSecondTurnPlacements(out, played_tiles_[0]);
    return;
  }
  
  // ========================================================================
  // REGULAR PLACEMENT (move_num >= 2)
  // ========================================================================
  
  // Check if queen must be played this turn
  const bool must_play_queen = (move_num == 6 || move_num == 7) && 
        !IsInPlay(to_play == Colour::kWhite ? HiveTile::wQ : HiveTile::bQ);
  
  // pre-calculate which tiles are placeable
  std::vector<HiveTile> placeable_tiles{};
  placeable_tiles.reserve(14);
  for (HiveTile tile : HiveTile::GetTilesForColour(to_play)) {
    if (!expansions_.IsBugTypeEnabled(tile.GetBugType()) || IsInPlay(tile)) {
      continue;
    }
    
    if (must_play_queen && tile.GetBugType() != BugType::kQueen) {
      continue;
    }
    
    if (tile.GetOrdinal() > 1) {
      HiveTile prev_ordinal = HiveTile::GetTileFrom(
          to_play, tile.GetBugType(), tile.GetOrdinal() - 1);
      if (!IsInPlay(prev_ordinal)) {
        continue;
      }
    }
    
    placeable_tiles.push_back(tile);
  };
  
  // iterate through valid_placements and use adjacency_grid to find reference tiles
  HexBitboard32x32 valid_placements = PlaceablePositions(static_cast<Player>(to_play));
  valid_placements.for_each_set_bit([&](size_t placement_pos) {
    // For this valid placement position, find which played tiles are adjacent
    // and can serve as reference points
    
    uint8_t adj_to_placement = adjacency_grid_[placement_pos];
    
    // Iterate through each direction that has an adjacent tile
    while (adj_to_placement != 0) {
      int dir = __builtin_ctz(adj_to_placement);
      size_t ref_pos = placement_pos + kNeighbourOffsets[dir];
      HiveTile ref_tile = tile_grid_[ref_pos];
      
      // Only use same-color, uncovered tiles as reference points
      if (ref_tile.HasValue() && 
          ref_tile.GetColour() == to_play &&
          !IsCovered(ref_tile)) {
        
        int opposite_dir = kOppositeDirection[dir];
        
        // Generate placement move for each placeable tile
        for (HiveTile tile : placeable_tiles) {
          out.set(kActions[tile][ref_tile][opposite_dir]);
        }
      }
      
      adj_to_placement &= adj_to_placement - 1;  // Clear lowest bit
    }
  });
  
  // ========================================================================
  // MOVEMENT MOVES
  // ========================================================================
  
  const bool queen_placed = move_num >= 8 ||
    IsInPlay(to_play == Colour::kWhite ? HiveTile::wQ : HiveTile::bQ);
  
  if (!queen_placed) {
    return;
  }
  
  for (HiveTile moving_tile : HiveTile::GetTilesForColour(to_play)) {
    if (!IsInPlay(moving_tile) || moving_tile == last_moved_) {
      continue;
    }
    
    GenerateMovesFor(out, moving_tile, moving_tile.GetBugType(), to_play);
  }



  
  // // Placement positions
  // HexBitboard32x32 valid_placements = PlaceablePositions(static_cast<Player>(to_play));
  // const bool queen_placed = move_num >= 8 ||
  //       IsInPlay(to_play == Colour::kWhite ? HiveTile::wQ : HiveTile::bQ);

  // // filter out which tiles are *actually* placeable right now
  // std::vector<HiveTile> tiles_to_place{};
  // for (HiveTile tile : HiveTile::GetTilesForColour(to_play)) {
  //   if (move_num == 0) {
  //     if (tile.GetBugType() == BugType::kQueen ||
  //         tile.GetOrdinal() > 1 ||
  //         !expansions_.IsBugTypeEnabled(tile.GetBugType())) {
  //       continue;
  //     }
  //   } else if (move_num == 1) {
  //     if (tile.GetBugType() == BugType::kQueen ||
  //         tile.GetOrdinal() > 1 ||
  //         !expansions_.IsBugTypeEnabled(tile.GetBugType())) {
  //       continue;
  //     }
  //   } else {
  //     if (!expansions_.IsBugTypeEnabled(tile.GetBugType()) ||
  //         IsInPlay(tile)) {
  //       continue;
  //     }

  //     // Queen *must* be played by each player's 4th turn (8 total moves).
  //     if ((move_num == 6 || move_num == 7) && !queen_placed &&
  //         tile.GetBugType() != BugType::kQueen) {
  //       continue;
  //     }

  //     // check if previous tile ordinal has been played
  //     HiveTile prev_ordinal_tile = 
  //       HiveTile::GetTileFrom(to_play, tile.GetBugType(), tile.GetOrdinal() - 1);
  //     if (tile.GetOrdinal() != 1 && !IsInPlay(prev_ordinal_tile)) {
  //         continue;
  //     }
  //   }

  //   tiles_to_place.push_back(tile);
  // }

  // // move 0: white must play a (non-queen) tile at the origin
  // if (move_num == 0) { // TODO: cold path
  //   valid_placements.set(kStartPos);

  //   for (HiveTile tile : tiles_to_place) {
  //     // playing the first tile at the origin is encoded as a move where
  //     // a tile is placed "on top of itself"
  //     out.set(kActions[tile][tile][Direction::kAbove]);
  //   }
  // } else if (move_num == 1) { // TODO: cold path
  //   // move 1: black must play a (non-queen) tile next to white's first tile.
  //   // this is the only time placing a tile next to an opponent's is allowed
  //   for (HiveTile tile : tiles_to_place) {
  //     for (int dir = 0; dir < Direction::kNumCardinalDirections; ++dir) {
  //       // a fixed starting orientation allows for the simplification of the
  //       // game's opening, reducing the branching factor five-fold
  //       if (fixed_orientation_ && static_cast<Direction>(dir) != Direction::kE) {
  //         continue;
  //       }

  //       out.set(
  //           kActions[tile][played_tiles_[0]][dir]);
  //     }
  //   }
  // } else { // TODO: Hot path
  //   for (HiveTile ref_tile : played_tiles_) {
  //     if (ref_tile.GetColour() != to_play || IsCovered(ref_tile)) {
  //       continue;
  //     }

  //     // quick optimization check before running more nested loops
  //     HexBitboard32x32 adj_placements = kNeighbourMasks[GetPositionOf(ref_tile)] &
  //                                    valid_placements;
  //     if (adj_placements.none()) {
  //       continue;
  //     }

  //     for (uint8_t dir = 0; dir < Direction::kNumCardinalDirections; ++dir) {
  //       size_t to_test = GetPositionOf(ref_tile) + kNeighbourOffsets[dir];
  //       if (valid_placements.test(to_test)) {
  //         for (HiveTile tile : tiles_to_place) {
  //           out.set(kActions[tile][ref_tile][dir]);
  //         }
  //       }
  //     }
  //   }
  // }
  
  // // Movements:
  // if (!queen_placed) {
  //   return;
  // }

  // for (HiveTile moving_tile : played_tiles_) {
  //   // Can't use the last_moved_ tile (Pillbug special)
  //   if (moving_tile.GetColour() != to_play || moving_tile == last_moved_) {
  //     continue;
  //   }

  //   GenerateMovesFor(out, moving_tile, moving_tile.GetBugType(), to_play);
  // }
}

void HiveBoard::GenerateFirstTurnPlacements(std::bitset<kNumDistinctActions>& out) const {
  // Only white tiles, ordinal 1, non-queen, enabled expansions
  constexpr std::array<HiveTile, 7> first_move_tiles = {
    HiveTile::wA1, HiveTile::wG1, HiveTile::wS1, HiveTile::wB1,
    HiveTile::wM, HiveTile::wL, HiveTile::wP
  };
  
  for (HiveTile tile : first_move_tiles) {
    if (expansions_.IsBugTypeEnabled(tile.GetBugType())) {
      out.set(kActions[tile][tile][Direction::kAbove]);
    }
  }
}

void HiveBoard::GenerateSecondTurnPlacements(
    std::bitset<kNumDistinctActions>& out,
    HiveTile white_first_tile) const {
  // Only black tiles, ordinal 1, non-queen, enabled expansions
  constexpr std::array<HiveTile, 7> second_move_tiles = {
    HiveTile::bA1, HiveTile::bG1, HiveTile::bS1, HiveTile::bB1,
    HiveTile::bM, HiveTile::bL, HiveTile::bP
  };
  
  const int dir_start = fixed_orientation_ ? Direction::kE : 0;
  const int dir_end = fixed_orientation_ ? Direction::kE + 1 : Direction::kNumCardinalDirections;
  
  for (HiveTile tile : second_move_tiles) {
    if (expansions_.IsBugTypeEnabled(tile.GetBugType())) {
      for (int dir = dir_start; dir < dir_end; ++dir) {
        out.set(kActions[tile][white_first_tile][dir]);
      }
    }
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
      valid_positions |= ValidAntPositionsBFS(to_move);
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
      if (GetStackHeight(to_move) > 1) {
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
  // for (size_t valid_pos : valid_positions.all_set_bits()) {
  //   GenerateMove(out, to_move, valid_pos);
  // }
  valid_positions.for_each_set_bit([&](size_t valid_pos) {
    GenerateMove(out, to_move, valid_pos);
  });

  // ALTERNATE PARALLEL APPROACH
  // shift a board in each direction and generate moves
  // HexBitboard32x32 adjacent_tiles{};
  // for (uint8_t dir = 0; dir < Direction::kNumAllDirections; ++dir) {
  //   switch (static_cast<Direction>(dir)) {
  //     case Direction::kNE:
  //       adjacent_tiles = valid_positions.north_east() & occupied_;
  //       break;
  //     case Direction::kE:
  //       adjacent_tiles = valid_positions.east() & occupied_;
  //       break;
  //     case Direction::kSE:
  //       adjacent_tiles = valid_positions.south_east() & occupied_;
  //       break;
  //     case Direction::kSW:
  //       adjacent_tiles = valid_positions.south_west() & occupied_;
  //       break;
  //     case Direction::kW:
  //       adjacent_tiles = valid_positions.west() & occupied_;
  //       break;
  //     case Direction::kNW:
  //       adjacent_tiles = valid_positions.north_west() & occupied_;
  //       break;
  //     default:
  //       // no-op
  //       break;
  //   }

  //   for (size_t pos : adjacent_tiles.all_set_bits()) {
  //     if (GetTopTileAt(pos) != to_move) {
  //       out.set(GenerateAction(to_move, GetTopTileAt(pos), OppositeDirection(dir)));
  //     } else {
  //       // generate action for the tile underneath itself (if it exists)
  //       if (GetTopTileAt(pos) == to_move && GetTileUnderneath(pos).HasValue()) {
  //         out.set(GenerateAction(to_move, GetTileUnderneath(pos), OppositeDirection(dir)));
  //       }
  //     }
  //   }
  // }

  // // also do above separately
  // adjacent_tiles = valid_positions & occupied_;
  // for (size_t pos : adjacent_tiles.all_set_bits()) {
  //   out.set(GenerateAction(to_move, GetTopTileAt(pos), Direction::kAbove));
  // }
}


void HiveBoard::GenerateMove(std::bitset<kNumDistinctActions>& out, HiveTile from_tile, size_t to_pos) const {
  // Case 1: if this is a climb UP onto another tile
  if (occupied_.test(to_pos)) {
    out.set(kActions[from_tile][tile_grid_[to_pos]][Direction::kAbove]);
  }
  
  // Case 2: find reference tiles to use in all 6 normal directions
  uint8_t to_adj = adjacency_grid_[to_pos];
  while (to_adj != 0) {
    int dir = __builtin_ctz(to_adj);
    int opp_dir = kOppositeDirection[dir];
    HiveTile ref_tile = tile_grid_[to_pos + kNeighbourOffsets[dir]];

    if (ref_tile != from_tile) {
      out.set(kActions[from_tile][ref_tile][opp_dir]);
    } else if (GetStackHeight(ref_tile) > 1) {
      // Special Case: beetle is dropping down next to itself
      out.set(kActions[from_tile][GetTileUnderneath(ref_tile)][opp_dir]);
    }

    to_adj &= to_adj - 1;
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
      if (to_move.HasValue() && !IsPinned(to_move) && GetStackHeight(to_move) <= 1 && to_move != last_moved_) {
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
  // any arbitrary starting point would do, but the Queen is guaranteed to be
  // in play when generating moves.
  // However, queen could be covered, so use the topmost tile at the queen's pos

  HiveTile start_queen = HiveTile::wQ;
  if (!IsInPlay(start_queen)) {
    start_queen = HiveTile::bQ;
  }

  if (!IsInPlay(start_queen)) {
    // no queen in play, don't need articulation points
    return;
  }

  HiveTile start = GetTopTileAt(GetPositionOf(start_queen));

  // reset all tiles
  for (HiveTile tile : played_tiles_) {
    SetPinned(tile, false);
  }

  // the DFS graph algorithm is faster than any approach with bitboards
  std::bitset<HiveTile::kNumTiles> visited{};
  std::array<int, HiveTile::kNumTiles> entry_point{};
  std::array<int, HiveTile::kNumTiles> low_point{};

  
  int visit_order = 0;
  DFSArticulation(start, start, visited, visit_order, entry_point, low_point);
}

void HiveBoard::DFSArticulation(HiveTile tile, HiveTile parent,
    std::bitset<HiveTile::kNumTiles>& visited, int& visit_order,
    std::array<int, HiveTile::kNumTiles>& entry_point,
    std::array<int, HiveTile::kNumTiles>& low_point) {

  visited.set(tile);
  size_t vertex = tile_positions_[tile];
  entry_point[tile] = low_point[tile] = visit_order;
  ++visit_order;

  bool is_root = tile == parent;
  int children = 0;
  for (int dir = 0; dir < Direction::kNumCardinalDirections; ++dir) {
    HiveTile neighbour = tile_grid_[vertex + kNeighbourOffsets[dir]];
    if (!neighbour.HasValue() || neighbour == parent) {
      continue;
    }

    if (visited.test(neighbour)) {
      low_point[tile] = std::min(low_point[tile], entry_point[neighbour]);
    } else {
      DFSArticulation(neighbour, tile, visited,
        visit_order, entry_point, low_point);
      ++children;
      low_point[tile] = std::min(low_point[tile], low_point[neighbour]);
      if (low_point[neighbour] >= entry_point[tile] && !is_root) {
        // std::cout << "Setting " << tile.ToUHP() << " as pinned because low_point[" << neighbour.ToUHP() << "] (" 
        //   << low_point[neighbour] << ")  >=   entry_point[" << tile.ToUHP() << "] ( " << entry_point[tile] << ") && !is_root" << std::endl;
        SetPinned(tile, true);
      }
    }
  }

  if (is_root && children > 1) {
    // std::cout << "Setting " << tile.ToUHP() << " as pinned because is_root && children == " << children << std::endl;
    SetPinned(tile, true);
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

  SetCovered(tile, false);
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
  // for (uint8_t dir = 0; dir < Direction::kNumCardinalDirections; ++dir) {
  //   if (!IsGated(start_pos, (Direction)dir)) {
  //     result.set(start_pos + kNeighbourOffsets[dir]);
  //   }
  // }
  uint8_t valid_moves = kValidMovesLUT[adjacency_grid_[start_pos]];
  while (valid_moves) {
    int dir = __builtin_ctz(valid_moves);
    result.set(start_pos + kNeighbourOffsets[dir]);

    valid_moves &= valid_moves - 1;
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
  HexBitboard32x32 visited{};
  visited.set(start_pos);
  HexBitboard32x32 new_positions{};
  new_positions.set(start_pos);
  HexBitboard32x32 occ = occupied_;
  occ.clear(start_pos);
  HexBitboard32x32 not_occ = ~occ;

  // calculate gates once
  HexBitboard32x32 directional_gates[kNumCardinalDirections];
  directional_gates[0] = (occ.north_west() ^ occ.east()) & not_occ;
  directional_gates[1] = (occ.north_east() ^ occ.south_east()) & not_occ;
  directional_gates[2] = (occ.east() ^ occ.south_west()) & not_occ;

  // If I can slide from A to B by going North-East, I can also slide from
  // B to A going South-West. So we can save some redundant calls here
  directional_gates[3] = directional_gates[0].south_west() & not_occ;
  directional_gates[4] = directional_gates[1].west() & not_occ;
  directional_gates[5] = directional_gates[2].north_west() & not_occ;
  // directional_gates[3] = (occ.south_east() ^ occ.west()) & not_occ;
  // directional_gates[4] = (occ.south_west() ^ occ.north_west()) & not_occ;
  // directional_gates[5] = (occ.west() ^ occ.north_east()) & not_occ;

  // Continue until no new positions are found
  while (new_positions.any()) {
    HexBitboard32x32 not_visited = ~visited;
    
    // For each direction, compute all valid movements from all frontier positions at once
    new_positions = 
      (new_positions.north_east() & not_visited & directional_gates[0]) |
      (new_positions.east() & not_visited & directional_gates[1]) |
      (new_positions.south_east() & not_visited & directional_gates[2]) |
      (new_positions.south_west() & not_visited & directional_gates[3]) |
      (new_positions.west() & not_visited & directional_gates[4]) |
      (new_positions.north_west() & not_visited & directional_gates[5]);

    visited |= new_positions;
  }

  visited.clear(start_pos);
  return visited;
}

HexBitboard32x32 HiveBoard::ValidAntPositionsBFS(HiveTile tile) const {
  size_t start_pos = GetPositionOf(tile);
  HexBitboard32x32 result{};
  result.set(start_pos);

  // temporarly tell adjacent neighbours the ant is gone
  for (int dir = 0; dir < Direction::kNumCardinalDirections; ++dir) {
    size_t nb = start_pos + kNeighbourOffsets[dir];
    adjacency_grid_[nb] &= ~(1 << ((dir + 3) % Direction::kNumCardinalDirections));
  }

  std::deque<size_t> positions;
  positions.push_back(start_pos);

  while (!positions.empty()) {
    size_t cur_pos = positions.front();
    positions.pop_front();

    uint8_t valid_moves = kValidMovesLUT[adjacency_grid_[cur_pos]];
    while (valid_moves) {
      int dir = __builtin_ctz(valid_moves);
      size_t test_pos = cur_pos + kNeighbourOffsets[dir];
      if (!result.test(test_pos)) {
        positions.push_back(test_pos);
        result.set(test_pos);
      }
      
      valid_moves &= valid_moves - 1;
    }
  }

  // re-instate ant to its neighbours
  for (int dir = 0; dir < Direction::kNumCardinalDirections; ++dir) {
    size_t nb = start_pos + kNeighbourOffsets[dir];
    adjacency_grid_[nb] |= (1 << ((dir + 3) % Direction::kNumCardinalDirections));
  }

  result.clear(start_pos);
  return result;
}

HexBitboard32x32 HiveBoard::ValidGrasshopperPositionsAlt(HiveTile tile) const {
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

HexBitboard32x32 HiveBoard::ValidGrasshopperPositions(HiveTile tile) const {
  size_t start_pos = GetPositionOf(tile);
  HexBitboard32x32 result{};

  for (uint8_t dir = 0; dir < kNumCardinalDirections; ++dir) {
    if (occupied_.test(start_pos + kNeighbourOffsets[dir])) {
      size_t test_pos = start_pos + kNeighbourOffsets[dir] + kNeighbourOffsets[dir];
      while (occupied_.test(test_pos)) {
        test_pos = test_pos + kNeighbourOffsets[dir];
      }

      result.set(test_pos);
    }
  }

  return result;
}

HexBitboard32x32 HiveBoard::ValidClimbPositions(HiveTile tile) const {
  size_t start_pos = GetPositionOf(tile);
  HexBitboard32x32 result{};

  for (uint8_t dir = 0; dir < Direction::kNumCardinalDirections; ++dir) {
    int from_height = GetStackHeight(tile);
    int dest_height = 1 + GetStackHeight(GetTopTileAt(start_pos + kNeighbourOffsets[dir]));

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
