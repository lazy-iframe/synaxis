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

#include "mpv_item.hpp"

#include <QDebug>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QPointer>
#include <QQuickWindow>

namespace synaxis::gui {

// mpv asks for GL entry points by name. Qt's current context is the authority
// on what those are — dlsym'ing libGL directly would resolve the system's
// symbols rather than the ones this context actually uses, which breaks under
// EGL, Wayland, and llvmpipe alike.
void* GetProcAddress(void* ctx, const char* name) {
    Q_UNUSED(ctx)
    QOpenGLContext* context = QOpenGLContext::currentContext();
    if (!context) return nullptr;
    return reinterpret_cast<void*>(context->getProcAddress(name));
}

// Lives on the render thread for its whole life: Qt creates it there, calls
// render() there, and destroys it there. That's exactly the guarantee mpv's
// render context needs, and the reason the context is created and freed here
// rather than in MpvItem.
class MpvRenderer : public QQuickFramebufferObject::Renderer {
public:
    explicit MpvRenderer(Player* player) : player_(player) {}

    ~MpvRenderer() override {
        // The GL context is still current here, which is the only moment this
        // is safe. Leaving it to Player's destructor would run it on the UI
        // thread with no context — mpv would either crash or leak the GPU
        // resources it allocated.
        if (initialized_ && player_) {
            qInfo() << "player: render context shut down";
            player_->ShutdownRenderer();
        }
    }

    void synchronize(QQuickFramebufferObject* item) override {
        // Runs with the UI thread blocked, which is the one safe window to read
        // the item's state.
        auto* mpv_item = static_cast<MpvItem*>(item);
        player_ = mpv_item->GetPlayer();
        window_ = mpv_item->window();
        item_ = mpv_item;
    }

    void render() override {
        if (!player_) return;

        if (!initialized_) {
            initialized_ = player_->InitializeRenderer(&GetProcAddress, nullptr);
            if (!initialized_) return;

            // mpv reports new frames from its own thread, so the redraw request
            // has to be queued onto the UI thread — the only one allowed to ask
            // for one.
            //
            // It must be the *item* that's updated, not the window. A window
            // repaint re-composes the scene from what it already has, and this
            // item's contribution is an FBO texture the scene graph is happy to
            // reuse; only marking the item dirty re-runs render() and pulls a
            // new frame out of mpv. Updating the window instead leaves the very
            // first frame (drawn before any file was open, so black) on screen
            // forever, while the clock and scrubber advance quite happily.
            QPointer<MpvItem> item = item_;
            player_->SetRenderUpdateCallback([item] {
                if (!item) return;
                QMetaObject::invokeMethod(
                    item, [item] { if (item) item->update(); }, Qt::QueuedConnection);
            });

            // Queued, because this is the render thread and RendererReady's
            // subscribers live on the UI thread. Nothing may be opened until
            // this lands.
            if (item_) {
                QMetaObject::invokeMethod(
                    item_, [item = item_] { if (item) emit item->RendererReady(); },
                    Qt::QueuedConnection);
            }
        }

        QOpenGLFramebufferObject* target = framebufferObject();
        if (!target) return;

        // mpv renders into this item's own FBO, not into the scene graph's
        // command stream, so its GL state changes can't corrupt the rest of the
        // UI — which is just as well, since Qt 6 removed the
        // resetOpenGLState() that Qt 5 mpv integrations relied on. Wrapping the
        // call tells the scene graph its cached state assumptions are void.
        if (window_) window_->beginExternalCommands();
        player_->RenderTo(static_cast<int>(target->handle()), target->width(), target->height());
        if (window_) window_->endExternalCommands();

        // Once per renderer, on the first frame rendered with a file actually
        // loaded — the moment "playing" stops being a promise and becomes
        // pixels. The state check skips the black frame the scene graph asks
        // for before anything is open.
        if (!first_frame_logged_) {
            const Player::State state = player_->GetStatus().state;
            if (state == Player::State::Playing || state == Player::State::Paused) {
                first_frame_logged_ = true;
                qInfo() << "player: frames streaming";
            }
        }
    }

private:
    Player* player_ = nullptr;
    QQuickWindow* window_ = nullptr;
    // Weak: the item lives on the UI thread and can be destroyed (the overlay's
    // Loader deactivates) while this renderer is still winding down.
    QPointer<MpvItem> item_;
    bool initialized_ = false;
    bool first_frame_logged_ = false;
};

// Vertical orientation is mpv's job, not Qt's: the backend passes FLIP_Y to
// the render API, so the FBO already arrives the right way up and
// mirrorVertically stays at its default of false. Doing it in both places
// flips the video twice and looks identical to doing it in neither.
MpvItem::MpvItem(QQuickItem* parent) : QQuickFramebufferObject(parent) {}

MpvItem::~MpvItem() = default;

void MpvItem::SetPlayer(Player* player) {
    player_ = player;
    update();
}

QQuickFramebufferObject::Renderer* MpvItem::createRenderer() const {
    return new MpvRenderer(player_);
}

} // namespace synaxis::gui
