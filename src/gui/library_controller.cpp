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

#include "mpv_item.hpp"
#include "tmdb_provider.hpp"

#include <QDebug>
#include <QJSEngine>
#include <QMetaObject>
#include <QQmlEngine>

#include <algorithm>
#include <utility>

namespace synaxis::gui {

namespace {

QString ToQString(const std::filesystem::path& path) {
    return QString::fromStdString(path.string());
}

// "S01E02" — zero-padded, because unpadded episode numbers make a shelf's
// subtitles ragged and are the convention every release uses anyway.
QString FormatEpisode(const ParsedFilename& metadata) {
    return QStringLiteral("S%1E%2")
        .arg(metadata.season.value_or(0), 2, 10, QLatin1Char('0'))
        .arg(metadata.episode.value_or(0), 2, 10, QLatin1Char('0'));
}

// The line under a tile's title: year and source for a film, the episode marker
// for an episode. Parts that didn't survive parsing are simply left out rather
// than rendered as a placeholder — a tile reading "Unknown · Unknown" is worse
// than one with no subtitle.
QString FormatSubtitle(const MediaEntry& entry) {
    const ParsedFilename& metadata = entry.metadata;

    QStringList parts;
    if (metadata.season && metadata.episode) parts << FormatEpisode(metadata);
    if (metadata.year) parts << QString::number(*metadata.year);
    if (metadata.source) parts << QString::fromStdString(*metadata.source);

    return parts.join(QStringLiteral(" · "));
}

QString DisplayTitle(const MediaEntry& entry) {
    if (entry.metadata.title) return QString::fromStdString(*entry.metadata.title);
    // Nothing parsed: the filename is all we have, and showing it beats an
    // untitled tile the user can't identify.
    return ToQString(entry.path.filename());
}

Tile MakeTile(const MediaEntry& entry) {
    Tile tile;
    tile.title = DisplayTitle(entry);
    tile.subtitle = FormatSubtitle(entry);
    tile.path = ToQString(entry.path);
    return tile;
}

} // namespace

// ---------------------------------------------------------------------------
// ShelvesModel
// ---------------------------------------------------------------------------

// The list of shelves. Its ItemsRole hands QML a ShelfModel*, which QML uses
// directly as the inner ListView's model — the standard way to nest a model
// without flattening the structure into one list and re-deriving the grouping
// in QML.
class ShelvesModel : public QAbstractListModel {
public:
    enum Role {
        TitleRole = Qt::UserRole + 1,
        ItemsRole,
    };

    using QAbstractListModel::QAbstractListModel;

    void SetShelves(QList<QPair<QString, ShelfModel*>> shelves) {
        beginResetModel();
        shelves_ = std::move(shelves);
        endResetModel();
    }

    int rowCount(const QModelIndex& parent = {}) const override {
        if (parent.isValid()) return 0;
        return static_cast<int>(shelves_.size());
    }

    QVariant data(const QModelIndex& index, int role) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= shelves_.size()) return {};

        const auto& [title, model] = shelves_.at(index.row());
        switch (role) {
            case TitleRole: return title;
            case ItemsRole: return QVariant::fromValue(static_cast<QObject*>(model));
            default: return {};
        }
    }

    QHash<int, QByteArray> roleNames() const override {
        return {{TitleRole, "shelfTitle"}, {ItemsRole, "shelfItems"}};
    }

private:
    QList<QPair<QString, ShelfModel*>> shelves_;
};

// ---------------------------------------------------------------------------
// ArtworkWorker
// ---------------------------------------------------------------------------

ArtworkWorker::~ArtworkWorker() = default;

bool ArtworkWorker::EnsureCache() {
    if (cache_) return true;

    // Built here rather than in the constructor: the constructor runs on the UI
    // thread (the worker is created there and then moved), and both providers
    // own thread-affine resources — libmpv's handle and a
    // QNetworkAccessManager — that belong to whichever thread will drive them.
    std::vector<std::unique_ptr<ArtworkProvider>> providers;

    // TMDB first: a real backdrop beats a frame from the film. Absent an API
    // key this is null and the chain is frame-only, which is the offline
    // default rather than a degraded mode.
    if (std::unique_ptr<ArtworkProvider> tmdb = MakeTmdbProvider(ReadTmdbApiKey())) {
        providers.push_back(std::move(tmdb));
    }

    std::unique_ptr<ArtworkProvider> frame = MakeFrameProvider();
    if (!frame) {
        qWarning() << "artwork: libmpv unavailable; tiles will fall back to typography";
    } else {
        providers.push_back(std::move(frame));
    }

    // Every provider failed to construct: there's nothing to consult, and
    // proceeding would mean an ArtworkCache that can only ever decline.
    if (providers.empty()) return false;

    cache_ = std::make_unique<ArtworkCache>(std::move(providers));
    return true;
}

