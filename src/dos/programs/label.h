// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_PROGRAM_LABEL_H
#define DOSBOX_PROGRAM_LABEL_H

#include "dos/programs.h"

#include <optional>
#include <string>
#include <string_view>

// LABEL as MS-DOS 4.0 LABEL.ASM: the command tail as typed, blanks
// included, because a label may hold them.
namespace LabelCommand {

struct TailParts {
	// The drive as typed, upper case; ask when nothing was given, or a
	// blank after "X:".
	std::optional<char> drive = {};
	std::string text          = {};
	bool ask                  = false;
};

TailParts ParseCommandTail(std::string_view tail);

// What the user typed at the prompt: leading blanks and the line end go.
std::string TextOfInputLine(std::string_view line);

enum class TextCheck { Empty, Invalid, Valid };

TextCheck CheckLabelText(std::string_view text);

// The name 5Bh is given: upper case, at most 11 characters, a dot after
// the eighth ("sales1991" is "SALES199.1").
std::string ToLabelName(std::string_view text);

// An FCB search result's 11 name bytes as LABEL prints them.
std::string LabelFromFcbName(std::string_view name11);

} // namespace LabelCommand

class LABEL final : public Program {
public:
	LABEL()
	{
		AddMessages();
		help_detail = {HELP_Filter::All,
		               HELP_Category::File,
		               HELP_CmdType::Program,
		               "LABEL"};
	}
	void Run() override;

private:
	void AddMessages();
};

#endif
