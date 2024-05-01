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

  std::cout << "Creating HexBoard size: " << board_radius << std::endl;
  tiles_.reserve(num_tiles_);

  for (auto& direction_list : slidability_cache_) {
    direction_list.second.reserve(Direction::kNumDirections);
  }
}


void HexBoard::CreateTile(const Player player, const BugType type, int ordinal) {
  // allocate a HiveTile object here, and add to relevant mappings
  HiveTilePtr tile = std::make_shared<HiveTile>(kNullPosition, player, type, ordinal);
  tiles_.emplace_back(tile);

  player_tiles_[player].emplace_back(tile);
  type_tiles_[type].emplace_back(tile);
}


void HexBoard::GetNeighbourTilePositions(std::vector<HivePosition>& in_vec,
                                         const HivePosition pos) {
  for (auto offset : kNeighbourOffsets) {
    HivePosition new_pos = pos + offset;

    // is there a tile at this position?
    if (position_cache_.contains(new_pos)) {
        in_vec.push_back(new_pos);
    }
  }
}

void HexBoard::GetNeighbourEmptyPositions(std::vector<HivePosition>& in_vec,
                                          const HivePosition pos) {
  for (auto& offset : kNeighbourOffsets) {
    HivePosition new_pos = pos + offset;

    // is there no tile at this position?
    if (!position_cache_.contains(new_pos)) {
        in_vec.push_back(new_pos);
    }
  }
}


// A tile can be encoded by its position in an array of tiles
// laid out left-to-right containing every piece in the game.
// Conveniently, this is the order the tiles are created in
int HexBoard::EncodeTile(HiveTilePtr& tile, const Player player) const {
  int encoding = -1;

  auto it = std::find(tiles_.begin(), tiles_.end(), tile);
  SPIEL_CHECK_TRUE(it != tiles_.end());
  encoding = std::distance(tiles_.begin(), it);

  // if current player is black, we want black tiles to be indices 0->numtiles/2
  // and white tiles to be numtiles/2->numtiles. So return an offset modulo to
  // represent black's point of view

  // when it is black's turn, the encoding should be as if they were white,
  // so add an offset and return the modulo to represent black's point of view
  // if (player == kPlayerBlack) {
  //   encoding = (encoding + num_tiles_/2) % num_tiles_;
  // }

  return encoding;
}

HiveTilePtr& HexBoard::DecodeTile(int encoding, const Player player) {
  // undo the offset if player is black
  // if (player == kPlayerBlack) {
  //   encoding = (encoding + num_tiles_/2) % num_tiles_;
  // }

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
      if (tile->GetPlayer() != player) {
        continue;
      }

      GenerateMovesFor(out_vec, tile, player);
    }
  }
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
      out.emplace_back(Move{player,
                            tile,
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

      for (int i = 0; i < Direction::kNumDirections; ++i) {
        if (i == Direction::kAbove) {
          continue;
        }

        out.emplace_back(Move{player,
                              tile,
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

        // for completeness, ANY neighbouring tile can be used as the reference
        // excluding kAbove, as you can't place a tile from hand on top of another
        for (uint8_t i = 0; i < Direction::kNumDirections; ++i) {
          if (i == Direction::kAbove) {
            continue;
          }

          HivePosition to_position = pos + kNeighbourOffsets[i];
          if (position_cache_.contains(to_position)) {
            out.emplace_back(Move{player,
                                  tile,
                                  position_cache_.at(to_position),
                                  OppositeDirection(i)});
          }
        }
      }
    }
  }
}


void HexBoard::GenerateMovesFor(std::vector<Move>& out, HiveTilePtr& tile, Player player) {
  std::vector<HivePosition> positions;
  switch (tile->GetBugType()) {
    case BugType::kQueen:
      GenerateValidSlides(positions, tile, 1);
      break;

    case BugType::kAnt:
      GenerateValidSlides(positions, tile, -1);
      break;

    case BugType::kGrasshopper:
      GenerateValidGrasshopperPositions(positions, tile);
      break;

    case BugType::kSpider:
      GenerateValidSlides(positions, tile, 3);
      break;

    case BugType::kBeetle:
      GenerateValidClimbs(positions, tile);
      GenerateValidSlides(positions, tile, 1);
      break;

    case BugType::kMosquito:
      GenerateValidMosquitoPositions(positions, tile);
      break;

    case BugType::kLadybug:
      GenerateValidLadybugPositions(positions, tile);
      break;

    case BugType::kPillbug:
      GenerateValidSlides(positions, tile, 1);
      GenerateValidPillbugSpecials(positions, tile);
      break;
  }

  // turn each position into moves by finding neighbouring tiles as reference
  for (auto& pos : positions) {
    std::cout << pos << ", ";
    for (uint8_t i = 0; i < Direction::kNumDirections; ++i) {
      if (i == Direction::kAbove) {
        continue;
      }

      HivePosition to_position = pos + kNeighbourOffsets[i];
      if (position_cache_.contains(to_position)) {
        out.emplace_back(Move{player,
                              tile,
                              position_cache_.at(to_position),
                              OppositeDirection(i)});
      }
    }
  }
  std::cout << std::endl;
}


void HexBoard::GenerateValidSlides(std::vector<HivePosition>& out, HiveTilePtr& tile, int distance) {
  // In order for a tile to slide in direction D, the following must hold true:
  // 1) The tile must not be "pinned" (i.e. at an articulation point)
  // 2) The tile must be on ground level, or sliding on top of another tile
  // 3) The tile must be able to physically slide into the position without
  //    hitting other tiles. That is, when sliding in direction D, exactly one
  //    of the two adjacent positions (D-1) (D+1) must be empty to physically
  //    move in, and the other position must occupied by another tile in order
  //    to remain attached to the hive at all times (One-Hive rule)

  MaybeUpdateSlideCache();
  
  if (tile->IsPinned()) {
    return;
  }
  
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
      if (slide.ref_tile ==  tile || slide.dir == Direction::kAbove || slide.dir == from) {
        continue;
      }

      search(search, visited, pos + kNeighbourOffsets[slide.dir], OppositeDirection(slide.dir), dist - 1);
    }
  };

  // start search in the directions the slideability cache has already computed
  // TODO: MAKE WORK WITH kABOVE
  HivePosition pos = tile->GetPosition();
  std::cout << tile->ToUHP() << " has " << std::to_string(slidability_cache_[pos].size()) << " slidable positions:" << std::endl;
  for (auto& slide : slidability_cache_[pos]) {
    if (slide.dir == Direction::kAbove) {
      continue;
    }

    // need a fresh visited list for each starting direction
    absl::flat_hash_set<HivePosition> v = {pos};
    search(search, v, pos + kNeighbourOffsets[slide.dir], OppositeDirection(slide.dir), distance - 1);
  }
}


