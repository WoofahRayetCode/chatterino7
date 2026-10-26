// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "controllers/notifications/ViewerSoundController.hpp"

#include "Application.hpp"
#include "controllers/highlights/Sounds.hpp"
#include "controllers/sound/ISoundController.hpp"
#include "singletons/Settings.hpp"
#include "singletons/StreamerMode.hpp"
#include "singletons/WindowManager.hpp"
#include "util/PostToThread.hpp"

#include <QApplication>

#include <algorithm>

namespace chatterino {

ViewerSoundController::ViewerSoundController(QObject *parent)
    : QObject(parent)
{
}

void ViewerSoundController::markSelfJoinedChannel(const QString &platform,
                                                  const QString &channelName)
{
    std::lock_guard<std::mutex> lock(this->mutex_);
    QString key = platform.toLower() + ":" + channelName.toLower();
    this->selfJoinedAt_[key] = std::chrono::steady_clock::now();
}

void ViewerSoundController::resetSessionState(const QString &channelKey)
{
    std::lock_guard<std::mutex> lock(this->mutex_);
    if (channelKey.isEmpty())
    {
        this->announcedJoinsThisSession_.clear();
        this->lastChatSoundAt_.clear();
    }
    else
    {
        QString prefix = channelKey.toLower() + ":";
        for (auto it = this->announcedJoinsThisSession_.begin();
             it != this->announcedJoinsThisSession_.end();)
        {
            if (it->startsWith(prefix))
            {
                it = this->announcedJoinsThisSession_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
}

std::optional<ViewerSoundRule> ViewerSoundController::findMatchingRule(
    const QString &username, const QString &platform,
    const QString &channelName) const
{
    if (username.trimmed().isEmpty())
    {
        return std::nullopt;
    }

    auto rules = getSettings()->viewerSoundRules.readOnly();

    std::optional<ViewerSoundRule> channelSpecificRule;
    std::optional<ViewerSoundRule> globalRule;

    for (const auto &rule : *rules)
    {
        if (!rule.enabled || !rule.matchesUser(username))
        {
            continue;
        }

        if (!rule.channelFilter.trimmed().isEmpty())
        {
            if (rule.matchesChannel(platform, channelName))
            {
                channelSpecificRule = rule;
                break;  // Best possible match
            }
        }
        else
        {
            if (!globalRule.has_value())
            {
                globalRule = rule;
            }
        }
    }

    if (channelSpecificRule.has_value())
    {
        return channelSpecificRule;
    }
    return globalRule;
}

void ViewerSoundController::onViewerJoined(const QString &platform,
                                          const QString &channelName,
                                          const QString &username)
{
    if (!getSettings()->viewerSoundsEnabled.getValue())
    {
        return;
    }

    if (getSettings()->isBlacklistedUser(username))
    {
        return;
    }

    if (getApp()->getStreamerMode()->isEnabled() &&
        getSettings()->streamerModeMuteMentions)
    {
        return;
    }

    if (getSettings()->isMutedChannel(channelName))
    {
        return;
    }

    if (!getSettings()->viewerSoundsAlwaysPlay.getValue() &&
        QApplication::focusWidget() == nullptr)
    {
        return;
    }

    QString channelKey = platform.toLower() + ":" + channelName.toLower();
    QString sessionKey = channelKey + ":" + username.toLower();

    ViewerSoundRule ruleToPlay;
    {
        std::lock_guard<std::mutex> lock(this->mutex_);

        // 5-second grace window after client itself joined channel
        auto selfJoinIt = this->selfJoinedAt_.find(channelKey);
        if (selfJoinIt != this->selfJoinedAt_.end())
        {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - selfJoinIt->second);
            if (elapsed.count() < 5)
            {
                return;
            }
        }

        if (this->announcedJoinsThisSession_.contains(sessionKey))
        {
            return;
        }

        auto rule = this->findMatchingRule(username, platform, channelName);
        if (!rule.has_value() || !rule->playOnJoin)
        {
            return;
        }

        this->announcedJoinsThisSession_.insert(sessionKey);
        ruleToPlay = *rule;
    }

    this->playSound(ruleToPlay.joinSound, ruleToPlay.volume);
}

bool ViewerSoundController::handleViewerMessage(
    const QString &platform, const QString &channelName,
    const QString &username, bool isSelf, bool alreadyPlayedHighlightSound)
{
    if (!getSettings()->viewerSoundsEnabled.getValue())
    {
        return false;
    }

    if (isSelf && getSettings()->chatSoundIgnoreSelf.getValue())
    {
        return false;
    }

    if (getSettings()->isBlacklistedUser(username))
    {
        return false;
    }

    if (getApp()->getStreamerMode()->isEnabled() &&
        getSettings()->streamerModeMuteMentions)
    {
        return false;
    }

    if (getSettings()->isMutedChannel(channelName))
    {
        return false;
    }

    if (!getSettings()->viewerSoundsAlwaysPlay.getValue() &&
        QApplication::focusWidget() == nullptr)
    {
        return false;
    }

    auto rule = this->findMatchingRule(username, platform, channelName);
    if (!rule.has_value())
    {
        return false;
    }

    QString channelKey = platform.toLower() + ":" + channelName.toLower();
    QString sessionKey = channelKey + ":" + username.toLower();
    auto now = std::chrono::steady_clock::now();

    QString soundToPlay;
    int volumeToPlay = rule->volume;

    {
        std::lock_guard<std::mutex> lock(this->mutex_);

        // Check Walk-On Fallback first
        if (rule->playOnJoin &&
            getSettings()->viewerSoundsWalkOnFallback.getValue() &&
            !this->announcedJoinsThisSession_.contains(sessionKey))
        {
            this->announcedJoinsThisSession_.insert(sessionKey);
            this->lastChatSoundAt_[sessionKey] = now;
            soundToPlay = rule->joinSound;
        }
        else if (rule->playOnChat)
        {
            if (alreadyPlayedHighlightSound)
            {
                return true;  // Handled by highlight, suppress generic chat sound
            }

            auto lastPlayIt = this->lastChatSoundAt_.find(sessionKey);
            if (lastPlayIt != this->lastChatSoundAt_.end())
            {
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    now - lastPlayIt->second);
                if (elapsed.count() < rule->cooldownSecs)
                {
                    return true;  // On cooldown, but viewer rule matched so suppress generic sound
                }
            }

            this->lastChatSoundAt_[sessionKey] = now;
            soundToPlay = rule->chatSound;
        }
        else
        {
            return false;
        }
    }

    if (!soundToPlay.isEmpty())
    {
        this->playSound(soundToPlay, volumeToPlay);
        return true;
    }

    return false;
}

QUrl ViewerSoundController::resolveSoundUrl(const QString &soundId)
{
    if (auto def = highlights::resolveDefaultSound(soundId))
    {
        return QUrl(def->resourcePath);
    }
    return QUrl::fromUserInput(soundId);
}

void ViewerSoundController::playSound(const QString &soundId, int volume)
{
    QUrl soundUrl = resolveSoundUrl(soundId);
    if (soundUrl.isEmpty())
    {
        return;
    }

    float vol = std::clamp(volume / 100.0F, 0.0F, 1.0F);

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

void ViewerSoundController::upsertRule(const ViewerSoundRule &rule)
{
    auto rules = getSettings()->viewerSoundRules.raw();
    bool found = false;

    for (size_t i = 0; i < rules.size(); ++i)
    {
        if (rules[i].matchesUser(rule.username) &&
            QString::compare(rules[i].channelFilter.trimmed(),
                             rule.channelFilter.trimmed(),
                             Qt::CaseInsensitive) == 0)
        {
            getSettings()->viewerSoundRules.removeAt(static_cast<int>(i));
            getSettings()->viewerSoundRules.insert(rule, static_cast<int>(i));
            found = true;
            break;
        }
    }

    if (!found)
    {
        getSettings()->viewerSoundRules.append(rule);
    }
}

bool ViewerSoundController::removeRule(const QString &username,
                                      const QString &channelFilter)
{
    auto rules = getSettings()->viewerSoundRules.raw();
    for (size_t i = 0; i < rules.size(); ++i)
    {
        if (rules[i].matchesUser(username) &&
            QString::compare(rules[i].channelFilter.trimmed(),
                             channelFilter.trimmed(),
                             Qt::CaseInsensitive) == 0)
        {
            getSettings()->viewerSoundRules.removeAt(i);
            return true;
        }
    }
    return false;
}

}  // namespace chatterino
