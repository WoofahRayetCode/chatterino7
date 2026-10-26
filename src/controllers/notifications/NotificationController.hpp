// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common/ChatterinoSetting.hpp"
#include "common/SignalVector.hpp"
#include "util/QCompareTransparent.hpp"

#include <QTimer>

namespace chatterino {

class Settings;
class Paths;
struct HelixStream;

class NotificationModel;
class SocialPostController;
class ViewerSoundController;
class BlerpController;

enum class Platform : uint8_t {
    Twitch,  // 0
};

class NotificationController final
{
public:
    NotificationController();
    ~NotificationController();

    // Perform an initial load so we don't have to wait for the timer
    void initialize();

    bool isChannelNotified(const QString &channelName, Platform p) const;
    void updateChannelNotification(const QString &channelName, Platform p);
    void addChannelNotification(const QString &channelName, Platform p);
    void removeChannelNotification(const QString &channelName, Platform p);

    struct NotificationPayload {
        QString channelId;
        QString streamId;
        QString channelName;
        QString displayName;
        QString title;
        QString gameName;
        bool isInitialUpdate = false;
        bool isRerun = false;
    };

    struct KickNotificationPayload {
        QString slug;
        QString displayName;
        QString title;
        QString category;
        bool isInitialUpdate = false;
    };

    /// @brief Sends out notifications for a channel that has gone live
    ///
    /// Duplicate calls for the same stream have no effect
    void notifyTwitchChannelLive(const NotificationPayload &payload);

    /// @brief Sends out notifications for a Kick channel that has gone live
    void notifyKickChannelLive(const KickNotificationPayload &payload);

    /// @brief Sends out notifications for a channel that has gone offline
    ///
    /// This doesn't check for duplicate notifications.
    void notifyTwitchChannelOffline(const QString &id) const;

    void playSound() const;

    NotificationModel *createModel(QObject *parent, Platform p);

    SocialPostController *socialPosts();
    ViewerSoundController *viewerSounds();
    BlerpController *blerp();

private:
    void fetchFakeChannels();
    void removeFakeChannel(const QString &channelName);
    void updateFakeChannel(const QString &channelName,
                           const std::optional<HelixStream> &stream);

    struct FakeChannel {
        QString id;
        bool isLive = false;
    };

    /// @brief This map tracks channels without an associated TwitchChannel
    ///
    /// These channels won't be tracked in LiveController.
    /// Channels are identified by their login name (case insensitive).
    std::map<QString, FakeChannel, QCompareCaseInsensitive> fakeChannels_;
    std::map<QString, bool, QCompareCaseInsensitive> fakeKickChannels_;

    /// Maps channel IDs to their most recently notified stream IDs
    std::map<QString, QString> lastNotifiedStreamIds_;

    QTimer liveStatusTimer_;

    std::map<Platform, SignalVector<QString>> channelMap;

    ChatterinoSetting<std::vector<QString>> twitchSetting_ = {
        "/notifications/twitch"};

    std::unique_ptr<SocialPostController> socialPosts_;
    std::unique_ptr<ViewerSoundController> viewerSounds_;
    std::unique_ptr<BlerpController> blerp_;
};

}  // namespace chatterino
