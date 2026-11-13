// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_LUA_API_H
#define DOSBOX_LUA_API_H

#include <expected>
#include <string>

#include "hardware/memory.h"

struct lua_State;

namespace Lua {

class DebugLog;

// Registers all dosbox.* API functions into the Lua state.
// Called from LuaCoroutine::RegisterApi after the dosbox table
// and coroutine context are set up.
void RegisterDosboxApi(lua_State* L, DebugLog* debug_log);

// Read the text mode screen buffer. Returns empty string if not in
// a text mode. Works for VGA, CGA, Hercules, and Tandy text modes. The
// error is the first address the guest's page tables do not map.
std::expected<std::string, PhysPt> ReadScreenText();

// Substring match with optional case-insensitive mode.
bool MatchSubstring(const std::string& haystack, const std::string& needle,
                    bool ignorecase);

} // namespace Lua

#endif
