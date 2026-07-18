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

    // Playback-scoped fullscreen: entered from the control bar's bottom-right
    // toggle, left with Escape (or the same toggle), and always unwound when
    // the overlay goes away so browsing never inherits a fullscreen window.
    readonly property bool fullscreen: Window.window
                                       && Window.window.visibility === Window.FullScreen

    function setFullscreen(on) {
        const w = Window.window;
        if (!w || root.fullscreen === on) return;
        console.log(on ? "player: fullscreen entered" : "player: fullscreen exited");
        if (on) w.showFullScreen();
        else w.showNormal();
    }

    Component.onDestruction: setFullscreen(false)

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
    // Movement has to be checked by hand: Qt Quick synthesizes a hover pass on
    // every rendered frame (to keep hover states current as items move under a
    // stationary cursor), and pointChanged fires for each one — during video
    // playback that's every frame, which would hold the controls awake
    // forever with the mouse untouched.
    HoverHandler {
        property point lastPosition: Qt.point(-1, -1)
        onPointChanged: {
            if (point.position.x === lastPosition.x && point.position.y === lastPosition.y)
                return;
            lastPosition = Qt.point(point.position.x, point.position.y);
            root.wake();
        }
    }

    // The whole-surface click target, and just as importantly the input
    // blocker. The browsing page is still alive underneath this overlay, and
    // Qt Quick keeps delivering a press to items below until one accepts it —
    // TapHandlers only grab, they don't accept, so with them here a click on
    // the overlay also reached the shelves: the ListView stole the grab
    // (cancelling any exclusive-grab tap on our controls outright), and tiles
    // or the hero underneath fired Play() right after the back button's
    // Close(). A MouseArea accepts the press, which stops delivery cold.
    // The controls above are MouseAreas for the same reason: their press
    // stops here, so a tap on a button no longer also toggles pause.
    MouseArea {
        anchors.fill: parent
        // When the controls sleep, the cursor sleeps with them: an idle film
        // should be nothing but the picture. Any movement wakes both.
        cursorShape: root.controlsVisible ? Qt.ArrowCursor : Qt.BlankCursor
        onClicked: root.controlsVisible ? Library.TogglePause() : root.wake()
        // Wheel would otherwise scroll the shelves under the film.
        onWheel: (wheel) => { wheel.accepted = true; }
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

    // Escape peels back one layer at a time: first out of fullscreen, then
    // out of playback.
    Keys.onEscapePressed: {
        if (root.fullscreen) root.setFullscreen(false);
        else Library.Close();
    }
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
                    // Negative margins buy click slack around a glyph whose
                    // implicit bounds are only ~21x39px.
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -12
                        onClicked: Library.Close()
                    }
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

                    // Vertical slack only: the track is 16px tall, thinner than
                    // a comfortable click target. Horizontally the area matches
                    // the track exactly so mouse.x maps straight onto it.
                    MouseArea {
                        anchors.fill: parent
                        anchors.topMargin: -12
                        anchors.bottomMargin: -12
                        onClicked: (mouse) => {
                            if (Library.duration <= 0) return;
                            const fraction = Math.max(0, Math.min(1, mouse.x / track.width));
                            Library.Seek(Library.duration * fraction);
                            root.wake();
                        }
                    }
                }

                Item {
                    width: parent.width
                    height: 22

                    Row {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
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

                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -12
                                onClicked: {
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

                    // Fullscreen toggle, bottom-right by convention. Corner
                    // brackets drawn like the play/pause glyph: outward when
                    // windowed (room to grow), inward when fullscreen.
                    Item {
                        width: 22
                        height: 22
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter

                        Canvas {
                            id: fullscreenGlyph

                            anchors.fill: parent
                            // Canvas caches; without the explicit repaint the
                            // brackets keep their old direction after toggling.
                            property bool fs: root.fullscreen
                            onFsChanged: requestPaint()

                            onPaint: {
                                const ctx = getContext("2d");
                                ctx.reset();
                                ctx.strokeStyle = "#ffffff";
                                ctx.lineWidth = 2;
                                const s = 7;   // bracket arm length
                                const e = 1;   // inset so strokes aren't clipped
                                const w = width, h = height;
                                ctx.beginPath();
                                if (root.fullscreen) {
                                    // Corner vertices pulled inward, arms
                                    // reaching back out: the "shrink" glyph.
                                    ctx.moveTo(e + s, e); ctx.lineTo(e + s, e + s); ctx.lineTo(e, e + s);
                                    ctx.moveTo(w - e - s, e); ctx.lineTo(w - e - s, e + s); ctx.lineTo(w - e, e + s);
                                    ctx.moveTo(w - e - s, h - e); ctx.lineTo(w - e - s, h - e - s); ctx.lineTo(w - e, h - e - s);
                                    ctx.moveTo(e + s, h - e); ctx.lineTo(e + s, h - e - s); ctx.lineTo(e, h - e - s);
                                } else {
                                    ctx.moveTo(e, e + s); ctx.lineTo(e, e); ctx.lineTo(e + s, e);
                                    ctx.moveTo(w - e - s, e); ctx.lineTo(w - e, e); ctx.lineTo(w - e, e + s);
                                    ctx.moveTo(w - e, h - e - s); ctx.lineTo(w - e, h - e); ctx.lineTo(w - e - s, h - e);
                                    ctx.moveTo(e + s, h - e); ctx.lineTo(e, h - e); ctx.lineTo(e, h - e - s);
                                }
                                ctx.stroke();
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -12
                            onClicked: {
                                root.setFullscreen(!root.fullscreen);
                                root.wake();
                            }
                        }
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
