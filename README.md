# Synaxis

A simple offline media aggregator and player. Scans a directory of video
files, parses titles/seasons/episodes/year/source out of scene-release-style
filenames, and plays a chosen file via [libmpv](https://mpv.io/) or
[libVLC](https://www.videolan.org/vlc/libvlc.html).

Offline by default: nothing reaches the network unless you configure a TMDB
API key, and the GUI works fully without one (see [Artwork](#artwork)).

## Build

Dependencies: CMake 3.20+, a C++20 compiler, [nlohmann_json](https://github.com/nlohmann/json),
libmpv (`libmpv-dev` on Debian/Ubuntu), and libVLC 3.x (`libvlc-dev`).

The GUI additionally needs Qt 6.5+ (Core, Gui, Network, Quick). The tests need
[GoogleTest](https://github.com/google/googletest), and generate their video
fixtures with `ffmpeg` — without it, the tests that decode video skip.

```sh
cmake -S . -B build
cmake --build build -j"$(nproc)"
```

Both extras are optional:

```sh
# CLI only — no Qt, no GoogleTest required
cmake -S . -B build -DSYNAXIS_BUILD_GUI=OFF -DSYNAXIS_BUILD_TESTS=OFF
```

Run the tests with:

```sh
ctest --test-dir build
```

## Usage

```sh
# Scan a directory and (re)build the library
synaxis_cli -d <directory>

# Restrict scanning to specific extensions (must be a subset of the
# built-in defaults: mp4, mkv, mov, avi, webm, wmv, flv, m4v, mpg, mpeg, ts)
synaxis_cli -d <directory> -x mp4,mkv

# Play a movie
synaxis_cli -p -f "Inception" [-y 2010]

# Play a TV episode
synaxis_cli -p -f "The Silent Orbit" -s 1 -e 3

# Play via libVLC instead of the default libmpv backend
synaxis_cli -p -f "The Silent Orbit" -s 1 -e 3 -b vlc

# Use a library somewhere other than the default location
synaxis_cli -d <directory> --library /tmp/scratch.json
synaxis_cli -p -f "Dune" --library /tmp/scratch.json

# Print version, copyright, and license notice
synaxis_cli --version
```

`-p` requires a library built beforehand via `-d`. If more than one file
matches, you'll be prompted to pick one.

`-f` matches any part of a parsed title, ignoring case, so `-f dune` finds
`Dune partI`. Pass a longer fragment to narrow an ambiguous match.

### Library location

The library index lives at `$XDG_DATA_HOME/synaxis/library.json`, falling
back to `~/.local/share/synaxis/library.json`. It's a fixed location rather
than a file in the working directory, so every front-end sees the same
library however it was launched. `--library <path>` overrides it.

### Playback backends

`-b mpv` (the default) plays through libmpv, which brings its own window,
keyboard bindings, and OSD, so playback is interactive.

`-b vlc` plays through libVLC. Its window has no keyboard bindings and no
OSD and can only be closed — libVLC leaves that surface to the embedding
application.

mpv is the intended foundation for the planned GUI: its render API embeds
natively on Wayland, which libVLC 3.x cannot do (it has no Wayland surface
setter, and VLC 4's replacement API is still unreleased). libVLC is kept as
a working comparison.

## GUI

```sh
synaxis_gui
```

Reads the same library the CLI builds, so run `synaxis_cli -d <directory>`
first. Playback still opens mpv's own window; embedding it in the GUI is not
done yet.

### Artwork

Tiles are 16:9 and come from the first source that can supply one:

1. **TMDB** — real backdrops for films, per-episode stills for series.
   Requires an API key, and is skipped entirely without one.
2. **Extracted frames** — a frame from the video itself, via libmpv. Always
   available, needs no network or account.

To enable TMDB, put your key in `$XDG_CONFIG_HOME/synaxis/config.json`
(falling back to `~/.config/synaxis/config.json`):

```json
{ "tmdb_api_key": "your-key-here" }
```

With no key — or no network, or no match — Synaxis falls back to extracted
frames and stays entirely offline. A missing or malformed config is treated as
"no key" rather than an error.

Artwork is cached under `$XDG_CACHE_HOME/synaxis/artwork/` (falling back to
`~/.cache/synaxis/artwork/`), keyed by file path. It's a cache in the XDG
sense: deleting it costs only the time to regenerate.

Resume positions live in `$XDG_DATA_HOME/synaxis/watch.json`, beside the
library — unlike artwork, they can't be regenerated, so they're data rather
than cache.

## License

GPLv3-or-later. See [LICENSE](LICENSE).
