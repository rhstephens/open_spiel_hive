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

#include "open_spiel/abseil-cpp/absl/algorithm/container.h"
#include "open_spiel/abseil-cpp/absl/strings/ascii.h"
#include "open_spiel/abseil-cpp/absl/strings/numbers.h"
#include "open_spiel/abseil-cpp/absl/strings/str_join.h"
#include "open_spiel/spiel.h"
#include "open_spiel/spiel_utils.h"
#include "open_spiel/bots/hive_bot/hive_bot.h"

namespace open_spiel {
namespace hive {

HiveBot::HiveBot(GameParameters params, Player player_id) : params_(params), player_id_(player_id) {

}

void HiveBot::Restart() {

}

Action HiveBot::Step(const State& state) {
  return 0;
}

bool HiveBot::ProvidesPolicy() { return true; }

std::pair<ActionsAndProbs, Action> HiveBot::StepWithPolicy(const State& state) {
  return {};
}

ActionsAndProbs HiveBot::GetPolicy(const State& state) {

}

bool HiveBot::IsClonable() const { return true; }
std::unique_ptr<Bot> HiveBot::Clone() {
  return std::make_unique<HiveBot>(*this);
}

}  // namespace hive
}  // namespace open_spiel
