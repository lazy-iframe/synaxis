# Synaxis

A simple offline media aggregator and player. Scans a directory of video
files, parses titles/seasons/episodes/year/source out of scene-release-style
filenames, and plays a chosen file via [libmpv](https://mpv.io/).

## Build

Dependencies: CMake 3.20+, a C++20 compiler, [nlohmann_json](https://github.com/nlohmann/json),
libmpv (`libmpv-dev` on Debian/Ubuntu), and GTest (for tests).

```sh
cmake -S . -B build
cmake --build build -j"$(nproc)"
```

Run tests with:

```sh
cd build && ctest --output-on-failure
```

## Usage

```sh
# Scan a directory and (re)build library.json
synaxis_cli -d <directory>

# Restrict scanning to specific extensions (must be a subset of the
# built-in defaults: mp4, mkv, mov, avi, webm, wmv, flv, m4v, mpg, mpeg, ts)
synaxis_cli -d <directory> -x mp4,mkv

# Play a movie
synaxis_cli -p -f "Inception" [-y 2010]

# Play a TV episode
synaxis_cli -p -f "The Silent Orbit" -s 1 -e 3

# Print version, copyright, and license notice
synaxis_cli --version
```

`-p` requires a library built beforehand via `-d`. If more than one file
matches, you'll be prompted to pick one.

## License

GPLv3-or-later. See [LICENSE](LICENSE).
