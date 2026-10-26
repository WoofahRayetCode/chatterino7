// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "controllers/social/TwitterClient.hpp"

#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"

#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <QDateTime>
#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

namespace chatterino {

QString OAuth1Signer::percentEncode(const QString &val)
{
    return QString::fromUtf8(QUrl::toPercentEncoding(val, "~"));
}

QString OAuth1Signer::percentEncode(const QByteArray &val)
{
    return QString::fromUtf8(QUrl::toPercentEncoding(QString::fromUtf8(val), "~"));
}

QString OAuth1Signer::buildAuthorizationHeader(
    const QString &httpMethod, const QString &url, const QString &consumerKey,
    const QString &consumerSecret, const QString &accessToken,
    const QString &tokenSecret, const QString &customNonce,
    qint64 customTimestampSec)
{
    QString nonce = customNonce;
    if (nonce.isEmpty())
    {
        // 32-character random hex string
        quint64 r1 = QRandomGenerator::system()->generate64();
        quint64 r2 = QRandomGenerator::system()->generate64();
        nonce = QStringLiteral("%1%2")
                    .arg(r1, 16, 16, QLatin1Char('0'))
                    .arg(r2, 16, 16, QLatin1Char('0'));
    }

    qint64 timestamp = customTimestampSec > 0
                           ? customTimestampSec
                           : QDateTime::currentSecsSinceEpoch();

    // 1. Collect and sort OAuth parameters
    std::map<QString, QString> params;
    params["oauth_consumer_key"] = percentEncode(consumerKey);
    params["oauth_nonce"] = percentEncode(nonce);
    params["oauth_signature_method"] = "HMAC-SHA1";
    params["oauth_timestamp"] = QString::number(timestamp);
    params["oauth_token"] = percentEncode(accessToken);
    params["oauth_version"] = "1.0";

    // Parse base URL (without query parameters) and add any existing query params to params map
    QUrl parsedUrl(url);
    if (parsedUrl.hasQuery())
    {
        QUrlQuery query(parsedUrl);
        for (const auto &item : query.queryItems())
        {
            params[percentEncode(item.first)] = percentEncode(item.second);
        }
    }

    // 2. Build normalized parameter string
    QString paramString;
    bool first = true;
    for (const auto &[k, v] : params)
    {
        if (!first)
        {
            paramString += '&';
        }
        paramString += k + '=' + v;
        first = false;
    }

    // Normalized base URL (scheme://host:port/path in lowercase, standard ports omitted)
    QString baseUrl =
        parsedUrl.scheme().toLower() + "://" + parsedUrl.host().toLower();
    if (parsedUrl.port() != -1 &&
        !((parsedUrl.scheme() == "https" && parsedUrl.port() == 443) ||
          (parsedUrl.scheme() == "http" && parsedUrl.port() == 80)))
    {
        baseUrl += ':' + QString::number(parsedUrl.port());
    }
    baseUrl += parsedUrl.path();

    // 3. Build signature base string
    QString baseString = httpMethod.toUpper() + '&' + percentEncode(baseUrl) +
                         '&' + percentEncode(paramString);

    // 4. Build signing key
    QString signingKey =
        percentEncode(consumerSecret) + '&' + percentEncode(tokenSecret);

    // 5. Compute HMAC-SHA1
    QByteArray keyBytes = signingKey.toUtf8();
    QByteArray dataBytes = baseString.toUtf8();

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digestLen = 0;

    HMAC(EVP_sha1(), keyBytes.constData(), static_cast<int>(keyBytes.size()),
         reinterpret_cast<const unsigned char *>(dataBytes.constData()),
         dataBytes.size(), digest, &digestLen);

    QByteArray signature =
        QByteArray(reinterpret_cast<char *>(digest), digestLen).toBase64();

    // 6. Build Authorization header
    QString authHeader = "OAuth ";
    authHeader += QStringLiteral("oauth_consumer_key=\"%1\", ")
                      .arg(params["oauth_consumer_key"]);
    authHeader +=
        QStringLiteral("oauth_nonce=\"%1\", ").arg(params["oauth_nonce"]);
    authHeader += QStringLiteral("oauth_signature=\"%1\", ")
                      .arg(percentEncode(signature));
    authHeader += QStringLiteral("oauth_signature_method=\"HMAC-SHA1\", ");
    authHeader += QStringLiteral("oauth_timestamp=\"%1\", ")
                      .arg(params["oauth_timestamp"]);
    authHeader +=
        QStringLiteral("oauth_token=\"%1\", ").arg(params["oauth_token"]);
    authHeader += QStringLiteral("oauth_version=\"1.0\"");

    return authHeader;
}

void TwitterClient::openWebIntent(const QString &text)
{
    QUrl intentUrl("https://x.com/intent/post");
    QUrlQuery query;
    query.addQueryItem("text", text);
    intentUrl.setQuery(query);

    QDesktopServices::openUrl(intentUrl);
}

void TwitterClient::postApiV2(const QString &apiKey, const QString &apiSecret,
                              const QString &accessToken,
                              const QString &accessSecret, const QString &text,
                              PostCallback cb)
{
    const QString endpoint = "https://api.x.com/2/tweets";

    QString authHeader = OAuth1Signer::buildAuthorizationHeader(
        "POST", endpoint, apiKey.trimmed(), apiSecret.trimmed(),
        accessToken.trimmed(), accessSecret.trimmed());

    QJsonObject body;
    body["text"] = text;

    NetworkRequest(endpoint, NetworkRequestType::Post)
        .header("Authorization", authHeader.toUtf8())
        .json(body)
        .onSuccess([cb](auto result) {
            auto root = result.parseJson();
            auto data = root["data"].toObject();
            QString id = data["id"].toString();
            cb(true, id.isEmpty() ? "Tweet posted successfully"
                                  : "Tweet ID: " + id);
        })
        .onError([cb](auto result) {
            auto root = result.parseJson();
            QString title = root["title"].toString();
            QString detail = root["detail"].toString();
            QString errMsg = detail.isEmpty() ? title : detail;
            if (errMsg.isEmpty())
            {
                errMsg = result.formatError();
            }
            cb(false, "Failed to post to X: " + errMsg);
        })
        .execute();
}

void TwitterClient::checkRecentLivePost(const QString &handle,
                                        const QString &streamUrl,
                                        int maxAgeMinutes,
                                        CheckPostCallback cb)
{
    QString cleanHandle = handle.trimmed();
    if (cleanHandle.startsWith('@'))
    {
        cleanHandle.remove(0, 1);
    }

    if (cleanHandle.isEmpty())
    {
        cb({});
        return;
    }

    QString jinaUrl = QStringLiteral("https://r.jina.ai/https://x.com/%1")
                          .arg(QString::fromUtf8(
                              QUrl::toPercentEncoding(cleanHandle)));

    NetworkRequest(jinaUrl, NetworkRequestType::Get)
        .onSuccess([cleanHandle, streamUrl, maxAgeMinutes, cb](auto result) {
            QString markdown = QString::fromUtf8(result.getData());
            static const QRegularExpression statusRegex(
                QStringLiteral("https://x\\.com/[^/\\s\\)]+/status/([0-9]{15,22})"));

            QString latestStatusId;
            auto it = statusRegex.globalMatch(markdown);
            if (it.hasNext())
            {
                latestStatusId = it.next().captured(1);
            }

            if (latestStatusId.isEmpty())
            {
                cb({});
                return;
            }

            QString fxUrl =
                QStringLiteral("https://api.fxtwitter.com/%1/status/%2")
                    .arg(QString::fromUtf8(
                             QUrl::toPercentEncoding(cleanHandle)),
                         latestStatusId);

            NetworkRequest(fxUrl, NetworkRequestType::Get)
                .onSuccess([streamUrl, maxAgeMinutes, cb](auto fxResult) {
                    auto root = fxResult.parseJson();
                    auto tweetObj = root["tweet"].toObject();
                    if (tweetObj.isEmpty())
                    {
                        cb({});
                        return;
                    }

                    QString text = tweetObj["text"].toString();
                    QString createdAtStr = tweetObj["created_at"].toString();
                    qint64 createdTs =
                        tweetObj["created_timestamp"].toVariant().toLongLong();
                    QString cardUrl =
                        tweetObj["card"].toObject()["url"].toString();

                    const qint64 nowSec = QDateTime::currentSecsSinceEpoch();
                    if (createdTs <= 0)
                    {
                        cb({});
                        return;
                    }

                    int ageMin =
                        static_cast<int>(std::max<qint64>(0, (nowSec - createdTs) / 60));
                    if (ageMin > maxAgeMinutes)
                    {
                        cb({});
                        return;
                    }

                    bool matchesUrl =
                        !streamUrl.isEmpty() &&
                        (text.contains(streamUrl, Qt::CaseInsensitive) ||
                         cardUrl.contains(streamUrl, Qt::CaseInsensitive));
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

                    cb({});
                })
                .onError([cb](auto /*err*/) {
                    cb({});
                })
                .execute();
        })
        .onError([cb](auto /*err*/) {
            cb({});
        })
        .execute();
}

}  // namespace chatterino
