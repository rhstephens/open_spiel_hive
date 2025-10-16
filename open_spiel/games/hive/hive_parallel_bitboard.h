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

#ifndef OPEN_SPIEL_GAMES_HIVE_BITBOARD_H_
#define OPEN_SPIEL_GAMES_HIVE_BITBOARD_H_

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <immintrin.h>

#include "open_spiel/spiel_utils.h"

namespace open_spiel {
namespace hive {

enum Direction : uint8_t {
  kNE = 0,
  kE,
  kSE,
  kSW,
  kW,
  kNW,
  kAbove,
  kNumCardinalDirections = kAbove,  // syntactic sugar for iterating
  kNumAllDirections
};

// High-performance Bitboard with SIMD acceleration.
// Utilizes 32-bit words for easy mapping of word -> row, col while also
// supporting SIMD and intrinsic instructions.
class HexBitboard32x32 {
 private:
  static constexpr size_t __num_words = 32;
  static constexpr size_t __word_size = 32;

 public:

  static bool valid_index(size_t idx) { return idx < __word_size * __num_words; }

  static Direction direction_between(size_t from_pos, size_t to_pos) {
    const int offset = static_cast<int>(to_pos) - static_cast<int>(from_pos);
    const int row_size = static_cast<int>(__word_size);

    switch (offset) {
      case row_size + 1:
        return Direction::kNE;
      case 1:
        return Direction::kE;
      case -row_size:
        return Direction::kSE;
      case -row_size - 1:
        return Direction::kSW;
      case -1:
        return Direction::kW;
      case row_size:
        return Direction::kNW;
      default:
        return static_cast<Direction>(Direction::kNumAllDirections);
    }

    return static_cast<Direction>(Direction::kNumAllDirections);
  }

  HexBitboard32x32() : data_{} {}

  // New Bitboard with provided bit set
  HexBitboard32x32(size_t idx) : data_{} { set(idx); }

  HexBitboard32x32(const HexBitboard32x32& other) {
    std::memcpy(data_, other.data_, sizeof(data_));
  }

  HexBitboard32x32& operator=(const HexBitboard32x32& other) {
    std::memcpy(data_, other.data_, sizeof(data_));
    return *this;
  }

  bool operator==(const HexBitboard32x32& other) const {
    return std::memcmp(data_, other.data_, sizeof(data_)) == 0;
  }

  uint32_t& operator[](size_t idx) { return data_[idx]; }
  const uint32_t& operator[](size_t idx) const { return data_[idx]; }

  // ------------------------------------------------------
  // -------- Bitwise operations between Bitboards --------
  // ------------------------------------------------------

