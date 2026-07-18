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
import QtQuick.Dialogs
import Synaxis

// Full-window settings, reached from the gear icon in Main.qml's nav bar.
//
// A Loader recreates this from scratch each time it opens (see Main.qml),
// which is why the fields read Library's properties in Component.onCompleted
// rather than through a live binding: a live binding would fight the user's
// typing every time configChanged fires elsewhere (e.g. a directory add).
Item {
    id: root

    signal closed()

    Rectangle {
        anchors.fill: parent
        color: Theme.background
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("Add a directory to scan")
        onAccepted: Library.AddLibraryDirectory(folderDialog.selectedFolder)
    }

    // Header: back + title, the same visual weight as PlayerOverlay's.
    Item {
        id: header

        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 96

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
                TapHandler { onTapped: root.closed() }
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Settings")
                color: Theme.textPrimary
                font.pixelSize: 26
                font.bold: true
            }
        }
    }

    Keys.onEscapePressed: root.closed()

    Flickable {
        id: content

        anchors {
            left: parent.left
            right: parent.right
            top: header.bottom
            bottom: parent.bottom
        }
        contentWidth: width
        contentHeight: column.height + Theme.gutter * 2
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: column

            anchors {
                left: parent.left
                right: parent.right
                top: parent.top
                margins: Theme.gutter
                topMargin: 0
            }
            width: Math.min(720, parent.width - Theme.gutter * 2)
            spacing: 24

            // --- TMDB API key ---------------------------------------------

            Rectangle {
                width: column.width
                height: apiKeySection.height + 32
                radius: Theme.radius
                color: Theme.surface

                Column {
                    id: apiKeySection

                    anchors {
                        left: parent.left
                        right: parent.right
                        top: parent.top
                        margins: 16
                    }
                    spacing: 12

                    Text {
                        text: qsTr("TMDB API Key")
                        color: Theme.textPrimary
                        font.pixelSize: 18
                        font.bold: true
                    }

                    Text {
                        width: parent.width
                        text: qsTr("Used to fetch backdrops and episode stills. Without a key, " +
                                   "artwork falls back to a frame pulled from the file itself.")
                        color: Theme.textSecondary
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                    }

                    Row {
                        width: parent.width
                        spacing: 12

                        Rectangle {
                            id: apiKeyField

                            width: parent.width - saveKeyButton.width - parent.spacing
                            height: 40
                            radius: Theme.radius
                            color: Theme.background
                            border.width: 1
                            border.color: apiKeyInput.activeFocus ? Theme.accent : "#333333"

                            TextInput {
                                id: apiKeyInput

                                anchors.fill: parent
                                anchors.margins: 10
                                verticalAlignment: TextInput.AlignVCenter
                                color: Theme.textPrimary
                                font.pixelSize: 14
                                clip: true
                                selectByMouse: true
                                echoMode: activeFocus ? TextInput.Normal : TextInput.Password

                                Component.onCompleted: text = Library.tmdbApiKey
                                Keys.onReturnPressed: Library.SetTmdbApiKey(text)
                            }
                        }

                        Rectangle {
                            id: saveKeyButton

                            width: 88
                            height: 40
                            radius: Theme.radius
                            color: saveKeyHover.hovered ? "#d9ffffff" : Theme.textPrimary

                            Behavior on color {
                                ColorAnimation { duration: Theme.hoverDuration }
                            }

                            HoverHandler { id: saveKeyHover }
                            TapHandler { onTapped: Library.SetTmdbApiKey(apiKeyInput.text) }

                            Text {
                                anchors.centerIn: parent
                                text: qsTr("Save")
                                color: "#000000"
                                font.pixelSize: 14
                                font.bold: true
                            }
                        }
                    }

                    // TMDB's required attribution for applications using
                    // their API. Keys are the user's own — see the README's
                    // "Getting a TMDB API key" for why none ships with the app.
                    Text {
                        width: parent.width
                        text: qsTr("This product uses the TMDB API but is not endorsed " +
                                   "or certified by TMDB.")
                        color: Theme.textSecondary
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // --- Library directories ---------------------------------------

            Rectangle {
                width: column.width
                height: dirSection.height + 32
                radius: Theme.radius
                color: Theme.surface

                Column {
                    id: dirSection

                    anchors {
                        left: parent.left
                        right: parent.right
                        top: parent.top
                        margins: 16
                    }
                    spacing: 12

                    Text {
                        text: qsTr("Library Directories")
                        color: Theme.textPrimary
                        font.pixelSize: 18
                        font.bold: true
                    }

                    Text {
                        width: parent.width
                        text: qsTr("Synaxis scans these folders for video files. Add a folder, " +
                                   "then rescan to pick up its contents.")
                        color: Theme.textSecondary
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        width: parent.width
                        visible: Library.libraryDirectories.length === 0
                        text: qsTr("No directories added yet.")
                        color: Theme.textDim
                        font.pixelSize: 13
                        font.italic: true
                    }

                    Column {
                        width: parent.width
                        spacing: 8

                        Repeater {
                            model: Library.libraryDirectories

                            delegate: Rectangle {
                                required property string modelData

                                width: dirSection.width
                                height: 40
                                radius: Theme.radius
                                color: Theme.background

                                Text {
                                    anchors {
                                        left: parent.left
                                        right: removeButton.left
                                        verticalCenter: parent.verticalCenter
                                        margins: 12
                                    }
                                    text: modelData
                                    color: Theme.textSecondary
                                    font.pixelSize: 13
                                    elide: Text.ElideMiddle
                                }

                                Text {
                                    id: removeButton

                                    anchors {
                                        right: parent.right
                                        rightMargin: 12
                                        verticalCenter: parent.verticalCenter
                                    }
                                    text: "✕"
                                    color: removeHover.hovered ? Theme.accent : Theme.textDim
                                    font.pixelSize: 15

                                    HoverHandler { id: removeHover }
                                    TapHandler {
                                        onTapped: Library.RemoveLibraryDirectory(modelData)
                                    }
                                }
                            }
                        }
                    }

                    // The GUI face of the CLI's -x flag: limit scans to these
                    // file types. Saved on Return and applied automatically
                    // when Rescan is clicked; the field snaps back to the
                    // accepted list, so a rejected typo is visible feedback.
                    Text {
                        width: parent.width
                        text: qsTr("File types to scan, comma-separated (e.g. \"mkv, mp4\"). " +
                                   "Leave empty for all supported types: %1.")
                              .arg(Library.availableExtensions.join(", "))
                        color: Theme.textSecondary
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                    }

                    Rectangle {
                        width: parent.width
                        height: 40
                        radius: Theme.radius
                        color: Theme.background
                        border.width: 1
                        border.color: extensionsInput.activeFocus ? Theme.accent : "#333333"

                        TextInput {
                            id: extensionsInput

                            anchors.fill: parent
                            anchors.margins: 10
                            verticalAlignment: TextInput.AlignVCenter
                            color: Theme.textPrimary
                            font.pixelSize: 14
                            clip: true
                            selectByMouse: true

                            Component.onCompleted: text = Library.scanExtensions.join(", ")
                            Keys.onReturnPressed: Library.SetScanExtensions(text)

                            // Reflect the accepted (normalized, validated)
                            // list back — but never while the user is still
                            // typing in it.
                            Connections {
                                target: Library
                                function onConfigChanged() {
                                    if (!extensionsInput.activeFocus)
                                        extensionsInput.text = Library.scanExtensions.join(", ");
                                }
                            }
                        }
                    }

                    Row {
                        spacing: 12

                        Rectangle {
                            width: addDirRow.width + 32
                            height: 36
                            radius: Theme.radius
                            color: "transparent"
                            border.width: 1
                            border.color: addDirHover.hovered ? Theme.textPrimary : Theme.textDim

                            HoverHandler { id: addDirHover }
                            TapHandler { onTapped: folderDialog.open() }

                            Row {
                                id: addDirRow
                                anchors.centerIn: parent
                                spacing: 8

                                Text {
                                    text: qsTr("Add Directory…")
                                    color: Theme.textPrimary
                                    font.pixelSize: 13
                                }
                            }
                        }

                        Rectangle {
                            width: rescanRow.width + 32
                            height: 36
                            radius: Theme.radius
                            opacity: Library.scanning || Library.libraryDirectories.length === 0 ? 0.5 : 1
                            color: rescanHover.hovered ? "#d9ffffff" : Theme.textPrimary

                            HoverHandler {
                                id: rescanHover
                                enabled: !Library.scanning && Library.libraryDirectories.length > 0
                            }
                            TapHandler {
                                enabled: !Library.scanning && Library.libraryDirectories.length > 0
                                onTapped: {
                                    // A filter typed but not yet committed with
                                    // Return still applies to the scan being asked for.
                                    Library.SetScanExtensions(extensionsInput.text);
                                    Library.RescanLibrary();
                                }
                            }

                            Row {
                                id: rescanRow
                                anchors.centerIn: parent
                                spacing: 8

                                Text {
                                    text: Library.scanning
                                          ? qsTr("Scanning… %1 indexed").arg(Library.scanFilesIndexed)
                                          : qsTr("Rescan Library")
                                    color: "#000000"
                                    font.pixelSize: 13
                                    font.bold: true
                                }
                            }
                        }
                    }
                }
            }

            // --- Player backend ---------------------------------------------

            Rectangle {
                width: column.width
                height: backendSection.height + 32
                radius: Theme.radius
                color: Theme.surface

                Column {
                    id: backendSection

                    anchors {
                        left: parent.left
                        right: parent.right
                        top: parent.top
                        margins: 16
                    }
                    spacing: 12

                    Text {
                        text: qsTr("Player Backend")
                        color: Theme.textPrimary
                        font.pixelSize: 18
                        font.bold: true
                    }

                    Text {
                        width: parent.width
                        text: qsTr("mpv renders directly into the window and is the only backend " +
                                   "the embedded player supports today; VLC is available on the " +
                                   "command line only.")
                        color: Theme.textSecondary
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                    }

                    Row {
                        spacing: 8

                        Rectangle {
                            width: mpvLabel.width + 24
                            height: 30
                            radius: 15
                            color: Theme.accent

                            Text {
                                id: mpvLabel
                                anchors.centerIn: parent
                                text: qsTr("mpv")
                                color: Theme.textPrimary
                                font.pixelSize: 13
                                font.bold: true
                            }
                        }

                        Rectangle {
                            width: vlcLabel.width + 24
                            height: 30
                            radius: 15
                            color: "transparent"
                            border.width: 1
                            border.color: "#333333"

                            Text {
                                id: vlcLabel
                                anchors.centerIn: parent
                                text: qsTr("vlc (coming soon)")
                                color: Theme.textDim
                                font.pixelSize: 13
                            }
                        }
                    }
                }
            }
        }
    }
}
