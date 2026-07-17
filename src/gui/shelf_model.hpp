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

#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QUrl>

namespace synaxis::gui {

// One tile in a shelf.
//
// Flattened for display rather than holding a MediaEntry: the view wants a
// title and a subtitle, not a ParsedFilename it would have to format in QML.
// Formatting an episode as "S01E02" is a presentation decision, and QML is the
// wrong place to make it — it has no access to the metadata's optionality.
struct Tile {
    QString title;
    QString subtitle;
    QString path;      // canonical media path; what playback opens
    QUrl artwork;      // empty until the artwork worker fills it in
    double progress = 0.0;  // 0..1; drives the resume bar. 0 means no bar.
};

// The tiles of a single shelf.
//
// Artwork arrives late — it's generated on a worker thread — so tiles are
// created with none and patched in place as it lands. That's why this is a
// mutable model rather than a snapshot: the alternative is either blocking the
// UI on a decode per tile, or rebuilding the whole shelf for each image and
// losing scroll position and focus every time one arrives.
class ShelfModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        TitleRole = Qt::UserRole + 1,
        SubtitleRole,
        PathRole,
        ArtworkRole,
        ProgressRole,
    };

    explicit ShelfModel(QList<Tile> tiles, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Sets the artwork for every tile whose path matches, and notifies the
    // view. Matching on path rather than row because the same file can appear
    // in more than one shelf — a half-watched film is in both Continue
    // Watching and Movies — and one decode should light up both.
    void SetArtwork(const QString& path, const QUrl& artwork);

    const QList<Tile>& Tiles() const { return tiles_; }

private:
    QList<Tile> tiles_;
};

} // namespace synaxis::gui
