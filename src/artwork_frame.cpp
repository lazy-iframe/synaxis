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

#include "synaxis/artwork.hpp"

#include <mpv/client.h>

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>

// Kept in its own translation unit for the reason player_backend.hpp gives:
// libmpv's headers define symbols in the global namespace and don't belong in
// Synaxis' public ones. This file shares that constraint but not that
// interface — it never plays anything, so it deliberately doesn't reuse
// MpvBackend. That class owns a visible window and an event-pump thread, and a
// thumbnailer wants neither: it needs a headless handle it can drive
// synchronously and block on.
namespace synaxis {

namespace {

// Where in the file to try grabbing, in order. Never 0: films open on black,
// studio idents, and fade-ins, so the first frame is frequently a solid colour.
//
// 10% alone isn't enough, though — measured against the sample media, a file
// whose opening minutes are a dark fade still yields a black tile there. The
// later offsets are fallbacks, tried only when an earlier one looks blank, so
// the common case stays at a single decode.
constexpr double kSeekFractions[] = {0.10, 0.35, 0.60};

// Below this, a JPEG at kArtworkWidth is almost certainly a blank, letterboxed,
// or fade-to-black frame rather than a picture.
//
// Encoded size stands in for visual detail here. That's only sound because
// every candidate for a given file is scaled to the same width and encoded at
// the same quality, so the sizes are comparable to each other: a flat frame
// has little to encode and comes out several times smaller than a real one.
// On the sample media the split is stark — 4-6KB for the dark fades against
// 26-65KB for real frames — but this is a heuristic tuned on a handful of
// files, not a measurement of brightness, and a genuinely dark film may well
// trip it. The cost of being wrong is only a tile from further into the file.
constexpr std::uintmax_t kMinInterestingBytes = 12000;

// Per-wait ceiling. Decoding a frame from a large file on slow storage is not
// instant, but a provider that blocks forever would wedge the worker thread
// and stall every tile behind it, so every wait is bounded.
constexpr double kWaitTimeoutSeconds = 10.0;

// Waits for `want`, draining everything else. False on timeout, on shutdown,
// or when the file fails to load — all of which are ordinary here (an
// unreadable file just means this provider declines) so none of them throw.
bool WaitFor(mpv_handle* mpv, mpv_event_id want) {
    for (;;) {
        mpv_event* event = mpv_wait_event(mpv, kWaitTimeoutSeconds);

        if (event->event_id == want) return true;
        if (event->event_id == MPV_EVENT_NONE) return false;      // timed out
        if (event->event_id == MPV_EVENT_SHUTDOWN) return false;  // core is gone

        if (event->event_id == MPV_EVENT_END_FILE) {
            const auto* end_file = static_cast<mpv_event_end_file*>(event->data);
            // keep-open holds the file open at EOF, so an END_FILE while we're
            // waiting means the load itself failed rather than that playback
            // finished normally.
            if (end_file->reason == MPV_END_FILE_REASON_ERROR) return false;
        }
    }
}

class FrameProvider : public ArtworkProvider {
public:
    explicit FrameProvider(mpv_handle* mpv) : mpv_(mpv) {}

    ~FrameProvider() override {
        if (mpv_) mpv_terminate_destroy(mpv_);
    }

    std::string_view Name() const override { return "frame"; }

    bool Fetch(const MediaEntry& entry, const std::filesystem::path& out_path) override {
        const std::string path_str = entry.path.string();
        const char* load[] = {"loadfile", path_str.c_str(), nullptr};
        if (mpv_command(mpv_, load) < 0) return false;
        if (!WaitFor(mpv_, MPV_EVENT_FILE_LOADED)) return false;

        // An unknown duration means nothing to take a fraction of, so there's
        // no seeking to be done: grab wherever mpv loaded and take what we get.
        // A worse tile than one from 10% in, but better than none.
        double duration = 0.0;
        if (mpv_get_property(mpv_, "duration", MPV_FORMAT_DOUBLE, &duration) < 0 ||
            duration <= 0.0) {
            return Screenshot(out_path);
        }

        // Candidates are written beside the target and promoted only when they
        // beat the best so far, so out_path always holds the best frame seen
        // and never a worse one written over it.
        const std::filesystem::path candidate = out_path.string() + ".cand";
        std::uintmax_t best = 0;
        std::error_code ec;

        for (double fraction : kSeekFractions) {
            if (!Seek(duration * fraction)) break;
            if (!Screenshot(candidate)) continue;

            const std::uintmax_t size = std::filesystem::file_size(candidate, ec);
            if (ec) continue;

            if (size > best) {
                std::filesystem::rename(candidate, out_path, ec);
                if (!ec) best = size;
            }
            // Good enough: stop before paying for another decode.
            if (best >= kMinInterestingBytes) break;
        }

        std::filesystem::remove(candidate, ec);
        return best > 0;
    }

private:
    bool Seek(double seconds) {
        const std::string target = std::to_string(seconds);
        const char* seek[] = {"seek", target.c_str(), "absolute", nullptr};
        if (mpv_command(mpv_, seek) < 0) return false;
        return WaitFor(mpv_, MPV_EVENT_PLAYBACK_RESTART);
    }

    // "video" grabs the decoded frame at its filtered size and without OSD or
    // subtitles burned in — the vf scale set up in MakeFrameProvider applies,
    // so this writes a tile-sized image rather than a full-resolution one.
    bool Screenshot(const std::filesystem::path& out_path) {
        const std::string out_str = out_path.string();
        const char* shot[] = {"screenshot-to-file", out_str.c_str(), "video", nullptr};
        return mpv_command(mpv_, shot) >= 0;
    }

    mpv_handle* mpv_ = nullptr;
};

} // namespace

std::unique_ptr<ArtworkProvider> MakeFrameProvider() {
    mpv_handle* mpv = mpv_create();
    if (!mpv) {
        std::cerr << "error: failed to create mpv instance for artwork\n";
        return nullptr;
    }

    // Headless: no window, no audio device. vo=null still decodes, which is all
    // screenshot-to-file needs.
    mpv_set_option_string(mpv, "vo", "null");
    mpv_set_option_string(mpv, "ao", "null");

    // Never play. The handle only ever loads, seeks, and grabs one frame.
    mpv_set_option_string(mpv, "pause", "yes");

    // Holds a file open at EOF, so a file shorter than the seek target doesn't
    // unload itself out from under the screenshot.
    mpv_set_option_string(mpv, "keep-open", "yes");

    // The user's mpv.conf is for watching films, not for thumbnailing: it may
    // set a vo, hwdec, or filters that break a headless grab. libmpv already
    // defaults this off; it's set explicitly because the correctness of
    // everything below depends on it.
    mpv_set_option_string(mpv, "config", "no");

    mpv_set_option_string(mpv, "screenshot-format", "jpg");
    mpv_set_option_string(mpv, "screenshot-jpeg-quality", "85");

    // Scale during decode rather than writing a full-resolution frame: a 1080p
    // JPEG is ~600KB and a tile needs ~40KB, and the difference is multiplied
    // by every file in the library. -2 keeps the source aspect (16:9 for
    // ordinary video) while holding the height even, which the JPEG encoder's
    // chroma subsampling requires.
    const std::string scale_filter = "scale=" + std::to_string(kArtworkWidth) + ":-2";
    mpv_set_option_string(mpv, "vf", scale_filter.c_str());

    if (mpv_initialize(mpv) < 0) {
        std::cerr << "error: failed to initialize mpv for artwork\n";
        mpv_terminate_destroy(mpv);
        return nullptr;
    }

    return std::make_unique<FrameProvider>(mpv);
}

} // namespace synaxis
