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

import QtQuick

// One titled, horizontally scrolling row of tiles.
Item {
    id: root

    property string title: ""
    property var items: null

    signal activated(string path)

    height: heading.height + row.height + 8

    Text {
        id: heading

        anchors {
            left: parent.left
            leftMargin: Theme.gutter
            top: parent.top
        }
        text: root.title
        color: "#e5e5e5"
        font.pixelSize: 21
        font.bold: true
    }

    ListView {
        id: row

        anchors {
            left: parent.left
            right: parent.right
            top: heading.bottom
            topMargin: 8
        }
        height: Theme.rowHeight

        orientation: ListView.Horizontal
        model: root.items
        spacing: Theme.tileSpacing
        // Horizontal overflow is clipped, but the row is tall enough (see
        // Theme.rowHeight) that a hovered tile grows into reserved space rather
        // than into the clip boundary.
        clip: true

        // Leaves room for the first and last tiles to grow sideways without
        // being cut off at the viewport edge.
        leftMargin: Theme.gutter
        rightMargin: Theme.gutter

        boundsBehavior: Flickable.StopAtBounds

        // Roles arrive via `model` rather than as individual required
        // properties: Tile already declares title/subtitle/artwork/progress, and
        // redeclaring them here would collide with its own.
        delegate: Tile {
            required property var model

            // Centred in the row's reserved height, which is what gives the
            // hover scale somewhere to grow.
            y: (row.height - height) / 2

            title: model.title
            subtitle: model.subtitle
            artwork: model.artwork
            progress: model.progress

            onActivated: root.activated(model.path)
        }
    }
}
