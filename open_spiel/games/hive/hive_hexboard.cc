#include "open_spiel/games/hive/hive_hexboard.h"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

//#include "open_spiel/abseil-cpp/absl/random/uniform_int_distribution.h"
#include "open_spiel/spiel.h"


namespace open_spiel {
namespace hive {


Player OtherPlayer(Player p) {
  return p == kPlayerWhite ? kPlayerBlack : kPlayerWhite;
}

Direction OppositeDirection(uint8_t in) {
  SPIEL_CHECK_TRUE(in != Direction::kAbove);
  return static_cast<Direction>((in + 3) % 6);
}

Direction ClockwiseDirection(uint8_t in) {
  SPIEL_CHECK_TRUE(in != Direction::kAbove);
  return static_cast<Direction>((in + 1) % 6);
}

Direction CounterClockwiseDirection(uint8_t in) {
  SPIEL_CHECK_TRUE(in != Direction::kAbove);
  return static_cast<Direction>((in + 5) % 6);
}

// Creates a regular hexagonal board with a default radius of 8 (excluding
// the center) The formula is: "3r^2 + 3r + 1" plus an extra tile for each 
// stackable piece
// 
HexBoard::HexBoard(std::shared_ptr<const Game> game,
                   const int board_radius, const int num_stackable /*= 6*/)
    : game_(game),
      board_radius_(board_radius),
      num_stackable_(num_stackable),
      num_tiles_(28) { // constant 28 for now?

  tiles_.reserve(num_tiles_);

  for (auto& direction_list : slidability_cache_) {
    direction_list.second.reserve(Direction::kNumCardinalDirections);
  }
}


void HexBoard::CreateTile(const Player player, const BugType type, int ordinal) {
  // allocate a HiveTile object here, and add to relevant mappings
  HiveTilePtr tile = std::make_shared<HiveTile>(kNullPosition, player, type, ordinal);
  tiles_.emplace_back(tile);

  player_tiles_[player].emplace_back(tile);
  type_tiles_[type].emplace_back(tile);
}


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


absl::optional<HiveTilePtr> HexBoard::GetTileAbove(const HivePosition& pos) {
  HivePosition to_test = {pos.Q(), pos.R(), pos.H() + 1};
  if (position_cache_.contains(to_test)) {
    return position_cache_.at(to_test);
  }
  return absl::nullopt;
}


absl::optional<HiveTilePtr> HexBoard::GetTileBelow(const HivePosition& pos) {
  HivePosition to_test = {pos.Q(), pos.R(), pos.H() - 1};
  if (position_cache_.contains(to_test)) {
    return position_cache_.at(to_test);
  }
  return absl::nullopt;
}


// A tile can be encoded by its position in an array of tiles
// laid out left-to-right containing every piece in the game.
// Conveniently, this is the order the tiles are created in
int HexBoard::EncodeTile(HiveTilePtr& tile) const {
  int encoding = -1;

  auto it = std::find(tiles_.begin(), tiles_.end(), tile);
  SPIEL_CHECK_TRUE(it != tiles_.end());
  encoding = std::distance(tiles_.begin(), it);

  return encoding;
}

HiveTilePtr& HexBoard::DecodeTile(int encoding) {
  SPIEL_CHECK_TRUE(encoding >= 0 && encoding < num_tiles_);
  return tiles_.at(encoding);
}


void HexBoard::GenerateAllMoves(std::vector<Move>& out_vec, Player player, int move_number) {
  // find all HivePositions where player can place a tile from hand
  // and present them as Moves
  GeneratePlacementMoves(out_vec, player, move_number);

  // generate legal moves for tiles already on the board
  if (IsInPlay(player, BugType::kQueen)) {
    for (auto& tile : played_tiles_) {
      if (tile->GetPlayer() != player || tile == last_stunned_) {
        continue;
      }

      GenerateMovesFor(out_vec, tile->GetPosition(), tile->GetBugType(), player);
    }
  }

  // TODO: REMOVE
  std::cout << "Articulation points:" << std::endl;
  for (auto pos : articulation_points_) {
    std::cout << position_cache_.at(pos)->ToUHP() << ", ";
  }
  std::cout << std::endl;
}


void HexBoard::GeneratePlacementMoves(std::vector<Move>& out, Player player, int move_number) {
  // first two turns have special placement rules
  // turn 1: white must play a (non-queen) tile at the origin
  if (move_number == 0) {
    for (auto& tile : player_tiles_[player]) {
      if (tile->GetBugType() == BugType::kQueen) {
        continue;
      }

      // playing the first tile at the origin is encoded as a move where
      // a tile is placed "on top of nothing", i.e. from=tile, to=nullptr
      out.emplace_back(Move{tile,
                            nullptr,
                            Direction::kAbove});
    }

  // turn 2: black must play a (non-queen) tile next to white's first tile
  // this is the only time placing a tile next to an opponent's is allowed
  } else if (move_number == 1) {
    for (auto& tile : player_tiles_[player]) {
      if (tile->GetBugType() == BugType::kQueen) {
        continue;
      }

      for (int i = 0; i < Direction::kNumCardinalDirections; ++i) {
        out.emplace_back(Move{tile,
                              played_tiles_.front(),
                              static_cast<Direction>(i)});
      }
    }
  } else {
    // Queen *must* be played by each player's 4th turn. For all other turns,
    // find valid locations to place a tile by computing a set difference of
    // the player's influence positions
    bool queen_placed = move_number >= 8 || IsInPlay(player, BugType::kQueen);
    for (auto& tile : player_tiles_[player]) {
      if (tile->IsInPlay()) {
        continue;
      }

      // Queen *must* be played by each player's 4th turn
      if ((move_number == 6 or move_number == 7) &&
            !queen_placed &&
            tile->GetBugType() != BugType::kQueen) {
        continue;
      }

      // check all positions for validity
      for (auto& pos : player_influence_[player]) {
        // skip - there is already a tile here
        if (position_cache_.contains(pos)) {
          continue;
        }

        // skip - other player's tile is next to this spot
        if (player_influence_[OtherPlayer(player)].contains(pos)) {
          continue;
        }

        // for completeness, any neighbouring tile can be used as the reference
        for (uint8_t i = 0; i < Direction::kNumCardinalDirections; ++i) {
          HivePosition to_pos = pos + kNeighbourOffsets[i];
          auto maybe_tile = GetTopTileAt(to_pos);
          if (maybe_tile.has_value()) {
            out.emplace_back(Move{tile,
                                  *maybe_tile,
                                  OppositeDirection(i)});
          }
        }
      }
    }
  }
}


void HexBoard::GenerateMovesFor(std::vector<Move>& out, const HivePosition& start_pos, BugType type, Player player) {
  MaybeUpdateSlideCache();
  MaybeUpdateArticulationPoints();
  cache_valid_ = true;

  std::vector<HivePosition> positions;
  switch (type) {
    case BugType::kQueen:
      GenerateValidSlides(positions, start_pos, 1);
      break;

    case BugType::kAnt:
      GenerateValidSlides(positions, start_pos, -1);
      break;

    case BugType::kGrasshopper:
      GenerateValidGrasshopperPositions(positions, start_pos);
      break;

    case BugType::kSpider:
      GenerateValidSlides(positions, start_pos, 3);
      break;

    case BugType::kBeetle:
      GenerateValidSlides(positions, start_pos, 1);
      GenerateValidClimbs(positions, start_pos);
      break;

    case BugType::kMosquito:
      GenerateValidMosquitoPositions(positions, start_pos);
      break;

    case BugType::kLadybug:
      GenerateValidLadybugPositions(positions, start_pos);
      break;

    case BugType::kPillbug:
      GenerateValidSlides(positions, start_pos, 1);

      // pillbug special constructs its own moves
      GenerateValidPillbugSpecials(out, start_pos);
      break;
  }

  // since we are generating Moves and not Placements, tile must already
  // be in the position cache
  HiveTilePtr& tile = position_cache_.at(start_pos);

  // turn each position into moves by finding neighbouring tiles as reference
  for (auto& to_pos : positions) {
    //std::cout << to_pos << ", ";
    if (to_pos.H() > 0) {
      // only generate kAbove moves for on top the hive
      out.emplace_back(Move{tile,
                            *GetTileBelow(to_pos),
                            Direction::kAbove});
    } else {
      // check for a valid reference tile in each direction
      // for (auto& tile_ref : NeighbourReferenceTiles(to_pos)) {
      //   if (tile != tile_ref.tile) {
      //     out.emplace_back(Move{tile,
      //                           tile_ref.tile,
      //                           tile_ref.dir});
      //   }
      // }

      for (uint8_t d = 0; d < Direction::kNumCardinalDirections; ++d) {
        auto maybe_tile = GetTopTileAt(to_pos + kNeighbourOffsets[d]);
        if (maybe_tile.has_value()) {
          if (start_pos.H() > 0 && *maybe_tile == tile) {
            out.emplace_back(Move{tile,
                                  *GetTileBelow(start_pos),
                                  OppositeDirection(d)});
          } else if (*maybe_tile != tile) {
            out.emplace_back(Move{tile,
                                  *maybe_tile,
                                  OppositeDirection(d)});
          }
        }
      }
    }
  }
  //std::cout << std::endl;
}


// In order for a tile to slide in direction D, the following must hold true:
// 1) The tile must not be "pinned" (i.e. at an articulation point)
// 2) The tile must be on ground level, or sliding on top of another tile
// 3) The tile must be able to physically slide into the position without
//    hitting other tiles. That is, when sliding in direction D, exactly one
//    of the two adjacent positions (D-1) (D+1) must be empty to physically
//    move in, and the other position must be occupied in order to remain
//    attached to the hive at all times (One-Hive rule)
void HexBoard::GenerateValidSlides(std::vector<HivePosition>& out, const HivePosition& start_pos, int distance) {
  if (IsPinned(start_pos) || IsCovered(start_pos)) {
    return;
  }
  
  //HiveTilePtr& tile = position_cache_.at(start_pos);
  auto maybe_tile = GetTopTileAt(start_pos);
  
  auto search = [&](auto& search, absl::flat_hash_set<HivePosition>& visited, HivePosition pos, Direction from, int dist) -> void {
    if (visited.contains(pos)) {
      return;
    }
    visited.insert(pos);

    if (dist == 0) {
      out.push_back(pos);
      return;
    } else if (distance < 0) {
       out.push_back(pos);
    }

    for (auto& slide : slidability_cache_[pos]) {
      if (slide.tile ==  maybe_tile || slide.dir == from) {
        continue;
      }

      search(search, visited, pos + kNeighbourOffsets[slide.dir], OppositeDirection(slide.dir), dist - 1);
    }
  };

  // start search in the directions the slideability cache has already computed
  // TODO: MAKE WORK WITH kABOVE
  //std::cout << tile->ToUHP() << " has " << std::to_string(slidability_cache_[pos].size()) << " slidable positions:" << std::endl;
  for (auto& slide : slidability_cache_[start_pos]) {
    // need a fresh visited list for each starting direction
    absl::flat_hash_set<HivePosition> v = {start_pos};
    v.reserve(player_influence_[kPlayerWhite].size() + player_influence_[kPlayerBlack].size() + played_tiles_.size());
    search(search, v, start_pos + kNeighbourOffsets[slide.dir], OppositeDirection(slide.dir), distance - 1);
  }
}


// A climb consists of a slide on top the hive laterally, with an optional
// vertical movement, in any non-gated direction. This slide is less
// restrictive than a ground-level slide as you do not require neighbours
// to remain connected to the hive
void HexBoard::GenerateValidClimbs(std::vector<HivePosition>& out, const HivePosition& start_pos) {
  if (IsPinned(start_pos) || IsCovered(start_pos)) {
    return;
  }
  
  HivePosition ground_pos = {start_pos.Q(), start_pos.R(), 0};

  // find the top tile, or an empty position, in each adjacent position
  for (uint8_t d = 0; d < Direction::kNumCardinalDirections; ++d) {

    // search for a tile from ground up
    auto maybe_tile = GetTopTileAt(ground_pos + kNeighbourOffsets[d]);
    if (maybe_tile.has_value()) {
      HivePosition to_pos = (*maybe_tile)->GetPosition() + kNeighbourOffsets[Direction::kAbove];

      // climbing up: check for a gate at the *target*'s height
      if (to_pos.H() > start_pos.H() && !IsGated({start_pos.Q(), start_pos.R(), to_pos.H()}, static_cast<Direction>(d))) {
        out.push_back(to_pos);
      // climbing down or across: check for gate at *this* tile's height
      } else if (to_pos.H() <= start_pos.H() && !IsGated(start_pos, static_cast<Direction>(d)) && !position_cache_.contains(to_pos)) {
        out.push_back(to_pos);
      }
    } else {
      HivePosition to_pos = ground_pos + kNeighbourOffsets[d];

      // climbing down to empty space: check for a gate at *this* tile's height
      if (to_pos.H() < start_pos.H() && !IsGated(start_pos, static_cast<Direction>(d)) && !position_cache_.contains(to_pos)) {
        out.push_back(to_pos);
      }
    }
  }
}


void HexBoard::GenerateValidGrasshopperPositions(std::vector<HivePosition>& out, const HivePosition& start_pos) {
  if (IsPinned(start_pos) || IsCovered(start_pos)) {
    return;
  }

  // in each cardinal direction that contains a tile, jump over all tiles in
  // that direction until reaching an empty space to land
  for (uint8_t d = 0; d < Direction::kAbove; ++d) {
    bool found = false;
    HivePosition to_test = start_pos + kNeighbourOffsets[d];
    while (position_cache_.contains(to_test)) {
      to_test += kNeighbourOffsets[d];
      found = true;
    }

    if (found) {
      out.push_back(to_test);
    }
  }
}


// A lady bug moves in *exactly* 3 distinct steps: a climb onto the hive, a
// slide or climb across the hive, and a climb down from the hive
void HexBoard::GenerateValidLadybugPositions(std::vector<HivePosition>& out, const HivePosition& start_pos) {
  if (IsPinned(start_pos) || IsCovered(start_pos)) {
    return;
  }
  
  std::vector<HivePosition> intermediates1;
  std::vector<HivePosition> intermediates2;
  std::vector<HivePosition> intermediates3;

  // intermediates1.reserve(player_influence_[0].size());
  // intermediates2.reserve(player_influence_[0].size());
  // intermediates3.reserve(player_influence_[0].size());

  // step 1
  GenerateValidClimbs(intermediates1, start_pos);

  // step 2
  for (auto pos : intermediates1) {
    GenerateValidClimbs(intermediates2, pos);
  }

  // step 3
  for (auto pos : intermediates2) {
    // ensure on top of the hive and not on top of the original tile
    if (pos.H() == 0 || pos == start_pos + kNeighbourOffsets[Direction::kAbove]) {
      continue;
    }

    GenerateValidClimbs(intermediates3, pos);
  }

  // dumb way to check for dupes?
  //TODO absl::flat_hash_set<HivePosition> end_positions(intermediates3.begin(), intermediates3.end());
  for (auto pos : absl::flat_hash_set<HivePosition>(intermediates3.begin(), intermediates3.end())) {
    if (pos.H() == 0) {
      out.push_back(pos);
    }
  }
}


void HexBoard::GenerateValidMosquitoPositions(std::vector<HivePosition>& out, const HivePosition& start_pos) {
  // TODO: consider using Pillbug special (dont consider IsPinned)
  if (IsCovered(start_pos)) {
    return;
  }


}


void HexBoard::GenerateValidPillbugSpecials(std::vector<Move>& out, const HivePosition& start_pos) {
  // Pillbug can still perform its special when Pinned, unlike all other bugs
  if (IsCovered(start_pos)) {
    return;
  }

  std::vector<HiveTilePtr> valid_targets;
  std::vector<HivePosition> valid_positions;

  for (uint8_t dir = 0; dir < Direction::kNumCardinalDirections; ++dir) {
    // ensure there is no "gate" blocking above for this direction
    if (IsGated(start_pos + kNeighbourOffsets[Direction::kAbove], static_cast<Direction>(dir))) {
      continue;
    }

    HivePosition to_test = start_pos + kNeighbourOffsets[dir];
    if (position_cache_.contains(to_test)) {
      // valid IFF the target tile is not: Pinned, Covered, Stunned, LastMoved
      if (!IsPinned(to_test) && !IsCovered(to_test) && position_cache_.at(to_test) != LastMovedTile() && position_cache_.at(to_test) != LastStunnedTile()) {
        valid_targets.push_back(position_cache_.at(to_test));
      }
    } else {
      valid_positions.push_back(to_test);
    }
  }

  for (auto& tile : valid_targets) {
    for (auto pos : valid_positions) {
      for (auto& tile_ref : NeighbourReferenceTiles(pos)) {
        out.emplace_back(Move{tile,
                              tile_ref.tile,
                              tile_ref.dir});
      }
    }
  }
}


HiveTileList HexBoard::NeighboursOf(HivePosition pos) {
  HiveTileList neighbours;
  for (uint8_t i = 0; i < Direction::kNumCardinalDirections; ++i) {
    HivePosition to_test = pos + kNeighbourOffsets[i];
    if (position_cache_.contains(to_test)) {
      neighbours.push_back(position_cache_.at(to_test));
    }
  }

  return neighbours;
}




void HexBoard::PlaceTile(HiveTilePtr& tile, HivePosition new_pos) {
  SPIEL_CHECK_TRUE(tile);
  SPIEL_CHECK_FALSE(tile->IsInPlay());

  // IMPORTANT: if the reference tile was higher on the hive, the new_pos
  // may need to "fall down" until it hits ground or another tile
  int8_t new_height = new_pos.H();
  while (new_height > 0) {
    if (!position_cache_.contains({new_pos.Q(), new_pos.R(), new_height - 1})) {
      new_height--;
    } else {
      break;
    }
  }

  new_pos = {new_pos.Q(), new_pos.R(), new_height};

  // add tile to relevant containers
  played_tiles_.push_back(tile);
  position_cache_.insert({new_pos, tile});
  tile->SetPosition(new_pos);
  UpdateInfluence(tile->GetPlayer());
  last_moved_ = tile;
  cache_valid_ = false;
}



// TODO: Combine MoveTile and PlaceTile
void HexBoard::MoveTile(HiveTilePtr& tile, HivePosition new_pos, Player player) {
  SPIEL_CHECK_TRUE(tile);
  SPIEL_CHECK_TRUE(tile->IsInPlay());

  HivePosition old_pos = tile->GetPosition();
  auto handle = position_cache_.extract(old_pos);
  if (handle.empty()) {
    SpielFatalError("HexBoard::MoveTile() - Tile not found in position cache");
  }

  // IMPORTANT: if the reference tile was higher on the hive, the new_pos
  // may need to "fall down" until it hits ground or another tile
  int8_t new_height = new_pos.H();
  while (new_height > 0) {
    if (!position_cache_.contains({new_pos.Q(), new_pos.R(), new_height - 1})) {
      new_height--;
    } else {
      break;
    }
  }

  new_pos = {new_pos.Q(), new_pos.R(), new_height};


  // replace old mapping with new position key
  handle.key() = new_pos;
  position_cache_.insert(std::move(handle));
  tile->SetPosition(new_pos);


  // stun if moved on the other player's turn (which implies Pillbug special)
  last_stunned_ = player != tile->GetPlayer() ? tile : nullptr;
  last_moved_ = tile;
  cache_valid_ = false;

  // update influence for the moved tile's player
  // potentially have to update both if the moved tile was part of a stack
  UpdateInfluence(tile->GetPlayer());
  if (old_pos.H() > 0 || new_pos.H() > 0) {
    UpdateInfluence(OtherPlayer(tile->GetPlayer()));
  }
}


// reset any turn-dependent variables
void HexBoard::Pass() {
  last_moved_ = nullptr;
  last_stunned_ = nullptr;
}



const HiveTilePtr& HexBoard::LastMovedTile() const {
  return last_moved_;
}

const HiveTilePtr& HexBoard::LastStunnedTile() const {
  return last_stunned_;
}


bool HexBoard::IsInPlay(Player player, BugType type, int ordinal /*= -1*/) const {
  for (auto& tile : played_tiles_) {
    if (player == tile->GetPlayer() && type == tile->GetBugType() && (ordinal == -1 || ordinal == tile->GetOrdinal())) {
      return tile->IsInPlay();
    }
  }

  return false;
}


bool HexBoard::IsQueenSurrounded(Player player) {
  HiveTilePtr queen = GetTileFromUHP(player == kPlayerWhite ? "wQ" : "bQ");
  return queen->IsInPlay() && NeighboursOf(queen->GetPosition()).size() == 6;
}


absl::optional<HiveTilePtr> HexBoard::GetTopTileAt(const HivePosition& pos) {
  auto it = position_cache_.find(pos);
  int8_t height = 0;

  while (it != position_cache_.end()) {
    if (!IsCovered(it->second->GetPosition())) {
      return it->second;
    } else {
      it = position_cache_.find({pos.Q(), pos.R(), height});
    }
    ++height;
  }

  return absl::nullopt;
}


// Expects a string of a specific player's tile in UHP form, with no extra chars
HiveTilePtr& HexBoard::GetTileFromUHP(const std::string& uhp) {
  // kinda cheeky but idk
  const static std::unordered_map<std::string,int> index_mapping = {
    {"wQ",   0},
    {"wA1",  1},
    {"wA2",  2},
    {"wA3",  3},
    {"wG1",  4},
    {"wG2",  5},
    {"wG3",  6},
    {"wS1",  7},
    {"wS2",  8},
    {"wB1",  9},
    {"wB2", 10},
    {"wL",  11},
    {"wM",  12},
    {"wP",  13},
    {"bQ",  14},
    {"bA1", 15},
    {"bA2", 16},
    {"bA3", 17},
    {"bG1", 18},
    {"bG2", 19},
    {"bG3", 20},
    {"bS1", 21},
    {"bS2", 22},
    {"bB1", 23},
    {"bB2", 24},
    {"bL",  25},
    {"bM",  26},
    {"bP",  27}
  };

  auto it = index_mapping.find(uhp);
  SPIEL_CHECK_TRUE(it != index_mapping.end());
  return tiles_.at(it->second);
}


// IsGated verifies requirement (3) in GenerateValidSlides()
bool HexBoard::IsGated(const HivePosition& pos, Direction d) const {
  bool clockwise = position_cache_.contains(pos + kNeighbourOffsets[ClockwiseDirection(d)]);
  bool counter_clockwise = position_cache_.contains(pos + kNeighbourOffsets[CounterClockwiseDirection(d)]);
  return pos.H() == 0 ? clockwise == counter_clockwise : clockwise && counter_clockwise;
}


bool HexBoard::IsCovered(const HivePosition& pos) const {
  return position_cache_.contains(pos + kNeighbourOffsets[Direction::kAbove]);
}


// clear and recalculate this tile's player's influence range
void HexBoard::UpdateInfluence(Player player) {
  player_influence_[player].clear();
  for (auto& tile : player_tiles_[player]) {
    // if a tile is covered, it has no influence
    if (IsCovered(tile->GetPosition())) {
      continue;
    }

    // exert influence on all neighbouring positions
    for (HiveOffset offset : kNeighbourOffsets) {
      HivePosition pos = tile->GetPosition() + offset;

      // 0 out the height, so that stacked tiles influence the ground tiles
      // around them, not tiles floating in air
      player_influence_[player].emplace(pos.Q(), pos.R(), 0);
    }
  }
}


void HexBoard::MaybeUpdateSlideCache() {
  // We need to calculate the valid slidable directions for every HivePosition
  // that is currently reachable. This accounts for every tile in play, as
  // well as all positions surrounding those tiles in play

  if (cache_valid_) {
    return;
  }

  // TODO: make sure cache is calculated properly for heights higher than 0??
  // clear old cached slides
  for (auto& slides : slidability_cache_) {
    slides.second.clear();
  }

  absl::flat_hash_set<HivePosition> visited;
  visited.reserve(player_influence_[kPlayerWhite].size() + player_influence_[kPlayerBlack].size() + played_tiles_.size());
  auto dfs = [&](auto& dfs, HivePosition pos, Direction to_ignore) -> void {
    if (visited.contains(pos) || IsCovered(pos)) {
      return;
    }
    visited.insert(pos);

    // make sure we aren't calculating a position that is currently unreachable
    HiveTileList my_neighbours = NeighboursOf(pos);
    if ((pos.H() == 0 && my_neighbours.size() == 0) ||
        (pos.H() > 0 && !position_cache_.contains(pos - kNeighbourOffsets[Direction::kAbove]))) {
      return;
    }

    // check each direction for slidability. Can never slide above/upwards
    for (uint8_t i = 0; i < Direction::kNumCardinalDirections; ++i) {
      // can't slide into another tile
      HivePosition to_test = pos + kNeighbourOffsets[i];
      if (position_cache_.contains(to_test)) {
        continue;
      }

      // IsGated verifies requirement (3) in GenerateValidSlides()
      if (IsGated(pos, static_cast<Direction>(i))) {
        continue;
      }

      // if on top of the hive, there must also be a tile directly underneath
      //TODO: this is redundant? already covered in ValidClimbs maybe?
      if (to_test.H() > 0) {
        auto it = position_cache_.find({to_test.Q(), to_test.R(), to_test.H() - 1});
        if (it != position_cache_.end()) {
          slidability_cache_[pos].push_back({it->second, static_cast<Direction>(i)});
        }

        // regardless of outcome, we are done
        continue;
      }

      // must have at least one neighbour
      HiveTileList to_neighbours = NeighboursOf(to_test);
      if (to_test.H() > 0 && to_neighbours.size() == 0) {
        continue;
      }

      // the reference tile for this slide is the neighbour of both positions
      bool found = false;
      for (auto& tile : my_neighbours) {
        auto it = std::find(to_neighbours.begin(), to_neighbours.end(), tile);

        if (it != to_neighbours.end() && (*it)->GetPosition() != pos) {
          // if here, sliding around "tile" from "pos" in direction "i" is valid
          slidability_cache_[pos].push_back({tile, static_cast<Direction>(i)});
          found = true;
        }
      }

      if (!found) {
        SpielFatalError(absl::StrCat("HexBoard::MaybeUpdateSlideCache() - "
                        "Can't find reference tile for a slide at position ",
                        pos.ToString(),
                        " and direction ",
                        std::to_string(i)));
      }
        
    }

    // perform search for all neighbours
    // TODO: fix for kAbove
    for (uint8_t i = 0; i < Direction::kNumCardinalDirections; ++i) {
      if (i == to_ignore) {
        continue;
      }

      dfs(dfs, pos + kNeighbourOffsets[i], OppositeDirection(i));
    }
  };

  // traverse across the hive starting at an arbitrary tile
  for (auto& tile : played_tiles_) {
    dfs(dfs, tile->GetPosition(), Direction::kNumAllDirections);
  }

  // std::cout << "\nSlide cache:" << std::endl;
  // std::cout << "  size: " << slidability_cache_.size() << std::endl;
  // for (auto& item : slidability_cache_) {
  //   if (item.second.size() == 0) {
  //     continue;
  //   }
  //   std::cout << item.first << ": [" << std::flush;
  //   for (auto& slide : item.second) {
  //     std::cout << std::to_string(slide.dir) << "->" << slide.ref_tile->ToUHP() << ", " << std::flush;
  //   }
  //   std::cout << "]\n" << std::endl;
  // }
}


// Articulation points in a connected graph are vertices where, when removed,
// separate the graph into multiple components that are no longer connected.
// Tiles at an articulation point are considered "pinned" (and thus, can't be
// moved) as it would separate the hive and invalidate the "One-Hive" rule
// https://en.wikipedia.org/wiki/Biconnected_component
// https://cp-algorithms.com/graph/cutpoints.html
void HexBoard::MaybeUpdateArticulationPoints() {
  if (cache_valid_) {
    return;
  }

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
    for (auto& to_tile : NeighboursOf(vertex)) {
      HivePosition to_vertex = to_tile->GetPosition();
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
  // in play when generating moves, and will never be above the hive
  dfs(dfs, GetTileFromUHP("wQ")->GetPosition(), kNullPosition, true);
}


// the tile(s) used as reference for a Move, from a top-down view.
// tiles on the hive will also reference the tile underneath
std::vector<HexBoard::TileReference> HexBoard::NeighbourReferenceTiles(HivePosition& pos) {
  std::vector<HexBoard::TileReference> refs;
  for (uint8_t d = 0; d < Direction::kNumCardinalDirections; ++d) {
    auto maybe_tile = GetTopTileAt(pos + kNeighbourOffsets[d]);
    if (maybe_tile.has_value()) {
      refs.push_back({*maybe_tile, OppositeDirection(d)});
    }
  }

  return refs;
}




// void HiveTile::GetNeighbours(std::vector<HiveTilePtr>& in_vec) {
//   for (auto ptr : neighbours_) {
//     if (ptr) {
//       in_vec.emplace_back(ptr);
//     }
//   }
// }

// void HiveTile::GetEmptyNeighbours(std::vector<HivePosition>& in_vec) {
//   for (auto ptr : neighbours_) {
//     if (!ptr) {
//       in_vec.emplace_back(ptr->GetPosition());
//     }
//   }
// }

void HiveTile::GetAllAbove(std::vector<HiveTilePtr>& in_vec) {
  for (auto tile : stack_) {
    if (tile) {
      if (tile->GetPosition().H() > pos_.H()) {
        in_vec.emplace_back(tile);
      }
    }
  }
}

void HiveTile::GetAllBelow(std::vector<HiveTilePtr>& in_vec) {
  for (auto tile : stack_) {
    if (tile) {
      if (tile->GetPosition().H() < pos_.H()) {
        in_vec.emplace_back(tile);
      }
    }
  }
}








std::string HiveTile::ToUHP() const {
  SPIEL_CHECK_TRUE(player_ != kInvalidPlayer && type_ != BugType::kNone);
  std::string uhp = "";

  // colour
  player_ == kPlayerWhite ? absl::StrAppend(&uhp, "w") : absl::StrAppend(&uhp, "b");

  // bug type
  switch (type_) {
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
  if (type_ == BugType::kAnt || type_ == BugType::kGrasshopper ||
      type_ == BugType::kSpider || type_ == BugType::kBeetle) {
    absl::StrAppend(&uhp, type_ordinal_);
  }
  
  return uhp;
}

std::string HiveTile::ToUnicode() const {
  SPIEL_CHECK_TRUE(player_ != kInvalidPlayer && type_ != BugType::kNone);
  std::string unicode = "";

  // colour
  player_ == kPlayerWhite ? absl::StrAppend(&unicode, "w") : absl::StrAppend(&unicode, "b");

  // bug type
  switch (type_) {
    case BugType::kQueen:
        absl::StrAppend(&unicode, "🐝");
        break;
    case BugType::kAnt:
        absl::StrAppend(&unicode, "🐜");
        break;
    case BugType::kGrasshopper:
        absl::StrAppend(&unicode, "🦗");
        break;
    case BugType::kSpider:
        absl::StrAppend(&unicode, "🕷️");
        break;
    case BugType::kBeetle:
        absl::StrAppend(&unicode, "🪲");
        break;
    case BugType::kLadybug:
        absl::StrAppend(&unicode, "🐞");
        break;
    case BugType::kMosquito:
        absl::StrAppend(&unicode, "🦟");
        break;
    case BugType::kPillbug:
        absl::StrAppend(&unicode, "🐛");
        break;
  }

  // bug type ordinal (for bugs where there can be more than 1)
  if (type_ == BugType::kAnt || type_ == BugType::kGrasshopper ||
      type_ == BugType::kSpider || type_ == BugType::kBeetle) {
    absl::StrAppend(&unicode, type_ordinal_);
  }

  return unicode;
}


// UHP string representation of a move (there is exactly 1 move per player turn)
std::string Move::ToUHP() {
  // special case: pass for when a player has no possible legal moves 
  if (is_pass) {
    return "pass";
  }

  // special case: for the first turn, there is no reference tile
  if (to == nullptr) {
    return from->ToUHP();
  }

  std::string reference_tile_uhp = to->ToUHP();
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

  return absl::StrCat(from->ToUHP(), " ", offset_formatted);
}



} // namespace hive
} // namespace open_spiel
