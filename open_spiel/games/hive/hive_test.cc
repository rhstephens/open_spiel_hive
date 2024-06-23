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

#include "open_spiel/spiel.h"
#include "open_spiel/spiel_utils.h"
#include "open_spiel/tests/basic_tests.h"
#include "open_spiel/tests/console_play_test.h"
#include "open_spiel/games/hive/hive.h"

namespace open_spiel {
namespace hive {
namespace {

namespace testing = open_spiel::testing;

void MyTests() {
  std::shared_ptr<const Game> game = LoadGame("hive(board_size=8)");
  std::unique_ptr<HiveState> state(dynamic_cast<HiveState*>(game->NewInitialState().release()));
  std::unique_ptr<HiveState> cloned_before(dynamic_cast<HiveState*>(state->Clone().release()));


  // JUST RANDOM MOVES
  std::mt19937 rng;
  double num_sims = 1000;
  double draws = 0;
  double white_wins = 0;
  double black_wins = 0;
  double turn_lengths = 0;
  double max_radii = 0;
  double overflowed = 0;
  for (int i = 0; i < num_sims; ++i) {
    auto whatever = std::chrono::system_clock::now().time_since_epoch();
    rng.seed(std::chrono::duration_cast<std::chrono::milliseconds>(whatever).count());

    std::unique_ptr<HiveState> s(dynamic_cast<HiveState*>(game->NewInitialState().release()));
    while (!s->IsTerminal()) {
      std::vector<Action> actions = s->LegalActions();
      std::uniform_int_distribution<int> dis(0, actions.size() - 1);
      Action action = actions[dis(rng)];
      s->ApplyAction(action);
    }

    turn_lengths += s->MoveNumber();
    max_radii += s->Board().largest_radius;
    if (s->Board().largest_radius > s->Board().Radius()) {
      overflowed++;
    }

    if (s->Returns()[0] > 0) {
      ++white_wins;
    } else if (s->Returns()[1] > 0) {
      ++black_wins;
    } else {
      ++draws;
    }
  }

  // std::cout << std::setprecision(6);
  std::cout << "white wins: " << white_wins << " (" << (white_wins / num_sims) * 100.0f << "%)" << std::endl;
  std::cout << "black wins: " << black_wins << " (" << (black_wins / num_sims) * 100.0f << "%)" << std::endl;
  std::cout << "num draws: " << draws << " (" << (draws / num_sims) * 100.0f << "%)" << std::endl;
  std::cout << "num games overflowed: " << overflowed << " (" << (overflowed / num_sims) * 100.0f << "%)" << std::endl;
  std::cout << "avg turn#: " << (turn_lengths / num_sims) << std::endl;
  std::cout << "avg radius: " << (max_radii / num_sims) << std::endl;


  return;



  

  // test cloning/copying states
  std::cout << "***initial board***" << std::endl;
  state->ApplyAction(state->StringToAction(state->CurrentPlayer(), "wG3"));
  state->ApplyAction(state->StringToAction(state->CurrentPlayer(), "bL wG3-"));
  state->ApplyAction(state->StringToAction(state->CurrentPlayer(), "wA1 -wG3"));
  state->ApplyAction(state->StringToAction(state->CurrentPlayer(), "bQ bL/"));
  state->ApplyAction(state->StringToAction(state->CurrentPlayer(), "wQ -wA1"));
  state->ApplyAction(state->StringToAction(state->CurrentPlayer(), "bA1 bL-"));
  state->ApplyAction(state->StringToAction(state->CurrentPlayer(), "wQ /wA1"));
  std::cout << state->ToString() << std::endl;

  std::unique_ptr<HiveState> cloned(dynamic_cast<HiveState*>(state->Clone().release()));

  std::cout << "***initial board***" << std::endl;
  state->ApplyAction(state->StringToAction(state->CurrentPlayer(), "bP \\bQ"));
  std::cout << state->ToString() << std::endl;

  std::cout << "***cloned board***" << std::endl;
  cloned->ApplyAction(cloned->StringToAction(cloned->CurrentPlayer(), "bP bQ-"));
  std::cout << cloned->ToString() << std::endl;

  std::cout << "***cloned before board***" << std::endl;
  std::cout << cloned_before->ToString() << std::endl;
  std::cout << "Cur player: " << cloned_before->CurrentPlayer() << std::endl;
  std::cout << "Num moves: " << cloned_before->MoveNumber() << std::endl;
  std::cout << "Played tiles: " << cloned_before->Board().IsInPlay(hive::NewHiveTile::UHPToTile("wG3")) << std::endl;

  SPIEL_CHECK_TRUE(state->History() != cloned->History());
}

void BasicHiveTests() {
  // std::cout << "**Begin Chess**" << std::endl;
  // std::flush(std::cout);
  // testing::RandomSimTest(*LoadGame("chess"), 100, false, false);
  // std::cout << "**End Chess**" << std::endl;
  // std::flush(std::cout);
  // std::cout << "**Begin TicTacToe**" << std::endl;
  // std::flush(std::cout);
  // testing::RandomSimTest(*LoadGame("tic_tac_toe"), 100, false, false);
  // std::cout << "**End TicTacToe**" << std::endl;
  // std::flush(std::cout);
  testing::RandomSimTest(*LoadGame("hive"), 10, false);
  std::cout << "**Begin Hive100**" << std::endl;
  std::flush(std::cout);
  testing::RandomSimTest(*LoadGame("hive"), 100, false, false);
  std::cout << "**End Hive100**" << std::endl;


  std::cout << "**Begin Test**" << std::endl;
  std::shared_ptr<const Game> game = LoadGame("hive(board_size=8)");
  std::unique_ptr<State> state = game->NewInitialState();

  // HivePosition p = {0,2,1};
  // std::shared_ptr<HivePosition> pp = std::make_shared<HivePosition>(1,2,3);
  // std::vector<HivePosition> vec = {{0, 0, 1}, {0,1,0}};
  std::cout << "sizeof(HivePosition): " << sizeof(HivePosition) << std::endl;
  std::cout << "sizeof(NewHiveTile): " << sizeof(NewHiveTile) << std::endl;
  std::cout << "sizeof(void*): " << sizeof(void*) << std::endl;
  std::cout << "sizeof(HexBoard): " << sizeof(HexBoard) << std::endl;

  std::cout << "**Testing action to string mappings**" << '\n';
  for (int i = 0; i < state->NumDistinctActions(); ++ i) {
    std::string a_to_s = state->ActionToString(i);
    SPIEL_CHECK_EQ(i, state->StringToAction(a_to_s));

    //std::cout << i << ": " << a_to_s << '\n';
  }
  std::cout << "Action to String mapping passed!" << std::endl;

  // while(true) {
  //   std::cout << "[Enter move string or action number]> ";f
  //   std::string line = "";
  //   std::getline(std::cin, line);
  //   absl::StripAsciiWhitespace(&line);

  //   Action action;
  //   bool valid_integer = absl::SimpleAtoi(line, &action);
  //   if (valid_integer) {
  //     std::cout << state->ActionToString(action) << std::endl;
  //   } else {
  //     std:: cout << state->StringToAction(line) << std::endl;
  //   }
  // }
  

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

void Playtest() {
  testing::ConsolePlayTest(*LoadGame("hive(board_size=8)"));
}


// Queen moves
const char* game_string1 = "Base+MLP;InProgress;White[12];wG1;bG1 wG1-;wQ \\wG1;bQ bG1-;wG2 /wG1;bA1 bQ/;wG3 /wG2;bA1 bQ-;wB1 -wG3;bA1 bQ/;wB2 \\wB1;bA1 bQ-;wS1 \\wB2;bA1 bQ/;wS2 wS1/;bA1 bQ-;wA1 \\wS2;bA1 bQ/;wA2 wA1/;bA1 bQ-;wA3 wA2/;bA1 wA3-";
const char* moves1 = "wQ \\bG1;wQ -wG1";

// Grasshopper moves
const char* game_string2 = "Base+MLP;InProgress;White[11];wG1;bG1 wG1-;wQ /wG1;bQ bG1-;wS1 wQ\\;bA1 bQ-;wB1 /wS1;bA1 -wQ;wB1 wS1\\;bA2 bQ-;wB1 /wS1;bA2 wG1\\;wB1 wS1\\;bA3 bQ-;wB1 /wS1;bS1 bQ\\;wB1 wS1;bS1 wB1\\;wB1 /wB1;bA3 -wB1";
const char* moves2 = "wG1 bQ-;wG1 bA2\\;wG1 bA1\\;wG2 \\wG1;wS2 \\wG1;wA1 \\wG1;wB2 \\wG1";

// Ant moves
const char* game_string3 = "Base+MLP;InProgress;White[13];wS1;bB1 wS1-;wQ -wS1;bQ bB1-;wB1 \\wQ;bG1 bQ/;wB2 \\wB1;bG2 bG1/;wS2 \\wB2;bS1 bG2/;wA1 \\wS1;bB2 bS1/;wA2 \\wS2;bG3 \\bB2;wA1 -bG1;bA1 \\bG3;wG1 wA2/;bS2 -bA1;wG2 wG1/;bA2 -bS2;wA3 wG2-;bA3 bS2\\;wG3 wA3\\;bA3 wG3\\";
const char* moves3 = "wA1 -bG2;wA1 -bS1;wA1 /bG3;wA1 bS2\\;wA1 bA2\\;wA1 /bA2;wA1 bA3-;wA1 bA3\\;wA1 /bA3;wA1 /wG3;wA1 wG2\\;wA1 wG1\\;wA1 wB2/;wA1 wB1/;wA1 \\wS1;wA1 \\bB1";

// Spider moves
const char* game_string4 = "Base+MLP;InProgress;White[12];wG1;bA1 wG1-;wS1 \\wG1;bQ bA1-;wQ /wG1;bG1 bQ\\;wG2 wQ\\;bB1 /bG1;wB1 /wG2;bG2 bG1\\;wG3 /wB1;bG2 -bB1;wB2 wG3\\;bA1 bG1\\;wA1 wB2-;bA1 bB1\\;wA2 wA1/;bA1 bG1-;wS2 wA2-;bA1 bG1\\;wA3 wS2\\;bA1 wA3-";
const char* moves4 = "wS1 \\bQ;wS1 /bQ;wS1 wG1\\;wS1 /wQ";

// Spider moves 2
const char* game_string5 = "Base+MLP;InProgress;White[12];wG1;bA1 wG1/;wB1 /wG1;bA2 bA1-;wQ wB1\\;bQ bA2\\;wB2 /wQ;bG1 bQ\\;wS1 wG1\\;bB1 /bG1;wG2 /wB2;bG2 bG1\\;wG3 wG2\\;bG2 wS1\\;wA1 wG3-;bA1 -wB1;wS2 wA1/;bA3 bG1\\;wA2 wS2-;bA2 \\wG1;wA3 wA2\\;bA3 wA3-";
const char* moves5 = "wS1 bA2/;wS1 bQ/;wS1 wG1/;wS1 \\bQ";

// Beetle moves
const char* game_string6 = "Base+MLP;InProgress;White[12];wB1;bB1 wB1-;wQ \\wB1;bQ bB1/;wG1 /wB1;bB2 bB1\\;wA1 /wG1;bA1 bQ\\;wG2 -wA1;bQ \\bB1;wB2 /wG2;bA2 \\bA1;wG3 wB2\\;bA2 \\wQ;wA2 wG3-;bB2 wB1\\;wS1 wA2\\;bA1 bB1\\;wS2 wS1-;bA1 bB1-;wA3 wS2/;bA1 \\wA3";
const char* moves6 = "wB1 wQ;wB1 bQ;wB1 bB1;wB1 bB2;wB1 wG1";

// Mosquito moves
const char* game_string7 = "Base+M;InProgress;White[13];wM;bG1 wM-;wS1 /wM;bQ bG1-;wQ /wS1;bB1 bG1\\;wB1 /wQ;bB1 wM\\;wS2 /wB1;bA1 bQ-;wB2 wS2\\;bA1 bQ\\;wG1 wB2-;bA1 bQ-;wG2 wG1/;bA1 bQ\\;wG3 wG2/;bA1 bQ-;wA1 wG3-;bA1 bQ/;wA2 wA1-;bA1 bQ-;wA3 wA2\\;bA1 /wA3";
const char* moves7 = "wM bQ-;wM bB1\\;wM /wS2;wM \\bG1;wM bG1;wM bB1;wM wS1;wM \\wS1;wM bQ/;wM -wQ";

// Ladybug moves
const char* game_string8 = "Base+L;InProgress;White[14];wL;bL wL/;wQ -wL;bQ bL/;wQ -bL;bA1 bQ/;wB1 \\wQ;bA1 bQ-;wS1 \\wB1;bA1 bQ/;wB2 \\wS1;bA1 bQ-;wS2 wB2/;bA1 bQ/;wA1 wS2-;bA1 bQ-;wG1 wA1/;bA1 bQ/;wG2 wG1-;bA1 bQ-;wA2 wG2\\;bA1 bQ/;wA3 wA2-;bA1 bQ-;wG3 wA3/;bA1 \\wG3";
const char* moves8 = "wL wB1/;wL -bQ;wL /wB1;wL /wS1;wL bQ\\;wL bL\\;wL \\bQ;wL bQ/;wL bQ-;wL /wQ";

// Pillbug can't throw last move.
const char* game_string9 = "Base+P;InProgress;White[15];wP;bS1 wP-;wQ /wP;bQ bS1-;wB1 -wQ;bB1 bS1\\;wG1 wB1\\;bB1 wP\\;wS1 wG1\\;bQ bS1/;wB1 -wP;bB1 wQ;wG2 wS1\\;bB1 wB1;wG3 wG2\\;bA1 bQ\\;wS2 wG3-;bA1 bS1\\;wA1 wS2/;bA1 bQ\\;wA2 wA1/;bA1 bS1\\;wA3 wA2/;bA1 bQ\\;wB2 wA3/;bA1 wB2/;pass;bQ \\bS1";
const char* moves9 = "bS1 -bQ;bS1 wP\\";



}  // namespace
}  // namespace hive
}  // namespace open_spiel

int main(int argc, char** argv) {
  // open_spiel::hive::BasicHiveTests();
  open_spiel::hive::MyTests();
  //open_spiel::hive::Playtest();
}