  // Bitwise AND
  friend HexBitboard32x32 operator&(const HexBitboard32x32& lhs, const HexBitboard32x32& rhs) {
    HexBitboard32x32 result{};
    int idx = 0;

    for (; idx + 7 < __num_words; idx += 8) {
      __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(lhs.data_ + idx));
      __m256i vb = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rhs.data_ + idx));
      _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.data_ + idx), _mm256_and_si256(va, vb));
    }

    return result;
  }

  // Bitwise OR
  friend HexBitboard32x32 operator|(const HexBitboard32x32& lhs, const HexBitboard32x32& rhs) {
    HexBitboard32x32 result{};
    int idx = 0;

    for (; idx + 7 < __num_words; idx += 8) {
      __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(lhs.data_ + idx));
      __m256i vb = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rhs.data_ + idx));
      _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.data_ + idx), _mm256_or_si256(va, vb));
    }

    return result;
  }

  // Bitwise XOR
  friend HexBitboard32x32 operator^(const HexBitboard32x32& lhs, const HexBitboard32x32& rhs) {
    HexBitboard32x32 result{};
    int idx = 0;

    for (; idx + 7 < __num_words; idx += 8) {
      __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(lhs.data_ + idx));
      __m256i vb = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rhs.data_ + idx));
      _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.data_ + idx), _mm256_xor_si256(va, vb));
    }

    return result;
  }

  // Bitwise NOT
  HexBitboard32x32 operator~() const {
    HexBitboard32x32 result{};
    __m256i all_ones_256 = _mm256_set1_epi32(-1);
    __m128i all_ones_128 = _mm_set1_epi32(-1);
    int idx = 0;

    for (; idx + 7 < __num_words; idx += 8) {
      __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(data_ + idx));
      _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.data_ + idx), _mm256_xor_si256(va, all_ones_256));
    }

    return result;
  }

  // TODO: maybe vectorize these to reduce extra copy-assignment?
  // Bitwise AND-EQUAL
  HexBitboard32x32& operator&=(const HexBitboard32x32& rhs) {
    *this = *this & rhs;
    return *this;
  }

  // Bitwise OR-EQUAL
  HexBitboard32x32& operator|=(const HexBitboard32x32& rhs) {
    *this = *this | rhs;
    return *this;
  }

  // Bitwise XOR-EQUAL
  HexBitboard32x32& operator^=(const HexBitboard32x32& rhs) {
    *this = *this ^ rhs;
    return *this;
  }

  // ------------------------------------------------------
  // ------------- Manipulating specific bits -------------
  // ------------------------------------------------------

  // Sets the bit at idx on
  void set(size_t idx) {
    SPIEL_DCHECK_LT(idx, __word_size * __num_words);
    data_[idx / __word_size] |= static_cast<uint32_t>(1) << (idx % __word_size);
  }

  // Sets the bit at idx off
  void clear(size_t idx) {
    SPIEL_DCHECK_LT(idx, __word_size * __num_words);
    data_[idx / __word_size] &= ~(static_cast<uint32_t>(1) << (idx % __word_size));
  }

  // Flips the bit at idx
  void flip(size_t idx) {
    SPIEL_DCHECK_LT(idx, __word_size * __num_words);
    data_[idx / __word_size] ^= static_cast<uint32_t>(1) << (idx % __word_size);
  }

  // Checks if bit at idx is on
  bool test(size_t idx) const {
    SPIEL_DCHECK_LT(idx, __word_size * __num_words);
    return (data_[idx / __word_size] & (static_cast<uint32_t>(1) << (idx % __word_size))) != 0;
  }

  // Checks if any bits are set
  bool any() const {
    int idx = 0;

    for (; idx + 7 < __num_words; idx += 8) {
      __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(data_ + idx));
      if (!_mm256_testz_si256(v, v)) {
        return true;
      }
    }

    return false;
  }

  // Checks if no bits are set
  bool none() const {
    return !any();
  }

  // Counts number of set bits
  int popcount() const {
    int count = 0;
    int idx = 0;

    // todo
    // __m256i x = _mm256_popcnt_epi32(*(reinterpret_cast<__m256i*>(Bitboard32x32{}.data_)));
    for (int idx = 0; idx < __num_words; ++idx) {
      count += __builtin_popcount(data_[idx]);
    }

    return count;
  }

  // Finds first set bit (Least Significant Bit)
  size_t find_first() const {
    for (int idx = 0; idx < __num_words; ++idx) {
      if (data_[idx] != 0) {
        return idx * __word_size + __builtin_ctz(data_[idx]);
      }
    }

    return size_t(-1);
  }

  // Finds last set bit (Most Significant Bit)
  size_t find_last() const {
    for (int idx = __num_words - 1; idx >= 0; --idx) {
      if (data_[idx] != 0) {
        return idx * __word_size + (__word_size - 1) - __builtin_clz(data_[idx]);
      }
    }

    return size_t(-1);
  }
  
  // Reset all bits to zero
  void reset() {
    std::memset(data_, 0, sizeof(data_));
  }

  // ------------------------------------------------------
  // -------------- Spatial board operations --------------
  // ------------------------------------------------------

  // The six Hex directions use all compass directions except North/South
  HexBitboard32x32 east(int shift = 1) const { return shift_right(shift); }
  HexBitboard32x32 west(int shift = 1) const { return shift_left(shift); }
  HexBitboard32x32 north_east(int shift = 1) const { return shift_up(shift).shift_right(shift); }
  HexBitboard32x32 north_west(int shift = 1) const { return shift_up(shift); }
  HexBitboard32x32 south_east(int shift = 1) const { return shift_down(shift); }
  HexBitboard32x32 south_west(int shift = 1) const { return shift_down(shift).shift_left(shift); }

  // Create a bitboard with bits set for all hex-adjacent positions
  HexBitboard32x32 hex_adjacent_bits() const {
    return (north_east() |=
            east() |=
            south_east() |=
            south_west() |=
            west() |=
            north_west()) & ~(*this);
  }

  // Creates a bitboard with bits set at positions that can be reached through gates
  // A gate is valid if exactly one of its two adjacent positions has a bit set
  HexBitboard32x32 gate_movement(Direction direction) const {
    switch (direction) {
      case Direction::kNE:
        // For NorthEast, the gate positions are NorthWest and East
        return (north_west() ^ east()).north_east();
      case Direction::kE:
        // For East, the gate positions are NorthEast and SouthEast
        return (north_east() ^ south_east()).east();
      case Direction::kSE:
        // For SouthEast, the gate positions are East and SouthWest
        return (east() ^ south_west()).south_east();
      case Direction::kSW:
        // For SouthWest, the gate positions are SouthEast and West
        return (south_east() ^ west()).south_west();
      case Direction::kW:
        // For West, the gate positions are SouthWest and NorthWest
        return (south_west() ^ north_west()).west();
      case Direction::kNW:
        // For NorthWest, the gate positions are West and NorthEast
        return (west() ^ north_east()).north_west();
      default:
        return HexBitboard32x32();
    }
  }

  // Flood fill from a starting position
  HexBitboard32x32 flood_fill(int startPosition) const {
    HexBitboard32x32 result{};
    result.set(startPosition);
    
    HexBitboard32x32 frontier = result;
    HexBitboard32x32 visited = result;
    
    while (frontier.any()) {
      // Expand the frontier in all directions
      HexBitboard32x32 new_frontier = frontier.hex_adjacent_bits() & (*this);
      // Remove already visited positions
      new_frontier &= ~visited;
      
      // If no new positions, we're done
      if (new_frontier.none()) break;
      
      // Update frontier and visited
      frontier = new_frontier;
      visited |= frontier;
    }
    
    return visited;
  }

  // Raycast from a position in a given direction
  // Returns a bitboard with only the bit that gets hit (obstacle), or empty if no obstacle found
  HexBitboard32x32 raycast(size_t start_pos, Direction direction) const {
    HexBitboard32x32 result{};
    HexBitboard32x32 current_pos{start_pos};
    
    // Cast ray until we hit an obstacle or go off the board
    while (current_pos.any()) {
      HexBitboard32x32 next_pos{};
      switch (direction) {
        case Direction::kNE:
          next_pos = current_pos.north_east();
          break;
        case Direction::kE:
          next_pos = current_pos.east();
          break;
        case Direction::kSE:
          next_pos = current_pos.south_east();
          break;
        case Direction::kSW:
          next_pos = current_pos.south_west();
          break;
        case Direction::kW:
          next_pos = current_pos.west();
          break;
        case Direction::kNW:
          next_pos = current_pos.north_west();
          break;
        default:
          return result;
      }
      
      // If we hit an occupied position, return it
      HexBitboard32x32 hit = next_pos & (*this);
      if (hit.any()) {
        return hit;
      }
      
      // If we went off the board, stop
      if (next_pos.none()) {
        break;
      }
      
      // Continue to next position
      current_pos = next_pos;
    }
    
    return result;
  }

  // ------------------------------------------------------
  // ----------------------- Other ------------------------
  // ------------------------------------------------------

  std::vector<size_t> all_set_bits() const {
    std::vector<size_t> result;
    result.reserve(popcount());
    
    for (int idx = 0; idx < __num_words; ++idx) {
      uint32_t word = data_[idx];

      // Find position of least significant bit, then clear it. Repeat.
      while (word != 0) {
        result.push_back(idx * __word_size + __builtin_ctz(word));
        word &= word - 1;
      }
    }
    
    return result;
  }

  template<typename Func>
  void for_each_set_bit(Func&& f) const {
    for (int idx = 0; idx < __num_words; ++idx) {
      uint32_t word = data_[idx];
      while (word != 0) {
        f(idx * __word_size + __builtin_ctz(word));
        word &= word - 1;
      }
    }
  }

  // Does this board contain "pattern" as a subset?
  bool contains_pattern(const HexBitboard32x32& pattern) {
    // TODO: Implement stub
    return false;
  }

  // 32x32 grid string
  __attribute__((noinline))
  std::string to_string() const {
    std::string result;
    result.reserve(__num_words * (__word_size + 1)); 
    
    // Process rows from top to bottom for display purposes
    for (int idx = __num_words - 1; idx >= 0; --idx) {
      // Process bits from left to right in this row
      uint32_t word = data_[idx];
      for (int bit = 0; bit < __word_size; ++bit) {
        result.push_back((word & (1u << bit)) ? '1' : '0');
        result.push_back(' ');
      }
      
      if (idx > 0) {
        result.push_back('\n');
      }
    }
    
    return result;
  }

 private:
  // The following functions all generate bitboards with every bit shifted in a
  // particular direction, assuming an LSB-to-MSB board representation.
  // e.g. shift_right(1) applies << 1 because left bit-shift moves towards MSB
  // Also note that bit wraparound between words DOES NOT HAPPEN (intentionally)

  // Copy of bitboard with every bit in every row shifted towards MSB
  HexBitboard32x32 shift_right(int shift = 1) const {
    HexBitboard32x32 result = *this;
    int idx = 0;

    for (; idx + 7 < __num_words; idx += 8) {
      __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(result.data_ + idx));
      _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.data_ + idx), _mm256_slli_epi32(va, shift));
    }
    
    return result;
  }

  // Copy of bitboard with every bit in every row shifted towards LSB
  HexBitboard32x32 shift_left(int shift = 1) const {
    HexBitboard32x32 result = *this;
    int idx = 0;

    for (; idx + 7 < __num_words; idx += 8) {
      __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(result.data_ + idx));
      _mm256_storeu_si256(reinterpret_cast<__m256i*>(result.data_ + idx), _mm256_srli_epi32(va, shift));
    }
    
    return result;
  }

  // Copy of bitboard with each word shifted up "shift" # of rows
  HexBitboard32x32 shift_up(int shift = 1) const {
    HexBitboard32x32 result{};
    std::memcpy(result.data_ + shift, data_, (__num_words - shift) * sizeof(uint32_t));

    return result;
  }

  // Copy of bitboard with each word shifted down "shift" # of rows
  HexBitboard32x32 shift_down(int shift = 1) const {
    HexBitboard32x32 result{};
    std::memcpy(result.data_, data_ + shift, (__num_words - shift) * sizeof(uint32_t));
    
    return result;
  }


  alignas(__word_size) uint32_t data_[__num_words];
};

} // namespace hive
} // namespace open_spiel


#endif // OPEN_SPIEL_GAMES_HIVE_BITBOARD_H_
