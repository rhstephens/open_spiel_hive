#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <chrono>

#include "open_spiel/games/hive/hive.h"
#include "open_spiel/spiel.h"

namespace hive_uhp {

// ============================================================================
// Core UHP Engine Class
// ============================================================================

class UHPEngine {
 public:
  UHPEngine();
  ~UHPEngine() = default;

  // Main event loop - reads from stdin and processes commands
  void Run();

  // Output management
  void SendInfo();
  void SendOk();
  void SendError(const std::string& message);
  void SendInvalidMove(const std::string& message);
  void SendLine(const std::string& line);

 private:
  // Command handlers
  void HandleNewGame(const std::vector<std::string>& args);
  void HandlePlay(const std::vector<std::string>& args);
  void HandlePass(const std::vector<std::string>& args);
  void HandleValidMoves(const std::vector<std::string>& args);
  void HandleBestMove(const std::vector<std::string>& args);
  void HandleUndo(const std::vector<std::string>& args);
  void HandleOptions(const std::vector<std::string>& args);

  // Utility functions
  std::vector<std::string> ParseCommand(const std::string& line);
  std::string GetGameString();
  std::string GetGameTypeString() const;
  std::string GetGameStateString() const;
  std::string GetTurnString() const;

  // Game state
  std::shared_ptr<const open_spiel::Game> game_;
  std::unique_ptr<open_spiel::State> current_state_;

  // Engine configuration
  struct EngineOptions {
    int max_branching_factor = 500;
    bool use_mosquito = true;
    bool use_ladybug = true;
    bool use_pillbug = true;
    bool pretty_print = false;
  } options_;

  // Move history tracking for undo
  std::vector<std::string> move_history_;
};

// ============================================================================
// Command-Line Interface Helper
// ============================================================================

class CommandParser {
 public:
  static std::vector<std::string> ParseLine(const std::string& line);
  static std::string TrimWhitespace(const std::string& str);
};

// ============================================================================
// Response Formatter
// ============================================================================

class ResponseFormatter {
 public:
  // Format a GameString according to UHP spec
  static std::string FormatGameString(
      const std::string& game_type,
      const std::string& game_state,
      const std::string& turn_info,
      const std::vector<std::string>& moves);

  // Format capability string
  static std::string FormatCapabilities(
      bool mosquito, bool ladybug, bool pillbug);

  // Format option response
  static std::string FormatOption(
      const std::string& name,
      const std::string& type,
      const std::string& value,
      const std::string& default_val);

  // Format integer option with range
  static std::string FormatIntOption(
      const std::string& name,
      int value, int default_val,
      int min_val, int max_val);

  static std::string FormatBoolOption(
      const std::string& name,
      bool value, bool default_value);

  // Format move list
  static std::string FormatMoveList(
      const std::vector<std::string>& moves);
};

}  // namespace hive_uhp