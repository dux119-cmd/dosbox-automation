// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

#include "archive_fixtures.h"
#include "dos/archive/archive_reader.h"
#include "dos/archive/sidecar_record.h"
#include "dos/drives.h"
#include "dosbox_test_fixture.h"
#include "test_temp_dir.h"

namespace {

namespace std_fs = std::filesystem;
using ArchiveFixtures::Kind;
using ArchiveFixtures::MinimalIso;
using ArchiveFixtures::WriteArchive;

constexpr int SectorSize   = 512;
constexpr int DirEntrySize = 32;

void Put16(std::vector<uint8_t>& image, const size_t at, const uint16_t value)
{
	image[at]     = static_cast<uint8_t>(value & 0xff);
	image[at + 1] = static_cast<uint8_t>(value >> 8);
}

uint16_t Get16(const std::vector<uint8_t>& image, const size_t at)
{
	return static_cast<uint16_t>(image[at] | (image[at + 1] << 8));
}

// A 360K DOS 2 floppy: FAT12, two FATs of two sectors, 112 root entries.
// The extended BPB (signature 29h at 26h, serial at 27h) only when a
// serial is given, as on disks formatted before DOS 4.0.
std_fs::path WriteFloppy(const std_fs::path& path,
                         const std::optional<uint32_t> serial = {})
{
	std::vector<uint8_t> image(368640, 0);
	image[0]              = 0xeb;
	image[1]              = 0x3c;
	image[2]              = 0x90;
	const std::string oem = "MSDOS5.0";
	std::copy(oem.begin(), oem.end(), image.begin() + 3);
	Put16(image, 11, SectorSize);
	image[13] = 2; // sectors per cluster
	Put16(image, 14, 1);
	image[16] = 2; // FAT copies
	Put16(image, 17, 112);
	Put16(image, 19, 720);
	image[21] = 0xfd;
	Put16(image, 22, 2); // sectors per FAT
	Put16(image, 24, 9);
	Put16(image, 26, 2);
	if (serial) {
		image[0x26] = 0x29;
		for (int i = 0; i < 4; ++i) {
			image[0x27 + i] = static_cast<uint8_t>(*serial >> (8 * i));
		}
		const std::string tail = "NO NAME    FAT12   ";
		std::copy(tail.begin(), tail.end(), image.begin() + 0x2b);
	}
	image[510] = 0x55;
	image[511] = 0xaa;
	for (const int fat_sector : {1, 3}) {
		image[fat_sector * SectorSize]     = 0xfd;
		image[fat_sector * SectorSize + 1] = 0xff;
		image[fat_sector * SectorSize + 2] = 0xff;
	}
	std::ofstream(path, std::ios::binary)
	        .write(reinterpret_cast<const char*>(image.data()),
	               static_cast<std::streamsize>(image.size()));
	return path;
}

std::vector<uint8_t> Slurp(const std_fs::path& path)
{
	std::ifstream in(path, std::ios::binary);
	return {std::istreambuf_iterator<char>(in),
	        std::istreambuf_iterator<char>()};
}

struct RootEntry {
	std::string name = {};
	uint8_t attr     = 0;
};

// The root directory as the boot sector places it, read off the host file.
std::vector<RootEntry> RootEntries(const std_fs::path& path)
{
	const auto image      = Slurp(path);
	const auto reserved   = Get16(image, 14);
	const auto fats       = image[16];
	const auto root_count = Get16(image, 17);
	const auto per_fat    = Get16(image, 22);
	const size_t root_at = (reserved + fats * per_fat) * size_t{SectorSize};

	std::vector<RootEntry> entries = {};
	for (size_t i = 0; i < root_count; ++i) {
		const auto at = root_at + i * DirEntrySize;
		if (image[at] == 0x00) {
			break;
		}
		entries.push_back(
		        {std::string(image.begin() + at, image.begin() + at + 11),
		         image[at + 11]});
	}
	return entries;
}

// The first live entry with the volume bit that is not a long name piece.
std::optional<RootEntry> RootLabel(const std_fs::path& path)
{
	for (const auto& entry : RootEntries(path)) {
		if (static_cast<uint8_t>(entry.name[0]) != 0xe5 &&
		    (entry.attr & 0x08) && entry.attr != 0x0f) {
			return entry;
		}
	}
	return {};
}

void WriteRootEntry(const std_fs::path& path, const int index,
                    const std::string& name11, const uint8_t attr)
{
	ASSERT_EQ(name11.size(), 11u);
	const size_t root_at = 5 * size_t{SectorSize};
	std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
	file.seekp(static_cast<std::streamoff>(root_at +
	                                       index * size_t{DirEntrySize}));
	file.write(name11.data(), 11);
	file.put(static_cast<char>(attr));
	ASSERT_TRUE(file.good()) << path;
}

class VolumeLabelTest : public DOSBoxTestFixture {
protected:
	void SetUp() override
	{
		DOSBoxTestFixture::SetUp();
		tmp = TestTempDir::MakeUnique("label_");
		ASSERT_FALSE(tmp.empty());
		local = std::make_shared<localDrive>(
		        (tmp.string() + CROSS_FILESPLIT).c_str(),
		        512,
		        32,
		        32765,
		        16000,
		        0xf8,
		        false,
		        true);
	}
	void TearDown() override
	{
		if (c_taken) {
			Drives.at(DriveC) = previous_c;
		}
		local.reset();
		std::error_code ec = {};
		std::filesystem::remove_all(tmp, ec);
		DOSBoxTestFixture::TearDown();
	}
	std::shared_ptr<fatDrive> MountFloppy(const std_fs::path& path,
	                                      const bool read_only = false)
	{
		auto drive = std::make_shared<fatDrive>(
		        path.string().c_str(), 512, 0, 0, 0, 0xF0, read_only);
		EXPECT_TRUE(drive->created_successfully) << path;
		return drive;
	}
	std::shared_ptr<ArchiveDrive> MountArchive(const std::string& label = "GAME")
	{
		const auto archive = tmp / "game.zip";
		auto error         = std::string();
		EXPECT_TRUE(WriteArchive(archive,
		                         Kind::ZipStored,
		                         {
		                                 {"game.exe", "x"}
                },
		                         nullptr,
		                         &error))
		        << error;
		auto opened = ArchiveMount::ArchiveReader::Open(archive,
		                                                ArchiveMount::Limits{});
		EXPECT_EQ(opened.error, ArchiveMount::Error::None) << opened.detail;
		auto made = ArchiveDrive::Create(std::move(opened.reader),
		                                 {},
		                                 "",
		                                 label,
		                                 archive.string(),
		                                 ArchiveDrive::Options{});
		EXPECT_TRUE(made.drive) << made.error;
		return made.drive;
	}
	void UseLocalAsC()
	{
		UseAsC(local);
	}
	// A sidecar over the archive: the drive DOS can label.
	std::shared_ptr<UnionDrive> MountUnion(const std::shared_ptr<ArchiveDrive>& lower)
	{
		const auto sidecar = tmp / "sidecar";
		std_fs::create_directories(sidecar);
		ArchiveMount::SidecarRecord identity = {};
		identity.archive_path  = (tmp / "game.zip").string();
		identity.identity_hash = std::string(64, 'a');
		identity.entry_count   = 1;
		identity.total_bytes   = 1;
		auto error             = std::string();
		auto top = UnionDrive::Create(lower, sidecar, 'F', identity, &error);
		EXPECT_TRUE(top) << error;
		return top;
	}
	void UseAsC(std::shared_ptr<DOS_Drive> drive)
	{
		if (!c_taken) {
			previous_c = Drives.at(DriveC);
		}
		Drives.at(DriveC) = std::move(drive);
		c_taken           = true;
	}
	// DOS 2 and 3 LABEL and FORMAT /V: an extended FCB create, attribute 08h.
	static bool FcbCreateLabel(const uint8_t fcb_drive, const char* name8,
	                           const char* ext3)
	{
		const uint16_t seg = DOS_GetMemory(3);
		DOS_FCB fcb(seg, 0);
		fcb.Create(true);
		fcb.SetName(fcb_drive, name8, ext3);
		fcb.SetAttr(FatAttributeFlags::Volume);
		return DOS_FCBCreate(seg, 0) && DOS_FCBClose(seg, 0);
	}
	// LABEL's delete: an extended FCB, attribute 08h, name ???????????.
	static bool FcbDeleteLabel(const uint8_t fcb_drive,
	                           const char* name8 = "????????",
	                           const char* ext3  = "???")
	{
		const uint16_t seg = DOS_GetMemory(3);
		DOS_FCB fcb(seg, 0);
		fcb.Create(true);
		fcb.SetName(fcb_drive, name8, ext3);
		fcb.SetAttr(FatAttributeFlags::Volume);
		return DOS_FCBDeleteFile(seg, 0);
	}
	// The 11 name bytes of an extended FCB search, as LABEL reads them.
	static std::string FcbVolumeSearch(const uint8_t fcb_drive,
	                                   const char* name8 = "????????",
	                                   const char* ext3  = "???")
	{
		const uint16_t seg = DOS_GetMemory(6);
		DOS_FCB fcb(seg, 0);
		fcb.Create(true);
		fcb.SetName(fcb_drive, name8, ext3);
		fcb.SetAttr(FatAttributeFlags::Volume);
		const RealPt old_dta = dos.dta();
		dos.dta(RealMake(seg, 0x40));
		const bool found = DOS_FCBFindFirst(seg, 0);
		dos.dta(old_dta);
		if (!found) {
			return "(none)";
		}
		char name11[11] = {};
		MEM_BlockRead(PhysicalMake(seg, 0x40 + 8), name11, sizeof(name11));
		return std::string(name11, sizeof(name11));
	}
	static std::string VolumeSearch(const char* pattern)
	{
		if (!DOS_FindFirst(pattern, FatAttributeFlags::Volume, false)) {
			return "(none)";
		}
		DOS_DTA dta(dos.dta());
		DOS_DTA::Result result = {};
		dta.GetResult(result);
		return result.name;
	}
	std::vector<std::string> HostNames() const
	{
		std::vector<std::string> names = {};
		for (const auto& entry : std_fs::directory_iterator(tmp)) {
			names.push_back(entry.path().filename().string());
		}
		return names;
	}
	static constexpr int DriveC           = 2;
	std::filesystem::path tmp             = {};
	std::shared_ptr<localDrive> local     = {};
	std::shared_ptr<DOS_Drive> previous_c = {};
	bool c_taken                          = false;
};

// MOUNT -label locks the cache against SetLabel; a label change must win.
TEST_F(VolumeLabelTest, ALocalDriveTakesANewLabelAfterMountSetOne)
{
	local->dirCache.SetLabel("MOUNTED", false, false);
	EXPECT_TRUE(local->SetVolumeLabel("SALES199.1"));
	EXPECT_STREQ(local->GetLabel(), "SALES199.1");
	EXPECT_TRUE(local->DeleteVolumeLabel());
	EXPECT_STREQ(local->GetLabel(), "");
}

TEST_F(VolumeLabelTest, AFatImageWritesTheRootEntryAndRemovesIt)
{
	const auto path = WriteFloppy(tmp / "floppy.img");
	{
		const auto fat = MountFloppy(path);
		ASSERT_TRUE(fat->SetVolumeLabel("SALES199.1"));
		EXPECT_STREQ(fat->GetLabel(), "SALES199.1");
	}
	const auto label = RootLabel(path);
	ASSERT_TRUE(label.has_value());
	EXPECT_EQ(label->name, "SALES1991  ");
	EXPECT_EQ(label->attr, 0x08);

	{
		const auto fat = MountFloppy(path);
		ASSERT_TRUE(fat->DeleteVolumeLabel());
		EXPECT_STREQ(fat->GetLabel(), "");
	}
	EXPECT_FALSE(RootLabel(path).has_value());
}

// Microsoft FAT specification, DIR_Name[0]: E5h marks a free entry, so a
// name that begins with E5h is stored as 05h.
const std::string LabelWithE5h = std::string(1, '\xe5') + "ABC";
const std::string EntryWith05h = std::string(1, '\x05') + "ABC       ";

TEST_F(VolumeLabelTest, AFatLabelBeginningWithE5hIsStoredAs05h)
{
	const auto path = WriteFloppy(tmp / "floppy.img");
	{
		const auto fat = MountFloppy(path);
		ASSERT_TRUE(fat->SetVolumeLabel(LabelWithE5h));
		EXPECT_EQ(fat->GetLabel(), LabelWithE5h);
	}
	const auto label = RootLabel(path);
	ASSERT_TRUE(label.has_value());
	EXPECT_EQ(label->name, EntryWith05h);
}

TEST_F(VolumeLabelTest, AFatLabelStoredWith05hReadsBackAsE5h)
{
	const auto path = WriteFloppy(tmp / "floppy.img");
	WriteRootEntry(path, 0, EntryWith05h, 0x08);
	const auto fat = MountFloppy(path);
	EXPECT_EQ(fat->LabelOnMedium(), LabelWithE5h);
	UseAsC(fat);
	EXPECT_EQ(VolumeSearch("C:\\????????.???"), LabelWithE5h);
}

// The same rule for file names: MS-DOS 5.00 stores 05h for a created E5h
// name and finds it again as E5h.
TEST_F(VolumeLabelTest, AFatFileNameBeginningWithE5hIsStoredAs05h)
{
	const auto path = WriteFloppy(tmp / "floppy.img");
	const auto name = "C:\\" + LabelWithE5h + ".TXT";
	{
		UseAsC(MountFloppy(path));
		uint16_t handle = 0;
		ASSERT_TRUE(DOS_CreateFile(name.c_str(), {}, &handle, true));
		EXPECT_TRUE(DOS_CloseFile(handle, true));
		EXPECT_TRUE(DOS_FileExists(name.c_str()));
		ASSERT_TRUE(DOS_FindFirst("C:\\?ABC.TXT", {}, false));
		DOS_DTA dta(dos.dta());
		DOS_DTA::Result result = {};
		dta.GetResult(result);
		EXPECT_EQ(result.name, LabelWithE5h + ".TXT");
		UseLocalAsC();
	}
	const auto entries = RootEntries(path);
	ASSERT_EQ(entries.size(), 1u);
	EXPECT_EQ(entries[0].name, EntryWith05h.substr(0, 8) + "TXT");
}

TEST_F(VolumeLabelTest, ASecondLabelReplacesTheFirstEntry)
{
	const auto path = WriteFloppy(tmp / "floppy.img");
	WriteRootEntry(path, 0, "README  TXT", 0x20);
	{
		const auto fat = MountFloppy(path);
		ASSERT_TRUE(fat->SetVolumeLabel("ONE"));
		ASSERT_TRUE(fat->SetVolumeLabel("TWO"));
	}
	const auto entries = RootEntries(path);
	ASSERT_EQ(entries.size(), 2u);
	EXPECT_EQ(entries[0].name, "README  TXT");
	EXPECT_EQ(entries[0].attr, 0x20);
	EXPECT_EQ(entries[1].name, "TWO        ");
	EXPECT_EQ(entries[1].attr, 0x08);
}

// A long name piece carries 0Fh, volume bit included; it is never the label.
TEST_F(VolumeLabelTest, ALongNameEntryIsNotTakenForTheLabel)
{
	const auto path = WriteFloppy(tmp / "floppy.img");
	WriteRootEntry(path, 0, std::string("\x41l\0o\0n\0g\0\0\0", 11), 0x0f);
	{
		const auto fat = MountFloppy(path);
		ASSERT_TRUE(fat->SetVolumeLabel("DISK"));
	}
	const auto entries = RootEntries(path);
	ASSERT_EQ(entries.size(), 2u);
	EXPECT_EQ(entries[0].attr, 0x0f);
	EXPECT_EQ(entries[1].name, "DISK       ");
}

// RBIL INT 21h AH=11h: a search for the volume attribute alone returns the
// label even when the current directory is not the root.
TEST_F(VolumeLabelTest, AVolumeSearchFromASubdirectoryFindsTheLabel)
{
	const auto path = WriteFloppy(tmp / "floppy.img");
	{
		const auto fat = MountFloppy(path);
		ASSERT_TRUE(fat->SetVolumeLabel("OLD"));
	}
	UseAsC(MountFloppy(path));
	ASSERT_TRUE(DOS_MakeDir("C:\\SUB"));
	ASSERT_TRUE(DOS_ChangeDir("C:\\SUB"));

	EXPECT_EQ(VolumeSearch("C:????????.???"), "OLD");
	DOS_ChangeDir("C:\\");
}

// The volume bit of a long name piece does not make it the label.
TEST_F(VolumeLabelTest, AVolumeSearchSkipsLongNamePieces)
{
	const auto path = WriteFloppy(tmp / "floppy.img");
	WriteRootEntry(path, 0, std::string("\x41l\0o\0n\0g\0\0\0", 11), 0x0f);
	WriteRootEntry(path, 1, "DISK       ", 0x08);
	UseAsC(MountFloppy(path));

	EXPECT_EQ(VolumeSearch("C:\\????????.???"), "DISK");
}

// MS-DOS 5.00 answers DISK.  1 to 4Eh and the 11 entry bytes to 11h for
// this label.
TEST_F(VolumeLabelTest, AVolumeSearchKeepsTheBlanksInsideALabel)
{
	const auto path = WriteFloppy(tmp / "floppy.img");
	WriteRootEntry(path, 0, "DISK      1", 0x08);
	UseAsC(MountFloppy(path));

	EXPECT_EQ(VolumeSearch("C:\\????????.???"), "DISK.  1");
	EXPECT_EQ(FcbVolumeSearch(3), "DISK      1");
}

TEST_F(VolumeLabelTest, AFullRootRefusesALabelAndKeepsTheOldOne)
{
	const auto path = WriteFloppy(tmp / "floppy.img");
	for (int i = 0; i < 112; ++i) {
		const auto name = "FILE" + std::to_string(1000 + i) + "   ";
		WriteRootEntry(path, i, name, 0x20);
	}
	const auto fat = MountFloppy(path);
	fat->dirCache.SetLabel("OLD", false, true);
	EXPECT_FALSE(fat->SetVolumeLabel("NEW"));
	EXPECT_STREQ(fat->GetLabel(), "OLD");
	EXPECT_FALSE(RootLabel(path).has_value());
}

TEST_F(VolumeLabelTest, AReadOnlyFatImageRefusesALabel)
{
	const auto path = WriteFloppy(tmp / "floppy.img");
	const auto fat  = MountFloppy(path, true);
	EXPECT_FALSE(fat->SetVolumeLabel("X"));
	EXPECT_FALSE(fat->DeleteVolumeLabel());
	EXPECT_FALSE(RootLabel(path).has_value());
}

// VOL and LABEL show the serial FORMAT wrote into the image.
TEST_F(VolumeLabelTest, AFatImageReportsItsBootSectorSerial)
{
	const auto fat = MountFloppy(WriteFloppy(tmp / "floppy.img", 0x1234abcd));
	EXPECT_EQ(fat->GetSerial(), std::optional<uint32_t>(0x1234abcd));
}

TEST_F(VolumeLabelTest, AFatImageWithoutAnExtendedBpbHasNoSerial)
{
	const auto fat = MountFloppy(WriteFloppy(tmp / "floppy.img"));
	EXPECT_EQ(fat->GetSerial(), std::nullopt);
}

TEST_F(VolumeLabelTest, AFolderHasNoSerialOfItsOwn)
{
	EXPECT_EQ(local->GetSerial(), std::nullopt);
}

TEST_F(VolumeLabelTest, AnIsoImageRefusesALabel)
{
	const auto path = tmp / "cd.iso";
	std::ofstream(path, std::ios::binary) << MinimalIso();
	int error = 0;
	const auto iso = std::make_shared<isoDrive>('D', path.string().c_str(), 0xf8, error);
	ASSERT_EQ(error, 0);
	const std::string before = iso->GetLabel();
	EXPECT_FALSE(iso->SetVolumeLabel("X"));
	EXPECT_FALSE(iso->DeleteVolumeLabel());
	EXPECT_EQ(iso->GetLabel(), before);
}

TEST_F(VolumeLabelTest, AFolderMountedAsCdRefusesALabel)
{
	int error     = 0;
	const auto cd = std::make_shared<cdromDrive>(
	        'E', (tmp.string() + CROSS_FILESPLIT).c_str(), 2048, 1, 65535, 0, 0xf8, error);
	// A folder has no full MSCDEX support (5); MOUNT warns and goes on.
	ASSERT_TRUE(error == 0 || error == 5) << error;
	EXPECT_FALSE(cd->SetVolumeLabel("X"));
	EXPECT_FALSE(cd->DeleteVolumeLabel());
}

TEST_F(VolumeLabelTest, TheVirtualDriveRefusesALabel)
{
	const auto z             = std::make_shared<Virtual_Drive>();
	const std::string before = z->GetLabel();
	EXPECT_FALSE(z->SetVolumeLabel("X"));
	EXPECT_FALSE(z->DeleteVolumeLabel());
	EXPECT_EQ(z->GetLabel(), before);
}

// An archive's label lives until unmount, as MOUNT -label.
TEST_F(VolumeLabelTest, AnArchiveDriveKeepsALabelInMemory)
{
	const auto drive = MountArchive();
	ASSERT_TRUE(drive);
	ASSERT_TRUE(drive->SetVolumeLabel("SALES199.1"));
	EXPECT_STREQ(drive->GetLabel(), "SALES199.1");

	std::vector<ArchiveDrive::Listing> found = {};
	ASSERT_TRUE(drive->List("", "*.*", FatAttributeFlags::Volume, true, found));
	ASSERT_EQ(found.size(), 1u);
	EXPECT_EQ(found[0].name, "SALES199.1");
}

TEST_F(VolumeLabelTest, AnArchiveDriveWithItsLabelDeletedHasNone)
{
	const auto drive = MountArchive();
	ASSERT_TRUE(drive);
	ASSERT_TRUE(drive->DeleteVolumeLabel());
	EXPECT_STREQ(drive->GetLabel(), "");

	std::vector<ArchiveDrive::Listing> found = {};
	ASSERT_TRUE(drive->List("", "*.*", FatAttributeFlags::Volume, true, found));
	EXPECT_TRUE(found.empty());
}

TEST_F(VolumeLabelTest, AUnionDriveLabelsItsLower)
{
	const auto lower = MountArchive();
	ASSERT_TRUE(lower);
	const auto top = MountUnion(lower);
	ASSERT_TRUE(top);

	ASSERT_TRUE(top->SetVolumeLabel("NEWNAME"));
	EXPECT_STREQ(top->GetLabel(), "NEWNAME");
	EXPECT_STREQ(lower->GetLabel(), "NEWNAME");
	ASSERT_TRUE(top->DeleteVolumeLabel());
	EXPECT_STREQ(lower->GetLabel(), "");
}

// MS-DOS 4.0 LABEL.ASM creates the label with 5Bh, CX=08h.
TEST_F(VolumeLabelTest, ACreateWithTheVolumeAttributeLabelsTheDrive)
{
	UseLocalAsC();
	uint16_t handle = 0;
	ASSERT_TRUE(DOS_CreateFile(
	        "C:\\SALES199.1", FatAttributeFlags::Volume, &handle, true));
	EXPECT_TRUE(DOS_CloseFile(handle, true));
	EXPECT_STREQ(local->GetLabel(), "SALES199.1");
	EXPECT_TRUE(HostNames().empty());
}

// LABEL allows blanks inside a label; file name parsing strips them.
TEST_F(VolumeLabelTest, ALabelCreateKeepsItsBlanks)
{
	UseLocalAsC();
	uint16_t handle = 0;
	ASSERT_TRUE(DOS_CreateFile("C:\\my disk", FatAttributeFlags::Volume, &handle, true));
	EXPECT_TRUE(DOS_CloseFile(handle, true));
	EXPECT_STREQ(local->GetLabel(), "MY DISK");
}

TEST_F(VolumeLabelTest, AnFcbDeleteWithTheVolumeAttributeRemovesTheLabel)
{
	UseLocalAsC();
	std::ofstream(tmp / "KEEP.TXT") << "x";
	local->EmptyCache();
	ASSERT_TRUE(local->SetVolumeLabel("DISK"));
	ASSERT_EQ(HostNames(), std::vector<std::string>{"KEEP.TXT"});
	EXPECT_TRUE(FcbDeleteLabel(3));
	EXPECT_STREQ(local->GetLabel(), "");
	EXPECT_EQ(HostNames(), std::vector<std::string>{"KEEP.TXT"});
}

TEST_F(VolumeLabelTest, AnFcbLabelDeleteForAnotherNameLeavesTheLabel)
{
	UseLocalAsC();
	ASSERT_TRUE(local->SetVolumeLabel("DISK"));
	EXPECT_FALSE(FcbDeleteLabel(3, "OTHER   ", "   "));
	EXPECT_STREQ(local->GetLabel(), "DISK");
	EXPECT_TRUE(FcbDeleteLabel(3, "DI??    ", "   "));
	EXPECT_STREQ(local->GetLabel(), "");
}

TEST_F(VolumeLabelTest, AnFcbLabelDeleteWithNulBytesInTheNameMatchesNothing)
{
	UseLocalAsC();
	ASSERT_TRUE(local->SetVolumeLabel("DISK"));
	EXPECT_FALSE(FcbDeleteLabel(3,
	                            std::string(8, '\0').c_str(),
	                            std::string(3, '\0').c_str()));
	EXPECT_STREQ(local->GetLabel(), "DISK");
}

TEST_F(VolumeLabelTest, AnFcbLabelDeleteOnZOrPastTheLastDriveFails)
{
	EXPECT_FALSE(FcbDeleteLabel(26));
	EXPECT_FALSE(FcbDeleteLabel(200));
}

TEST_F(VolumeLabelTest, ACreateWithVolumeAndArchiveIsAnOrdinaryFile)
{
	UseLocalAsC();
	local->dirCache.SetLabel("OLD", false, false);
	uint16_t handle              = 0;
	const FatAttributeFlags attr = static_cast<uint8_t>(
	        FatAttributeFlags::Volume | FatAttributeFlags::Archive);
	ASSERT_TRUE(DOS_CreateFile("C:\\FILE.TXT", attr, &handle, true));
	EXPECT_TRUE(DOS_CloseFile(handle, true));
	EXPECT_STREQ(local->GetLabel(), "OLD");
	EXPECT_EQ(HostNames().size(), 1u);
}

// A label belongs to the root directory.
TEST_F(VolumeLabelTest, ALabelCreateInASubdirectoryIsRefused)
{
	UseLocalAsC();
	std_fs::create_directory(tmp / "SUB");
	local->EmptyCache();
	uint16_t handle = 0;
	EXPECT_FALSE(
	        DOS_CreateFile("C:\\SUB\\X", FatAttributeFlags::Volume, &handle, true));
	EXPECT_EQ(dos.errorcode, DOSERR_ACCESS_DENIED);
	EXPECT_TRUE(std_fs::is_empty(tmp / "SUB"));
	EXPECT_STREQ(local->GetLabel(), "");
}

// The FCB's name fields are blank-padded; the label is not.
TEST_F(VolumeLabelTest, AnFcbCreateWithTheVolumeAttributeTakesNoPadding)
{
	UseLocalAsC();
	ASSERT_TRUE(FcbCreateLabel(3, "GAME    ", "   "));
	EXPECT_STREQ(local->GetLabel(), "GAME");
	ASSERT_TRUE(FcbCreateLabel(3, "SALES199", "1  "));
	EXPECT_STREQ(local->GetLabel(), "SALES199.1");
}

// A label is at most 8 and 3 characters, as DOS name parsing cuts a name;
// a longer one would overrun the 13-byte name of the DTA on a search.
TEST_F(VolumeLabelTest, ALabelCreateWithALongNameIsCutTo83)
{
	UseLocalAsC();
	uint16_t handle = 0;
	ASSERT_TRUE(DOS_CreateFile("C:\\ABCDEFGHIJKLMNOP.QRSTU",
	                           FatAttributeFlags::Volume,
	                           &handle,
	                           true));
	EXPECT_TRUE(DOS_CloseFile(handle, true));
	EXPECT_STREQ(local->GetLabel(), "ABCDEFGH.QRS");

	const auto archive = MountArchive();
	ASSERT_TRUE(archive);
	const auto top = MountUnion(archive);
	ASSERT_TRUE(top);
	UseAsC(top);
	ASSERT_TRUE(DOS_CreateFile("C:\\ABCDEFGHIJKLMNOPQRSTUVWXYZ0123",
	                           FatAttributeFlags::Volume,
	                           &handle,
	                           true));
	EXPECT_TRUE(DOS_CloseFile(handle, true));
	EXPECT_STREQ(archive->GetLabel(), "ABCDEFGH");
}

// MS-DOS 4.0 SEARCH.ASM PackName: a handle search packs the 11 label bytes
// as 8.3, so an 11-character label comes back with a dot after the eighth.
TEST_F(VolumeLabelTest, AnArchiveLabelFromALongNameSearchesBackWhole)
{
	const auto drive = MountArchive("Quest for Glory IV");
	ASSERT_TRUE(drive);
	EXPECT_STREQ(drive->GetLabel(), "QUEST_FO.R_G");
	UseAsC(drive);
	EXPECT_EQ(VolumeSearch("C:\\????????.???"), "QUEST_FO.R_G");
	EXPECT_EQ(FcbVolumeSearch(3), "QUEST_FOR_G");
}

// The names DOS hands a drive are 8.3 with the dot (label_from_name); every
// drive refuses another form instead of cutting it.
const std::vector<std::string> LabelsNotIn83Form =
        {"", std::string(32, 'A'), "ABCDEFGHI", "A.BCDE", "A.B.C", "ABC.", ".ABC"};

TEST_F(VolumeLabelTest, AnArchiveDriveRefusesALabelNotIn83Form)
{
	const auto drive = MountArchive();
	ASSERT_TRUE(drive);
	const std::string before = drive->GetLabel();
	for (const auto& name : LabelsNotIn83Form) {
		EXPECT_FALSE(drive->SetVolumeLabel(name)) << name;
		EXPECT_EQ(drive->GetLabel(), before) << name;
	}
	EXPECT_TRUE(drive->SetVolumeLabel("ABCDEFGH.IJK"));
}

TEST_F(VolumeLabelTest, AFatImageRefusesALabelNotIn83Form)
{
	const auto path = WriteFloppy(tmp / "floppy.img");
	const auto fat  = MountFloppy(path);
	ASSERT_TRUE(fat->SetVolumeLabel("DISK"));
	for (const auto& name : LabelsNotIn83Form) {
		EXPECT_FALSE(fat->SetVolumeLabel(name)) << name;
		EXPECT_STREQ(fat->GetLabel(), "DISK") << name;
	}
	EXPECT_EQ(RootLabel(path)->name, "DISK       ");
	EXPECT_TRUE(fat->SetVolumeLabel("ABCDEFGH.IJK"));
}

TEST_F(VolumeLabelTest, AFolderRefusesALabelNotIn83Form)
{
	ASSERT_TRUE(local->SetVolumeLabel("DISK"));
	for (const auto& name : LabelsNotIn83Form) {
		EXPECT_FALSE(local->SetVolumeLabel(name)) << name;
		EXPECT_STREQ(local->GetLabel(), "DISK") << name;
	}
	EXPECT_TRUE(local->SetVolumeLabel("ABCDEFGH.IJK"));
}

// A freshly mounted image has not searched its root yet; an FCB delete by
// name matches the entry on the medium, not an empty cache.
TEST_F(VolumeLabelTest, AnFcbDeleteMatchesTheLabelOnAFatImage)
{
	const auto path = WriteFloppy(tmp / "floppy.img");
	{
		const auto fat = MountFloppy(path);
		ASSERT_TRUE(fat->SetVolumeLabel("DISK"));
	}
	{
		const auto fat = MountFloppy(path);
		EXPECT_STREQ(fat->GetLabel(), "");
		UseAsC(fat);
		EXPECT_TRUE(FcbDeleteLabel(3, "DISK    ", "   "));
		UseLocalAsC();
	}
	EXPECT_FALSE(RootLabel(path).has_value());
}

// The label reaches the host log; LABEL's own character check refuses these.
TEST_F(VolumeLabelTest, ALabelWithAControlByteIsRefused)
{
	UseLocalAsC();
	ASSERT_TRUE(local->SetVolumeLabel("DISK"));
	uint16_t handle = 0;
	EXPECT_FALSE(DOS_CreateFile(
	        "C:\\A\x1b]2;x", FatAttributeFlags::Volume, &handle, true));
	EXPECT_FALSE(
	        DOS_CreateFile("C:\\A\x7f" "B", FatAttributeFlags::Volume, &handle, true));
	EXPECT_STREQ(local->GetLabel(), "DISK");
}

// The kernel rule for every create (MS-DOS 4.0 MACRO2.ASM Canonicalize,
// FILE.ASM AccessFile); MS-DOS 5.00 answers the same.
TEST_F(VolumeLabelTest, AFileCreateWithAWildcardIsRefused)
{
	const auto path = WriteFloppy(tmp / "floppy.img");
	const auto fat  = MountFloppy(path);
	std_fs::create_directory(tmp / "SUB");
	local->EmptyCache();
	for (const auto& drive : {std::shared_ptr<DOS_Drive>(local),
	                          std::shared_ptr<DOS_Drive>(fat)}) {
		UseAsC(drive);
		uint16_t handle = 0;
		for (const auto name : {"C:\\A?B.TXT", "C:\\A*B", "C:\\SUB\\?"}) {
			dos.errorcode = DOSERR_NONE;
			EXPECT_FALSE(DOS_CreateFile(name, {}, &handle, true)) << name;
			EXPECT_EQ(dos.errorcode, DOSERR_FILE_NOT_FOUND) << name;
		}
		dos.errorcode = DOSERR_NONE;
		EXPECT_FALSE(DOS_CreateFile("C:\\S?B\\X.TXT", {}, &handle, true));
		EXPECT_EQ(dos.errorcode, DOSERR_PATH_NOT_FOUND);

		const uint16_t seg = DOS_GetMemory(3);
		DOS_FCB fcb(seg, 0);
		fcb.Create(false);
		fcb.SetName(3, "A?B     ", "TXT");
		EXPECT_FALSE(DOS_FCBCreate(seg, 0));
	}
	UseLocalAsC();
	auto names = HostNames();
	std::sort(names.begin(), names.end());
	EXPECT_EQ(names, std::vector<std::string>({"SUB", "floppy.img"}));
	EXPECT_TRUE(std_fs::is_empty(tmp / "SUB"));
	EXPECT_TRUE(RootEntries(path).empty());
}

// MS-DOS 4.0 FILE.ASM AccessFile: a create whose name holds * or ? fails
// with error 2; the FCB create stops in MKNODE.ASM MakeNode as a bad path.
TEST_F(VolumeLabelTest, ALabelCreateWithAWildcardIsRefused)
{
	UseLocalAsC();
	ASSERT_TRUE(local->SetVolumeLabel("DISK"));
	uint16_t handle = 0;
	for (const auto name : {"C:\\A*B", "C:\\A?B", "C:\\DISK.?"}) {
		dos.errorcode = DOSERR_NONE;
		EXPECT_FALSE(DOS_CreateFile(name, FatAttributeFlags::Volume, &handle, true))
		        << name;
		EXPECT_EQ(dos.errorcode, DOSERR_FILE_NOT_FOUND) << name;
		EXPECT_STREQ(local->GetLabel(), "DISK") << name;
	}
	EXPECT_FALSE(FcbCreateLabel(3, "A?B     ", "   "));
	EXPECT_STREQ(local->GetLabel(), "DISK");
	EXPECT_FALSE(FcbCreateLabel(3, "DISK    ", "?  "));
	EXPECT_STREQ(local->GetLabel(), "DISK");
}

// MS-DOS 5.00 VOL, DIR and an FCB search give the label as its 11 root
// entry bytes; drives keep it in 8.3 form with the dot.
TEST(LabelAsRootEntryName, GivesTheElevenEntryBytes)
{
	EXPECT_EQ(LabelAsRootEntryName("QUEST_FO.R_G"), "QUEST_FOR_G");
	EXPECT_EQ(LabelAsRootEntryName("DISK.  1"), "DISK      1");
	EXPECT_EQ(LabelAsRootEntryName("SALES199.1"), "SALES1991  ");
	EXPECT_EQ(LabelAsRootEntryName("MY DISK"), "MY DISK    ");
	EXPECT_EQ(LabelAsRootEntryName(""), "           ");
	EXPECT_EQ(LabelAsRootEntryName("ABCDEFGHIJ.KLMNO"), "ABCDEFGHKLM");
}

} // namespace