void ArtworkWorker::Generate(const std::vector<MediaEntry>& entries) {
    cancelled_ = false;

    for (const MediaEntry& entry : entries) {
        if (cancelled_) break;
        if (!EnsureCache()) break;

        std::optional<std::filesystem::path> artwork = cache_->Get(entry);
        if (!artwork) continue;  // declined; the tile keeps its typographic look

        emit Ready(ToQString(entry.path), QUrl::fromLocalFile(ToQString(*artwork)));
    }

    emit Finished();
}

void ArtworkWorker::Cancel() { cancelled_ = true; }

void ArtworkWorker::ResetCache() { cache_.reset(); }

// ---------------------------------------------------------------------------
// LibraryScanWorker
// ---------------------------------------------------------------------------

void LibraryScanWorker::Scan(const QStringList& directories) {
    std::vector<MediaEntry> merged;
    std::size_t indexed_so_far = 0;

    for (const QString& dir : directories) {
        auto on_progress = [&](const ScanProgress& progress) {
            emit Progress(static_cast<int>(indexed_so_far + progress.files_indexed));
            return true;
        };

        std::vector<MediaEntry> found =
            MediaLibrary::Scan(std::filesystem::path(dir.toStdString()), {}, on_progress);
        indexed_so_far += found.size();

        // A later directory wins on a path collision, so overlapping roots
        // (one tracked directory nested inside another) don't produce a
        // duplicate tile.
        for (MediaEntry& entry : found) {
            auto it = std::find_if(merged.begin(), merged.end(), [&entry](const MediaEntry& e) {
                return e.path == entry.path;
            });
            if (it != merged.end()) {
                *it = std::move(entry);
            } else {
                merged.push_back(std::move(entry));
            }
        }
    }

    MediaLibrary::SaveToJson(merged, MediaLibrary::DefaultLibraryPath());
    emit Finished();
}

// ---------------------------------------------------------------------------
// LibraryController
// ---------------------------------------------------------------------------

namespace {
// Set by main() before the engine loads anything. A plain pointer rather than a
// lazily-constructed instance because the object's lifetime is main()'s to
// control — see SetInstance's declaration.
LibraryController* g_instance = nullptr;
} // namespace

void LibraryController::SetInstance(LibraryController* instance) { g_instance = instance; }

LibraryController* LibraryController::create(QQmlEngine*, QJSEngine*) {
    // CppOwnership stops the engine from deleting an object it didn't make.
    // Without it the engine takes ownership of whatever create() returns and
    // would double-free main()'s instance at teardown.
    QJSEngine::setObjectOwnership(g_instance, QJSEngine::CppOwnership);
    return g_instance;
}

LibraryController::LibraryController(QObject* parent)
    : QObject(parent),
      watch_path_(WatchStore::DefaultPath()),
      config_path_(DefaultConfigPath()),
      shelves_model_(std::make_unique<ShelvesModel>()) {
    qRegisterMetaType<std::vector<MediaEntry>>();

    config_ = LoadAppConfig(config_path_);

    artwork_worker_ = new ArtworkWorker;
    artwork_worker_->moveToThread(&artwork_thread_);
    connect(&artwork_thread_, &QThread::finished, artwork_worker_, &QObject::deleteLater);
    connect(artwork_worker_, &ArtworkWorker::Ready, this, &LibraryController::OnArtworkReady);
    artwork_thread_.start();

    scan_worker_ = new LibraryScanWorker;
    scan_worker_->moveToThread(&scan_thread_);
    connect(&scan_thread_, &QThread::finished, scan_worker_, &QObject::deleteLater);
    connect(scan_worker_, &LibraryScanWorker::Progress, this, &LibraryController::OnScanProgress);
    connect(scan_worker_, &LibraryScanWorker::Finished, this, &LibraryController::OnScanFinished);
    scan_thread_.start();

    // The backend calls this from its own thread and must not block or re-enter
    // Player, so this does the one thing Player's docs prescribe: hop the value
    // onto the UI thread and return.
    player_.SetStatusCallback([this](const Player::Status& status) {
        QMetaObject::invokeMethod(
            this, [this, status] { OnPlayerStatus(status); }, Qt::QueuedConnection);
    });
}