void HexBoard::GenerateValidClimbs(std::vector<HivePosition>& out, HiveTilePtr& tile) {

}


void HexBoard::GenerateValidGrasshopperPositions(std::vector<HivePosition>& out, HiveTilePtr& tile) {
  // in each cardinal direction that contains a tile, jump over all tiles in
  // that direction until reaching an empty space to land
  for (uint8_t d = 0; d < Direction::kAbove; ++d) {
    bool found = false;
    HivePosition to_test = tile->GetPosition() + kNeighbourOffsets[d];
    while (position_cache_.contains(to_test)) {
      to_test += kNeighbourOffsets[d];
      found = true;
    }

    if (found) {
      out.push_back(to_test);
    }
  }
}


void HexBoard::GenerateValidLadybugPositions(std::vector<HivePosition>& out, HiveTilePtr& tile) {

}


void HexBoard::GenerateValidMosquitoPositions(std::vector<HivePosition>& out, HiveTilePtr& tile) {

}


void HexBoard::GenerateValidPillbugSpecials(std::vector<HivePosition>& out, HiveTilePtr& tile) {

}



HiveTileList HexBoard::NeighboursOf(HiveTilePtr& tile, bool ignore_above) {
  return NeighboursOf(tile->GetPosition(), ignore_above);
}

