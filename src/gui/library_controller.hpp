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

#include "app_config.hpp"
#include "shelf_model.hpp"

#include "synaxis/artwork.hpp"
#include "synaxis/media_library.hpp"
#include "synaxis/player.hpp"
#include "synaxis/watch_state.hpp"

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <memory>
#include <vector>

// std::vector<MediaEntry> crosses from the UI thread to the artwork worker
// through a queued connection, which copies the argument through the metatype
// system and needs it registered to do so.
Q_DECLARE_METATYPE(std::vector<synaxis::MediaEntry>)

// Only referenced by create()'s signature, which the engine calls. Forward
// declared to keep the QML engine's headers out of everything that includes
// this one.
class QJSEngine;
class QQmlEngine;

namespace synaxis::gui {

// Generates artwork off the UI thread.
//
// Owns the ArtworkCache outright and is the only thing that touches it, which
// is what satisfies the cache's "not thread-safe, caller owns threading"
// contract. Results leave by signal, so they cross back to the UI thread
// through a queued connection rather than by sharing anything.
class ArtworkWorker : public QObject {
    Q_OBJECT

public:
    ~ArtworkWorker() override;

public slots:
    // Walks `entries` in order, emitting Ready for each tile as it lands.
    // Runs on the worker thread; each entry costs a decode on a cold cache.
    void Generate(const std::vector<MediaEntry>& entries);

    // Asks the walk to stop at the next entry. Set from the UI thread while
    // Generate() is running, so it's atomic rather than a plain bool.
    void Cancel();

    // Drops the provider chain so the next Generate() rebuilds it from
    // scratch. Needed because the chain is built once, on first use, with
    // whatever TMDB key was live at that moment — without this, a key typed
    // into the settings page mid-session would never take effect until the
    // app restarted.
    void ResetCache();

signals:
    void Ready(const QString& path, const QUrl& artwork);
    void Finished();

private:
    // Built lazily on the worker thread: MakeFrameProvider() spins up libmpv,
    // which must not happen on the UI thread and must not happen at all for a
    // library whose artwork is already cached.
    bool EnsureCache();

