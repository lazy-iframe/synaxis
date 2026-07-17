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

#include "shelf_model.hpp"

namespace synaxis::gui {

ShelfModel::ShelfModel(QList<Tile> tiles, QObject* parent)
    : QAbstractListModel(parent), tiles_(std::move(tiles)) {}

int ShelfModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;  // flat list; children of an item are none
    return static_cast<int>(tiles_.size());
}

QVariant ShelfModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= tiles_.size()) return {};

    const Tile& tile = tiles_.at(index.row());
    switch (role) {
        case TitleRole: return tile.title;
        case SubtitleRole: return tile.subtitle;
        case PathRole: return tile.path;
        case ArtworkRole: return tile.artwork;
        case ProgressRole: return tile.progress;
        default: return {};
    }
}

QHash<int, QByteArray> ShelfModel::roleNames() const {
    return {
        {TitleRole, "title"},
        {SubtitleRole, "subtitle"},
        {PathRole, "path"},
        {ArtworkRole, "artwork"},
        {ProgressRole, "progress"},
    };
}

void ShelfModel::SetArtwork(const QString& path, const QUrl& artwork) {
    for (int row = 0; row < tiles_.size(); ++row) {
        if (tiles_[row].path != path) continue;

        tiles_[row].artwork = artwork;
        const QModelIndex changed = index(row);
        emit dataChanged(changed, changed, {ArtworkRole});
    }
}

} // namespace synaxis::gui
