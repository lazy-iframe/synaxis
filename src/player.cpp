// Synaxis: a simple offline media aggregator and player
// Copyright (C) 2026  Azhar Tanweer (azhar.tanweer404@gmail.com)

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include "synaxis/player.hpp"

#include <mpv/client.h>

#include <iostream>

namespace synaxis {

bool Player::Play(const std::filesystem::path& path) {
    mpv_handle* mpv = mpv_create();
    if (!mpv) {
        std::cerr << "error: failed to create mpv instance\n";
        return false;
    }

    mpv_set_option_string(mpv, "input-default-bindings", "yes");
    mpv_set_option_string(mpv, "input-vo-keyboard", "yes");

    if (mpv_initialize(mpv) < 0) {
        std::cerr << "error: failed to initialize mpv\n";
        mpv_terminate_destroy(mpv);
        return false;
    }

    const std::string path_str = path.string();
    const char* cmd[] = {"loadfile", path_str.c_str(), nullptr};
    if (mpv_command(mpv, cmd) < 0) {
        std::cerr << "error: failed to load file: " << path_str << "\n";
        mpv_terminate_destroy(mpv);
        return false;
    }

    bool ok = true;
    for (bool playing = true; playing;) {
        mpv_event* event = mpv_wait_event(mpv, -1);
        switch (event->event_id) {
            case MPV_EVENT_END_FILE: {
                auto* end_file = static_cast<mpv_event_end_file*>(event->data);
                if (end_file->reason == MPV_END_FILE_REASON_ERROR) {
                    std::cerr << "error: playback failed: " << mpv_error_string(end_file->error)
                              << "\n";
                    ok = false;
                }
                playing = false;
                break;
            }
            case MPV_EVENT_SHUTDOWN:
                playing = false;
                break;
            default:
                break;
        }
    }

    mpv_terminate_destroy(mpv);
    return ok;
}

} // namespace synaxis
