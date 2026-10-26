// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "controllers/notifications/NotificationController.hpp"

#include "Application.hpp"
#include "common/QLogging.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "controllers/notifications/NotificationModel.hpp"
#include "controllers/notifications/ViewerSoundController.hpp"
#include "controllers/blerp/BlerpController.hpp"
#include "controllers/social/SocialPostController.hpp"
#include "controllers/sound/ISoundController.hpp"
#include "messages/Message.hpp"
#include "messages/MessageBuilder.hpp"
#include "providers/kick/KickApi.hpp"
#include "providers/twitch/api/Helix.hpp"
#include "providers/twitch/TwitchIrcServer.hpp"
#include "singletons/Settings.hpp"
#include "singletons/StreamerMode.hpp"
#include "singletons/Toasts.hpp"
#include "singletons/WindowManager.hpp"
#include "util/Helpers.hpp"

#include <QUrl>

#include <cassert>
#include <ranges>

namespace ranges = std::ranges;

namespace chatterino {

NotificationController::NotificationController()
    : socialPosts_(std::make_unique<SocialPostController>())
    , viewerSounds_(std::make_unique<ViewerSoundController>())
    , blerp_(std::make_unique<BlerpController>())
{
    for (const QString &channelName : this->twitchSetting_.getValue())
    {
        this->channelMap[Platform::Twitch].append(channelName);
    }

    // We can safely ignore this signal connection since channelMap will always be destroyed
    // before the NotificationController
    std::ignore =
        this->channelMap[Platform::Twitch].delayedItemsChanged.connect([this] {
            this->twitchSetting_.setValue(
                this->channelMap[Platform::Twitch].raw());
        });

    QObject::connect(&this->liveStatusTimer_, &QTimer::timeout, [this] {
        this->fetchFakeChannels();
    });
    this->liveStatusTimer_.start(60 * 1000);
}

NotificationController::~NotificationController() = default;

SocialPostController *NotificationController::socialPosts()
{
    return this->socialPosts_.get();
}

ViewerSoundController *NotificationController::viewerSounds()
{
    return this->viewerSounds_.get();
}

BlerpController *NotificationController::blerp()
{
    return this->blerp_.get();
}

void NotificationController::initialize()
{
    this->fetchFakeChannels();
    if (this->blerp_)
    {
        this->blerp_->initialize();
    }
}

void NotificationController::updateChannelNotification(
    const QString &channelName, Platform p)
{
    if (this->isChannelNotified(channelName, p))
    {
        this->removeChannelNotification(channelName, p);
    }
    else
    {
        this->addChannelNotification(channelName, p);
    }
}

bool NotificationController::isChannelNotified(const QString &channelName,
                                               Platform p) const
{
    return ranges::any_of(this->channelMap.at(p).raw(), [&](const auto &name) {
        return name.compare(channelName, Qt::CaseInsensitive) == 0;
    });
}

void NotificationController::addChannelNotification(const QString &channelName,
                                                    Platform p)
{
    this->channelMap[p].append(channelName);
}

void NotificationController::removeChannelNotification(
    const QString &channelName, Platform p)
{
    for (std::vector<int>::size_type i = 0;
         i != this->channelMap[p].raw().size(); i++)
    {
        if (this->channelMap[p].raw()[i].compare(channelName,
                                                 Qt::CaseInsensitive) == 0)
        {
            this->channelMap[p].removeAt(static_cast<int>(i));
            i--;
        }
    }
}

void NotificationController::playSound() const
{
    QUrl highlightSoundUrl =
        getSettings()->notificationCustomSound
            ? QUrl::fromLocalFile(
                  getSettings()->notificationPathSound.getValue())
            : QUrl("qrc:/sounds/ping2.wav");

    getApp()->getSound()->play(highlightSoundUrl);
}

NotificationModel *NotificationController::createModel(QObject *parent,
                                                       Platform p)
{
    auto *model = new NotificationModel(parent);
    model->initialize(&this->channelMap[p]);
    return model;
}

void NotificationController::notifyTwitchChannelLive(
    const NotificationPayload &payload)
{
    assert(!payload.channelId.isEmpty() && !payload.streamId.isEmpty());

    auto [streamIt, inserted] = this->lastNotifiedStreamIds_.try_emplace(
        payload.channelId, payload.streamId);
    if (!inserted && streamIt->second == payload.streamId)
    {
        return;
    }
    if (!inserted)
    {
        this->notifyTwitchChannelOffline(payload.channelId);
        streamIt->second = payload.streamId;
    }

    if (this->viewerSounds_)
    {
        this->viewerSounds_->resetSessionState("twitch:" + payload.channelName);
    }

    bool showNotification =
        !(getSettings()->suppressInitialLiveNotification &&
          payload.isInitialUpdate) &&
        !(getApp()->getStreamerMode()->isEnabled() &&
          getSettings()->streamerModeSuppressLiveNotifications);
    bool playedSound = false;

    if (showNotification &&
        this->isChannelNotified(payload.channelName, Platform::Twitch))
    {
        if (Toasts::isEnabled())
        {
            getApp()->getToasts()->sendChannelNotification(payload.channelName,
                                                           payload.title);
        }
        if (getSettings()->notificationPlaySound)
        {
            this->playSound();
            playedSound = true;
        }
        if (getSettings()->notificationFlashTaskbar)
        {
            getApp()->getWindows()->sendAlert();
        }
    }

    // Message in /live channel
    getApp()->getTwitch()->getLiveChannel()->addMessage(
        MessageBuilder::makeLiveMessage(
            {
                .id = payload.channelId,
                .login = payload.channelName,
                .displayName = payload.displayName,
            },
            payload.title),
        MessageContext::Original);

    // Notify on all channels with a ping sound
    if (showNotification && !playedSound &&
        getSettings()->notificationOnAnyChannel)
    {
        this->playSound();
    }

    if (this->socialPosts_)
    {
        QString twitchTarget = this->socialPosts_->resolveTwitchTarget();
        QString kickTarget = this->socialPosts_->resolveKickTarget();

        this->socialPosts_->handleBroadcasterLive({
            .platform = "Twitch",
            .channelName = payload.channelName,
            .displayName = payload.displayName,
            .streamId = payload.streamId,
            .title = payload.title,
            .game = payload.gameName,
            .url = "https://twitch.tv/" + payload.channelName,
            .twitchUrl = !twitchTarget.isEmpty()
                             ? "https://twitch.tv/" + twitchTarget
                             : "https://twitch.tv/" + payload.channelName,
            .kickUrl = !kickTarget.isEmpty()
                           ? "https://kick.com/" + kickTarget
                           : "",
            .isInitialUpdate = payload.isInitialUpdate,
            .isRerun = payload.isRerun,
        });
    }
}

void NotificationController::notifyKickChannelLive(
    const KickNotificationPayload &payload)
{
    if (this->viewerSounds_)
    {
        this->viewerSounds_->resetSessionState("kick:" + payload.slug);
    }
    if (this->socialPosts_)
    {
        QString twitchTarget = this->socialPosts_->resolveTwitchTarget();
        QString kickTarget = this->socialPosts_->resolveKickTarget();

        this->socialPosts_->handleBroadcasterLive({
            .platform = "Kick",
            .channelName = payload.slug,
            .displayName = payload.displayName,
            .streamId = "",
            .title = payload.title,
            .game = payload.category,
            .url = "https://kick.com/" + payload.slug,
            .twitchUrl = !twitchTarget.isEmpty()
                             ? "https://twitch.tv/" + twitchTarget
                             : "",
            .kickUrl = !kickTarget.isEmpty()
                           ? "https://kick.com/" + kickTarget
                           : "https://kick.com/" + payload.slug,
            .isInitialUpdate = payload.isInitialUpdate,
            .isRerun = false,
        });
    }
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void NotificationController::notifyTwitchChannelOffline(const QString &id) const
{
    // "delete" old 'CHANNEL is live' message
    auto snapshot =
        getApp()->getTwitch()->getLiveChannel()->getMessageSnapshot(200);
    for (const auto &s : snapshot | std::views::reverse)
    {
        if (s->id == id)
        {
            s->flags.set(MessageFlag::Disabled);
            break;
        }
    }
}

void NotificationController::fetchFakeChannels()
{
    qCDebug(chatterinoNotification) << "fetching fake channels";

    QStringList channels;
    for (size_t i = 0; i < this->channelMap[Platform::Twitch].raw().size(); i++)
    {
        const auto &name = this->channelMap[Platform::Twitch].raw()[i];
        auto chan = getApp()->getTwitch()->getChannelOrEmpty(name);
        if (chan->isEmpty())
        {
            channels.push_back(name);
        }
        else
        {
            this->fakeChannels_.erase(name);
        }
    }

    if (getSettings()->socialPostEnabled && this->socialPosts_)
    {
        QString twitchTarget = this->socialPosts_->resolveTwitchTarget();
        if (!twitchTarget.isEmpty())
        {
            auto chan = getApp()->getTwitch()->getChannelOrEmpty(twitchTarget);
            if (chan->isEmpty() &&
                !channels.contains(twitchTarget, Qt::CaseInsensitive))
            {
                channels.push_back(twitchTarget);
            }
        }

        QString kickTarget = this->socialPosts_->resolveKickTarget();
        if (!kickTarget.isEmpty() && getApp()->getAccounts()->kick.isLoggedIn())
        {
            getKickApi()->getChannelByName(
                kickTarget, [this, kickTarget](auto expectedInfo) {
                    if (!expectedInfo)
                    {
                        return;
                    }
                    bool isLive = expectedInfo->stream.isLive;
                    auto it = this->fakeKickChannels_.find(kickTarget);
                    bool isInitial = (it == this->fakeKickChannels_.end());
                    if (isInitial)
                    {
                        this->fakeKickChannels_[kickTarget] = isLive;
                        return;
                    }
                    if (!it->second && isLive)
                    {
                        it->second = true;
                        this->notifyKickChannelLive({
                            .slug = kickTarget,
                            .displayName = kickTarget,
                            .title = expectedInfo->streamTitle,
                            .category = expectedInfo->category.name,
                            .isInitialUpdate = false,
                        });
                    }
                    else if (!isLive)
                    {
                        it->second = false;
                    }
                });
        }
    }

    for (const auto &batch : splitListIntoBatches(channels))
    {
        getHelix()->fetchStreams(
            {}, batch,
            [batch, this](const auto &streams) {
                std::map<QString, std::optional<HelixStream>,
                         QCompareCaseInsensitive>
                    liveStreams;
                for (const auto &stream : streams)
                {
                    liveStreams.emplace(stream.userLogin, stream);
                }

                for (const auto &name : batch)
                {
                    auto it = liveStreams.find(name);
                    if (it == liveStreams.end())
                    {
                        this->updateFakeChannel(name, std::nullopt);
                    }
                    else
                    {
                        this->updateFakeChannel(name, it->second);
                    }
                }
            },
            [batch]() {
                // we done fucked up.
                qCWarning(chatterinoNotification)
                    << "Failed to fetch live status for " << batch;
            },
            []() {
                // finally
            });
    }
}
void NotificationController::updateFakeChannel(
    const QString &channelName, const std::optional<HelixStream> &stream)
{
    bool live = stream.has_value();
    qCDebug(chatterinoNotification).nospace().noquote()
        << "[FakeTwitchChannel " << channelName
        << "] New live status: " << stream.has_value();

    auto channelIt = this->fakeChannels_.find(channelName);
    bool isInitialUpdate = false;
    if (channelIt == this->fakeChannels_.end())
    {
        channelIt = this->fakeChannels_
                        .emplace(channelName,
                                 FakeChannel{
                                     .id = {},
                                     .isLive = live,
                                 })
                        .first;
        isInitialUpdate = true;
    }
    if (channelIt->second.isLive == live && !isInitialUpdate)
    {
        return;  // nothing changed
    }

    if (live && channelIt->second.id.isNull())
    {
        channelIt->second.id = stream->userId;
    }

    channelIt->second.isLive = live;

    // Similar code can be found in TwitchChannel::onLiveStatusChange.
    // Since this is a fake channel, we don't send a live message in the
    // TwitchChannel.
    if (!live)
    {
        // Stream is offline
        this->notifyTwitchChannelOffline(channelIt->second.id);
        return;
    }

    bool isRerun = false;
    for (const auto &tag : stream->tags)
    {
        if (QString::compare(tag, "Rerun", Qt::CaseInsensitive) == 0)
        {
            isRerun = true;
            break;
        }
    }

    this->notifyTwitchChannelLive({
        .channelId = stream->userId,
        .streamId = stream->id,
        .channelName = channelName,
        .displayName = stream->userName,
        .title = stream->title,
        .gameName = stream->gameName,
        .isInitialUpdate = isInitialUpdate,
        .isRerun = isRerun,
    });
}

}  // namespace chatterino
