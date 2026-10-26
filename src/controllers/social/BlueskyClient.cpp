// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "controllers/social/BlueskyClient.hpp"

#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"

#include <QDateTime>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUrl>

using namespace Qt::StringLiterals;

namespace chatterino {

QJsonArray BlueskyClient::extractFacets(const QString &text)
{
    QJsonArray facets;
    if (text.isEmpty())
    {
        return facets;
    }

    const QByteArray utf8 = text.toUtf8();
    const QString utf8Str = QString::fromUtf8(utf8);

    // 1. Extract URLs
    static const QRegularExpression urlRegex(u"https?://[^\\s]+"_s);
    auto urlMatches = urlRegex.globalMatch(utf8Str);
    while (urlMatches.hasNext())
    {
        auto match = urlMatches.next();
        QString rawUrl = match.captured();

        // Strip trailing punctuation often typed at the end of sentences
        while (!rawUrl.isEmpty() &&
               (rawUrl.endsWith('.') || rawUrl.endsWith(',') ||
                rawUrl.endsWith(';') || rawUrl.endsWith('!') ||
                rawUrl.endsWith('?') || rawUrl.endsWith(')')))
        {
            rawUrl.chop(1);
        }

        if (rawUrl.isEmpty())
        {
            continue;
        }

        // Calculate byte positions in utf8
        QString prefix = utf8Str.left(match.capturedStart());
        int byteStart = prefix.toUtf8().size();
        int byteEnd = byteStart + rawUrl.toUtf8().size();

        QJsonObject facet;
        QJsonObject indexObj;
        indexObj["byteStart"] = byteStart;
        indexObj["byteEnd"] = byteEnd;
        facet["index"] = indexObj;

        QJsonObject feature;
        feature["$type"] = "app.bsky.richtext.facet#link";
        feature["uri"] = rawUrl;

        QJsonArray features;
        features.append(feature);
        facet["features"] = features;

        facets.append(facet);
    }

    // 2. Extract Hashtags
    static const QRegularExpression tagRegex(u"(?:^|\\s)#([A-Za-z0-9_]+)"_s);
    auto tagMatches = tagRegex.globalMatch(utf8Str);
    while (tagMatches.hasNext())
    {
        auto match = tagMatches.next();
        QString tag = match.captured(1);
        if (tag.isEmpty())
        {
            continue;
        }

        // Start index at the '#' character
        int hashCharPos = match.capturedStart() +
                          (match.captured().startsWith('#') ? 0 : 1);
        QString prefix = utf8Str.left(hashCharPos);
        int byteStart = prefix.toUtf8().size();
        int byteEnd = byteStart + (1 + tag.toUtf8().size());

        QJsonObject facet;
        QJsonObject indexObj;
        indexObj["byteStart"] = byteStart;
        indexObj["byteEnd"] = byteEnd;
        facet["index"] = indexObj;

        QJsonObject feature;
        feature["$type"] = "app.bsky.richtext.facet#tag";
        feature["tag"] = tag;

        QJsonArray features;
        features.append(feature);
        facet["features"] = features;

        facets.append(facet);
    }

    return facets;
}

void BlueskyClient::clearSession()
{
    this->cachedSession_ = std::nullopt;
    this->cachedHandle_.clear();
    this->cachedPassword_.clear();
}

void BlueskyClient::getOrCreateSession(
    const QString &pdsUrl, const QString &handle, const QString &appPassword,
    std::function<void(bool success, const Session &session,
                       const QString &error)>
        cb)
{
    if (this->cachedSession_ && this->cachedHandle_ == handle &&
        this->cachedPassword_ == appPassword)
    {
        cb(true, *this->cachedSession_, "");
        return;
    }

    QString baseUrl = pdsUrl.trimmed();
    if (baseUrl.endsWith('/'))
    {
        baseUrl.chop(1);
    }
    if (!baseUrl.startsWith("http://") && !baseUrl.startsWith("https://"))
    {
        baseUrl = "https://" + baseUrl;
    }

    QString endpoint = baseUrl + "/xrpc/com.atproto.server.createSession";

    QJsonObject reqJson;
    reqJson["identifier"] = handle.trimmed();
    reqJson["password"] = appPassword.trimmed();

    NetworkRequest(endpoint, NetworkRequestType::Post)
        .json(reqJson)
        .onSuccess([this, handle, appPassword, baseUrl, cb](auto result) {
            auto root = result.parseJson();
            QString did = root["did"].toString();
            QString accessJwt = root["accessJwt"].toString();
            QString refreshJwt = root["refreshJwt"].toString();
            QString pdsEndpoint = root["pdsEndpoint"].toString();
            if (pdsEndpoint.isEmpty())
            {
                pdsEndpoint = baseUrl;
            }

            if (did.isEmpty() || accessJwt.isEmpty())
            {
                cb(false, {}, "Invalid response from Bluesky auth server.");
                return;
            }

            Session s{
                .did = did,
                .accessJwt = accessJwt,
                .refreshJwt = refreshJwt,
                .pdsEndpoint = pdsEndpoint,
            };

            this->cachedSession_ = s;
            this->cachedHandle_ = handle;
            this->cachedPassword_ = appPassword;

            cb(true, s, "");
        })
        .onError([cb](auto result) {
            auto root = result.parseJson();
            QString errMsg = root["message"].toString();
            if (errMsg.isEmpty())
            {
                errMsg = result.formatError();
            }
            cb(false, {}, "Bluesky login failed: " + errMsg);
        })
        .execute();
}

void BlueskyClient::verifyCredentials(const QString &pdsUrl,
                                      const QString &handle,
                                      const QString &appPassword,
                                      VerifyCallback cb)
{
    this->clearSession();
    this->getOrCreateSession(
        pdsUrl, handle, appPassword,
        [cb](bool success, const Session &session, const QString &error) {
            if (success)
            {
                cb(true, "Successfully authenticated as " + session.did);
            }
            else
            {
                cb(false, error);
            }
        });
}

void BlueskyClient::post(const QString &pdsUrl, const QString &handle,
                         const QString &appPassword, const QString &text,
                         const std::optional<EmbedCard> &embedCard,
                         PostCallback cb)
{
    this->getOrCreateSession(
        pdsUrl, handle, appPassword,
        [this, pdsUrl, handle, appPassword, text, embedCard, cb](
            bool success, const Session &session, const QString &error) {
            if (!success)
            {
                cb(false, error);
                return;
            }

            QString endpoint =
                session.pdsEndpoint + "/xrpc/com.atproto.repo.createRecord";

            QJsonObject recordObj;
            recordObj["$type"] = "app.bsky.feed.post";
            recordObj["text"] = text;
            recordObj["createdAt"] =
                QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);

            auto facets = extractFacets(text);
            if (!facets.isEmpty())
            {
                recordObj["facets"] = facets;
            }

            if (embedCard && !embedCard->uri.isEmpty())
            {
                QJsonObject embedObj;
                embedObj["$type"] = "app.bsky.embed.external";
                QJsonObject extObj;
                extObj["uri"] = embedCard->uri;
                extObj["title"] = embedCard->title;
                extObj["description"] = embedCard->description;
                embedObj["external"] = extObj;
                recordObj["embed"] = embedObj;
            }

            QJsonObject body;
            body["repo"] = session.did;
            body["collection"] = "app.bsky.feed.post";
            body["record"] = recordObj;

            NetworkRequest(endpoint, NetworkRequestType::Post)
                .header("Authorization",
                        "Bearer " + session.accessJwt.toUtf8())
                .json(body)
                .onSuccess([cb](auto result) {
                    auto root = result.parseJson();
                    QString uri = root["uri"].toString();
                    cb(true, uri.isEmpty() ? "Post created successfully" : uri);
                })
                .onError([this, pdsUrl, handle, appPassword, text, embedCard, cb,
                          session](auto result) {
                    if (result.status() == 401)
                    {
                        // Session expired, clear and retry once
                        this->clearSession();
                        this->getOrCreateSession(
                            pdsUrl, handle, appPassword,
                            [this, text, embedCard,
                             cb](bool ok, const Session &newSession,
                                 const QString &err) {
                                if (!ok)
                                {
                                    cb(false, err);
                                    return;
                                }
                                this->post(newSession.pdsEndpoint,
                                           this->cachedHandle_,
                                           this->cachedPassword_, text,
                                           embedCard, cb);
                            });
                        return;
                    }

                    auto root = result.parseJson();
                    QString errMsg = root["message"].toString();
                    if (errMsg.isEmpty())
                    {
                        errMsg = result.formatError();
                    }
                    cb(false, "Failed to post to Bluesky: " + errMsg);
                })
                .execute();
        });
}

