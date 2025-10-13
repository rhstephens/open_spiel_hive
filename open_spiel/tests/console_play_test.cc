// Copyright 2023 DeepMind Technologies Limited
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

#include "open_spiel/tests/console_play_test.h"

#include <iostream>
#include <memory>
#include <string>
#include <array>

#include "open_spiel/abseil-cpp/absl/algorithm/container.h"
#include "open_spiel/abseil-cpp/absl/strings/ascii.h"
#include "open_spiel/abseil-cpp/absl/strings/numbers.h"
#include "open_spiel/abseil-cpp/absl/strings/match.h"
#include "open_spiel/abseil-cpp/absl/strings/str_join.h"
#include "open_spiel/abseil-cpp/absl/strings/str_split.h"
#include "open_spiel/games/hive/hive.h"
#include "open_spiel/spiel.h"
#include "open_spiel/spiel_utils.h"

namespace open_spiel {
namespace testing {

namespace {
void PrintHelpMenu() {
  std::cout << "Extra commands: " << std::endl;
  std::cout << "  #b: Back one move" << std::endl;
  std::cout << "  #h: Print the history" << std::endl;
  std::cout << "  #l: List legal actions" << std::endl;
  std::cout << "  #q: Quit" << std::endl;
  std::cout << std::endl;
}

void PrintLegals(const std::vector<Action>& legal_actions, const State* state) {
  std::cout << "Legal actions: " << std::endl;
  for (Action action : legal_actions) {
    std::cout << "  " << action << ": "
              << state->ActionToString(state->CurrentPlayer(), action)
              << std::endl;
  }
}

bool ParseCommand(const std::string& line, const Game& game, const State* state,
                  const std::vector<Action>& legal_actions) {
  if (line == "#h") {
    std::cout << "History: " << absl::StrJoin(state->History(), ", ")
              << std::endl;
    return true;
  } else if (line == "#l") {
    PrintLegals(legal_actions, state);
    return true;
  } else {
    return false;
  }
}

}  // namespace

void ConsolePlayTest(
    const Game& game, State* start_state,
    const std::vector<Action>* start_history,
    const std::unordered_map<Player, std::unique_ptr<Bot>>* bots) {
  // Sampled stochastic and simultaneous move games are not yet supported.
  GameType type = game.GetType();
  SPIEL_CHECK_NE(type.chance_mode, GameType::ChanceMode::kSampledStochastic);
  SPIEL_CHECK_NE(type.dynamics, GameType::Dynamics::kSimultaneous);

  std::unique_ptr<hive::HiveState> state;
  if (start_state != nullptr) {
    state.reset(dynamic_cast<hive::HiveState*>(start_state));
  } else {
    state.reset(dynamic_cast<hive::HiveState*>(game.NewInitialState().release()));
  }
  if (start_history != nullptr) {
    for (Action action : *start_history) {
      state->ApplyAction(action);
    }
  }

  bool applied_action = true;

  while (true) {
    if (applied_action) {
      std::cout << state->ToString() << std::endl << std::endl;
    }
    applied_action = false;
    Player player = state->CurrentPlayer();
    std::vector<Action> legal_actions = state->LegalActions();

    if (state->IsTerminal()) {
      std::cout << "Warning! State is terminal. Returns: ";
      for (Player p = 0; p < game.NumPlayers(); ++p) {
        std::cout << state->PlayerReturn(p) << " ";
      }
      std::cout << std::endl;
    }

    if (bots != nullptr && bots->at(player) != nullptr) {
      Action action = bots->at(player)->Step(*state);
      std::cout << "Bot chose action: " << state->ActionToString(player, action)
                << std::endl;
      state->ApplyAction(action);
      applied_action = true;
    } else {
      std::cout << "[Enter move, or press enter for help menu]> ";
      std::string line = "";
      std::getline(std::cin, line);
      absl::StripAsciiWhitespace(&line);
      if (line.empty()) {
        PrintHelpMenu();
      } else if (line == "#b") {
        // Action last_action = state->History().back();
        // new_state = game.NewInitialState();
        // std::vector<Action> history = state->History();
        // for (int i = 0; i < history.size() - 1; ++i) {
        //   new_state->ApplyAction(history[i]);
        // }
        // state = std::move(new_state);
        // std::cout << "Popped action: " << last_action << std::endl;
        // applied_action = true;
      } else if (line == "#q") {
        return;
      } else if (ParseCommand(line, game, state.get(), legal_actions)) {
        // Do nothing, was already handled.
      } else if (line == "#nb") {
        // print neighbours
        // for (auto tile : state->Board().GetPlayedTiles()) {
        //   std::cout << "Neighbours of " << tile.ToUHP() << ": ";
        //   for (auto nb : state->Board().GetNeighboursOf(state->Board().GetPositionOf(tile))) {
        //     if (nb.HasValue()) {
        //      std::cout << nb.ToUHP() << " ";
        //     } else {
        //       std::cout << " - ";
        //     }
        //   }
        //   std::cout << std::endl;
        // }

      } else if (line == "#p") {
        std::cout << state->ToString() << std::endl;
      } else if (line == "#pin") {
        for (hive::HiveTile tile = hive::HiveTile::wQ; tile < hive::HiveTile::kNumTiles; tile = (hive::HiveTile)(tile + 1)) {
          if (state->Board().IsInPlay(tile)) {
            if (state->Board().IsPinned(tile)) {
              std::cout << tile.ToUHP() << std::endl;
            } 
          }
        }
      } else if (line == "#wQ") {
        std::cout << state->PrintBoard(hive::HiveTile::wQ) << std::endl;
      } else if (line == "#wA1") {
        std::cout << state->PrintBoard(hive::HiveTile::wA1) << std::endl;
      } else if (line == "#wA2") {
        std::cout << state->PrintBoard(hive::HiveTile::wA2) << std::endl;
      } else if (line == "#wA3") {
        std::cout << state->PrintBoard(hive::HiveTile::wA3) << std::endl;
      } else if (line == "#wG1") {
        std::cout << state->PrintBoard(hive::HiveTile::wG1) << std::endl;
      } else if (line == "#wG2") {
        std::cout << state->PrintBoard(hive::HiveTile::wG2) << std::endl;
      } else if (line == "#wG3") {
        std::cout << state->PrintBoard(hive::HiveTile::wG3) << std::endl;
      } else if (line == "#wS1") {
        std::cout << state->PrintBoard(hive::HiveTile::wS1) << std::endl;
      } else if (line == "#wS2") {
        std::cout << state->PrintBoard(hive::HiveTile::wS2) << std::endl;
      } else if (line == "#wB1") {
        std::cout << state->PrintBoard(hive::HiveTile::wB1) << std::endl;
      } else if (line == "#wB2") {
        std::cout << state->PrintBoard(hive::HiveTile::wB2) << std::endl;
      } else if (line == "#wM") {
        std::cout << state->PrintBoard(hive::HiveTile::wM) << std::endl;
      } else if (line == "#wL") {
        std::cout << state->PrintBoard(hive::HiveTile::wL) << std::endl;
      } else if (line == "#wP") {
        std::cout << state->PrintBoard(hive::HiveTile::wP) << std::endl;
      } else if (line == "#bQ") {
        std::cout << state->PrintBoard(hive::HiveTile::bQ) << std::endl;
      } else if (line == "#bA1") {
        std::cout << state->PrintBoard(hive::HiveTile::bA1) << std::endl;
      } else if (line == "#bA2") {
        std::cout << state->PrintBoard(hive::HiveTile::bA2) << std::endl;
      } else if (line == "#bA3") {
        std::cout << state->PrintBoard(hive::HiveTile::bA3) << std::endl;
      } else if (line == "#bG1") {
        std::cout << state->PrintBoard(hive::HiveTile::bG1) << std::endl;
      } else if (line == "#bG2") {
        std::cout << state->PrintBoard(hive::HiveTile::bG2) << std::endl;
      } else if (line == "#bG3") {
        std::cout << state->PrintBoard(hive::HiveTile::bG3) << std::endl;
      } else if (line == "#bS1") {
        std::cout << state->PrintBoard(hive::HiveTile::bS1) << std::endl;
      } else if (line == "#bS2") {
        std::cout << state->PrintBoard(hive::HiveTile::bS2) << std::endl;
      } else if (line == "#bB1") {
        std::cout << state->PrintBoard(hive::HiveTile::bB1) << std::endl;
      } else if (line == "#bB2") {
        std::cout << state->PrintBoard(hive::HiveTile::bB2) << std::endl;
      } else if (line == "#bM") {
        std::cout << state->PrintBoard(hive::HiveTile::bM) << std::endl;
      } else if (line == "#bL") {
        std::cout << state->PrintBoard(hive::HiveTile::bL) << std::endl;
      } else if (line == "#bP") {
        std::cout << state->PrintBoard(hive::HiveTile::bP) << std::endl;
      } else if (line == "#") {
        
      } else if (line == "#") {
        
      } else if (line == "#uhp") {
        std::cout << state->Serialize() << std::endl;
      } else if (absl::StrContains(line, "#import")) {
        line = line.substr(8);
        state.reset(dynamic_cast<hive::HiveState*>(hive::DeserializeUHPGameAndState(line, true).second.release()));
      } else if (line == "#") {
        
      } else if (line == "#") {
        
      } else if (line == "#") {
      
      } else {
        Action action;
        bool valid_integer = absl::SimpleAtoi(line, &action);
        if (valid_integer) {
          auto iter = absl::c_find(legal_actions, action);
          SPIEL_CHECK_TRUE(iter != legal_actions.end());
          state->ApplyAction(action);
          applied_action = true;
        } else {
          // Search for the move string.
          for (Action action : legal_actions) {
            if (line == state->ActionToString(player, action)) {
              state->ApplyAction(action);
              applied_action = true;
              break;
            }
          }
        }
      }
    }
  }

  std::cout << "Terminal state:" << std::endl
            << std::endl
            << state->ToString() << std::endl;
  std::cout << "Returns: ";
  std::vector<double> returns = state->Returns();
  for (Player p = 0; p < game.NumPlayers(); ++p) {
    std::cout << returns[p] << " ";
  }
  std::cout << std::endl;
}

}  // namespace testing
}  // namespace open_spiel
