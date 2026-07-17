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

// One piece of media. Grows on hover and reveals its label, which is the whole
// trick of a <inspiration> row: the resting grid stays quiet, and detail appears only
// where the user is actually looking.
Item {
    id: root

    property string title: ""
    property string subtitle: ""
    property url artwork
    property real progress: 0

    signal activated()

    width: Theme.tileWidth
    height: Theme.tileHeight

    // Raised while hovered so the growing tile passes over its neighbours
    // rather than under them.
    z: hover.hovered || root.activeFocus ? 10 : 0

    scale: hover.hovered || root.activeFocus ? Theme.tileHoverScale : 1.0
    Behavior on scale {
        NumberAnimation {
            duration: Theme.hoverDuration
            easing.type: Easing.OutCubic
        }
    }

    activeFocusOnTab: true
    Keys.onReturnPressed: root.activated()
    Keys.onEnterPressed: root.activated()

    HoverHandler { id: hover }

    TapHandler {
        onTapped: root.activated()
    }

    Rectangle {
        id: card

        anchors.fill: parent
        color: Theme.surface
        radius: Theme.radius
        clip: true

        // The typographic fallback, and the layer every tile rests on. It shows
        // through whenever there's no artwork — a title TMDB never matched and
        // whose frame couldn't be decoded still reads as a real tile rather
        // than an empty box.
        Text {
            anchors {
                left: parent.left
                right: parent.right
                verticalCenter: parent.verticalCenter
                margins: 16
            }
            visible: !art.visible
            text: root.title
            color: Theme.textSecondary
            font.pixelSize: 20
            font.bold: true
            elide: Text.ElideRight
            maximumLineCount: 3
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
        }

        Image {
            id: art

            anchors.fill: parent
            source: root.artwork
            // Decoding on the UI thread would stall the scroll; every tile in a
            // row would pay for it at once.
            asynchronous: true
            fillMode: Image.PreserveAspectCrop
            visible: status === Image.Ready

            // Fade in rather than pop: artwork arrives from a worker thread at
            // an arbitrary moment, and a row of tiles snapping in one by one
            // reads as broken.
            opacity: status === Image.Ready ? 1 : 0
            Behavior on opacity {
                NumberAnimation { duration: Theme.fadeDuration }
            }
        }

        // Scrim behind the label. Without it the text sits directly on the
        // frame and is unreadable against a bright one.
        Rectangle {
            anchors {
                left: parent.left
                right: parent.right
                bottom: parent.bottom
            }
            height: parent.height * 0.5
            visible: label.opacity > 0
            opacity: label.opacity
            gradient: Gradient {
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 1.0; color: "#e6000000" }
            }
        }

        Column {
            id: label

            anchors {
                left: parent.left
                right: parent.right
                bottom: progressTrack.visible ? progressTrack.top : parent.bottom
                margins: 12
            }
            spacing: 2

            opacity: hover.hovered || root.activeFocus ? 1 : 0
            Behavior on opacity {
                NumberAnimation { duration: Theme.hoverDuration }
            }

            Text {
                width: parent.width
                text: root.title
                color: Theme.textPrimary
                font.pixelSize: 15
                font.bold: true
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                text: root.subtitle
                color: Theme.textSecondary
                font.pixelSize: 12
                elide: Text.ElideRight
                visible: text.length > 0
            }
        }

        // The resume bar. Only drawn when there's genuine progress, so an
        // unwatched grid stays clean.
        Rectangle {
            id: progressTrack

            anchors {
                left: parent.left
                right: parent.right
                bottom: parent.bottom
            }
            height: 3
            color: "#4d4d4d"
            visible: root.progress > 0

            Rectangle {
                width: parent.width * Math.min(1, root.progress)
                height: parent.height
                color: Theme.accent
            }
        }
    }

    // Focus ring, for keyboard navigation. Drawn outside the clipping card so
    // it frames the tile rather than being cut off by it.
    Rectangle {
        anchors.fill: parent
        radius: Theme.radius
        color: "transparent"
        border.color: Theme.textPrimary
        border.width: 2
        visible: root.activeFocus
    }
}
