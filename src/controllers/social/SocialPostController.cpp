// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "controllers/social/SocialPostController.hpp"

#include "Application.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "messages/Message.hpp"
#include "messages/MessageBuilder.hpp"
#include "providers/kick/KickAccount.hpp"
#include "providers/kick/KickApi.hpp"
#include "providers/kick/KickChannel.hpp"
#include "providers/kick/KickChatServer.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "providers/twitch/TwitchIrcServer.hpp"
#include "providers/twitch/api/Helix.hpp"
#include "singletons/Paths.hpp"
#include "singletons/Settings.hpp"
#include "widgets/dialogs/LivePostConfirmDialog.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>

namespace chatterino {

SocialPostController::SocialPostController(QObject *parent)
    : QObject(parent)
{
    this->loadPersistentState();
}

void SocialPostController::loadPersistentState()
{
    if (!getApp())
    {
        return;
    }

    QFile file(QDir(getApp()->getPaths().settingsDirectory)
                   .filePath(QStringLiteral("social-post-state.json")));
    if (!file.open(QIODevice::ReadOnly))
    {
        return;
    }

    auto doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
    {
        return;
    }

    auto obj = doc.object();
    QString handle = obj["twitterHandle"].toString().trimmed();
    if (!handle.isEmpty())
    {
        this->twitterHandle_ = handle;
    }
    this->lastBlueskyPostedEpochSec_ =
        obj["lastBlueskyPostedEpochSec"].toVariant().toLongLong();
    this->lastBlueskyStreamId_ = obj["lastBlueskyStreamId"].toString();
    this->lastTwitterPostedEpochSec_ =
        obj["lastTwitterPostedEpochSec"].toVariant().toLongLong();
    this->lastTwitterStreamId_ = obj["lastTwitterStreamId"].toString();
}

void SocialPostController::savePersistentState() const
{
    if (!getApp())
    {
        return;
    }

    QFile file(QDir(getApp()->getPaths().settingsDirectory)
                   .filePath(QStringLiteral("social-post-state.json")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return;
    }

    QJsonObject obj;
    obj["twitterHandle"] = this->twitterHandle_;
    obj["lastBlueskyPostedEpochSec"] =
        static_cast<qint64>(this->lastBlueskyPostedEpochSec_);
    obj["lastBlueskyStreamId"] = this->lastBlueskyStreamId_;
    obj["lastTwitterPostedEpochSec"] =
        static_cast<qint64>(this->lastTwitterPostedEpochSec_);
    obj["lastTwitterStreamId"] = this->lastTwitterStreamId_;

    file.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

QString SocialPostController::twitterHandle() const
{
    return this->twitterHandle_;
}

void SocialPostController::setTwitterHandle(const QString &handle)
{
    QString clean = handle.trimmed();
    if (clean.startsWith('@'))
    {
        clean.remove(0, 1);
    }
    this->twitterHandle_ = clean;
    this->savePersistentState();
}

BlueskyClient &SocialPostController::bluesky()
{
    return this->blueskyClient_;
}

TwitterClient &SocialPostController::twitter()
{
    return this->twitterClient_;
}

QString SocialPostController::resolveTwitchTarget() const
{
    QString overrideName =
        getSettings()->socialPostTwitchChannelOverride.getValue().trimmed();
    if (!overrideName.isEmpty())
    {
        return overrideName;
    }

    auto currentUser = getApp()->getAccounts()->twitch.getCurrent();
    if (currentUser && !currentUser->isAnon())
    {
        return currentUser->getUserName();
    }

    return "";
}

QString SocialPostController::resolveKickTarget() const
{
    QString overrideName =
        getSettings()->socialPostKickChannelOverride.getValue().trimmed();
    if (!overrideName.isEmpty())
    {
        return overrideName;
    }

    if (getApp()->getAccounts()->kick.isLoggedIn())
    {
        auto kickUser = getApp()->getAccounts()->kick.current();
        if (kickUser)
        {
            return kickUser->username();
        }
    }

    return "";
}

void SocialPostController::fetchLiveStreamMetadata(
    const QString &platform, const QString &channelName,
    std::function<void(const QString &title, const QString &game)> cb)
{
    QString chan = channelName.trimmed();
    if (chan.isEmpty())
    {
        cb("", "");
        return;
    }

    if (platform.compare("Kick", Qt::CaseInsensitive) == 0)
    {
        getKickApi()->getChannelByName(
            chan, [cb](auto expectedInfo) {
                if (expectedInfo)
                {
                    cb(expectedInfo->streamTitle, expectedInfo->category.name);
                }
                else
                {
                    cb("", "");
                }
            });
        return;
    }

    // Default to Twitch
    getHelix()->getStreamByName(
        chan,
        [chan, cb](bool isLive, HelixStream stream) {
            if (isLive &&
                (!stream.title.isEmpty() || !stream.gameName.isEmpty()))
            {
                cb(stream.title, stream.gameName);
                return;
            }

            // Fallback to channel info if stream is offline or missing metadata
            getHelix()->getUserByName(
                chan,
                [cb](const HelixUser &user) {
                    if (!user.id.isEmpty())
                    {
                        getHelix()->getChannel(
                            user.id,
                            [cb](HelixChannel channel) {
                                cb(channel.title, channel.gameName);
                            },
                            [cb]() {
                                cb("", "");
                            });
                    }
                    else
                    {
                        cb("", "");
                    }
                },
                [cb]() {
                    cb("", "");
                });
        },
        [cb]() {
            cb("", "");
        },
        []() {});
}

void SocialPostController::handleBroadcasterLive(const LiveStreamContext &ctx)
{
    if (!getSettings()->socialPostEnabled)
    {
        return;
    }

    if (!getSettings()->socialPostBlueskyEnabled &&
        !getSettings()->socialPostTwitterEnabled)
    {
        return;
    }

    if (ctx.isInitialUpdate || ctx.isRerun)
    {
        return;
    }

    // Verify channel matches target
    QString target;
    if (ctx.platform.compare("Twitch", Qt::CaseInsensitive) == 0)
    {
        target = this->resolveTwitchTarget();
    }
    else if (ctx.platform.compare("Kick", Qt::CaseInsensitive) == 0)
    {
        target = this->resolveKickTarget();
    }

    if (target.isEmpty() ||
        ctx.channelName.compare(target, Qt::CaseInsensitive) != 0)
    {
        return;
    }

    // If title or game is missing, fetch fresh metadata first
    if (ctx.title.trimmed().isEmpty() || ctx.game.trimmed().isEmpty())
    {
        this->fetchLiveStreamMetadata(
            ctx.platform, ctx.channelName,
            [this, ctx](const QString &freshTitle, const QString &freshGame) {
                LiveStreamContext enriched = ctx;
                if (enriched.title.trimmed().isEmpty())
                {
                    enriched.title = freshTitle;
                }
                if (enriched.game.trimmed().isEmpty())
                {
                    enriched.game = freshGame;
                }
                this->handleBroadcasterLive(enriched);
            });
        return;
    }

    // Deduplication check
    if (!ctx.streamId.isEmpty() &&
        ctx.streamId == getSettings()->socialPostLastTwitchStreamId.getValue())
    {
        return;
    }

    qint64 now = QDateTime::currentSecsSinceEpoch();
    qint64 lastPosted = getSettings()->socialPostLastPostedEpochSec.getValue();
    int cooldownSec =
        getSettings()->socialPostCooldownMinutes.getValue() * 60;

    if (lastPosted > 0 && (now - lastPosted) < cooldownSec)
    {
        return;
    }

    if (this->confirmDialogOpen_)
    {
        return;
    }

    QString rendered = LivePostTemplate::render(
        getSettings()->socialPostTemplate.getValue(), ctx);

    if (getSettings()->socialPostTriggerMode == 1)
    {
        // Auto Mode
        if (!ctx.streamId.isEmpty())
        {
            getSettings()->socialPostLastTwitchStreamId.setValue(ctx.streamId);
        }
        getSettings()->socialPostLastPostedEpochSec.setValue(
            static_cast<int>(now));

        this->publishAll(
            rendered, getSettings()->socialPostBlueskyEnabled,
            getSettings()->socialPostTwitterEnabled, ctx,
            [](bool /*bOk*/, const QString & /*bMsg*/, bool /*xOk*/,
               const QString & /*xMsg*/) {});
    }
    else
    {
        // Confirm Mode
        this->confirmDialogOpen_ = true;
        auto *dialog = new LivePostConfirmDialog(ctx, rendered, this);
        QObject::connect(dialog, &LivePostConfirmDialog::closed, this,
                          [this] {
                              this->confirmDialogOpen_ = false;
                          });
        dialog->show();
        dialog->raise();
        dialog->activateWindow();
    }
}

void SocialPostController::openManualPostDialog(
    const std::optional<LiveStreamContext> &optCtx)
{
    LiveStreamContext ctx;
    if (optCtx)
    {
        ctx = *optCtx;
    }
    else
    {
        QString twitchTarget = this->resolveTwitchTarget();
        QString kickTarget = this->resolveKickTarget();

        ctx.platform = !twitchTarget.isEmpty() ? "Twitch" : "Kick";
        ctx.channelName =
            !twitchTarget.isEmpty() ? twitchTarget : kickTarget;
        ctx.displayName = ctx.channelName;
        ctx.twitchUrl = !twitchTarget.isEmpty()
                            ? "https://twitch.tv/" + twitchTarget
                            : "";
        ctx.kickUrl =
            !kickTarget.isEmpty() ? "https://kick.com/" + kickTarget : "";
        ctx.url = ctx.platform == "Twitch" ? ctx.twitchUrl : ctx.kickUrl;

        // Try local channel status
        if (ctx.platform == "Twitch" && getApp())
        {
            if (auto *twitch = getApp()->getTwitch())
            {
                auto chan = twitch->getChannelOrEmpty(ctx.channelName);
                if (auto *tc = dynamic_cast<TwitchChannel *>(chan.get()))
                {
                    const auto status = tc->accessStreamStatus();
                    ctx.title = status->title;
                    ctx.game = status->game;
                    ctx.streamId = status->streamId;
                }
            }
        }
        else if (ctx.platform == "Kick" && getApp())
        {
            if (auto *kick = getApp()->getKickChatServer())
            {
                if (auto kc = kick->findBySlug(ctx.channelName))
                {
                    auto data = kc->streamData();
                    ctx.title = data.title;
                    ctx.game = data.category;
                }
            }
        }
    }

    QString rendered = LivePostTemplate::render(
        getSettings()->socialPostTemplate.getValue(), ctx);

    auto *dialog = new LivePostConfirmDialog(ctx, rendered, this);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();

    // Always refresh from platform API to ensure the latest title and game
    QPointer<LivePostConfirmDialog> pDialog(dialog);
    this->fetchLiveStreamMetadata(
        ctx.platform, ctx.channelName,
        [pDialog](const QString &title, const QString &game) {
            if (pDialog && (!title.isEmpty() || !game.isEmpty()))
            {
                pDialog->updateStreamMetadata(title, game);
            }
        });
}

void SocialPostController::checkPostStatus(
    const LiveStreamContext &ctx,
    std::function<void(const PostStatusReport &report)> cb)
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const int cooldownMin =
        std::max(1, getSettings()->socialPostCooldownMinutes.getValue());
    const int cooldownSec = cooldownMin * 60;

    PostStatusReport initialReport;

    // 1. Check Twitter/X local session + persistent streamId/cooldown
    if (!ctx.streamId.isEmpty() && this->lastTwitterStreamId_ == ctx.streamId)
    {
        initialReport.twitterAlreadyPosted = true;
        int minsAgo = static_cast<int>(
            std::max<qint64>(0, (now - this->lastTwitterPostedEpochSec_) / 60));
        initialReport.twitterDetail =
            QStringLiteral("Already posted (%1m ago)").arg(minsAgo);
    }
    else if (this->lastTwitterPostedEpochSec_ > 0 &&
             (now - this->lastTwitterPostedEpochSec_) < cooldownSec)
    {
        initialReport.twitterAlreadyPosted = true;
        int minsAgo = static_cast<int>((now - this->lastTwitterPostedEpochSec_) / 60);
        initialReport.twitterDetail =
            QStringLiteral("Already posted (%1m ago)").arg(minsAgo);
    }
    else
    {
        initialReport.twitterDetail = QStringLiteral("Not posted yet");
    }

    // 2. Check Bluesky local session first, then verify live Bluesky feed
    if (!ctx.streamId.isEmpty() && this->lastBlueskyStreamId_ == ctx.streamId)
    {
        initialReport.blueskyAlreadyPosted = true;
        int minsAgo = static_cast<int>(
            std::max<qint64>(0, (now - this->lastBlueskyPostedEpochSec_) / 60));
        initialReport.blueskyDetail =
            QStringLiteral("Already posted (%1m ago)").arg(minsAgo);
    }
    else if (this->lastBlueskyPostedEpochSec_ > 0 &&
             (now - this->lastBlueskyPostedEpochSec_) < cooldownSec)
    {
        initialReport.blueskyAlreadyPosted = true;
        int minsAgo = static_cast<int>((now - this->lastBlueskyPostedEpochSec_) / 60);
        initialReport.blueskyDetail =
            QStringLiteral("Already posted (%1m ago)").arg(minsAgo);
    }

    QString bskyHandle =
        getSettings()->socialPostBlueskyHandle.getValue().trimmed();
    QString xHandle = this->twitterHandle_.trimmed();
    QString streamUrl = ctx.url.isEmpty()
                            ? ("https://twitch.tv/" + ctx.channelName)
                            : ctx.url;

    struct DualCheckState {
        PostStatusReport report;
        bool bskyDone = false;
        bool xDone = false;
        bool finished = false;
    };

    auto state = std::make_shared<DualCheckState>();
    state->report = initialReport;

    auto tryFinish = [state, cb] {
        if (state->bskyDone && state->xDone && !state->finished)
        {
            state->finished = true;
            cb(state->report);
        }
    };

    if (bskyHandle.isEmpty())
    {
        if (!state->report.blueskyAlreadyPosted)
        {
            state->report.blueskyDetail = QStringLiteral("Not posted yet");
        }
        state->bskyDone = true;
    }
    else
    {
        this->blueskyClient_.checkRecentLivePost(
            bskyHandle, streamUrl, cooldownMin,
            [state, tryFinish](
                const BlueskyClient::RecentPostCheck &bskyCheck) {
                if (bskyCheck.alreadyPosted)
                {
                    state->report.blueskyAlreadyPosted = true;
                    state->report.blueskyDetail =
                        QStringLiteral("Already posted on Bluesky (%1m ago)")
                            .arg(bskyCheck.ageMinutes);
                }
                else if (!state->report.blueskyAlreadyPosted)
                {
                    state->report.blueskyDetail =
                        QStringLiteral("Not posted yet");
                }
                state->bskyDone = true;
                tryFinish();
            });
    }

    if (xHandle.isEmpty())
    {
        state->xDone = true;
    }
    else
    {
        TwitterClient::checkRecentLivePost(
            xHandle, streamUrl, cooldownMin,
            [state, xHandle, tryFinish](
                const TwitterClient::RecentPostCheck &xCheck) {
                if (xCheck.alreadyPosted)
                {
                    state->report.twitterAlreadyPosted = true;
                    state->report.twitterDetail =
                        QStringLiteral("Already posted on X @%1 (%2m ago)")
                            .arg(xHandle)
                            .arg(xCheck.ageMinutes);
                }
                else if (!state->report.twitterAlreadyPosted)
                {
                    state->report.twitterDetail =
                        QStringLiteral("Not posted yet on @%1").arg(xHandle);
                }
                state->xDone = true;
                tryFinish();
            });
    }

    tryFinish();
}

void SocialPostController::postInBackground(
    const std::optional<LiveStreamContext> &optCtx)
{
    LiveStreamContext ctx;
    if (optCtx)
    {
        ctx = *optCtx;
    }
    else
    {
        QString twitchTarget = this->resolveTwitchTarget();
        QString kickTarget = this->resolveKickTarget();

        ctx.platform = !twitchTarget.isEmpty() ? "Twitch" : "Kick";
        ctx.channelName =
            !twitchTarget.isEmpty() ? twitchTarget : kickTarget;
        ctx.displayName = ctx.channelName;
        ctx.twitchUrl = !twitchTarget.isEmpty()
                            ? "https://twitch.tv/" + twitchTarget
                            : "";
        ctx.kickUrl =
            !kickTarget.isEmpty() ? "https://kick.com/" + kickTarget : "";
        ctx.url = ctx.platform == "Twitch" ? ctx.twitchUrl : ctx.kickUrl;
    }

    this->fetchLiveStreamMetadata(
        ctx.platform, ctx.channelName,
        [this, ctx](const QString &freshTitle, const QString &freshGame) {
            LiveStreamContext enriched = ctx;
            if (!freshTitle.isEmpty())
            {
                enriched.title = freshTitle;
            }
            if (!freshGame.isEmpty())
            {
                enriched.game = freshGame;
            }

            this->checkPostStatus(
                enriched, [this, enriched](const PostStatusReport &report) {
                    bool wantBsky = getSettings()->socialPostBlueskyEnabled;
                    bool wantX = getSettings()->socialPostTwitterEnabled;

                    bool doBsky = wantBsky && !report.blueskyAlreadyPosted;
                    bool doX = wantX && !report.twitterAlreadyPosted;

                    if (!doBsky && !doX)
                    {
                        QStringList skipped;
                        if (wantBsky && report.blueskyAlreadyPosted)
                        {
                            skipped << ("Bluesky: " + report.blueskyDetail);
                        }
                        if (wantX && report.twitterAlreadyPosted)
                        {
                            skipped << ("X: " + report.twitterDetail);
                        }
                        if (!skipped.isEmpty())
                        {
                            this->postChannelNotice(
                                enriched,
                                QStringLiteral(
                                    "📢 [Social] Skipped duplicate post (%1)")
                                    .arg(skipped.join(" | ")));
                        }
                        return;
                    }

                    QString rendered = LivePostTemplate::render(
                        getSettings()->socialPostTemplate.getValue(), enriched);

                    this->publishAll(
                        rendered, doBsky, doX, enriched,
                        [](bool /*bOk*/, const QString & /*bMsg*/, bool /*xOk*/,
                           const QString & /*xMsg*/) {});
                });
        });
}

void SocialPostController::postChannelNotice(const LiveStreamContext &ctx,
                                             const QString &message)
{
    if (!getApp())
    {
        return;
    }

    ChannelPtr chan;
    if (ctx.platform.compare("Kick", Qt::CaseInsensitive) == 0)
    {
        if (auto *kick = getApp()->getKickChatServer())
        {
            chan = kick->findBySlug(ctx.channelName);
        }
    }
    else if (auto *twitch = getApp()->getTwitch())
    {
        chan = twitch->getChannelOrEmpty(ctx.channelName);
    }

    if (chan && !chan->isEmpty())
    {
        chan->addMessage(makeSystemMessage(message), MessageContext::Original);
    }
}

void SocialPostController::publishAll(
    const QString &text, bool postToBluesky, bool postToTwitter,
    const LiveStreamContext &ctx,
    std::function<void(bool bskySuccess, const QString &bskyMsg, bool xSuccess,
                       const QString &xMsg)>
        cb)
{
    struct State {
        bool bskyDone = false;
        bool bskySuccess = true;
        QString bskyMsg;

        bool xDone = false;
        bool xSuccess = true;
        QString xMsg;
        bool notified = false;
    };

    auto state = std::make_shared<State>();
    if (!postToBluesky)
    {
        state->bskyDone = true;
    }
    if (!postToTwitter)
    {
        state->xDone = true;
    }

    auto checkDone = [this, state, postToBluesky, postToTwitter, ctx, cb] {
        if (state->bskyDone && state->xDone && !state->notified)
        {
            state->notified = true;
            const qint64 nowSec = QDateTime::currentSecsSinceEpoch();
            QStringList results;
            if (postToBluesky)
            {
                if (state->bskySuccess)
                {
                    this->lastBlueskyPostedEpochSec_ = nowSec;
                    this->lastBlueskyStreamId_ = ctx.streamId;
                }
                results.append(state->bskySuccess
                                   ? QStringLiteral("Bluesky: Posted in background (%1)")
                                         .arg(state->bskyMsg)
                                   : QStringLiteral("Bluesky error: %1")
                                         .arg(state->bskyMsg));
            }
            if (postToTwitter)
            {
                if (state->xSuccess)
                {
                    this->lastTwitterPostedEpochSec_ = nowSec;
                    this->lastTwitterStreamId_ = ctx.streamId;
                }
                results.append(state->xSuccess
                                   ? QStringLiteral("X: %1").arg(state->xMsg)
                                   : QStringLiteral("X error: %1")
                                         .arg(state->xMsg));
            }
            this->savePersistentState();
            if (!results.isEmpty())
            {
                this->postChannelNotice(
                    ctx, QStringLiteral("📢 [Social] %1 — %2 (%3)")
                             .arg(results.join(" | "), ctx.title, ctx.game));
            }

            if (cb)
            {
                cb(state->bskySuccess, state->bskyMsg, state->xSuccess,
                   state->xMsg);
            }
        }
    };

    if (postToBluesky)
    {
        QString bskyText = LivePostTemplate::clampForBluesky(text);
        std::optional<BlueskyClient::EmbedCard> embedCard;
        if (getSettings()->socialPostBlueskyEmbedCard && !ctx.url.isEmpty())
        {
            embedCard = BlueskyClient::EmbedCard{
                .uri = ctx.url,
                .title = QStringLiteral("%1 — Live on %2")
                             .arg(ctx.displayName, ctx.platform),
                .description = QStringLiteral("%1 (%2)").arg(
                    ctx.title, ctx.game.isEmpty() ? "Live" : ctx.game),
            };
        }

        this->blueskyClient_.post(
            getSettings()->socialPostBlueskyPdsUrl.getValue(),
            getSettings()->socialPostBlueskyHandle.getValue(),
            getSettings()->socialPostBlueskyAppPassword.getValue(), bskyText,
            embedCard,
            [state, checkDone](bool success, const QString &msg) {
                state->bskyDone = true;
                state->bskySuccess = success;
                state->bskyMsg = msg;
                checkDone();
            });
    }

    if (postToTwitter)
    {
        QString xText = LivePostTemplate::clampForTwitter(text);
        int mode = getSettings()->socialPostTwitterMode.getValue();
        bool hasApiKeys =
            !getSettings()->socialPostTwitterApiKey.getValue().trimmed().isEmpty() &&
            !getSettings()->socialPostTwitterApiSecret.getValue().trimmed().isEmpty() &&
            !getSettings()->socialPostTwitterAccessToken.getValue().trimmed().isEmpty() &&
            !getSettings()->socialPostTwitterAccessSecret.getValue().trimmed().isEmpty();

        if (mode == 0 && !hasApiKeys)
        {
            // Web Intent
            TwitterClient::openWebIntent(xText);
            state->xDone = true;
            state->xSuccess = true;
            state->xMsg = "Opened X Web Intent in browser";
            checkDone();
        }
        else
        {
            // API v2 (Background)
            TwitterClient::postApiV2(
                getSettings()->socialPostTwitterApiKey.getValue(),
                getSettings()->socialPostTwitterApiSecret.getValue(),
                getSettings()->socialPostTwitterAccessToken.getValue(),
                getSettings()->socialPostTwitterAccessSecret.getValue(), xText,
                [state, checkDone](bool success, const QString &msg) {
                    state->xDone = true;
                    state->xSuccess = success;
                    state->xMsg = msg;
                    checkDone();
                });
        }
    }

    checkDone();
}

}  // namespace chatterino