LibraryController::~LibraryController() {
    // Ask the walk to stop before quitting the thread: a Generate() midway
    // through a decode would otherwise keep the thread alive until libmpv
    // finished with a file nobody is waiting for any more.
    if (artwork_worker_) artwork_worker_->Cancel();
    artwork_thread_.quit();
    artwork_thread_.wait();

    scan_thread_.quit();
    scan_thread_.wait();
}

QAbstractListModel* LibraryController::shelves() { return shelves_model_.get(); }


void LibraryController::Reload() {
    config_ = LoadAppConfig(config_path_);
    emit configChanged();

    const std::filesystem::path library_path = MediaLibrary::DefaultLibraryPath();

    std::error_code ec;
    if (!std::filesystem::exists(library_path, ec)) {
        entries_.clear();
    } else {
        try {
            entries_ = MediaLibrary::LoadFromJson(library_path);
        } catch (const std::exception& error) {
            // A corrupt library shouldn't take the window down with it; the
            // empty state at least tells the user to rescan.
            qWarning() << "failed to read library:" << error.what();
            entries_.clear();
        }
    }

    watch_ = WatchStore::LoadFromJson(watch_path_);

    BuildShelves();
    emit shelvesChanged();

    if (entries_.empty()) return;

    // Every entry goes to the worker, cached or not. Resolving the cache here
    // instead would mean reading it from the UI thread, and the cache is keyed
    // per provider — so that would require either duplicating the provider
    // order or building the providers (and libmpv with them) on the UI thread.
    // An already-cached tile costs the worker one exists() check and comes
    // straight back, so the saving wouldn't have been worth either.
    QMetaObject::invokeMethod(artwork_worker_, "Generate", Qt::QueuedConnection,
                              Q_ARG(std::vector<MediaEntry>, entries_));
}

void LibraryController::BuildShelves() {
    qDeleteAll(shelves_);
    shelves_.clear();

    QList<QPair<QString, ShelfModel*>> shelves;

    auto add_shelf = [&](const QString& title, QList<Tile> tiles) {
        if (tiles.isEmpty()) return;
        auto* model = new ShelfModel(std::move(tiles), this);
        shelves_.append(model);
        shelves.append({title, model});
    };

    // Continue Watching leads, as it does on <inspiration>: the thing you're partway
    // through is the likeliest reason you opened the app.
    QList<Tile> resume;
    for (const WatchRecord& record : watch_.ContinueWatching()) {
        auto it = std::find_if(entries_.begin(), entries_.end(), [&record](const MediaEntry& e) {
            return e.path == record.path;
        });
        // A record whose file has left the library is skipped rather than
        // shown: the tile would be unplayable.
        if (it == entries_.end()) continue;

        Tile tile = MakeTile(*it);
        tile.artwork = artwork_by_path_.value(tile.path);
        if (record.duration > 0.0) tile.progress = record.position / record.duration;
        resume.append(tile);
    }
    add_shelf(QStringLiteral("Continue Watching"), std::move(resume));

    const LibraryTree tree = MediaLibrary::Group(entries_);

    QList<Tile> movies;
    for (const MediaEntry& entry : tree.movies) {
        Tile tile = MakeTile(entry);
        tile.artwork = artwork_by_path_.value(tile.path);
        movies.append(tile);
    }
    add_shelf(QStringLiteral("Movies"), std::move(movies));

    // One shelf per series, its episodes in order. Seasons are flattened into
    // a single row for now; a series with many seasons wants a detail view
    // rather than an ever-longer shelf, and that's a later step.
    for (const Series& series : tree.series) {
        QList<Tile> episodes;
        for (const Season& season : series.seasons) {
            for (const MediaEntry& entry : season.episodes) {
                Tile tile = MakeTile(entry);
                tile.artwork = artwork_by_path_.value(tile.path);
                // The series name is already the shelf title; repeating it on
                // every tile wastes the line, so the episode marker leads.
                tile.title = FormatEpisode(entry.metadata);
                tile.subtitle = QString::fromStdString(
                    entry.metadata.source.value_or(std::string()));
                episodes.append(tile);
            }
        }
        add_shelf(QString::fromStdString(series.title), std::move(episodes));
    }

    shelves_model_->SetShelves(std::move(shelves));

    // The hero is whatever the first shelf leads with — Continue Watching if
    // there is one, otherwise the first film. That makes it the most relevant
    // thing on screen without needing a separate notion of "featured".
    hero_ = Tile{};
    if (!shelves_.isEmpty() && !shelves_.first()->Tiles().isEmpty()) {
        hero_ = shelves_.first()->Tiles().first();
    }

    // heroArtwork notifies on its own signal, not on shelvesChanged, so
    // rebuilding the shelves has to say so explicitly. Without this the hero
    // picture only ever appears when the artwork worker happens to report one,
    // which means it works on a cold cache and silently breaks on a warm one —
    // the title and subtitle update either way, so the failure looks like a
    // missing image rather than a stale binding.
    emit heroArtworkChanged();
}

