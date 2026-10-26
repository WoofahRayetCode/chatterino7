// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "controllers/social/BlueskyClient.hpp"
#include "controllers/social/LivePostTemplate.hpp"
#include "controllers/social/TwitterClient.hpp"

#include <QObject>
#include <QString>

#include <memory>
#include <optional>

namespace chatterino {

class SocialPostController : public QObject
{
    Q_OBJECT

public:
    struct PostStatusReport {
        bool blueskyAlreadyPosted = false;
        QString blueskyDetail;
        bool twitterAlreadyPosted = false;
        QString twitterDetail;
    };

    explicit SocialPostController(QObject *parent = nullptr);

    BlueskyClient &bluesky();
    TwitterClient &twitter();

    QString twitterHandle() const;
    void setTwitterHandle(const QString &handle);

    QString resolveTwitchTarget() const;
    QString resolveKickTarget() const;

    void handleBroadcasterLive(const LiveStreamContext &ctx);

    void fetchLiveStreamMetadata(
        const QString &platform, const QString &channelName,
        std::function<void(const QString &title, const QString &game)> cb);

    void checkPostStatus(
        const LiveStreamContext &ctx,
        std::function<void(const PostStatusReport &report)> cb);

    void openManualPostDialog(
        const std::optional<LiveStreamContext> &ctx = std::nullopt);

    void postInBackground(
        const std::optional<LiveStreamContext> &ctx = std::nullopt);

    void postChannelNotice(const LiveStreamContext &ctx,
                           const QString &message);

    void publishAll(const QString &text, bool postToBluesky, bool postToTwitter,
                    const LiveStreamContext &ctx,
                    std::function<void(bool bskySuccess, const QString &bskyMsg,
                                       bool xSuccess, const QString &xMsg)>
                        cb);

private:
    void loadPersistentState();
    void savePersistentState() const;

    BlueskyClient blueskyClient_;
    TwitterClient twitterClient_;
    bool confirmDialogOpen_ = false;
    QString twitterHandle_ = QStringLiteral("KingManiacTech");
    qint64 lastBlueskyPostedEpochSec_ = 0;
    QString lastBlueskyStreamId_;
    qint64 lastTwitterPostedEpochSec_ = 0;
    QString lastTwitterStreamId_;
};

}  // namespace chatterino
