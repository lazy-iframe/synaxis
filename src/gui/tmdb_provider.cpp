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

#include "tmdb_provider.hpp"

#include <QByteArray>
#include <QEventLoop>
#include <QFile>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>

#include <cstdlib>

namespace synaxis::gui {

namespace {

constexpr auto kApiBase = "https://api.themoviedb.org/3";

// TMDB serves backdrops and stills at fixed widths. w780 is the narrowest that
// still exceeds kArtworkWidth (640), so it downscales into a tile without ever
// being upscaled into one.
constexpr auto kImageBase = "https://image.tmdb.org/t/p/w780";

// A request that hangs would stall every tile queued behind it on the worker
// thread. Five seconds is generous for a JSON lookup and short enough that a
// dead network degrades to frame extraction promptly rather than appearing to
// freeze.
constexpr int kTimeoutMs = 5000;

// Runs `reply` to completion on the calling thread.
//
// ArtworkProvider::Fetch is synchronous by contract, and QNetworkAccessManager
// is asynchronous, so something has to bridge them. A nested event loop is that
// bridge: it's safe here because this only ever runs on the artwork worker
// thread, which has nothing else to service — doing this on the UI thread would
// reenter the scene graph and is exactly what the worker exists to avoid.
QByteArray BlockingRead(QNetworkReply* reply) {
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    const QByteArray body = reply->error() == QNetworkReply::NoError ? reply->readAll()
                                                                      : QByteArray();
    reply->deleteLater();
    return body;
}

class TmdbProvider : public ArtworkProvider {
public:
    explicit TmdbProvider(QString api_key) : api_key_(std::move(api_key)) {
        manager_.setTransferTimeout(kTimeoutMs);
    }

    std::string_view Name() const override { return "tmdb"; }

    bool Fetch(const MediaEntry& entry, const std::filesystem::path& out_path) override {
        const ParsedFilename& metadata = entry.metadata;
        // Nothing to search with. The frame provider doesn't care about titles,
        // so declining here costs nothing.
        if (!metadata.title || metadata.title->empty()) return false;

        const QString title = QString::fromStdString(*metadata.title);
        const QString image_path = metadata.season && metadata.episode
                                       ? FindEpisodeStill(title, *metadata.season, *metadata.episode)
                                       : FindMovieBackdrop(title, metadata.year);
        if (image_path.isEmpty()) return false;

        return DownloadImage(image_path, out_path);
    }

private:
    QByteArray Get(const QString& endpoint, QUrlQuery query) {
        query.addQueryItem(QStringLiteral("api_key"), api_key_);

        QUrl url(QString::fromLatin1(kApiBase) + endpoint);
        url.setQuery(query);

        return BlockingRead(manager_.get(QNetworkRequest(url)));
    }

    // The best usable result, or an empty object.
    //
    // Deliberately not just the top hit. TMDB's ranking is fuzzy, and a title
    // the parser mangled can rank something wildly unrelated first: searching
    // the real "Dune partI" yields a 1971 French film about whist as its top
    // result. Taking that on trust would cache confidently wrong artwork, which
    // is worse than none — the frame fallback is always reasonable, so the bar
    // for preferring TMDB is that it actually looks like the same title.
    //
    // `key` is "title" for films and "name" for series; TMDB names the field
    // differently per endpoint.
    static QJsonObject BestResult(const QByteArray& body, const QString& wanted,
                                   const QString& key) {
        const QJsonArray results =
            QJsonDocument::fromJson(body).object().value("results").toArray();

        for (const QJsonValue& value : results) {
            const QJsonObject candidate = value.toObject();
            if (!PlausibleMatch(wanted, candidate.value(key).toString())) continue;
            return candidate;
        }
        return {};
    }

    // Whether `candidate` is plausibly the same title as `wanted`.
    //
    // The test is that the first meaningful word of the parsed title appears in
    // the candidate at all — titles lead with their distinctive word, and the
    // trailing junk is exactly what the parser is least reliable about. So
    // "Dune partII" accepts "Dune: Part Two" on `dune`, while "Dune partI"
    // rejects the whist film because nothing in it is `dune`.
    //
    // A heuristic, and a deliberately loose one: it exists to reject nonsense,
    // not to verify identity. It will pass a wrong film that shares a first
    // word, and will reject a legitimate match under a translated title. Both
    // are cheap failures — the second costs a frame instead of a backdrop.
    static bool PlausibleMatch(const QString& wanted, const QString& candidate) {
        if (candidate.isEmpty()) return false;

        const QStringList wanted_words = SignificantWords(wanted);
        if (wanted_words.isEmpty()) return false;

        return SignificantWords(candidate).contains(wanted_words.first());
    }

