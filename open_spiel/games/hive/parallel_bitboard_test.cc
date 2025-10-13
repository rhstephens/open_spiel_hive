// Copyright 2025 DeepMind Technologies Limited
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

#include "open_spiel/games/hive/hive_parallel_bitboard.h"
#include "open_spiel/games/hive/hive_board.h"

#include <bitset>
#include <chrono>
#include <random>
#include <string>
#include <vector>

#include "open_spiel/spiel_utils.h"

namespace open_spiel {
namespace hive {
namespace {

void TestBitboardConstruction() {
  HexBitboard32x32 bitboard;
  SPIEL_CHECK_TRUE(bitboard.none());
  SPIEL_CHECK_FALSE(bitboard.any());
  SPIEL_CHECK_EQ(bitboard.popcount(), 0);
  SPIEL_CHECK_EQ(bitboard.find_first(), size_t(-1));
  SPIEL_CHECK_EQ(bitboard.find_last(), size_t(-1));

  HexBitboard32x32 one_bit(69);
  SPIEL_CHECK_TRUE(one_bit.test(69));
  one_bit.clear(69);
  SPIEL_CHECK_TRUE(one_bit.none());
}

void TestBitboardCopyConstructor() {
  HexBitboard32x32 original;
  original.set(0);
  original.set(31);
  original.set(1023);  // Last bit

  HexBitboard32x32 copy(original);
  SPIEL_CHECK_TRUE(copy == original);
  SPIEL_CHECK_TRUE(copy.test(0));
  SPIEL_CHECK_TRUE(copy.test(31));
  SPIEL_CHECK_TRUE(copy.test(1023));
}

void TestBitboardAssignment() {
  HexBitboard32x32 original;
  original.set(100);
  original.set(500);

  HexBitboard32x32 assigned;
  assigned = original;
  SPIEL_CHECK_TRUE(assigned == original);
  SPIEL_CHECK_TRUE(assigned.test(100));
  SPIEL_CHECK_TRUE(assigned.test(500));
}

void TestBitManipulation() {
  HexBitboard32x32 bitboard;
  
  // Test set
  bitboard.set(0);
  SPIEL_CHECK_TRUE(bitboard.test(0));
  SPIEL_CHECK_TRUE(bitboard.any());
  SPIEL_CHECK_FALSE(bitboard.none());
  
  bitboard.set(31);
  SPIEL_CHECK_TRUE(bitboard.test(31));
  
  bitboard.set(1023);  // Last bit
  SPIEL_CHECK_TRUE(bitboard.test(1023));
  
  // Test clear
  bitboard.clear(0);
  SPIEL_CHECK_FALSE(bitboard.test(0));
  SPIEL_CHECK_TRUE(bitboard.test(31));
  SPIEL_CHECK_TRUE(bitboard.test(1023));
  
  // Test flip
  bitboard.flip(0);
  SPIEL_CHECK_TRUE(bitboard.test(0));
  bitboard.flip(0);
  SPIEL_CHECK_FALSE(bitboard.test(0));
  
  // Test reset
  bitboard.reset();
  SPIEL_CHECK_TRUE(bitboard.none());
  SPIEL_CHECK_FALSE(bitboard.any());
}

void TestBitwiseOperations() {
  HexBitboard32x32 a, b, result;
  
  // Set up test patterns
  a.set(0);
  a.set(2);
  a.set(100);
  
  b.set(1);
  b.set(2);
  b.set(100);
  
  // Test AND
  result = a & b;
  SPIEL_CHECK_FALSE(result.test(0));
  SPIEL_CHECK_FALSE(result.test(1));
  SPIEL_CHECK_TRUE(result.test(2));
  SPIEL_CHECK_TRUE(result.test(100));
  
  // Test OR
  result = a | b;
  SPIEL_CHECK_TRUE(result.test(0));
  SPIEL_CHECK_TRUE(result.test(1));
  SPIEL_CHECK_TRUE(result.test(2));
  SPIEL_CHECK_TRUE(result.test(100));
  
  // Test XOR
  result = a ^ b;
  SPIEL_CHECK_TRUE(result.test(0));
  SPIEL_CHECK_TRUE(result.test(1));
  SPIEL_CHECK_FALSE(result.test(2));
  SPIEL_CHECK_FALSE(result.test(100));
  
  // Test NOT
  HexBitboard32x32 c;
  c.set(5);
  result = ~c;
  SPIEL_CHECK_FALSE(result.test(5));
  SPIEL_CHECK_TRUE(result.test(0));
  SPIEL_CHECK_TRUE(result.test(1023));
}

void TestCompoundAssignment() {
  HexBitboard32x32 a, b;
  
  a.set(0);
  a.set(2);
  
  b.set(1);
  b.set(2);
  
  // Test &=
  HexBitboard32x32 test_and = a;
  test_and &= b;
  SPIEL_CHECK_FALSE(test_and.test(0));
  SPIEL_CHECK_FALSE(test_and.test(1));
  SPIEL_CHECK_TRUE(test_and.test(2));
  
  // Test |=
  HexBitboard32x32 test_or = a;
  test_or |= b;
  SPIEL_CHECK_TRUE(test_or.test(0));
  SPIEL_CHECK_TRUE(test_or.test(1));
  SPIEL_CHECK_TRUE(test_or.test(2));
  
  // Test ^=
  HexBitboard32x32 test_xor = a;
  test_xor ^= b;
  SPIEL_CHECK_TRUE(test_xor.test(0));
  SPIEL_CHECK_TRUE(test_xor.test(1));
  SPIEL_CHECK_FALSE(test_xor.test(2));
}

void TestPopcount() {
  HexBitboard32x32 bitboard;
  SPIEL_CHECK_EQ(bitboard.popcount(), 0);
  
  bitboard.set(0);
  SPIEL_CHECK_EQ(bitboard.popcount(), 1);
  
  bitboard.set(31);
  SPIEL_CHECK_EQ(bitboard.popcount(), 2);
  
  bitboard.set(31);
  bitboard.set(63);
  bitboard.set(1023);
  SPIEL_CHECK_EQ(bitboard.popcount(), 4);
}

void TestFindFirstLast() {
  HexBitboard32x32 bitboard;
  
  // Empty bitboard
  SPIEL_CHECK_EQ(bitboard.find_first(), size_t(-1));
  SPIEL_CHECK_EQ(bitboard.find_last(), size_t(-1));
  
  // Single bit at beginning
  bitboard.set(0);
  SPIEL_CHECK_EQ(bitboard.find_first(), 0);
  SPIEL_CHECK_EQ(bitboard.find_last(), 0);
  
  // Add bit at end
  bitboard.set(1023);
  SPIEL_CHECK_EQ(bitboard.find_first(), 0);
  SPIEL_CHECK_EQ(bitboard.find_last(), 1023);
  
  // Add bit in middle
  bitboard.set(500);
  SPIEL_CHECK_EQ(bitboard.find_first(), 0);
  SPIEL_CHECK_EQ(bitboard.find_last(), 1023);
  
  // Clear first bit
  bitboard.clear(0);
  SPIEL_CHECK_EQ(bitboard.find_first(), 500);
  SPIEL_CHECK_EQ(bitboard.find_last(), 1023);
}

void TestSpatialOperations() {
  HexBitboard32x32 bitboard;
  
  // Test east shift (within a row)
  bitboard.set(0);  // Bottom-left corner
  HexBitboard32x32 shifted_east = bitboard.east(1);
  SPIEL_CHECK_FALSE(shifted_east.test(0));
  SPIEL_CHECK_TRUE(shifted_east.test(1));
  
  // Test west shift
  bitboard.reset();
  bitboard.set(1);
  HexBitboard32x32 shifted_west = bitboard.west(1);
  SPIEL_CHECK_TRUE(shifted_west.test(0));
  SPIEL_CHECK_FALSE(shifted_west.test(1));
}

void TestCompoundDirections() {
  HexBitboard32x32 bitboard;
  bitboard.set(0);  // Bottom-left corner
  
  // Test north_east
  HexBitboard32x32 ne = bitboard.north_east(1);
  SPIEL_CHECK_FALSE(ne.test(0));
  SPIEL_CHECK_TRUE(ne.test(33));  // Row 1, column 1
  
  // Test south_west from a middle position
  bitboard.reset();
  bitboard.set(33);  // Row 1, column 1
  HexBitboard32x32 sw = bitboard.south_west(1);
  SPIEL_CHECK_TRUE(sw.test(0));   // Row 0, column 0
  SPIEL_CHECK_FALSE(sw.test(33));
}

void TestHexAdjacentBits() {
  HexBitboard32x32 bitboard;
  bitboard.set(33);  // Row 1, column 1
  
  HexBitboard32x32 hex_adjacent = bitboard.hex_adjacent_bits();
  
  // Check 6-directional hex adjacency (should not include the original bit)
  SPIEL_CHECK_FALSE(hex_adjacent.test(33));  // Original bit should be excluded
  SPIEL_CHECK_FALSE(hex_adjacent.test(2));   // South-east bit should be excluded
  SPIEL_CHECK_FALSE(hex_adjacent.test(64));  // North-west bit should be excluded
  SPIEL_CHECK_TRUE(hex_adjacent.test(32));   // West
  SPIEL_CHECK_TRUE(hex_adjacent.test(34));   // East
  SPIEL_CHECK_TRUE(hex_adjacent.test(0));    // South-west
  SPIEL_CHECK_TRUE(hex_adjacent.test(1));    // South
  SPIEL_CHECK_TRUE(hex_adjacent.test(65));   // North
  SPIEL_CHECK_TRUE(hex_adjacent.test(66));   // North-east
}

void TestToString() {
  HexBitboard32x32 bitboard;
  
  // Test with a few bits set
  bitboard.set(0);    // Bottom-left (should appear bottom-left in string)
  bitboard.set(1023); // Top-right (should appear top-right in string)
  
  std::string str = bitboard.to_string();
  
  // Count ones and zeros
  size_t one_count = 0;
  size_t zero_count = 0;
  for (char c : str) {
    if (c == '1') one_count++;
    else if (c == '0') zero_count++;
  }
  SPIEL_CHECK_EQ(one_count, 2);
  SPIEL_CHECK_EQ(zero_count, 32 * 32 - 2);
}

void TestLargeShifts() {
  HexBitboard32x32 bitboard;
  bitboard.set(100);
  
  // Test large north shift
  HexBitboard32x32 large_north = bitboard.north_west(10);
  SPIEL_CHECK_FALSE(large_north.test(100));
  SPIEL_CHECK_TRUE(large_north.test(100 + 10 * 32));
  
  // Test shift beyond bounds
  HexBitboard32x32 beyond_bounds = bitboard.north_west(50);  // Should result in empty bitboard
  SPIEL_CHECK_TRUE(beyond_bounds.none());
}

void TestArrayAccess() {
  HexBitboard32x32 bitboard;
  
  // Test direct array access
  bitboard[0] = 0xFFFFFFFF;  // Set all bits in first word
  SPIEL_CHECK_EQ(bitboard.popcount(), 32);
  
  // Test const access
  const HexBitboard32x32& const_ref = bitboard;
  SPIEL_CHECK_EQ(const_ref[0], 0xFFFFFFFF);
  SPIEL_CHECK_EQ(const_ref[1], 0);
}

void TestEquality() {
  HexBitboard32x32 a, b;
  
  // Test empty equality
  SPIEL_CHECK_TRUE(a == b);
  
  // Test inequality
  a.set(5);
  SPIEL_CHECK_FALSE(a == b);
  
  // Test equality after same operations
  b.set(5);
  SPIEL_CHECK_TRUE(a == b);
  
  // Test with complex patterns
  for (int i = 0; i < 100; i += 7) {
    a.set(i);
    b.set(i);
  }
  SPIEL_CHECK_TRUE(a == b);
}

void TestStressOperations() {
  HexBitboard32x32 a, b, result;
  
  // Set alternating pattern in a
  for (int i = 0; i < 1024; i += 2) {
    a.set(i);
  }
  
  // Set different alternating pattern in b
  for (int i = 1; i < 1024; i += 2) {
    b.set(i);
  }
  
  // Test that OR gives all bits set
  result = a | b;
  SPIEL_CHECK_EQ(result.popcount(), 1024);
  
  // Test that AND gives no bits set
  result = a & b;
  SPIEL_CHECK_EQ(result.popcount(), 0);
  
  // Test that XOR gives all bits set
  result = a ^ b;
  SPIEL_CHECK_EQ(result.popcount(), 1024);
}

void TestBitboardPerformance() {
  constexpr int num_iterations = 10000000;
  HexBitboard32x32 a, b, result;
  // Set up some bits
  for (int i = 0; i < 1024; i += 3) a.set(i);
  for (int i = 0; i < 1024; i += 5) b.set(i);

  auto start = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < num_iterations; ++i) {
    result = a & b;
    result = result | a;
    result = result ^ b;
    result = ~result;
  }
  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> elapsed = end - start;
  std::cout << "HexBitboard32x32 performance test: " << num_iterations << " iterations in " << elapsed.count() << " seconds." << std::endl;

