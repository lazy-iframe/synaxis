# Synaxis

A simple offline media aggregator and player. Scans a directory of video
files, parses titles/seasons/episodes/year/source out of scene-release-style
filenames, and plays a chosen file via [libmpv](https://mpv.io/) or
[libVLC](https://www.videolan.org/vlc/libvlc.html).

## Build

Dependencies: CMake 3.20+, a C++20 compiler, [nlohmann_json](https://github.com/nlohmann/json),
libmpv (`libmpv-dev` on Debian/Ubuntu), and libVLC 3.x (`libvlc-dev`).

```sh
cmake -S . -B build
cmake --build build -j"$(nproc)"
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

## License

GPLv3-or-later. See [LICENSE](LICENSE).