    // Lowercased words, punctuation dropped, with articles and one- or
    // two-letter fragments removed: they carry no identifying signal and would
    // make "The Matrix" match on "the".
    static QStringList SignificantWords(const QString& title) {
        static const QStringList kStopWords = {
            QStringLiteral("the"), QStringLiteral("and"), QStringLiteral("for"),
        };

        QStringList words;
        for (const QString& word :
             title.toLower().split(QRegularExpression(QStringLiteral("[^a-z0-9]+")),
                                    Qt::SkipEmptyParts)) {
            if (word.size() < 3 || kStopWords.contains(word)) continue;
            words << word;
        }
        return words;
    }

    QString FindMovieBackdrop(const QString& title, std::optional<int> year) {
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("query"), title);
        // The year narrows a title that's been remade; without it a search for
        // "Dune" is a coin toss between 1984 and 2021.
        if (year) query.addQueryItem(QStringLiteral("year"), QString::number(*year));

        const QJsonObject movie = BestResult(Get(QStringLiteral("/search/movie"), query), title,
                                              QStringLiteral("title"));
        return movie.value("backdrop_path").toString();
    }

    QString FindEpisodeStill(const QString& series, int season, int episode) {
        const int series_id = SeriesId(series);
        if (series_id <= 0) return {};

        const QByteArray body = Get(QStringLiteral("/tv/%1/season/%2/episode/%3")
                                        .arg(series_id).arg(season).arg(episode),
                                    {});
        const QJsonObject json = QJsonDocument::fromJson(body).object();

        const QString still = json.value("still_path").toString();
        if (!still.isEmpty()) return still;

        // No still for this episode — fall back to the series backdrop. Every
        // episode then shares one image, which is a poor shelf but a better one
        // than a row of blanks.
        return SeriesBackdrop(series_id);
    }

    // Series lookups are cached because a season means one search followed by N
    // episode requests, and repeating the search for every episode would be N
    // redundant round trips against a rate-limited API. Negative results are
    // cached too (as 0): a series TMDB has never heard of shouldn't be searched
    // for once per episode.
    int SeriesId(const QString& series) {
        auto it = series_ids_.constFind(series);
        if (it != series_ids_.constEnd()) return it.value();

        QUrlQuery query;
        query.addQueryItem(QStringLiteral("query"), series);

        const QJsonObject show =
            BestResult(Get(QStringLiteral("/search/tv"), query), series, QStringLiteral("name"));
        const int id = show.value("id").toInt();

        series_ids_.insert(series, id);
        if (id > 0) series_backdrops_.insert(id, show.value("backdrop_path").toString());
        return id;
    }

    QString SeriesBackdrop(int series_id) { return series_backdrops_.value(series_id); }

    bool DownloadImage(const QString& image_path, const std::filesystem::path& out_path) {
        const QByteArray body =
            BlockingRead(manager_.get(QNetworkRequest(QUrl(QString::fromLatin1(kImageBase) +
                                                            image_path))));
        if (body.isEmpty()) return false;

        QImage image;
        if (!image.loadFromData(body)) return false;

        // Normalized to kArtworkWidth so a TMDB tile and an extracted-frame tile
        // are byte-for-byte the same shape in the cache. Without this the grid
        // would mix 780px and 640px images and the shelf would render at two
        // different sharpnesses depending on which provider happened to win.
        if (image.width() > kArtworkWidth) {
            image = image.scaledToWidth(kArtworkWidth, Qt::SmoothTransformation);
        }

        return image.save(QString::fromStdString(out_path.string()), "JPEG", 85);
    }

    QString api_key_;
    QNetworkAccessManager manager_;
    QHash<QString, int> series_ids_;
    QHash<int, QString> series_backdrops_;
};

} // namespace

std::filesystem::path DefaultConfigPath() {
    if (const char* xdg_config_home = std::getenv("XDG_CONFIG_HOME");
        xdg_config_home && *xdg_config_home) {
        return std::filesystem::path(xdg_config_home) / "synaxis" / "config.json";
    }
    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / ".config" / "synaxis" / "config.json";
    }
    return std::filesystem::path("config.json");
}

QString ReadTmdbApiKey(const std::filesystem::path& config_path) {
    QFile file(QString::fromStdString(config_path.string()));
    if (!file.open(QIODevice::ReadOnly)) return {};

    // A malformed config is treated as no config rather than as an error: the
    // consequence is falling back to frame extraction, which is a working
    // program, and refusing to start over a stray comma would not be.
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) return {};

    return document.object().value("tmdb_api_key").toString();
}

std::unique_ptr<ArtworkProvider> MakeTmdbProvider(const QString& api_key) {
    if (api_key.isEmpty()) return nullptr;
    return std::make_unique<TmdbProvider>(api_key);
}

} // namespace synaxis::gui
