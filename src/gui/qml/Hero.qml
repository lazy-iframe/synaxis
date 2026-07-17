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

// The billboard: one large backdrop with the title over it, and the shelves
// scrolling up beneath. The two scrims are what make the text legible over an
// arbitrary frame we have no control over — one from the left for the copy, one
// at the bottom to melt the image into the page.
Item {
    id: root

    property string title: ""
    property string subtitle: ""
    property url artwork

    signal play()

    Image {
        id: backdrop

        anchors.fill: parent
        source: root.artwork
        asynchronous: true
        fillMode: Image.PreserveAspectCrop
        visible: status === Image.Ready

        opacity: status === Image.Ready ? 1 : 0
        Behavior on opacity {
            NumberAnimation { duration: Theme.fadeDuration }
        }
    }

    // Left-to-right scrim: holds the copy against whatever the frame happens
    // to be.
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "#f2141414" }
            GradientStop { position: 0.55; color: "#66141414" }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }

    // Bottom scrim: hides the hard edge where the backdrop stops and hands off
    // to the first shelf.
    Rectangle {
        anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        height: parent.height * 0.4
        gradient: Gradient {
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 1.0; color: Theme.background }
        }
    }

    Column {
        anchors {
            left: parent.left
            leftMargin: Theme.gutter
            bottom: parent.bottom
            bottomMargin: 72
        }
        width: Math.min(560, parent.width - Theme.gutter * 2)
        spacing: 16

        Text {
            width: parent.width
            text: root.title
            color: Theme.textPrimary
            font.pixelSize: 52
            font.bold: true
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }

        Text {
            width: parent.width
            text: root.subtitle
            color: Theme.textSecondary
            font.pixelSize: 17
            visible: text.length > 0
        }

        // Play button. White on dark is the one high-contrast element on the
        // page, which is what makes it the obvious thing to press.
        Rectangle {
            width: playRow.width + 48
            height: 44
            radius: Theme.radius
            color: playHover.hovered ? "#d9ffffff" : Theme.textPrimary

            Behavior on color {
                ColorAnimation { duration: Theme.hoverDuration }
            }

            HoverHandler { id: playHover }
            TapHandler { onTapped: root.play() }

            Row {
                id: playRow

                anchors.centerIn: parent
                spacing: 10

                // Play glyph, drawn rather than shipped as an icon: it's a
                // triangle, and an asset would be one more thing to package.
                Canvas {
                    width: 16
                    height: 18
                    anchors.verticalCenter: parent.verticalCenter

                    onPaint: {
                        const ctx = getContext("2d");
                        ctx.reset();
                        ctx.fillStyle = "#000000";
                        ctx.beginPath();
                        ctx.moveTo(0, 0);
                        ctx.lineTo(width, height / 2);
                        ctx.lineTo(0, height);
                        ctx.closePath();
                        ctx.fill();
                    }
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Play")
                    color: "#000000"
                    font.pixelSize: 17
                    font.bold: true
                }
            }
        }
    }
}