void BlueskyClient::checkRecentLivePost(const QString &handle,
                                        const QString &streamUrl,
                                        int maxAgeMinutes,
                                        CheckPostCallback cb)
{
    QString actor = handle.trimmed();
    if (actor.isEmpty())
    {
        cb({});
        return;
    }

    QString endpoint =
        QStringLiteral(
            "https://public.api.bsky.app/xrpc/app.bsky.feed.getAuthorFeed?actor=%1&limit=10")
            .arg(QString::fromUtf8(QUrl::toPercentEncoding(actor)));

    NetworkRequest(endpoint, NetworkRequestType::Get)
        .onSuccess([streamUrl, maxAgeMinutes, cb](auto result) {
            auto root = result.parseJson();
            auto feed = root["feed"].toArray();
            const QDateTime nowUtc = QDateTime::currentDateTimeUtc();

            for (const auto &itemVal : feed)
            {
                auto postObj = itemVal.toObject()["post"].toObject();
                auto recordObj = postObj["record"].toObject();
                QString text = recordObj["text"].toString();
                QString createdAtStr = recordObj["createdAt"].toString();
                QString embedUri = recordObj["embed"]
                                       .toObject()["external"]
                                       .toObject()["uri"]
                                       .toString();

                QDateTime createdAt =
                    QDateTime::fromString(createdAtStr, Qt::ISODateWithMs);
                if (!createdAt.isValid())
                {
                    createdAt =
                        QDateTime::fromString(createdAtStr, Qt::ISODate);
                }

                if (!createdAt.isValid())
                {
                    continue;
                }

                qint64 ageSec = createdAt.secsTo(nowUtc);
                int ageMin = static_cast<int>(ageSec / 60);
                if (ageMin < 0 || ageMin > maxAgeMinutes)
                {
                    continue;
                }

                bool matchesUrl =
                    !streamUrl.isEmpty() &&
                    (text.contains(streamUrl, Qt::CaseInsensitive) ||
                     embedUri.contains(streamUrl, Qt::CaseInsensitive));
                bool matchesLivePattern =
                    text.contains("LIVE on", Qt::CaseInsensitive);

                if (matchesUrl || matchesLivePattern)
                {
                    RecentPostCheck found;
                    found.alreadyPosted = true;
                    found.postText = text.simplified();
                    found.createdAt = createdAtStr;
                    found.ageMinutes = ageMin;
                    cb(found);
                    return;
                }
            }

            cb({});
        })
        .onError([cb](auto /*result*/) {
            cb({});
        })
        .execute();
}

}  // namespace chatterino
