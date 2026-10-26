// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QByteArray>
#include <QString>

#include <functional>
#include <map>

namespace chatterino {

class OAuth1Signer
{
public:
    static QString percentEncode(const QString &val);
    static QString percentEncode(const QByteArray &val);

    static QString buildAuthorizationHeader(
        const QString &httpMethod, const QString &url,
        const QString &consumerKey, const QString &consumerSecret,
        const QString &accessToken, const QString &tokenSecret,
        const QString &nonce = "", qint64 timestampSec = 0);
};

class TwitterClient
{
public:
    struct RecentPostCheck {
        bool alreadyPosted = false;
        QString postText;
        QString createdAt;
        int ageMinutes = 0;
    };

    using PostCallback = std::function<void(bool success, const QString &message)>;
    using CheckPostCallback = std::function<void(const RecentPostCheck &result)>;

    static void openWebIntent(const QString &text);

    static void postApiV2(const QString &apiKey, const QString &apiSecret,
                          const QString &accessToken,
                          const QString &accessSecret, const QString &text,
                          PostCallback cb);

    static void checkRecentLivePost(const QString &handle,
                                    const QString &streamUrl,
                                    int maxAgeMinutes,
                                    CheckPostCallback cb);
};

}  // namespace chatterino
