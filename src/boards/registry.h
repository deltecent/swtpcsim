#pragma once
//
// Board type registry -- backs SHOW BOARDS, BOARDS ADD, and the MCP board_types
// tool. Adding a board type is one line here and nothing anywhere else.

#include "core/board.h"

#include <memory>
#include <string>
#include <vector>

namespace swtpc {

struct BoardType {
    std::string name;
    std::string summary;      // one line, for the SHOW BOARDS catalog and the index tables
    std::string description;  // the full paragraph, for SHOW BOARD <type>
};

std::vector<BoardType> boardTypes();
std::unique_ptr<Board> makeBoard(const std::string& type);

} // namespace swtpc