void LibraryController::OnArtworkReady(const QString& path, const QUrl& artwork) {
    // Remembered so a later BuildShelves() — after playback settles, say —
    // rebuilds tiles with their pictures instead of blanking them.
    artwork_by_path_.insert(path, artwork);

    for (ShelfModel* shelf : shelves_) shelf->SetArtwork(path, artwork);

    if (hero_.path == path) {
        hero_.artwork = artwork;
        emit heroArtworkChanged();
    }
}

void LibraryController::AttachVideoOutput(QObject* item) {
    auto* video = qobject_cast<MpvItem*>(item);
    if (!video) {
        qWarning() << "AttachVideoOutput: not an MpvItem";
        return;
    }

    video->SetPlayer(&player_);
    connect(video, &MpvItem::RendererReady, this, &LibraryController::OnRendererReady,
            Qt::UniqueConnection);
}

void LibraryController::OnRendererReady() {
    renderer_ready_ = true;
    OpenPending();
}

void LibraryController::OpenPending() {
    if (pending_path_.isEmpty() || !renderer_ready_) return;

    const std::filesystem::path media(pending_path_.toStdString());
    pending_path_.clear();

    // Resume where the user left off. Passed into Open() rather than seeked
    // afterward: Open() only queues the load and returns before the backend
    // has anything loaded, so a Seek() issued right after it races the load
    // and is silently dropped.
    const double resume = watch_.ResumePosition(media);

    if (!player_.Open(media, resume)) {
        qWarning() << "playback failed to start:" << ToQString(media);
        playing_path_.clear();
        playing_title_.clear();
        emit playbackChanged();
        return;
    }
}

void LibraryController::Play(const QString& path) {
    if (path.isEmpty()) return;

    const std::filesystem::path media(path.toStdString());
    std::error_code ec;
    if (!std::filesystem::exists(media, ec)) {
        qWarning() << "file no longer exists:" << path;
        return;
    }

    // `playing` is what makes QML build the video surface, and mpv can't open
    // anything until that surface's render context exists. So this only marks
    // the intent; OpenPending() does the work once the renderer reports in.
    playing_path_ = path;
    playing_title_ = TitleForPath(path);
    pending_path_ = path;
    status_ = Player::Status{};
    status_.state = Player::State::Loading;
    emit playbackChanged();

    // Already up — a second film in the same session, with the surface still
    // alive — so there is nothing to wait for.
    OpenPending();
}

void LibraryController::Close() {
    if (playing_path_.isEmpty()) return;

    // Save before stopping. Stop() reports Ended, and mpv zeroes time-pos on
    // unload, so waiting for the callback would record a position of 0 and
    // silently destroy the resume point the user just earned.
    Persist();

    player_.Stop();
    playing_path_.clear();
    playing_title_.clear();
    pending_path_.clear();
    status_ = Player::Status{};

    // The surface goes away with the overlay, taking its render context with
    // it, so the next Play() has to wait for a new one.
    renderer_ready_ = false;

    BuildShelves();
    emit shelvesChanged();
    emit playbackChanged();
}

