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

// Fullscreen playback: the video surface, plus the controls mpv no longer
// draws for us. An embedded mpv has no OSD and no key bindings of its own
// (see Player::Backend::MpvEmbedded), so everything here is ours to provide.
Item {
    id: root

    // Controls fade out while the film plays and come back on any input, which
    // is the behaviour every player has converged on.
    property bool controlsVisible: true

    focus: true

    Rectangle {
        anchors.fill: parent
        color: "black"
    }

    MpvItem {
        id: video

        anchors.fill: parent
        // The controller owns the Player; the surface only borrows it.
        Component.onCompleted: Library.AttachVideoOutput(video)
    }

    // Any pointer movement brings the controls back and restarts the clock.
    HoverHandler {
        onPointChanged: root.wake()
    }

    TapHandler {
        onTapped: root.controlsVisible ? Library.TogglePause() : root.wake()
    }

    Timer {
        id: hideTimer

        interval: 3000
        onTriggered: if (!Library.paused) root.controlsVisible = false
    }

    function wake() {
        root.controlsVisible = true;
        hideTimer.restart();
    }

    // A paused film keeps its controls: they're how you un-pause.
    Connections {
        target: Library
        function onPlaybackChanged() {
            if (Library.paused) root.wake();
        }
    }

    Keys.onEscapePressed: Library.Close()
    Keys.onSpacePressed: {
        Library.TogglePause();
        root.wake();
    }
    Keys.onLeftPressed: {
        Library.Seek(Math.max(0, Library.position - 10));
        root.wake();
    }
    Keys.onRightPressed: {
        Library.Seek(Library.position + 10);
        root.wake();
    }

    Component.onCompleted: hideTimer.restart()

    // --- Controls ---

    Item {
        anchors.fill: parent
        opacity: root.controlsVisible ? 1 : 0
        visible: opacity > 0

        Behavior on opacity {
            NumberAnimation { duration: Theme.fadeDuration }
        }

        // Top scrim + back + title.
        Rectangle {
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: 96
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#cc000000" }
                GradientStop { position: 1.0; color: "transparent" }
            }

            Row {
                anchors {
                    left: parent.left
                    leftMargin: Theme.gutter
                    verticalCenter: parent.verticalCenter
                }
                spacing: 20

                Text {
                    text: "←"
                    color: backHover.hovered ? Theme.textPrimary : Theme.textSecondary
                    font.pixelSize: 26

                    HoverHandler { id: backHover }
                    TapHandler { onTapped: Library.Close() }
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: Library.playingTitle
                    color: Theme.textPrimary
                    font.pixelSize: 22
                    font.bold: true
                }
            }
        }

        // Bottom scrim + scrubber.
        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 128
            gradient: Gradient {
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 1.0; color: "#e6000000" }
            }

            Column {
                anchors {
                    left: parent.left
                    right: parent.right
                    bottom: parent.bottom
                    margins: Theme.gutter
                }
                spacing: 12

                // Scrubber. Clicking seeks; there's no drag handling yet, which
                // is the obvious next thing this wants.
                Item {
                    width: parent.width
                    height: 16

                    Rectangle {
                        id: track

                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width
                        height: 4
                        radius: 2
                        color: "#4d4d4d"

                        Rectangle {
                            width: Library.duration > 0
                                   ? track.width * (Library.position / Library.duration)
                                   : 0
                            height: parent.height
                            radius: parent.radius
                            color: Theme.accent
                        }
                    }

                    TapHandler {
                        onTapped: (point) => {
                            if (Library.duration <= 0) return;
                            Library.Seek(Library.duration * (point.position.x / track.width));
                            root.wake();
                        }
                    }
                }

                Row {
                    spacing: 20

                    // Play/pause glyph, drawn rather than shipped as an asset.
                    Item {
                        width: 22
                        height: 22
                        anchors.verticalCenter: parent.verticalCenter

                        Canvas {
                            id: glyph

                            anchors.fill: parent
                            // Repainted on state change: Canvas caches, so
                            // without this the glyph never changes shape.
                            Connections {
                                target: Library
                                function onPlaybackChanged() { glyph.requestPaint(); }
                            }

                            onPaint: {
                                const ctx = getContext("2d");
                                ctx.reset();
                                ctx.fillStyle = "#ffffff";
                                if (Library.paused) {
                                    ctx.beginPath();
                                    ctx.moveTo(2, 0);
                                    ctx.lineTo(width - 2, height / 2);
                                    ctx.lineTo(2, height);
                                    ctx.closePath();
                                    ctx.fill();
                                } else {
                                    ctx.fillRect(3, 0, 5, height);
                                    ctx.fillRect(width - 8, 0, 5, height);
                                }
                            }
                        }

                        TapHandler {
                            onTapped: {
                                Library.TogglePause();
                                root.wake();
                            }
                        }
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.formatTime(Library.position) + " / " +
                              root.formatTime(Library.duration)
                        color: Theme.textSecondary
                        font.pixelSize: 14
                    }
                }
            }
        }
    }

    function formatTime(seconds) {
        if (!seconds || seconds < 0) seconds = 0;
        const total = Math.floor(seconds);
        const h = Math.floor(total / 3600);
        const m = Math.floor((total % 3600) / 60);
        const s = total % 60;
        const pad = (n) => String(n).padStart(2, "0");
        return h > 0 ? h + ":" + pad(m) + ":" + pad(s) : pad(m) + ":" + pad(s);
    }
}
