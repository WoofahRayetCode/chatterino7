// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common/Outcome.hpp"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <functional>
#include <optional>

namespace chatterino {

class BlueskyClient
{
public:
    struct EmbedCard {
        QString uri;
        QString title;
        QString description;
    };

    struct Session {
        QString did;
        QString accessJwt;
        QString refreshJwt;
        QString pdsEndpoint;
    };

    struct RecentPostCheck {
        bool alreadyPosted = false;
        QString postText;
        QString createdAt;
        int ageMinutes = 0;
    };

    using PostCallback = std::function<void(bool success, const QString &message)>;
    using VerifyCallback = std::function<void(bool success, const QString &message)>;
    using CheckPostCallback = std::function<void(const RecentPostCheck &result)>;

    BlueskyClient() = default;

    static QJsonArray extractFacets(const QString &text);

    void post(const QString &pdsUrl, const QString &handle,
              const QString &appPassword, const QString &text,
              const std::optional<EmbedCard> &embedCard, PostCallback cb);

    void verifyCredentials(const QString &pdsUrl, const QString &handle,
                           const QString &appPassword, VerifyCallback cb);

    void checkRecentLivePost(const QString &handle, const QString &streamUrl,
                             int maxAgeMinutes, CheckPostCallback cb);

    void clearSession();

private:
    void getOrCreateSession(
        const QString &pdsUrl, const QString &handle,
        const QString &appPassword,
        std::function<void(bool success, const Session &session,
                           const QString &error)> cb);

    std::optional<Session> cachedSession_;
    QString cachedHandle_;
    QString cachedPassword_;
};

}  // namespace chatterino
