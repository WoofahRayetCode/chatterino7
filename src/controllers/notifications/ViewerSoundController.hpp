// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "controllers/notifications/ViewerSoundRule.hpp"

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QUrl>

#include <chrono>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace chatterino {

class Channel;

class ViewerSoundController : public QObject
{
    Q_OBJECT

public:
    explicit ViewerSoundController(QObject *parent = nullptr);

    /// Record when this client joined a channel to implement the startup grace window
    void markSelfJoinedChannel(const QString &platform, const QString &channelName);

    /// Called on viewer JOIN (e.g. Twitch IRC JOIN or chatter arrival)
    void onViewerJoined(const QString &platform, const QString &channelName,
                        const QString &username);

    /// Called when a viewer posts a message in chat.
    /// Returns true if a viewer rule matched and handled sound playback
    /// (suppressing the generic channel-wide chat sound).
    bool handleViewerMessage(const QString &platform, const QString &channelName,
                             const QString &username, bool isSelf,
                             bool alreadyPlayedHighlightSound);

    /// Reset announced session joins (e.g. on stream start/end or manual reset)
    void resetSessionState(const QString &channelKey = QString());

    /// Find matching rule for a user and channel (channel-specific takes precedence over global)
    std::optional<ViewerSoundRule> findMatchingRule(
        const QString &username, const QString &platform,
        const QString &channelName) const;

    /// Add or update a rule
    void upsertRule(const ViewerSoundRule &rule);

    /// Remove a rule by username and channel filter
    bool removeRule(const QString &username, const QString &channelFilter);

    /// Resolve sound string to QUrl
    static QUrl resolveSoundUrl(const QString &soundId);

private:
    void playSound(const QString &soundId, int volume);

    mutable std::mutex mutex_;
    std::unordered_set<QString> announcedJoinsThisSession_;
    std::unordered_map<QString, std::chrono::steady_clock::time_point>
        selfJoinedAt_;
    std::unordered_map<QString, std::chrono::steady_clock::time_point>
        lastChatSoundAt_;
};

}  // namespace chatterino