  // Compare to same operations over a vector containing 32 32-bit words
  start = std::chrono::high_resolution_clock::now();
  std::vector<uint32_t> a_vec(32), b_vec(32), result_vec(32);
  for (int i = 0; i < 1000000; ++i) {
    for (int j = 0; j < 32; ++j) {
      result_vec[j] = a_vec[j] & b_vec[j];
      result_vec[j] |= a_vec[j];
      result_vec[j] ^= b_vec[j];
      result_vec[j] = ~result_vec[j];
    }
  }
  end = std::chrono::high_resolution_clock::now();
  elapsed = end - start;
  std::cout << "Vector<uint32_t>[32] performance test: " << num_iterations << " iterations in " << elapsed.count() << " seconds." << std::endl;
  
  // Compare to same operations over an std::bitset<1024>
  start = std::chrono::high_resolution_clock::now();
  std::bitset<1024> a_bitset(32), b_bitset(32), result_bitset(32);
  for (int i = 0; i < 1000000; ++i) {
    for (int j = 0; j < 32; ++j) {
      result_bitset[j] = a_bitset[j] & b_bitset[j];
      result_bitset[j] = result_bitset[j] | a_bitset[j];
      result_bitset[j] = result_bitset[j] ^ b_bitset[j];
      result_bitset[j] = ~result_bitset[j];
    }
  }
  end = std::chrono::high_resolution_clock::now();
  elapsed = end - start;
  std::cout << "Bitset<1024> performance test: " << num_iterations << " iterations in " << elapsed.count() << " seconds." << std::endl;

