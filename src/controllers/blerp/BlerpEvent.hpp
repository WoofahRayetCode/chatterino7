// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QString>
#include <QUrl>
#include <QUrlQuery>

#include <optional>

namespace chatterino {

struct BlerpEvent {
    QDateTime timestamp = QDateTime::currentDateTime();
    QString platform = "Twitch";
    QString channelName;
    QString username;
    QString soundTitle;
    QString triggerSource = "Blerp";
    QString blerpUrl;

    QString summary() const
    {
        QString user = this->username.isEmpty() ? "Someone" : this->username;
        QString title =
            this->soundTitle.isEmpty() ? "a sound alert" : ("\"" + this->soundTitle + "\"");
        QString src = this->triggerSource.isEmpty() ? "" : (" (" + this->triggerSource + ")");
        return QString("%1 played %2%3").arg(user, title, src);
    }
};

class BlerpPayloadParser
{
public:
    static QString extractRoomToken(const QString &input)
    {
        QString trimmed = input.trimmed();
        if (trimmed.isEmpty())
        {
            return QString();
        }

        // Check if full URL
        if (trimmed.startsWith("http://", Qt::CaseInsensitive) ||
            trimmed.startsWith("https://", Qt::CaseInsensitive) ||
            trimmed.startsWith("ws://", Qt::CaseInsensitive) ||
            trimmed.startsWith("wss://", Qt::CaseInsensitive))
        {
            QUrl url(trimmed);
            if (url.hasQuery())
            {
                QUrlQuery query(url);
                if (query.hasQueryItem("roomId"))
                {
                    return query.queryItemValue("roomId").trimmed();
                }
            }

            QString path = url.path();
            // Match /pwn/<token>
            static QRegularExpression pwnRegex(R"(/pwn/([a-zA-Z0-9_\-]+))");
            auto match = pwnRegex.match(path);
            if (match.hasMatch())
            {
                return match.captured(1).trimmed();
            }

            // Fallback: last path component
            QString last = path.section('/', -1, -1, QString::SectionSkipEmpty);
            if (!last.isEmpty() && last.length() >= 4 && last.length() <= 64)
            {
                return last;
            }
        }

        // Check regex for standalone alphanumeric room token (e.g. 8-32 chars)
        static QRegularExpression tokenRegex(R"(^[a-zA-Z0-9_\-]{4,64}$)");
        if (tokenRegex.match(trimmed).hasMatch())
        {
            return trimmed;
        }

        return trimmed;
    }

    static std::optional<BlerpEvent> parseEngineIoPacket(
        const QByteArray &frame, const QString &defaultChannel = QString())
    {
        if (frame.size() < 2)
        {
            return std::nullopt;
        }

        // Must start with '4' (MESSAGE)
        if (frame[0] != '4')
        {
            return std::nullopt;
        }

        int index = 1;
        // Skip packet type (e.g. '2' for EVENT)
        if (index < frame.size() && frame[index] == '2')
        {
            index++;
        }
        else
        {
            return std::nullopt;
        }

        // Skip any numeric acknowledgment ID (e.g. "4215[...]")
        while (index < frame.size() && frame[index] >= '0' &&
               frame[index] <= '9')
        {
            index++;
        }

        if (index >= frame.size() || frame[index] != '[')
        {
            return std::nullopt;
        }

        QJsonParseError err;
        QJsonDocument doc =
            QJsonDocument::fromJson(frame.mid(index), &err);
        if (err.error != QJsonParseError::NoError || !doc.isArray())
        {
            return std::nullopt;
        }

        QJsonArray arr = doc.array();
        if (arr.isEmpty())
        {
            return std::nullopt;
        }

        QString eventName = arr[0].toString();
        // Ignore internal keepalives and pings
        if (eventName.contains("ping", Qt::CaseInsensitive) ||
            eventName.contains("pong", Qt::CaseInsensitive) ||
            eventName.contains("heart_beat", Qt::CaseInsensitive) ||
            eventName.contains("connect", Qt::CaseInsensitive))
        {
            return std::nullopt;
        }

        if (arr.size() < 2 || !arr[1].isObject())
        {
            return std::nullopt;
        }

        QJsonObject obj = arr[1].toObject();

        BlerpEvent ev;
        ev.platform = "Blerp";
        ev.channelName = defaultChannel;

        // Search for title
        for (const auto &key :
             {"title", "soundTitle", "biteTitle", "name", "soundName", "biteName"})
        {
            if (obj.contains(key) && obj[key].isString())
            {
                ev.soundTitle = obj[key].toString();
                break;
            }
        }

        // If nested inside "data" or "bite" or "alert"
        if (ev.soundTitle.isEmpty())
        {
            for (const auto &subKey : {"data", "bite", "alert", "sound"})
            {
                if (obj.contains(subKey) && obj[subKey].isObject())
                {
                    QJsonObject sub = obj[subKey].toObject();
                    for (const auto &k :
                         {"title", "soundTitle", "name", "biteTitle"})
                    {
                        if (sub.contains(k) && sub[k].isString())
                        {
                            ev.soundTitle = sub[k].toString();
                            break;
                        }
                    }
                    if (!ev.soundTitle.isEmpty())
                    {
                        break;
                    }
                }
            }
        }

        // Search for user
        for (const auto &key :
             {"username", "user", "viewerUsername", "requester", "sender",
              "author", "displayName"})
        {
            if (obj.contains(key) && obj[key].isString())
            {
                ev.username = obj[key].toString();
                break;
            }
        }

        // Search for source / type
        for (const auto &key : {"type", "source", "triggerType", "redeemType"})
        {
            if (obj.contains(key) && obj[key].isString())
            {
                ev.triggerSource = obj[key].toString();
                break;
            }
        }

        if (obj.contains("bits") && obj["bits"].toInt() > 0)
        {
            ev.triggerSource = QString::number(obj["bits"].toInt()) + " Bits";
        }
        else if (obj.contains("beets") && obj["beets"].toInt() > 0)
        {
            ev.triggerSource = QString::number(obj["beets"].toInt()) + " Beets";
        }

        // Search for URL
        for (const auto &key : {"url", "blerpUrl", "shareUrl", "link"})
        {
            if (obj.contains(key) && obj[key].isString())
            {
                ev.blerpUrl = obj[key].toString();
                break;
            }
        }

        if (ev.soundTitle.isEmpty() && ev.username.isEmpty())
        {
            return std::nullopt;
        }

        if (ev.soundTitle.isEmpty())
        {
            ev.soundTitle = eventName;
        }

        return ev;
    }

