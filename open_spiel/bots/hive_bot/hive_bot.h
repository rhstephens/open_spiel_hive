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

#ifndef OPEN_SPIEL_BOTS_HIVE_BOT_H_
#define OPEN_SPIEL_BOTS_HIVE_BOT_H_

#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "open_spiel/abseil-cpp/absl/strings/ascii.h"
#include "open_spiel/spiel.h"
#include "open_spiel/spiel_bots.h"
#include "open_spiel/spiel_utils.h"

namespace open_spiel {
namespace hive {

class HiveBot : public Bot {
 public:
  HiveBot(GameParameters params, Player player_id);

  void Restart() override;
  Action Step(const State& state) override;

  bool ProvidesPolicy() override { return true; }
  std::pair<ActionsAndProbs, Action> StepWithPolicy(
      const State& state) override;
  ActionsAndProbs GetPolicy(const State& state) override;

  bool IsClonable() const override { return true; }
  std::unique_ptr<Bot> Clone() override {
    return std::make_unique<HiveBot>(*this);
  }
  HiveBot(const HiveBot& other) = default;

 private:
  GameParameters params_;
  const Player player_id_;
  //const GinRummyUtils utils_;

  bool knocked_ = false;
  std::vector<Action> next_actions_;

  std::vector<int> GetBestDeadwood(
      std::vector<int> hand, absl::optional<int> card = absl::nullopt) const;
  int GetDiscard(const std::vector<int>& hand) const;
  std::vector<int> GetMelds(std::vector<int> hand) const;
};

}  // namespace hive
}  // namespace open_spiel

#endif  // OPEN_SPIEL_BOTS_HIVE_BOT_H_
