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

#include "synaxis/player.hpp"

#include <QQuickFramebufferObject>
#include <QtQml/qqmlregistration.h>

namespace synaxis::gui {

// The video surface: a Qt Quick item that mpv draws into.
//
// QQuickFramebufferObject rather than QQuickRhiItem because mpv's render API
// speaks OpenGL, so the scene graph has to be on OpenGL too (main.cpp forces
// it). An FBO-backed item is exactly the shape mpv wants — it hands us a
// framebuffer to render into and asks for nothing else.
//
// The item itself owns no mpv state. It borrows a Player, which lives in
// LibraryController alongside the watch-state bookkeeping, so that embedding
// video doesn't fork playback into a second implementation.
class MpvItem : public QQuickFramebufferObject {
    Q_OBJECT
    QML_ELEMENT

public:
    explicit MpvItem(QQuickItem* parent = nullptr);
    ~MpvItem() override;

    // The item renders whatever this points at. Not owned; must outlive the
    // item, which LibraryController guarantees by owning both.
    void SetPlayer(Player* player);
    Player* GetPlayer() const { return player_; }

    // Called by Qt on the render thread.
    Renderer* createRenderer() const override;

signals:
    // The render context exists and mpv can output to it.
    //
    // Nothing may be opened before this: with vo=libmpv there is no window to
    // fall back on, so a file loaded while the context is still missing fails
    // outright with "no audio or video data played". The context can't be
    // created until the render thread first draws this item, which is strictly
    // after the item is constructed — so opening on construction is always too
    // early, and this is how the controller learns it's safe.
    //
    // Emitted on the UI thread.
    void RendererReady();

private:
    friend class MpvRenderer;

    Player* player_ = nullptr;
};

} // namespace synaxis::gui
