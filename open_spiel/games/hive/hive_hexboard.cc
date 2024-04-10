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
HexBoard::HexBoard(std::shared_ptr<const Game> game,
                   const int board_radius, const int num_stackable /*= 6*/)
    : game_(game),
      board_radius_(board_radius),
      num_stackable_(num_stackable),
      num_tiles_(28) { // constant 28 for now?

  tiles_.reserve(num_tiles_);
}


void HexBoard::CreateTile(const Player player, const BugType type, const int ordinal) {
  // allocate a HiveTile object here, and add to relevant mappings
  HiveTilePtr tile = std::make_shared<HiveTile>(kNullPosition, player, type, ordinal);
  tiles_.emplace_back(tile);

  player_tiles_[player].emplace(tile);
  type_tiles_[type].emplace(tile);
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
// Conveniently, this is the order the tiles were created in
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
  if (player == 1) {
    encoding = (encoding + num_tiles_/2) % num_tiles_;
  }

  return encoding;
}

HiveTilePtr& HexBoard::DecodeTile(int encoding, const Player player) {
  // undo the offset if player is black
  if (player == 1) {
    encoding = (encoding + num_tiles_/2) % num_tiles_;
  }

  SPIEL_CHECK_TRUE(encoding >= 0 && encoding < num_tiles_);
  return tiles_.at(encoding);
}












void HiveTile::GetNeighbours(std::vector<HiveTilePtr>& in_vec) {
  for (auto ptr : neighbours_) {
    if (ptr) {
      in_vec.emplace_back(ptr);
    }
  }
}

void HiveTile::GetEmptyNeighbours(std::vector<HivePosition>& in_vec) {
  for (auto ptr : neighbours_) {
    if (!ptr) {
      in_vec.emplace_back(ptr->GetPosition());
    }
  }
}

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
  player_ == 0 ? absl::StrAppend(&uhp, "w") : absl::StrAppend(&uhp, "b");

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
  player_ == 0 ? absl::StrAppend(&unicode, "w") : absl::StrAppend(&unicode, "b");

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
        absl::StrAppend(&unicode, "🕷");
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

// size_t HiveTile::HashValue() const {
//   return 0;
// }


void HexBoard::MoveTile(HiveTilePtr& tile, HivePosition new_position) {
  if (!tile) {
    SpielFatalError("");
  }

  // remove old mapping, add new
  position_cache_.erase(tile->GetPosition());
  tile->SetPosition(new_position);
  position_cache_[new_position] = tile;

  //TODO: Update the neighbours of old and new positions
}



// HiveTile& HexBoard::GetNeighbourFrom(const HivePosition& pos, const HiveOffset& offset) const {
//   // TODO: insert return statement here
// }


HiveTile& HexBoard::GetTileAbove(const HiveTile& tile) const {
  // TODO: insert return statement here
}

} // namespace hive
} // namespace open_spiel
