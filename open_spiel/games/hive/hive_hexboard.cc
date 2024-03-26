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
HexBoard::HexBoard(std::shared_ptr<const HiveGame> game,
                   const int board_radius, const int num_stackable /*= 6*/)
    : game_(game),
      board_radius_(board_radius),
      num_stackable_(num_stackable),
      num_tiles_(num_stackable + (3*board_radius*board_radius) + (3*board_radius) + 1) {

    //
    all_tiles_.reserve(num_tiles_);
    for (int i = 0; i < num_tiles_; ++i) {
      all_tiles_.emplace(kNullPosition, BugType::kNone, kInvalidPlayer);
    }
  }


HiveOffset HivePosition::DistanceTo(const HivePosition& other) const {
  return {};
}

std::string HiveTile::ToUHP() const {
  std::string uhp{};

  // colour 
  switch (player_) {
    case kPlayerWhite:
      absl::StrAppend(&uhp, "w");
      break;
    case kPlayerBlack:
      absl::StrAppend(&uhp, "b");
      break;
    default:
      SpielFatalError("HiveTile::ToUHP() called on tile with invalid Player");
  }

  // bug type
  absl::StrAppend(&uhp, kBugTypeToUHP.at(type_));

  // bug count (empty if there is only one instance of such bug)


  uhp += player_ == 0 ? "w" : "b";
  return uhp;
}

size_t HiveTile::HashValue() const {
  return 0;
}

HiveTile& HexBoard::GetNeighbourFrom(const HivePosition& pos, const HiveOffset& offset) const {
  // TODO: insert return statement here
}


HiveTile& HexBoard::GetTileAbove(const HiveTile& tile) const {
  // TODO: insert return statement here
}

} // namespace hive
} // namespace open_spiel