HiveTileList HexBoard::NeighboursOf(HivePosition pos, bool ignore_above) {
  HiveTileList neighbours;
  for (uint8_t i = 0; i < Direction::kNumDirections; ++i) {
    if (ignore_above && i == Direction::kAbove) {
      continue;
    }

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

  // add tile to relevant containers
  played_tiles_.push_back(tile);
  position_cache_.insert({new_pos, tile});
  tile->SetPosition(new_pos);
  UpdateInfluence(tile->GetPlayer());
  last_moved_ = tile;
  cache_valid_ = false;
}



// TODO: Combine MoveTile and PlaceTile
void HexBoard::MoveTile(HiveTilePtr& tile, HivePosition new_pos) {
  SPIEL_CHECK_TRUE(tile);
  SPIEL_CHECK_TRUE(tile->IsInPlay());

  HivePosition old_pos = tile->GetPosition();
  auto handle = position_cache_.extract(old_pos);
  if (handle.empty()) {
    SpielFatalError("HexBoard::MoveTile() - Tile not found in position cache");
  }

  // replace old mapping with new position key
  handle.key() = new_pos;
  position_cache_.insert(std::move(handle));
  tile->SetPosition(new_pos);
  last_moved_ = tile;
  cache_valid_ = false;
  // update influence for the moved tile's player
  // potentially have to update both if the moved tile was part of a stack
  UpdateInfluence(tile->GetPlayer());
  if (old_pos.H() > 0 || new_pos.H() > 0) {
    UpdateInfluence(OtherPlayer(tile->GetPlayer()));
  }
}


const HiveTilePtr& HexBoard::LastMovedTile() const {
  return last_moved_;
}


bool HexBoard::IsInPlay(Player player, BugType type, int ordinal /*= -1*/) const {
  for (auto& tile : played_tiles_) {
    if (player == tile->GetPlayer() && type == tile->GetBugType() && (ordinal == -1 || ordinal == tile->GetOrdinal())) {
      return tile->IsInPlay();
    }
  }

  return false;
}


bool HexBoard::IsPinned(HiveTilePtr& tile) const {
  return tile->IsPinned();
}


bool HexBoard::IsQueenSurrounded(Player player) {
  HiveTilePtr queen = GetTileFromUHP(player == kPlayerWhite ? "wQ" : "bQ");
  return queen->IsInPlay() && NeighboursOf(queen, true).size() == 6;
}


absl::optional<HiveTilePtr> HexBoard::GetTileAt(const HivePosition& pos, bool get_top_tile) {
  auto it = position_cache_.find(pos);
  while (it != position_cache_.end()) {
    if (!get_top_tile) {
      return it->second;
    }
    
    if (IsTopOfStack(it->second)) {
      return it->second;
    } else {
      it = position_cache_.find({pos.Q(), pos.R(), pos.H() + 1});
    }
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


HiveTile& HexBoard::GetTileAbove(const HiveTile& tile) const {
  // TODO: insert return statement here
}


// IsGated verifies requirement (3) in GenerateValidSlides()
bool HexBoard::IsGated(HivePosition pos, Direction d) const {
  bool clockwise = position_cache_.contains(pos + kNeighbourOffsets[ClockwiseDirection(d)]);
  bool counter_clockwise = position_cache_.contains(pos + kNeighbourOffsets[CounterClockwiseDirection(d)]);
  return clockwise == counter_clockwise;
}


bool HexBoard::IsTopOfStack(HiveTilePtr& tile) const {
  return !position_cache_.contains(tile->GetPosition() + kNeighbourOffsets[Direction::kAbove]);
}


// clear and recalculate this tile's player's influence range
void HexBoard::UpdateInfluence(Player player) {
  player_influence_[player].clear();
  for (auto& tile : player_tiles_[player]) {
    // if a tile is covered, it has no influence
    if (!IsTopOfStack(tile)) {
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
  auto dfs = [&](auto& dfs, HivePosition pos, Direction to_ignore) -> void {
    if (visited.contains(pos)) {
      return;
    }

    visited.insert(pos);

    // make sure we aren't calculating a position that is currently unreachable
    // TODO: MAKE SURE THIS WORKS FOR kABOVE
    HiveTileList my_neighbours = NeighboursOf(pos, true);
    if (my_neighbours.size() == 0) {
      return;
    }

    // std::cout << "neighbours of " << pos <<  ": ";
    // for (auto& i : my_neighbours) {
    //   std::cout << i->ToUHP() << ", ";
    // }
    // std::cout << std::endl;

    // check each direction for slidability
    for (uint8_t i = 0; i < Direction::kNumDirections; ++i) {
      // can never slide "above"
      if (i == Direction::kAbove) {
        continue;
      }

      HivePosition to_test = pos + kNeighbourOffsets[i];
      HiveTileList to_neighbours = NeighboursOf(to_test, true);

      if (to_neighbours.size() == 0) {
        continue;
      }

      // can't slide into another tile
      if (position_cache_.contains(to_test)) {
        continue;
      }

      // IsGated verifies requirement (3) in GenerateValidSlides()
      if (IsGated(pos, static_cast<Direction>(i))) {
        continue;
      }

      // if on top of the hive, there must also be a tile directly underneath
      if (to_test.H() > 0 && !position_cache_.contains({to_test.Q(),
                                                        to_test.R(),
                                                        to_test.H() - 1})) {
        continue;                                                
      }

      // the reference tile for this slide is the neighbour of both positions
      bool found = false;
      for (auto& tile : my_neighbours) {
        auto it = std::find(to_neighbours.begin(), to_neighbours.end(), tile);

        if (it != to_neighbours.end() && (*it)->GetPosition() != pos) {
          // if here, sliding around "tile" from "pos" in direction "i" is valid
          slidability_cache_[pos].push_back({static_cast<Direction>(i), tile});
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
    for (uint8_t i = 0; i < Direction::kNumDirections; ++i) {
      if (i == to_ignore || i == Direction::kAbove) {
        continue;
      }

      dfs(dfs, pos + kNeighbourOffsets[i], OppositeDirection(i));
    }
  };

  // traverse across the hive starting at an arbitrary tile
  for (auto& tile : played_tiles_) {
    dfs(dfs, tile->GetPosition(), Direction::kNumDirections);
  }

  cache_valid_ = true;

  std::cout << "\nSlide cache:" << std::endl;
  std::cout << "  size: " << slidability_cache_.size() << std::endl;
  for (auto& item : slidability_cache_) {
    if (item.second.size() == 0) {
      continue;
    }
    std::cout << item.first << ": [" << std::flush;
    for (auto& slide : item.second) {
      std::cout << std::to_string(slide.dir) << "->" << slide.ref_tile->ToUHP() << ", " << std::flush;
    }
    std::cout << "]\n" << std::endl;
  }
}


absl::optional<HiveTilePtr> HexBoard::FindNeighbourTileRef(HivePosition& pos) {
  for (int direction = 0; direction < Direction::kNumDirections; ++direction) {

  }

  for (HiveOffset offset : kNeighbourOffsets) {


    
  }

  return absl::nullopt;
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
