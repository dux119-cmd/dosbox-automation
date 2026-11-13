// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "label.h"

#include "cpu/callback.h"
#include "cpu/registers.h"
#include "dos/dos.h"
#include "dos/drives.h"
#include "hardware/memory.h"
#include "misc/messages.h"
#include "misc/support.h"
#include "more_output.h"
#include "utils/checks.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

CHECK_NARROWING();

extern unsigned int result_errorcode;

namespace LabelCommand {

constexpr char Blank         = ' ';
constexpr size_t MaxChars    = 11;
constexpr size_t DotLocation = 8;

static bool at_line_end(const std::string_view text, const size_t pos)
{
	return pos >= text.size() || text[pos] == '\r' || text[pos] == '\n';
}

static std::string_view up_to_line_end(const std::string_view text, size_t pos)
{
	const auto start = pos;
	while (!at_line_end(text, pos)) {
		++pos;
	}
	return text.substr(start, pos - start);
}

// LABEL.ASM, Find_First_Char and Check_For_Drive_Letter.
TailParts ParseCommandTail(const std::string_view tail)
{
	size_t pos = 0;
	while (!at_line_end(tail, pos) && tail[pos] == Blank) {
		++pos;
	}
	TailParts parts = {};
	if (!at_line_end(tail, pos) && pos + 1 < tail.size() && tail[pos + 1] == ':') {
		parts.drive = static_cast<char>(
		        toupper(static_cast<unsigned char>(tail[pos])));
		pos += 2;
		if (!at_line_end(tail, pos) && tail[pos] == Blank) {
			parts.ask = true;
			return parts;
		}
	}
	parts.text = std::string(up_to_line_end(tail, pos));
	parts.ask  = parts.text.empty();
	return parts;
}

std::string TextOfInputLine(const std::string_view line)
{
	size_t pos = 0;
	while (!at_line_end(line, pos) && line[pos] == Blank) {
		++pos;
	}
	return std::string(up_to_line_end(line, pos));
}

// LABEL.ASM, Character_Check.
static bool is_label_character(const unsigned char c)
{
	constexpr std::string_view Refused = "*?[]:<|>+=;,/\\.\"";
	return c >= 0x20 &&
	       Refused.find(static_cast<char>(c)) == std::string_view::npos;
}

TextCheck CheckLabelText(const std::string_view text)
{
	if (text.empty()) {
		return TextCheck::Empty;
	}
	const auto all_valid = std::all_of(text.begin(), text.end(), [](const char c) {
		return is_label_character(static_cast<unsigned char>(c));
	});
	return all_valid ? TextCheck::Valid : TextCheck::Invalid;
}

std::string ToLabelName(const std::string_view text)
{
	std::string name = {};
	for (const auto c : text.substr(0, MaxChars)) {
		if (name.size() == DotLocation) {
			name.push_back('.');
		}
		name.push_back(
		        static_cast<char>(toupper(static_cast<unsigned char>(c))));
	}
	return name;
}

std::string LabelFromFcbName(std::string_view name11)
{
	while (!name11.empty() && name11.back() == Blank) {
		name11.remove_suffix(1);
	}
	return std::string(name11);
}

} // namespace LabelCommand

using namespace LabelCommand;

namespace {

// Guest memory for the FCBs and the media ID buffer, given back on exit.
class ScratchBlock {
public:
	static constexpr uint16_t FindFcb   = 0x00;
	static constexpr uint16_t FindDta   = 0x30;
	static constexpr uint16_t DeleteFcb = 0x60;
	static constexpr uint16_t MediaId   = 0x90;

	ScratchBlock()
	{
		uint16_t paragraphs = 0x0b;
		uint16_t segment    = 0;
		if (DOS_AllocateMemory(&segment, &paragraphs)) {
			seg = segment;
		}
	}
	~ScratchBlock()
	{
		if (seg) {
			DOS_FreeMemory(*seg);
		}
	}
	ScratchBlock(const ScratchBlock&)            = delete;
	ScratchBlock& operator=(const ScratchBlock&) = delete;