  HiveBoard tester_({}, false);
  std::cout << HiveBoard::kNeighbourMasks[100].to_string() << std::endl;
}

void RyansTests() {
  std::cout << "Comparing speed of bitboard vs looping for neighbour iteration" << std::endl;
  std::mt19937 rng(std::random_device{}());
  std::bernoulli_distribution dist50(0.5);
  std::uniform_int_distribution dist(50, 900);

  // init a random bitboard
  HexBitboard32x32 rand_board{};
  for (int i = 0; i < 32 * 32; ++i) {
    if (dist50(rng)) {
      rand_board.set(i);
    }
  }

  int num_iterations = 100000;
  HexBitboard32x32 result{};

  //////////////////////////////////
  auto start = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < num_iterations; ++i) {
    // check every direction for the existence of a neighbour
    int idx = dist(rng);
    for (int dir = 0; dir < Direction::kNumCardinalDirections; ++dir) {
      size_t nb = idx + hive::HiveBoard::kNeighbourOffsets[dir];

      if (rand_board.test(nb)) {
        result.flip(nb);
      }
    }
  }
  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> elapsed = end - start;
  std::cout << "  FOR LOOP performance test: " << num_iterations << " iterations in " << elapsed.count() << " seconds." << std::endl;

  ////////////////////////////////////
  result.reset();
  start = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < num_iterations; ++i) {
    int idx = dist(rng);

    for (auto j : (hive::HiveBoard::kNeighbourMasks[idx] & rand_board).all_set_bits()) {
      result.set(j);
    }
  }
  end = std::chrono::high_resolution_clock::now();
  elapsed = end - start;
  std::cout << "  BITBOARD ALL_SET_BITS() performance test: " << num_iterations << " iterations in " << elapsed.count() << " seconds." << std::endl;
}

}  // namespace
}  // namespace hive
}  // namespace open_spiel

int main(int argc, char** argv) {
  open_spiel::hive::TestBitboardConstruction();
  open_spiel::hive::TestBitboardCopyConstructor();
  open_spiel::hive::TestBitboardAssignment();
  open_spiel::hive::TestBitManipulation();
  open_spiel::hive::TestBitwiseOperations();
  open_spiel::hive::TestCompoundAssignment();
  open_spiel::hive::TestPopcount();
  open_spiel::hive::TestFindFirstLast();
  open_spiel::hive::TestSpatialOperations();
  open_spiel::hive::TestCompoundDirections();
  open_spiel::hive::TestHexAdjacentBits();
  open_spiel::hive::TestToString();
  open_spiel::hive::TestLargeShifts();
  open_spiel::hive::TestArrayAccess();
  open_spiel::hive::TestEquality();
  open_spiel::hive::TestStressOperations();
  open_spiel::hive::TestBitboardPerformance();
  
  std::cout << "All HexBitboard32x32 tests passed!" << std::endl;
  open_spiel::hive::RyansTests();
  return 0;
}