    static std::optional<BlerpEvent> parseBotChatMessage(
        const QString &senderLogin, const QString &messageText,
        const QString &channelName, const QString &botListCsv)
    {
        QString sender = senderLogin.toLower().trimmed();
        bool isBot = false;

        for (const auto &item : botListCsv.split(',', Qt::SkipEmptyParts))
        {
            if (sender == item.trimmed().toLower())
            {
                isBot = true;
                break;
            }
        }

        // Also check if text contains direct blerp.com link
        bool hasBlerpLink = messageText.contains("blerp.com/", Qt::CaseInsensitive);

        if (!isBot && !hasBlerpLink)
        {
            return std::nullopt;
        }

        BlerpEvent ev;
        ev.channelName = channelName;
        ev.platform = "Twitch";
        ev.triggerSource = "Chat Alert";

        // Extract blerp URL if present
        static QRegularExpression urlRegex(R"(https?://(?:www\.)?blerp\.com/[^\s]+)");
        auto urlMatch = urlRegex.match(messageText);
        if (urlMatch.hasMatch())
        {
            ev.blerpUrl = urlMatch.captured(0);
        }

        // Pattern 1: Bits cheer: "@Viewer cheered 100 Bits and played "Sound Title"..."
        static QRegularExpression bitsRegex(
            R"(@?([a-zA-Z0-9_]+)\s+cheered\s+(\d+\s+Bits)\s+and\s+played\s+["“](.+?)["”])",
            QRegularExpression::CaseInsensitiveOption);
        auto m = bitsRegex.match(messageText);
        if (m.hasMatch())
        {
            ev.username = m.captured(1);
            ev.triggerSource = m.captured(2);
            ev.soundTitle = m.captured(3);
            return ev;
        }

        // Pattern 2: Channel points: "@Viewer redeemed channel point reward: "Sound Title"..."
        static QRegularExpression cpRegex(
            R"(@?([a-zA-Z0-9_]+)\s+redeemed\s+(?:channel\s+point\s+reward:?\s*)?["“](.+?)["”])",
            QRegularExpression::CaseInsensitiveOption);
        m = cpRegex.match(messageText);
        if (m.hasMatch())
        {
            ev.username = m.captured(1);
            ev.triggerSource = "Channel Points";
            ev.soundTitle = m.captured(2);
            return ev;
        }

        // Pattern 3: Walk-On: "@Viewer joined the stream with their WalkOn sound! ..."
        static QRegularExpression walkOnRegex(
            R"(@?([a-zA-Z0-9_]+)\s+joined\s+the\s+stream\s+with\s+their\s+Walk-?On\s+sound!?\s*["“]?([^"”\n\r]*)["”]?)",
            QRegularExpression::CaseInsensitiveOption);
        m = walkOnRegex.match(messageText);
        if (m.hasMatch())
        {
            ev.username = m.captured(1);
            ev.triggerSource = "Walk-On";
            ev.soundTitle = m.captured(2).trimmed();
            if (ev.soundTitle.isEmpty())
            {
                ev.soundTitle = "Walk-On Entrance";
            }
            return ev;
        }

        // Pattern 4: Generic played: "@Viewer played "Sound Title"..."
        static QRegularExpression playedRegex(
            R"((?:^|:\s*)@?([a-zA-Z0-9_]+)\s+played\s+["“](.+?)["”])",
            QRegularExpression::CaseInsensitiveOption);
        m = playedRegex.match(messageText);
        if (m.hasMatch())
        {
            ev.username = m.captured(1);
            ev.soundTitle = m.captured(2);
            return ev;
        }

        if (hasBlerpLink)
        {
            ev.soundTitle = "Shared Blerp";
            if (!sender.isEmpty() && sender != "blerp")
            {
                ev.username = senderLogin;
            }
            return ev;
        }

        return std::nullopt;
    }

    static bool matchesRewardKeywords(const QString &rewardTitle,
                                     const QString &keywordsCsv)
    {
        QString titleLower = rewardTitle.toLower();
        for (const auto &kw : keywordsCsv.split(',', Qt::SkipEmptyParts))
        {
            QString trimmed = kw.trimmed().toLower();
            if (!trimmed.isEmpty() && titleLower.contains(trimmed))
            {
                return true;
            }
        }
        return false;
    }
};

}  // namespace chatterino
