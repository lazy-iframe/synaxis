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

pragma Singleton

import QtQuick

// One place for the palette and the motion constants, so a tile and a hero
// can't drift apart. The values are deliberately few: a dark surface, a red
// accent, and two greys carry the entire look.
QtObject {
    // Near-black rather than black. A true #000 background makes the tiles'
    // own black bars vanish into it and the grid lose its structure.
    readonly property color background: "#141414"
    readonly property color surface: "#181818"
    readonly property color accent: "#e50914"

    readonly property color textPrimary: "#ffffff"
    readonly property color textSecondary: "#b3b3b3"
    readonly property color textDim: "#808080"

    // Tiles are 16:9 to match the artwork the cache produces; see artwork.hpp
    // for why the whole pipeline is landscape rather than portrait.
    readonly property int tileWidth: 280
    readonly property int tileHeight: Math.round(tileWidth * 9 / 16)

    // The row reserves enough height for a hovered tile to grow into, so it can
    // still clip its horizontal overflow without cropping the scale animation.
    readonly property real tileHoverScale: 1.15
    readonly property int rowHeight: Math.round(tileHeight * tileHoverScale) + 48

    readonly property int gutter: 48
    readonly property int tileSpacing: 8
    readonly property int radius: 4

    readonly property int hoverDuration: 200
    readonly property int fadeDuration: 400
}
