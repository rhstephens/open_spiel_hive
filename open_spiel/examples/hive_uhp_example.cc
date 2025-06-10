// UHP Engine to play Hive games
// also testing stuff


#include "open_spiel/games/hive/hive.h"

#include <memory>
#include <string>
#include <vector>

#include "open_spiel/abseil-cpp/absl/flags/flag.h"
#include "open_spiel/abseil-cpp/absl/flags/parse.h"
#include "open_spiel/abseil-cpp/absl/strings/str_format.h"
#include "open_spiel/abseil-cpp/absl/strings/str_split.h"
#include "open_spiel/game_parameters.h"
#include "open_spiel/bots/human/human_bot.h"
#include "open_spiel/games/hive/hive_board.h"

#include "open_spiel/spiel.h"

#include "open_spiel/spiel_bots.h"
#include "open_spiel/spiel_utils.h"
#include "open_spiel/tests/console_play_test.h"


ABSL_FLAG(std::string, game, "hive(ansi_color_output=true,fixed_orientation=true)", "The name of the game to play.");

namespace open_spiel {
namespace hive {




} // namespace hive
} // namespace open_spiel

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  
  std::unique_ptr<open_spiel::Bot> human1 = std::make_unique<open_spiel::HumanBot>();
  std::unique_ptr<open_spiel::Bot> human2 = std::make_unique<open_spiel::HumanBot>();

  std::unique_ptr<open_spiel::Bot> randbot = open_spiel::MakeUniformRandomBot(1, 69420);
  std::cout << absl::GetFlag(FLAGS_game) << std::endl;
  std::shared_ptr<const open_spiel::Game> game = open_spiel::LoadGame(absl::GetFlag(FLAGS_game));
  std::unordered_map<open_spiel::Player, std::unique_ptr<open_spiel::Bot>> bots;
  bots.emplace(0, std::make_unique<open_spiel::HumanBot>());
  bots.emplace(1, std::make_unique<open_spiel::HumanBot>());

  open_spiel::testing::ConsolePlayTest(*game, nullptr, nullptr, nullptr);
}