	std::optional<uint16_t> seg = {};
};

void PrepareLabelFcb(const uint16_t seg, const uint16_t offset, const uint8_t drive)
{
	DOS_FCB fcb(seg, offset, false);
	fcb.Create(true);
	fcb.SetName(static_cast<uint8_t>(drive + 1), "????????", "???");
	fcb.SetAttr(FatAttributeFlags::Volume);
}

std::optional<std::string> FindOldLabel(const uint16_t seg, const uint8_t drive)
{
	PrepareLabelFcb(seg, ScratchBlock::FindFcb, drive);
	const RealPt old_dta = dos.dta();
	dos.dta(RealMake(seg, ScratchBlock::FindDta));
	const bool found = DOS_FCBFindFirst(seg, ScratchBlock::FindFcb);
	dos.dta(old_dta);
	if (!found) {
		return {};
	}
	// The result is an extended FCB: 7 header bytes, the drive, 11 name bytes.
	char name11[MaxChars] = {};
	MEM_BlockRead(PhysicalMake(seg, ScratchBlock::FindDta + 8), name11, MaxChars);
	return LabelFromFcbName(std::string_view(name11, MaxChars));
}

// INT 21h 6900h, as LABEL.ASM Get_Serial_Num asks for it.
std::optional<uint32_t> MediaSerial(const uint16_t seg, const uint8_t drive)
{
	const auto old_ds = SegValue(ds);
	SegSet16(ds, seg);
	reg_ax = 0x6900;
	reg_bx = static_cast<uint16_t>(drive + 1);
	reg_dx = ScratchBlock::MediaId;
	CALLBACK_RunRealInt(0x21);
	const bool failed = (reg_flags & FLAG_CF) != 0;
	SegSet16(ds, old_ds);
	if (failed) {
		return {};
	}
	return mem_readd(PhysicalMake(seg, ScratchBlock::MediaId + 2));
}

bool IsRemoteDrive(const uint8_t drive)
{
	reg_ax = 0x4409;
	reg_bx = static_cast<uint16_t>(drive + 1);
	CALLBACK_RunRealInt(0x21);
	return (reg_flags & FLAG_CF) == 0 && (reg_dx & 0x1000) != 0;
}

// LABEL.ASM, Check_Trans_Drive: "X:\" names another drive after 60h.
bool IsTranslatedDrive(const char letter)
{
	const char root[]              = {letter, ':', '\\', '\0'};
	char canonical[DOS_PATHLENGTH] = {};
	return DOS_Canonicalize(root, canonical) && canonical[0] != letter;
}

// Bit 7 of the device word: the console, not a file STDIN was redirected from.
bool InputIsADevice()
{
	constexpr uint16_t IsDevice = 0x80;
	const auto handle           = RealHandle(STDIN);
	return handle >= DOS_FILES || !Files[handle] ||
	       (Files[handle]->GetInformation() & IsDevice) != 0;
}

std::string ReadLine()
{
	uint8_t buffer[CommandTail::MaxCmdtailBufferSize] = {};
	uint16_t count                                    = 0;
	if (InputIsADevice()) {
		count = sizeof(buffer);
		if (!DOS_ReadFile(STDIN, buffer, &count)) {
			count = 0;
		}
	} else {
		// As 0Ah: a file gives one line per answer, the next stays unread
		while (count < sizeof(buffer)) {
			uint16_t one = 1;
			if (!DOS_ReadFile(STDIN, &buffer[count], &one) || one == 0) {
				break;
			}
			if (buffer[count++] == '\n') {
				break;
			}
		}
	}
	return TextOfInputLine(
	        std::string_view(reinterpret_cast<const char*>(buffer), count));
}

} // namespace