    std::unique_ptr<ArtworkCache> cache_;
    std::atomic<bool> cancelled_{false};
};

// Walks the directories the settings page tracks and rebuilds library.json
// from scratch. Off the UI thread for the same reason ArtworkWorker is: a
// scan over a large tree takes long enough to notice, and nothing about
// walking a filesystem needs the UI thread.
class LibraryScanWorker : public QObject {
    Q_OBJECT

public slots:
    // Scans each of `directories` and merges the results into one
    // library.json at MediaLibrary::DefaultLibraryPath(). A path found under
    // more than one directory keeps whichever scan visited it last, so
    // overlapping roots don't produce a duplicate tile.
    void Scan(const QStringList& directories);

signals:
    void Progress(int filesIndexed);
    void Finished();
};

// The one object QML talks to.
//
// Holds the library, the watch state, and the shelves built from them, and
// owns the worker thread that fills in artwork. A singleton because it owns a
// thread, a libmpv handle, and a network manager; QML must not be able to make
// a second one.
//
// Declared to QML here rather than with qmlRegisterSingletonInstance() in
// main(). That call registers into the module's URI from outside the module,
// which pre-empts the deferred registration qt_add_qml_module generates — the
// module then counts as already registered and qml_register_types_Synaxis()
// never runs, so every QML_ELEMENT type in it silently vanishes while the
// hand-registered singleton keeps working. The failure surfaces as an
// unrelated "MpvItem is not a type", so it's worth not reintroducing.
class LibraryController : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Library)
    QML_SINGLETON

    // The shelves, in display order. CONSTANT because the list itself is
    // rebuilt wholesale on reload rather than mutated — but see shelvesChanged.
    Q_PROPERTY(QAbstractListModel* shelves READ shelves NOTIFY shelvesChanged)

    // True when there's nothing to show, which drives the empty state. Distinct
    // from "loading": an empty library is a normal condition (the user hasn't
    // scanned yet), not a failure.
    Q_PROPERTY(bool empty READ empty NOTIFY shelvesChanged)

    // The hero banner's subject: the most recently played thing, or the first
    // film if nothing's been played. Null when the library is empty.
    Q_PROPERTY(QString heroTitle READ heroTitle NOTIFY shelvesChanged)
    Q_PROPERTY(QString heroSubtitle READ heroSubtitle NOTIFY shelvesChanged)
    Q_PROPERTY(QUrl heroArtwork READ heroArtwork NOTIFY heroArtworkChanged)
    Q_PROPERTY(QString heroPath READ heroPath NOTIFY shelvesChanged)

    // Playback state, for the player overlay. `playing` is what QML switches
    // the video surface on: true from the moment Play() is accepted until
    // Close(), covering Loading as well, because the surface has to exist
    // before the first frame can be drawn into it.
    Q_PROPERTY(bool playing READ playing NOTIFY playbackChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY playbackChanged)
    Q_PROPERTY(QString playingTitle READ playingTitle NOTIFY playbackChanged)
    Q_PROPERTY(double position READ position NOTIFY playbackChanged)
    Q_PROPERTY(double duration READ duration NOTIFY playbackChanged)

    // Settings: the TMDB key and the directories RescanLibrary() walks, plus
    // how a scan in progress is going. Every setter below persists the whole
    // config and updates these in the same call, so the settings page never
    // has to poll.
    Q_PROPERTY(QString tmdbApiKey READ tmdbApiKey NOTIFY configChanged)
    Q_PROPERTY(QStringList libraryDirectories READ libraryDirectories NOTIFY configChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(int scanFilesIndexed READ scanFilesIndexed NOTIFY scanningChanged)

public:
    explicit LibraryController(QObject* parent = nullptr);
    ~LibraryController() override;

    // Hands QML the instance main() already owns, rather than letting the
    // engine construct one. Ownership stays in C++ precisely so this outlives
    // the engine: the QML scene holds an MpvItem whose renderer calls into our
    // Player from the render thread during teardown, and an engine-owned
    // singleton could be destroyed while that's still in flight.
    static void SetInstance(LibraryController* instance);
    static LibraryController* create(QQmlEngine* engine, QJSEngine* script_engine);

    QAbstractListModel* shelves();
    bool empty() const { return shelves_.isEmpty(); }
    QString heroTitle() const { return hero_.title; }
    QString heroSubtitle() const { return hero_.subtitle; }
    QUrl heroArtwork() const { return hero_.artwork; }
    QString heroPath() const { return hero_.path; }

    bool playing() const { return !playing_path_.isEmpty(); }
    bool paused() const { return status_.state == Player::State::Paused; }
    QString playingTitle() const { return playing_title_; }
    double position() const { return status_.position; }
    double duration() const { return status_.duration; }

    QString tmdbApiKey() const { return config_.tmdb_api_key; }
    QStringList libraryDirectories() const { return config_.library_directories; }
    bool scanning() const { return scanning_; }
    int scanFilesIndexed() const { return scan_files_indexed_; }

    // Reads library.json and watch.json and rebuilds the shelves, then starts
    // generating any artwork that isn't cached yet. Safe to call again.
    Q_INVOKABLE void Reload();

    // Starts playing `path`, into whatever surface has been attached.
    Q_INVOKABLE void Play(const QString& path);

    // Stops playback and returns to browsing.
    Q_INVOKABLE void Close();

    Q_INVOKABLE void TogglePause();

    // Absolute position, in seconds.
    Q_INVOKABLE void Seek(double seconds);

    // Hands the video surface the player to draw. Called by QML once the item
    // exists; the controller owns the Player, so the item only ever borrows it.
    Q_INVOKABLE void AttachVideoOutput(QObject* item);

    // Persists a new TMDB key and drops the artwork worker's cached provider
    // chain so the next tile generated picks it up.
    Q_INVOKABLE void SetTmdbApiKey(const QString& key);

    // Tracks `directory` (a file:// URL, as FolderDialog reports selections)
    // for RescanLibrary() to walk. Silently declined if it isn't a directory
    // or is already tracked — there's nothing actionable for the user to fix
    // in either case.
    Q_INVOKABLE void AddLibraryDirectory(const QUrl& directory);

    // Stops tracking `directory`. Entries already in library.json from it
    // are left alone until the next rescan.
    Q_INVOKABLE void RemoveLibraryDirectory(const QString& directory);

    // Re-walks every tracked directory and rebuilds library.json from
    // scratch, then reloads. No-op while a scan is already running, or if
    // nothing is tracked.
    Q_INVOKABLE void RescanLibrary();

signals:
    void shelvesChanged();
    void heroArtworkChanged();
    void playbackChanged();
    void configChanged();
    void scanningChanged();

private:
    void BuildShelves();
    void OnArtworkReady(const QString& path, const QUrl& artwork);
    void OnPlayerStatus(const Player::Status& status);

    // Called once the attached surface's render context exists. Opening before
    // this fails outright — see MpvItem::RendererReady.
    void OnRendererReady();

    // Hands the pending file to the backend. No-op unless both a file is
    // waiting and the renderer is up.
    void OpenPending();

    // Writes the current position to the watch store. Called at the moments
    // that matter rather than on every status tick.
    void Persist();

    void OnScanProgress(int filesIndexed);
    void OnScanFinished();

    // The display title for a media path, for the player overlay.
    QString TitleForPath(const QString& path) const;

    // Artwork resolved so far, by media path.
    //
    // The shelves are rebuilt whenever playback settles, and a rebuilt tile
    // would otherwise come back blank and stay blank — the worker has already
    // finished and won't report that file again. This is also what lets the UI
    // thread stay out of the cache entirely: the cache is keyed per provider,
    // so reading it means knowing the provider order, and knowing that means
    // either duplicating the chain here or constructing libmpv on the UI
    // thread. Neither is worth it to save the few milliseconds before the
    // worker reports a warm cache back.
    QHash<QString, QUrl> artwork_by_path_;

    std::vector<MediaEntry> entries_;
    WatchStore watch_;
    std::filesystem::path watch_path_;

    AppConfig config_;
    std::filesystem::path config_path_;

    QList<ShelfModel*> shelves_;
    std::unique_ptr<class ShelvesModel> shelves_model_;
    Tile hero_;

    QThread artwork_thread_;
    ArtworkWorker* artwork_worker_ = nullptr;

    QThread scan_thread_;
    LibraryScanWorker* scan_worker_ = nullptr;
    bool scanning_ = false;
    int scan_files_indexed_ = 0;

    // MpvEmbedded: no window of its own, drawn by whatever MpvItem is attached.
    // Constructed here rather than per-playback so the render context outlives
    // any single file — tearing it down between episodes would mean
    // re-establishing GL state on every play.
    Player player_{Player::Backend::MpvEmbedded};

    // Playback state, kept so a status callback can attribute progress to the
    // file it belongs to: Player::Status doesn't carry the path.
    QString playing_path_;
    QString playing_title_;
    Player::Status status_;

    // Play() can't open immediately: it has to return so QML can create the
    // video surface, whose render context is what mpv needs to exist first. The
    // path waits here until OnRendererReady() says the surface is live.
    QString pending_path_;
    bool renderer_ready_ = false;
};

} // namespace synaxis::gui