void LibraryController::TogglePause() {
    if (playing_path_.isEmpty()) return;
    if (status_.state == Player::State::Paused) {
        player_.Resume();
    } else {
        player_.Pause();
    }
}

void LibraryController::Seek(double seconds) {
    if (playing_path_.isEmpty()) return;
    player_.Seek(seconds);
}

QString LibraryController::TitleForPath(const QString& path) const {
    auto it = std::find_if(entries_.begin(), entries_.end(), [&path](const MediaEntry& entry) {
        return ToQString(entry.path) == path;
    });
    if (it == entries_.end()) return {};

    QString title = DisplayTitle(*it);
    if (it->metadata.season && it->metadata.episode) {
        title += QStringLiteral(" · ") + FormatEpisode(it->metadata);
    }
    return title;
}

void LibraryController::SetTmdbApiKey(const QString& key) {
    if (config_.tmdb_api_key == key) return;

    config_.tmdb_api_key = key;
    SaveAppConfig(config_, config_path_);
    emit configChanged();

    // The provider chain is built once, on first use, with whatever key was
    // live at that moment. Without dropping it here, a key typed into the
    // settings page mid-session would never take effect until the app
    // restarted.
    QMetaObject::invokeMethod(artwork_worker_, "ResetCache", Qt::QueuedConnection);
}

void LibraryController::AddLibraryDirectory(const QUrl& directory) {
    if (!directory.isLocalFile()) return;

    std::error_code ec;
    const std::filesystem::path path = std::filesystem::canonical(
        std::filesystem::path(directory.toLocalFile().toStdString()), ec);
    if (ec || !std::filesystem::is_directory(path)) {
        qWarning() << "not a directory:" << directory.toLocalFile();
        return;
    }

    const QString canonical = ToQString(path);
    if (config_.library_directories.contains(canonical)) return;

    config_.library_directories.append(canonical);
    SaveAppConfig(config_, config_path_);
    emit configChanged();
}

void LibraryController::RemoveLibraryDirectory(const QString& directory) {
    if (!config_.library_directories.removeOne(directory)) return;

    SaveAppConfig(config_, config_path_);
    emit configChanged();
}

void LibraryController::RescanLibrary() {
    if (scanning_ || config_.library_directories.isEmpty()) return;

    scanning_ = true;
    scan_files_indexed_ = 0;
    emit scanningChanged();

    QMetaObject::invokeMethod(scan_worker_, "Scan", Qt::QueuedConnection,
                              Q_ARG(QStringList, config_.library_directories));
}

void LibraryController::OnScanProgress(int filesIndexed) {
    scan_files_indexed_ = filesIndexed;
    emit scanningChanged();
}

void LibraryController::OnScanFinished() {
    scanning_ = false;
    emit scanningChanged();

    // Rereads library.json, which the worker just rewrote, and rebuilds the
    // shelves from it — the same path a fresh launch takes.
    Reload();
}

void LibraryController::Persist() {
    if (playing_path_.isEmpty()) return;
    if (status_.position <= 0.0) return;

    watch_.Record(std::filesystem::path(playing_path_.toStdString()), status_.position,
                  status_.duration);
    watch_.SaveToJson(watch_path_);
}

void LibraryController::OnPlayerStatus(const Player::Status& status) {
    if (playing_path_.isEmpty()) return;

    const Player::State previous = status_.state;
    status_ = status;
    emit playbackChanged();

    // Positions arrive several times a second, so the file write waits for a
    // moment that actually matters rather than riding every tick.
    const bool settled = status.state == Player::State::Paused ||
                         status.state == Player::State::Ended ||
                         status.state == Player::State::Error;
    if (!settled) return;

    // Ended arrives with the position already at (or near) the duration, which
    // is what marks the file finished and drops it off the shelf. Pausing keeps
    // the file open, so the resume point is simply where it stopped.
    Persist();

    if (status.state == Player::State::Paused) return;

    // The file finished or failed: leave playback and go back to browsing.
    // Rebuilt because a finished film drops off Continue Watching and a
    // half-watched one moves to its front.
    if (previous != status.state) {
        playing_path_.clear();
        playing_title_.clear();
        BuildShelves();
        emit shelvesChanged();
        emit playbackChanged();
    }
}

} // namespace synaxis::gui
