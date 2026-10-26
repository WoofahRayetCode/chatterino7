// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "util/RapidJsonSerializeQString.hpp"
#include "util/RapidjsonHelpers.hpp"

#include <pajlada/serialize.hpp>
#include <QString>

namespace chatterino {

struct ViewerSoundRule {
    QString username;
    QString channelFilter;  // empty means any channel
    bool enabled = true;
    bool playOnJoin = true;
    QString joinSound = "004-yfjesse-notification";
    bool playOnChat = false;
    QString chatSound = "004-yfjesse-notification";
    int volume = 80;         // 0-100
    int cooldownSecs = 5;    // per-viewer chat sound cooldown

    bool matchesUser(const QString &targetUser) const
    {
        return QString::compare(this->username.trimmed(), targetUser.trimmed(),
                                Qt::CaseInsensitive) == 0;
    }

    bool matchesChannel(const QString &platformPrefix,
                        const QString &channelName) const
    {
        if (this->channelFilter.trimmed().isEmpty())
        {
            return true;
        }

        QString filter = this->channelFilter.trimmed();
        // Check exact match (e.g. "forsen" or "twitch:forsen" or "kick:forsen")
        if (QString::compare(filter, channelName, Qt::CaseInsensitive) == 0)
        {
            return true;
        }

        if (!platformPrefix.isEmpty())
        {
            QString combined = platformPrefix + ":" + channelName;
            if (QString::compare(filter, combined, Qt::CaseInsensitive) == 0)
            {
                return true;
            }
        }

        return false;
    }

    bool operator==(const ViewerSoundRule &other) const = default;
};

}  // namespace chatterino

namespace pajlada {

template <>
struct Serialize<chatterino::ViewerSoundRule> {
    static rapidjson::Value get(const chatterino::ViewerSoundRule &value,
                                rapidjson::Document::AllocatorType &a)
    {
        rapidjson::Value ret(rapidjson::kObjectType);

        chatterino::rj::set(ret, "username", value.username, a);
        chatterino::rj::set(ret, "channelFilter", value.channelFilter, a);
        chatterino::rj::set(ret, "enabled", value.enabled, a);
        chatterino::rj::set(ret, "playOnJoin", value.playOnJoin, a);
        chatterino::rj::set(ret, "joinSound", value.joinSound, a);
        chatterino::rj::set(ret, "playOnChat", value.playOnChat, a);
        chatterino::rj::set(ret, "chatSound", value.chatSound, a);
        chatterino::rj::set(ret, "volume", value.volume, a);
        chatterino::rj::set(ret, "cooldownSecs", value.cooldownSecs, a);

        return ret;
    }
};

template <>
struct Deserialize<chatterino::ViewerSoundRule> {
    static chatterino::ViewerSoundRule get(const rapidjson::Value &value,
                                           bool *error = nullptr)
    {
        if (!value.IsObject())
        {
            PAJLADA_REPORT_ERROR(error)
            return chatterino::ViewerSoundRule{};
        }

        chatterino::ViewerSoundRule rule;
        chatterino::rj::getSafe(value, "username", rule.username);
        chatterino::rj::getSafe(value, "channelFilter", rule.channelFilter);
        chatterino::rj::getSafe(value, "enabled", rule.enabled);
        chatterino::rj::getSafe(value, "playOnJoin", rule.playOnJoin);
        chatterino::rj::getSafe(value, "joinSound", rule.joinSound);
        chatterino::rj::getSafe(value, "playOnChat", rule.playOnChat);
        chatterino::rj::getSafe(value, "chatSound", rule.chatSound);
        chatterino::rj::getSafe(value, "volume", rule.volume);
        chatterino::rj::getSafe(value, "cooldownSecs", rule.cooldownSecs);

        return rule;
    }
};

}  // namespace pajlada
