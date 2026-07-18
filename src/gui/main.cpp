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

#include "library_controller.hpp"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>

#include <clocale>
#include <cstdlib>

#ifndef SYNAXIS_VERSION
#define SYNAXIS_VERSION "unknown"
#endif

int main(int argc, char** argv) {
    // mpv's render API speaks OpenGL, so the scene graph has to as well —
    // MpvItem renders into a GL framebuffer the scene graph owns, and there's
    // no shared surface at all if Qt picked Vulkan or its software rasterizer.
    // Qt's default varies by platform and driver, so it's pinned rather than
    // hoped for. Must precede QGuiApplication, which is when the choice is made.
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

    QGuiApplication app(argc, argv);

    // libmpv refuses to initialize unless LC_NUMERIC is "C" — it parses option
    // values as numbers and a locale using ',' as the decimal separator would
    // silently corrupt them. Constructing QGuiApplication clobbers it: the
    // platform integration calls setlocale(LC_ALL, "") to pick up the user's
    // locale, so this has to be put back afterwards rather than before.
    //
    // Without it every mpv_create() fails and the whole artwork pipeline
    // silently degrades to typographic tiles. The CLI never hit this because it
    // never touches the locale.
    std::setlocale(LC_NUMERIC, "C");

    QCoreApplication::setApplicationName("Synaxis");
    QCoreApplication::setApplicationVersion(SYNAXIS_VERSION);
    QCoreApplication::setOrganizationName("Synaxis");

    // Declared before the engine so it is destroyed *after* it: the QML scene
    // holds an MpvItem that calls into this object's Player from the render
    // thread while the scene is torn down. QML reaches it through
    // LibraryController::create() (see the QML_SINGLETON there), not through a
    // registration call here — registering into the Synaxis URI from outside
    // the module silently disables the module's own type registration.
    synaxis::gui::LibraryController library(nullptr);
    synaxis::gui::LibraryController::SetInstance(&library);

    QQmlApplicationEngine engine;

    // A failure to instantiate the root object is fatal and must not leave a
    // zero-window process running with a zero exit code: QML errors are
    // reported to stderr by the engine, and this turns them into a failure the
    // shell can see.
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);

    // Development aid: SYNAXIS_CAPTURE=<path.png> renders the window, writes it
    // out, and exits. The look is the requirement here, and it can't be checked
    // by reading QML — this makes "does it still look right" something that can
    // be answered from a terminal, including under QT_QPA_PLATFORM=offscreen
    // where there's no screen to point a capture tool at.
    //
    // The delay lets artwork land first: tiles decode on a worker thread and
    // fade in, so grabbing immediately would only ever photograph the empty
    // typographic state.
    if (const char* capture_path = std::getenv("SYNAXIS_CAPTURE")) {
        const QString path = QString::fromLocal8Bit(capture_path);
        QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &app,
                         [path](QObject* object, const QUrl&) {
                             auto* window = qobject_cast<QQuickWindow*>(object);
                             if (!window) return;
                             QTimer::singleShot(4000, window, [window, path] {
                                 window->grabWindow().save(path);
                                 QCoreApplication::quit();
                             });
                         });
    }

    engine.loadFromModule("Synaxis", "Main");

    return app.exec();
}
