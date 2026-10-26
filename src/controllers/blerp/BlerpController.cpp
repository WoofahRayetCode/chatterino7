// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "controllers/blerp/BlerpController.hpp"

#include "Application.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "controllers/notifications/ViewerSoundController.hpp"
#include "controllers/sound/ISoundController.hpp"
#include "messages/Message.hpp"
#include "messages/MessageBuilder.hpp"
#include "messages/MessageElement.hpp"
#include "providers/kick/KickChatServer.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "providers/twitch/TwitchIrcServer.hpp"
#include "singletons/Settings.hpp"
#include "singletons/StreamerMode.hpp"
#include "util/PostToThread.hpp"

namespace chatterino {

BlerpController::BlerpController(QObject *parent)
    : QObject(parent)
{
}

void BlerpController::initialize()
{
    // Connect client blerp events to recordBlerp
    connect(&this->client_, &BlerpClient::blerpReceived, this,
            [this](const BlerpEvent &ev) {
                this->recordBlerp(ev, true);
            });

    // Auto-connect socket if enabled and URL configured
    if (getSettings()->blerpEnabled.getValue())
    {
        QString url = getSettings()->blerpBrowserSourceUrl.getValue();
        if (!url.trimmed().isEmpty())
        {
            this->client_.setBrowserSourceUrl(url);
        }
    }

    // Connect setting listener
    getSettings()->blerpBrowserSourceUrl.connect(
        [this](const QString &url, auto) {
            if (getSettings()->blerpEnabled.getValue())
            {
                this->client_.setBrowserSourceUrl(url);
            }
        },
        false);

    getSettings()->blerpEnabled.connect(
        [this](bool enabled, auto) {
            if (enabled)
            {
                this->client_.setBrowserSourceUrl(
                    getSettings()->blerpBrowserSourceUrl.getValue());
            }
            else
            {
                this->client_.disconnectSocket();
            }
        },
        false);
}

BlerpClient &BlerpController::client()
{
    return this->client_;
}

const std::deque<BlerpEvent> &BlerpController::history() const
{
    return this->history_;
}

void BlerpController::clearHistory()
{
    std::lock_guard<std::mutex> lock(this->mutex_);
    this->history_.clear();
    Q_EMIT this->historyCleared();
}

void BlerpController::recordBlerp(const BlerpEvent &event, bool postChatNotice)
{
    if (!getSettings()->blerpEnabled.getValue())
    {
        return;
    }

    // 5-second cross-source deduplication key: channel | user | soundTitle
    QString dedupeKey = QString("%1|%2|%3")
                            .arg(event.channelName.toLower(),
                                 event.username.toLower(),
                                 event.soundTitle.toLower());

    auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        auto it = this->recentDedupeKeys_.find(dedupeKey);
        if (it != this->recentDedupeKeys_.end())
        {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                now - it->second);
            if (elapsed.count() < 5)
            {
                return;  // Drop duplicate
            }
        }
        this->recentDedupeKeys_[dedupeKey] = now;

        this->history_.push_front(event);
        if (this->history_.size() > MAX_HISTORY)
        {
            this->history_.pop_back();
        }
    }

    Q_EMIT this->eventRecorded(event);

    if (postChatNotice)
    {
        this->postInlineNotice(event);
    }

    this->playLocalChime();
}

std::shared_ptr<Channel> BlerpController::resolveTargetChannel(
    const QString &hintChannel)
{
    QString overrideName =
        getSettings()->blerpTargetChannelOverride.getValue().trimmed();

    QString target = overrideName.isEmpty() ? hintChannel.trimmed() : overrideName;

    if (target.isEmpty())
    {
        // Try currently logged-in Twitch user
        auto current = getApp()->getAccounts()->twitch.getCurrent();
        if (current && !current->isAnon())
        {
            target = current->getUserName();
        }
    }

    if (target.isEmpty())
    {
        return nullptr;
    }

    // Check Kick prefix or find on Kick
    if (target.startsWith("kick:", Qt::CaseInsensitive))
    {
        QString slug = target.mid(5);
        if (auto *app = getApp())
        {
            if (auto *kick = app->getKickChatServer())
            {
                return kick->findBySlug(slug);
            }
        }
    }

    // Try Twitch channel
    if (auto *app = getApp())
    {
        if (auto *twitch = app->getTwitch())
        {
            auto chan = twitch->getChannelOrEmpty(target);
            if (!chan->isEmpty())
            {
                return chan;
            }
        }

        // Try Kick fallback
        if (auto *kick = app->getKickChatServer())
        {
            auto chan = kick->findBySlug(target);
            if (chan && !chan->isEmpty())
            {
                return chan;
            }
        }
    }

    return nullptr;
}

void BlerpController::postInlineNotice(const BlerpEvent &event)
{
    auto chan = this->resolveTargetChannel(event.channelName);
    if (!chan || chan->isEmpty())
    {
        return;
    }

    QString noticeText = QString("🔊 [Blerp] %1").arg(event.summary());
    if (!event.blerpUrl.isEmpty())
    {
        noticeText += " " + event.blerpUrl;
    }

    auto msg = makeSystemMessage(noticeText);
    chan->addMessage(msg, MessageContext::Original);
}

void BlerpController::playLocalChime()
{
    if (!getSettings()->blerpPlayLocalChime.getValue())
    {
        return;
    }

    if (getApp()->getStreamerMode()->isEnabled() &&
        getSettings()->streamerModeMuteMentions)
    {
        return;
    }

    QString soundId = getSettings()->blerpChimeSound.getValue();
    QUrl soundUrl = ViewerSoundController::resolveSoundUrl(soundId);
    if (soundUrl.isEmpty())
    {
        return;
    }

    float vol = std::clamp(
        getSettings()->blerpChimeVolume.getValue() / 100.0F, 0.0F, 1.0F);

    postToThread([soundUrl, vol]() {
        if (auto *app = getApp())
        {
            if (auto *sound = app->getSound())
            {
                sound->play(soundUrl, vol);
            }
        }
    });
}

void BlerpController::simulateTestBlerp()
{
    BlerpEvent test;
    test.platform = "Blerp";
    test.channelName =
        getSettings()->blerpTargetChannelOverride.getValue().trimmed();
    test.username = "TestViewer";
    test.soundTitle = "Emotional Damage";
    test.triggerSource = "100 Bits";
    test.blerpUrl = "https://blerp.com";

    this->recordBlerp(test, true);
}

}  // namespace chatterino
