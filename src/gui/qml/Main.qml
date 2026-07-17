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
import Synaxis

Window {
    id: root

    width: 1440
    height: 900
    visible: true
    title: qsTr("Synaxis")
    color: Theme.background

    Component.onCompleted: Library.Reload()

    // The shelves, with the hero riding along as the header so the whole page
    // scrolls as one — the billboard sliding away under the nav bar is most of
    // what makes this read as <inspiration> rather than as a grid with a picture on
    // top.
    ListView {
        id: page

        anchors.fill: parent
        visible: !Library.empty

        model: Library.shelves
        spacing: 4
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        cacheBuffer: 800

        header: Hero {
            width: page.width
            height: Math.round(page.height * 0.62)

            title: Library.heroTitle
            subtitle: Library.heroSubtitle
            artwork: Library.heroArtwork

            onPlay: Library.Play(Library.heroPath)
        }

        // The last shelf would otherwise end flush against the window edge.
        footer: Item {
            width: page.width
            height: Theme.gutter
        }

        delegate: Shelf {
            required property var model

            width: page.width

            title: model.shelfTitle
            items: model.shelfItems

            onActivated: (path) => Library.Play(path)
        }
    }

    // Nav bar. Fades from opaque to transparent so it sits over the hero at
    // rest, exactly as <inspiration>'s does.
    Rectangle {
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
        }
        height: 68

        gradient: Gradient {
            GradientStop { position: 0.0; color: "#cc000000" }
            GradientStop { position: 1.0; color: "transparent" }
        }

        Text {
            anchors {
                left: parent.left
                leftMargin: Theme.gutter
                verticalCenter: parent.verticalCenter
            }
            text: qsTr("SYNAXIS")
            color: Theme.accent
            font.pixelSize: 26
            font.bold: true
            font.letterSpacing: 2
        }
    }

    // Playback takes the whole window, over everything else.
    //
    // Loaded rather than merely hidden: MpvItem is a live GL surface with a
    // render context behind it, and keeping one around while browsing would
    // hold GPU resources for a film nobody is watching. The Loader also makes
    // teardown explicit — the renderer's destructor is what frees mpv's render
    // context, on the render thread, which is the only place that's legal.
    Loader {
        anchors.fill: parent
        active: Library.playing
        z: 100

        sourceComponent: PlayerOverlay {}

        // The overlay owns the keyboard while it's up.
        onLoaded: item.forceActiveFocus()
    }

    // Empty state. An unscanned library is the expected first run, not an
    // error, so this explains the next step rather than reporting a failure.
    Column {
        anchors.centerIn: parent
        width: Math.min(520, parent.width - Theme.gutter * 2)
        spacing: 12
        visible: Library.empty && !Library.playing

        Text {
            width: parent.width
            text: qsTr("Nothing here yet")
            color: Theme.textPrimary
            font.pixelSize: 30
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
        }

        Text {
            width: parent.width
            text: qsTr("Scan a folder to build your library:\nsynaxis_cli -d <directory>")
            color: Theme.textSecondary
            font.pixelSize: 15
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }
    }
}