// Red Baron's INSTALL.COM finds its disk with the FCB name "RBDISK ?" against
// the label "RBDISK 1": DOS compares the 11 bytes, the space included.
TEST_F(VolumeLabelTest, AnFcbLabelSearchMatchesTheSpaceInsideTheName)
{
	const auto path = WriteFloppy(tmp / "floppy.img");
	WriteRootEntry(path, 0, "RBDISK 1   ", 0x08);
	UseAsC(MountFloppy(path));
	EXPECT_EQ(FcbVolumeSearch(DriveC + 1, "RBDISK ?", "   "), "RBDISK 1   ");
	EXPECT_EQ(FcbVolumeSearch(DriveC + 1, "RBDISK? ", "   "), "(none)");
}

TEST_F(VolumeLabelTest, AnFcbLabelSearchWithASpaceMissesALabelWithout)
{
	const auto path = WriteFloppy(tmp / "floppy.img");
	WriteRootEntry(path, 0, "RBDISK1    ", 0x08);
	UseAsC(MountFloppy(path));
	EXPECT_EQ(FcbVolumeSearch(DriveC + 1, "RBDISK ?", "   "), "(none)");
	EXPECT_EQ(FcbVolumeSearch(DriveC + 1, "RBDISK? ", "   "), "RBDISK1    ");
}
