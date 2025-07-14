// Copyright 2021 DeepMind Technologies Limited
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

#include <cstdlib>
#include <string>
#include <vector>
#include <torch/torch.h>
#include <chrono>
#include <iostream>

#include "open_spiel/algorithms/alpha_zero_torch/vpnet.h"
#include "open_spiel/abseil-cpp/absl/flags/flag.h"
#include "open_spiel/abseil-cpp/absl/flags/parse.h"
#include "open_spiel/abseil-cpp/absl/strings/str_split.h"
#include "open_spiel/games/hive/hive.h"
#include "open_spiel/spiel_utils.h"
#include "open_spiel/utils/file.h"
#include "open_spiel/utils/init.h"

ABSL_FLAG(std::string, game, "hive(board_size=14,fixed_orientation=true)", "Name of the game with applicable parameters.");
ABSL_FLAG(int, batch_size, 1 << 10, "Number of states to save per file.");


namespace open_spiel {
namespace algorithms {
namespace torch_az {


int CreateTrajectories(std::vector<std::string> game_strings) {
  
  
  return 0;
}


} // torch_az
} // algorithms
} // open_spiel


// Create a trajectories dataset from provided list of UHP games
int main(int argc, char** argv) {
  // open_spiel::Init("", &argc, &argv, true);

  // std::vector<char*> positional_args = absl::ParseCommandLine(argc, argv);
  // SPIEL_CHECK_TRUE(positional_args.size() > 1);
  // std::string uhp_path = positional_args[1];

  // std::vector<std::string> game_strings = 
  //   absl::StrSplit(open_spiel::file::ReadContentsFromFile(uhp_path, "r"), "\n");

  // std::cout << "Creating trajectories from " << std::to_string(game_strings.size()) << " games." << std::endl;
  // std::cout << game_strings[0] << std::endl;
  
  // open_spiel::algorithms::torch_az::CreateTrajectories(game_strings);

  // return 0;

  torch::Device device(torch::kCUDA);

    const int batch_size = 32;
    const int in_channels = 256;
    const int out_channels = 256; // Typical for residual blocks
    const int height = 8;
    const int width = 8;
    const int kernel_size = 3;
    const int iterations = 100;

    auto input = torch::randn({batch_size, in_channels, height, width}, device);

    // Convolution + BatchNorm + ReLU layers
    auto conv1 = torch::nn::Conv2d(torch::nn::Conv2dOptions(in_channels, out_channels, kernel_size).padding(1).bias(false));

    auto bn1 = torch::nn::BatchNorm2d(out_channels);

    auto conv2 = torch::nn::Conv2d(torch::nn::Conv2dOptions(out_channels, out_channels, kernel_size).padding(1).bias(false));
    auto bn2 = torch::nn::BatchNorm2d(out_channels);

    conv1->to(device);
    bn1->to(device);
    conv2->to(device);
    bn2->to(device);

    conv1->train();
    bn1->train();
    conv2->train();
    bn2->train();

    // Warm-up to stabilize cuDNN kernel selection
    for (int i = 0; i < 10; ++i) {
        auto x = conv1->forward(input);
        x = bn1->forward(x);
        x = torch::relu(x);
        x = conv2->forward(x);
        x = bn2->forward(x);
        x = torch::relu(x);
    }
    torch::cuda::synchronize();

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; ++i) {
        auto x = conv1->forward(input);
        x = bn1->forward(x);
        x = torch::relu(x);
        x = conv2->forward(x);
        x = bn2->forward(x);
        x = torch::relu(x);

        // Simulate global average pooling over spatial dimensions (value head)
        auto y = torch::adaptive_avg_pool2d(x, {1, 1});
        y = y.view({batch_size, -1}); // Flatten to [batch_size, channels]
    }
    torch::cuda::synchronize();

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;

    std::cout << "Time for " << iterations << " AlphaZero-like forward passes: "
              << elapsed.count() << " seconds" << std::endl;
    std::cout << "Average per pass: "
              << (elapsed.count() / iterations) * 1000 << " ms" << std::endl;

    return 0;

  
}