void LABEL::Run()
{
	if (HelpRequested()) {
		MoreOutputStrings output(*this);
		output.AddString(MSG_Get("PROGRAM_LABEL_HELP_LONG"));
		output.Display();
		return;
	}
	result_errorcode = 1;

	CommandTail tail = {};
	MEM_BlockRead(PhysicalMake(dos.psp(), 128), &tail, 128);
	const auto tail_size = std::min<size_t>(tail.count,
	                                        CommandTail::MaxCmdtailBufferSize);
	const auto parts = ParseCommandTail(std::string_view(tail.buffer, tail_size));

	uint8_t drive = DOS_GetDefaultDrive();
	if (parts.drive) {
		const char typed = *parts.drive;
		if (typed < 'A' || typed > 'Z' || !Drives.at(drive_index(typed))) {
			WriteOut(MSG_Get("PROGRAM_LABEL_INVALID_DRIVE"));
			return;
		}
		drive = drive_index(typed);
	}
	const char letter = drive_letter(drive);
	if (IsRemoteDrive(drive)) {
		WriteOut(MSG_Get("PROGRAM_LABEL_NETWORK_DRIVE"));
		return;
	}
	if (IsTranslatedDrive(letter)) {
		WriteOut(MSG_Get("PROGRAM_LABEL_SUBST_DRIVE"));
		return;
	}
	if (Drives.at(drive)->IsReadOnly()) {
		WriteOut(MSG_Get("PROGRAM_LABEL_READ_ONLY"), letter);
		return;
	}
	const ScratchBlock scratch = {};
	if (!scratch.seg) {
		WriteOut(MSG_Get("PROGRAM_LABEL_NO_MEMORY"));
		return;
	}
	const auto seg       = *scratch.seg;
	const auto old_label = FindOldLabel(seg, drive);

	// LABEL.ASM, Get_New_Label: the old label and serial show once, before
	// the first prompt; an empty answer is accepted only when typed.
	std::string text = parts.ask ? std::string() : parts.text;
	bool typed       = false;
	for (;;) {
		const auto check = CheckLabelText(text);
		if (check == TextCheck::Valid ||
		    (check == TextCheck::Empty && typed)) {
			break;
		}
		if (!typed) {
			if (old_label) {
				WriteOut(MSG_Get("PROGRAM_LABEL_IS"),
				         letter,
				         old_label->c_str());
			} else {
				WriteOut(MSG_Get("PROGRAM_LABEL_NO_LABEL"), letter);
			}
			if (const auto serial = MediaSerial(seg, drive)) {
				WriteOut(MSG_Get("PROGRAM_LABEL_SERIAL"),
				         static_cast<unsigned>(*serial >> 16),
				         static_cast<unsigned>(*serial & 0xffff));
			}
		}
		if (check == TextCheck::Invalid) {
			WriteOut(MSG_Get("PROGRAM_LABEL_INVALID_CHARACTERS"));
		}
		WriteOut(MSG_Get("PROGRAM_LABEL_PROMPT"));
		text  = ReadLine();
		typed = true;
	}

	// LABEL.ASM, Check_Delete: an ENTER over an existing label asks first.
	bool delete_old = old_label.has_value();
	if (old_label && text.empty()) {
		for (;;) {
			WriteOut(MSG_Get("PROGRAM_LABEL_DELETE_PROMPT"));
			uint8_t key    = 0;
			uint16_t count = 1;
			if (!DOS_ReadFile(STDIN, &key, &count) || count == 0) {
				delete_old = false;
				break;
			}
			const auto answer = toupper(key);
			if (answer == 'Y') {
				break;
			}
			if (answer == 'N') {
				delete_old = false;
				break;
			}
		}
	}
	WriteOut("\n");

	if (delete_old) {
		PrepareLabelFcb(seg, ScratchBlock::DeleteFcb, drive);
		DOS_FCBDeleteFile(seg, ScratchBlock::DeleteFcb);
	}
	if (!text.empty()) {
		const auto create = [letter](const std::string_view label) {
			const auto path = std::string{letter, ':', '\\'} +
			                  ToLabelName(label);
			uint16_t handle = 0;
			if (!DOS_CreateFile(path.c_str(),
			                    FatAttributeFlags::Volume,
			                    &handle)) {
				return false;
			}
			DOS_CloseFile(handle);
			return true;
		};
		if (!create(text)) {
			if (dos.errorcode == DOSERR_TOO_MANY_OPEN_FILES) {
				WriteOut(MSG_Get("PROGRAM_LABEL_TOO_MANY_FILES"));
			} else {
				WriteOut(MSG_Get("PROGRAM_LABEL_NO_ROOM"));
				// LABEL.ASM puts the old label back when the
				// new one failed.
				if (old_label && delete_old) {
					create(*old_label);
				}
			}
			return;
		}
	}
	result_errorcode = 0;
}

