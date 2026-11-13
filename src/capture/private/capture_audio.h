// SPDX-FileCopyrightText:  2023-2025 The DOSBox Staging Team
// SPDX-FileCopyrightText:  2002-2021 The DOSBox Team
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 dosbox-automation contributors

#ifndef DOSBOX_CAPTURE_AUDIO_H
#define DOSBOX_CAPTURE_AUDIO_H

#include "misc/std_filesystem.h"

#include <string>

// Opens the file a named capture writes to; false when it cannot be created
// as a new file.
bool capture_audio_open_output(const std_fs::path& path);
// Closes and removes a named file that no audio reached.
void capture_audio_discard_output();

void capture_audio_add_data(uint32_t sample_rate_hz, uint32_t num_sample_frames,
                            const int16_t* sample_frames);

void capture_audio_finalise();

#endif
