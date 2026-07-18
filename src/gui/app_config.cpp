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

#include "app_config.hpp"

#include <QDebug>
#include <QFile>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace synaxis::gui {

AppConfig LoadAppConfig(const std::filesystem::path& path) {
    AppConfig config;

    QFile file(QString::fromStdString(path.string()));
    if (!file.open(QIODevice::ReadOnly)) return config;

    // A malformed config is treated as no config, same as ReadTmdbApiKey:
    // the consequence is an empty settings page, not a refusal to start.
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) return config;

    const QJsonObject object = document.object();
    config.tmdb_api_key = object.value("tmdb_api_key").toString();
    for (const QJsonValue& value : object.value("library_directories").toArray()) {
        config.library_directories.append(value.toString());
    }
    for (const QJsonValue& value : object.value("scan_extensions").toArray()) {
        config.scan_extensions.append(value.toString());
    }
    return config;
}

void SaveAppConfig(const AppConfig& config, const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    QJsonArray directories;
    for (const QString& directory : config.library_directories) directories.append(directory);

    QJsonArray extensions;
    for (const QString& extension : config.scan_extensions) extensions.append(extension);

    QJsonObject object;
    object.insert("tmdb_api_key", config.tmdb_api_key);
    object.insert("library_directories", directories);
    object.insert("scan_extensions", extensions);

    QFile file(QString::fromStdString(path.string()));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "failed to write config:" << file.errorString();
        return;
    }
    file.write(QJsonDocument(object).toJson());
}

} // namespace synaxis::gui
