// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QString>

namespace chatterino {

struct LiveStreamContext {
    QString platform;      // "Twitch" or "Kick"
    QString channelName;   // login / slug
    QString displayName;   // display name
    QString streamId;
    QString title;
    QString game;
    QString url;
    QString twitchUrl;
    QString kickUrl;
    bool isInitialUpdate = false;
    bool isRerun = false;
};

class LivePostTemplate
{
public:
    static QString render(const QString &templateStr,
                          const LiveStreamContext &ctx);

    static int countBlueskyGraphemes(const QString &text);
    static int countTwitterWeighted(const QString &text);

    static QString clampForBluesky(const QString &text,
                                   int maxGraphemes = 300);
    static QString clampForTwitter(const QString &text,
                                   int maxWeightedChars = 280);
};

}  // namespace chatterino