// Texts of MS-DOS 4.0 LABL.SKL and USA-MS.MSG.
void LABEL::AddMessages()
{
	if (MSG_Exists("PROGRAM_LABEL_HELP")) {
		return;
	}
	MSG_Add("PROGRAM_LABEL_HELP",
	        "Creates, changes, or deletes the volume label of a disk.\n");
	MSG_Add("PROGRAM_LABEL_HELP_LONG",
	        "Creates, changes, or deletes the volume label of a disk.\n"
	        "\n"
	        "Usage:\n"
	        "  [color=light-green]label[reset] [color=white]DRIVE:[reset][color=light-cyan]LABEL[reset]\n"
	        "  [color=light-green]label[reset] [color=white]DRIVE:[reset]\n"
	        "\n"
	        "Where:\n"
	        "  [color=white]DRIVE[reset] is the drive to label, the current one when left out.\n"
	        "  [color=light-cyan]LABEL[reset] is the new label, up to 11 characters, blanks allowed.\n"
	        "\n"
	        "Notes:\n"
	        "  - Without a label, [color=light-green]label[reset] shows the current one and asks for a new one;\n"
	        "    ENTER on an existing label asks whether to delete it.\n"
	        "  - On a disk image the label is written to the disk. On a host folder or an\n"
	        "    archive it lasts until the drive is unmounted, as a MOUNT -label does.\n"
	        "  - CD-ROM drives and drives mounted read-only cannot be labelled.\n");
	MSG_Add("PROGRAM_LABEL_INVALID_DRIVE", "Invalid drive specification\n");
	MSG_Add("PROGRAM_LABEL_NETWORK_DRIVE", "Cannot LABEL a network drive\n");
	MSG_Add("PROGRAM_LABEL_SUBST_DRIVE",
	        "Cannot LABEL a SUBSTed or ASSIGNed drive\n");
	MSG_Add("PROGRAM_LABEL_INVALID_CHARACTERS",
	        "Invalid characters in volume label\n");
	MSG_Add("PROGRAM_LABEL_NO_LABEL", "Volume in drive %c has no label\n");
	MSG_Add("PROGRAM_LABEL_IS", "Volume in drive %c is %s\n");
	MSG_Add("PROGRAM_LABEL_SERIAL", "Volume Serial Number is %04X-%04X\n");
	MSG_Add("PROGRAM_LABEL_PROMPT",
	        "Volume label (11 characters, ENTER for none)? ");
	MSG_Add("PROGRAM_LABEL_DELETE_PROMPT",
	        "\nDelete current volume label (Y/N)? ");
	MSG_Add("PROGRAM_LABEL_NO_ROOM", "Cannot make directory entry\n");
	MSG_Add("PROGRAM_LABEL_READ_ONLY", "Drive %c is read-only; its label cannot change\n");
	MSG_Add("PROGRAM_LABEL_TOO_MANY_FILES", "Too many open files\n");
	MSG_Add("PROGRAM_LABEL_NO_MEMORY", "Insufficient memory\n");
}
