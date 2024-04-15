// Copyright 2019 DeepMind Technologies Limited
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

#include "open_spiel/spiel.h"
#include "open_spiel/spiel_utils.h"
#include "open_spiel/tests/basic_tests.h"
#include "open_spiel/tests/console_play_test.h"
#include "open_spiel/games/hive/hive.h"

namespace open_spiel {
namespace hive {
namespace {

namespace testing = open_spiel::testing;

void BasicHiveTests() {

  std::cout << "**Begin Test**" << std::endl;
  std::shared_ptr<const Game> game = LoadGame("hive(board_size=8)");
  std::unique_ptr<State> state = game->NewInitialState();

  std::cout << "**Testing action to string mappings**" << '\n';
  for (int i = 0; i < state->NumDistinctActions(); ++ i) {
    std::string a_to_s = state->ActionToString(i);
    SPIEL_CHECK_EQ(i, state->StringToAction(a_to_s));

    std::cout << i << ": " << a_to_s << '\n';
  }
  std::cout << std::endl;

  while(true) {
    std::cout << "[Enter move string or action number]> ";
    std::string line = "";
    std::getline(std::cin, line);
    absl::StripAsciiWhitespace(&line);

    Action action;
    bool valid_integer = absl::SimpleAtoi(line, &action);
    if (valid_integer) {
      std::cout << state->ActionToString(action) << std::endl;
    } else {
      std:: cout << state->StringToAction(line) << std::endl;
    }
  }
  

  testing::ConsolePlayTest(*LoadGame("hive(board_size=8)"));

  std::cout << "**End Test**" << std::endl;



  // TODO REMOVE
  return; 

  testing::LoadGameTest("hive(board_size=8)");
  testing::NoChanceOutcomesTest(*LoadGame("hive(board_size=8)"));
  testing::RandomSimTest(*LoadGame("hive"), 1);

  // All the sizes we care about.
  for (int i = 3; i <= 13; i++) {
    testing::RandomSimTest(
        *LoadGame(absl::StrCat("hive(board_size=", i, ")")), 10);
  }

  // Run many tests hoping swap happens at least once.
  testing::RandomSimTest(*LoadGame("hive(board_size=3,swap=True)"), 20);

  // Ansi colors!
  testing::RandomSimTest(
      *LoadGame("hive", {{"board_size", GameParameter(6)},
                             {"ansi_color_output", GameParameter(true)}}),
      3);
  testing::RandomSimTest(
      *LoadGame("hive(board_size=5,ansi_color_output=True)"), 3);
}

}  // namespace
}  // namespace hive
}  // namespace open_spiel

int main(int argc, char** argv) { open_spiel::hive::BasicHiveTests(); }
