#include <iostream>
#include <sstream>
#include <algorithm>
#include <cctype>

#include "open_spiel/examples/hive_uhp/hive_uhp.h"


namespace hive_uhp {

// ============================================================================
// CommandParser Implementation
// ============================================================================

std::vector<std::string> CommandParser::ParseLine(const std::string& line) {
  std::vector<std::string> tokens;
  std::istringstream iss(line);
  std::string token;
  
  while (iss >> token) {
    tokens.push_back(token);
  }
  
  return tokens;
}

std::string CommandParser::TrimWhitespace(const std::string& str) {
  size_t start = str.find_first_not_of(" \t\n\r");
  size_t end = str.find_last_not_of(" \t\n\r");
  
  if (start == std::string::npos) return "";
  return str.substr(start, end - start + 1);
}

// ============================================================================
// ResponseFormatter Implementation
// ============================================================================

std::string ResponseFormatter::FormatGameString(
    const std::string& game_type,
    const std::string& game_state,
    const std::string& turn_info,
    const std::vector<std::string>& moves) {
  std::string result = game_type + ";" + game_state + ";" + turn_info;
  
  for (const auto& move : moves) {
    result += ";" + move;
  }
  
  return result;
}

std::string ResponseFormatter::FormatCapabilities(
    bool mosquito, bool ladybug, bool pillbug) {
  std::string caps;
  
  if (mosquito) caps += "Mosquito;";
  if (ladybug) caps += "Ladybug;";
  if (pillbug) caps += "Pillbug";
  
  // Remove trailing semicolon if present
  if (!caps.empty() && caps.back() == ';') {
    caps.pop_back();
  }
  
  return caps;
}

std::string ResponseFormatter::FormatIntOption(
    const std::string& name,
    int value, int default_val,
    int min_val, int max_val) {
  return name + ";int;" + std::to_string(value) + ";" +
         std::to_string(default_val) + ";" +
         std::to_string(min_val) + ";" + std::to_string(max_val);
}

std::string ResponseFormatter::FormatBoolOption(
    const std::string& name,
    bool value, bool default_val) {
  return name + ";bool;" + (value ? "true" : "false") + ";" +
         (default_val ? "true" : "false");
}

std::string ResponseFormatter::FormatMoveList(
    const std::vector<std::string>& moves) {
  if (moves.empty()) return "";
  
  std::string result = moves[0];
  for (size_t i = 1; i < moves.size(); ++i) {
    result += ";" + moves[i];
  }
  return result;
}

// ============================================================================
// UHPEngine Implementation
// ============================================================================

UHPEngine::UHPEngine() {
  // Initialize the Hive game with expansion pieces enabled
  game_ = open_spiel::LoadGame("hive");
  current_state_ = game_->NewInitialState();
}

void UHPEngine::Run() {
  // Send initial info to signal engine is ready
  SendInfo();
  
  std::string line;
  while (std::getline(std::cin, line)) {
    line = CommandParser::TrimWhitespace(line);
    
    if (line.empty()) continue;
    
    auto tokens = CommandParser::ParseLine(line);
    if (tokens.empty()) continue;
    
    std::string command = tokens[0];
    std::vector<std::string> args(tokens.begin() + 1, tokens.end());
    
    try {
      if (command == "info") {
        SendInfo();
      } else if (command == "newgame") {
        HandleNewGame(args);
      } else if (command == "play") {
        HandlePlay(args);
      } else if (command == "pass") {
        HandlePass(args);
      } else if (command == "validmoves") {
        HandleValidMoves(args);
      } else if (command == "bestmove") {
        HandleBestMove(args);
      } else if (command == "undo") {
        HandleUndo(args);
      } else if (command == "options") {
        HandleOptions(args);
      } else {
        SendError("Invalid command: " + command);
      }
    } catch (const std::exception& e) {
      SendError(std::string(e.what()));
    }
  }
}

void UHPEngine::SendInfo() {
  SendLine("info");
  SendLine("id OpenSpiel Hive Engine v1.0");
  
  std::string caps = ResponseFormatter::FormatCapabilities(
      options_.use_mosquito,
      options_.use_ladybug,
      options_.use_pillbug);
  
  if (!caps.empty()) {
    SendLine(caps);
  }
  
  SendOk();
}

void UHPEngine::SendOk() {
  std::cout << "ok" << std::endl;
}

void UHPEngine::SendError(const std::string& message) {
  std::cout << "err " << message << std::endl;
  SendOk();
}

void UHPEngine::SendInvalidMove(const std::string& message) {
  std::cout << "invalidmove " << message << std::endl;
  SendOk();
}

void UHPEngine::SendLine(const std::string& line) {
  std::cout << line << std::endl;
}

std::string UHPEngine::GetGameString() {
  return GetGameTypeString() + ";" +
         GetGameStateString() + ";" +
         GetTurnString();
}

std::string UHPEngine::GetGameTypeString() const {
  std::string result = "Base";
  
  if (options_.use_mosquito || options_.use_ladybug || 
      options_.use_pillbug) {
    result += "+";
    if (options_.use_mosquito) result += "M";
    if (options_.use_ladybug) result += "L";
    if (options_.use_pillbug) result += "P";
  }
  
  return result;
}

std::string UHPEngine::GetGameStateString() const {
  if (current_state_->IsTerminal()) {
    auto returns = current_state_->Returns();
    if (returns[0] > returns[1]) {
      return "WhiteWins";
    } else if (returns[0] < returns[1]) {
      return "BlackWins";
    } else {
      return "Draw";
    }
  }
  
  if (move_history_.empty()) {
    return "NotStarted";
  }
  
  return "InProgress";
}

std::string UHPEngine::GetTurnString() const {
  int move_count = move_history_.size();
  int turn_number = (move_count / 2) + 1;
  std::string player = (move_count % 2 == 0) ? "White" : "Black";
  
  return player + "[" + std::to_string(turn_number) + "]";
}

void UHPEngine::HandleNewGame(const std::vector<std::string>& args) {
  // TODO: Parse GameTypeString or GameString from args if provided
  
  current_state_ = game_->NewInitialState();
  move_history_.clear();
  
  SendLine(current_state_->Serialize());
  SendOk();
}

void UHPEngine::HandlePlay(const std::vector<std::string>& args) {
  if (args.empty()) {
    SendError("play command requires a move");
    return;
  }
  
  // Reconstruct the move string (may contain spaces)
  std::string move_str;
  for (size_t i = 0; i < args.size(); ++i) {
    if (i > 0) move_str += " ";
    move_str += args[i];
  }
  
  try {
    auto legal_moves = current_state_->LegalActions();
    auto action = current_state_->StringToAction(
        open_spiel::hive::kPlayerWhite, move_str);
    
    bool valid = false;
    for (auto legal_action : legal_moves) {
      if (legal_action == action) {
        valid = true;
        break;
      }
    }
    
    if (!valid) {
      SendInvalidMove("Illegal move: " + move_str);
      return;
    }
    
    current_state_->ApplyAction(action);
    move_history_.push_back(move_str);
    
    SendLine(current_state_->Serialize());
    SendOk();
  } catch (const std::exception& e) {
    SendInvalidMove("Invalid move format: " + move_str);
  }
}

void UHPEngine::HandlePass(const std::vector<std::string>& args) {
  // Equivalent to play pass
  std::vector<std::string> play_args = {"pass"};
  HandlePlay(play_args);
}

void UHPEngine::HandleValidMoves(const std::vector<std::string>& args) {
  auto legal_actions = current_state_->LegalActions();
  std::vector<std::string> moves;
  
  int current_player = current_state_->CurrentPlayer();
  for (auto action : legal_actions) {
    std::string move_str = 
        current_state_->ActionToString(current_player, action);
    moves.push_back(move_str);
  }
  
  SendLine(ResponseFormatter::FormatMoveList(moves));
  SendOk();
}

void UHPEngine::HandleBestMove(const std::vector<std::string>& args) {
  if (args.size() < 2) {
    SendError("bestmove requires 'time' or 'depth' parameter");
    return;
  }
  
  std::string param_type = args[0];
  std::string param_value = args[1];
  
  if (param_type == "time") {
    // TODO: Implement time-limited search
    SendError("Time-limited search not yet implemented");
  } else if (param_type == "depth") {
    // TODO: Implement depth-limited search
    SendError("Depth-limited search not yet implemented");
  } else {
    SendError("Unknown bestmove parameter: " + param_type);
  }
  
  SendOk();
}

void UHPEngine::HandleUndo(const std::vector<std::string>& args) {
  int moves_to_undo = 1;
  
  if (!args.empty()) {
    try {
      moves_to_undo = std::stoi(args[0]);
    } catch (const std::exception&) {
      SendError("Invalid number of moves to undo");
      return;
    }
  }
  
  if (moves_to_undo > static_cast<int>(move_history_.size())) {
    SendError("Cannot undo more moves than have been played");
    return;
  }
  
  // Recreate the game state from scratch
  current_state_ = game_->NewInitialState();
  
  int new_size = move_history_.size() - moves_to_undo;
  move_history_.resize(new_size);
  
  // Replay all remaining moves
  for (const auto& move : move_history_) {
    int current_player = current_state_->CurrentPlayer();
    auto action = current_state_->StringToAction(current_player, move);
    current_state_->ApplyAction(action);
  }
  
  SendLine(current_state_->Serialize());
  SendOk();
}

void UHPEngine::HandleOptions(const std::vector<std::string>& args) {
  if (args.empty()) {
    // List all options
    SendLine(ResponseFormatter::FormatIntOption(
        "MaxBranchingFactor",
        options_.max_branching_factor,
        500, 1, 500));
    SendLine(ResponseFormatter::FormatBoolOption(
      "PrettyPrint", options_.pretty_print, false));
    SendOk();
    return;
  }
  
  if (args[0] == "get") {
    if (args.size() < 2) {
      SendError("options get requires an option name");
      return;
    }
    
    // TODO: Handle specific option retrieval
    SendOk();
  } else if (args[0] == "set") {
    if (args.size() < 3) {
      SendError("options set requires name and value");
      return;
    }
    
    // TODO: Handle option setting
    SendOk();
  } else {
    SendError("Unknown options subcommand: " + args[0]);
  }
}

}  // namespace hive_uhp
class Exception {

};

int main(int argc, char** argv) {
  hive_uhp::UHPEngine engine{};

  try {
    engine.Run();
  } catch (...) {}
  exit(0);
}