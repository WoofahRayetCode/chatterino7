// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "controllers/blerp/BlerpClient.hpp"
#include "controllers/blerp/BlerpEvent.hpp"

#include <QDateTime>
#include <QObject>
#include <QString>

#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace chatterino {

class Channel;

class BlerpController : public QObject
{
    Q_OBJECT

public:
    explicit BlerpController(QObject *parent = nullptr);

    void initialize();

    BlerpClient &client();

    /// Record a blerp event with 5-second cross-source deduplication.
    /// If postChatNotice is true, injects an inline system message into target splits.
    void recordBlerp(const BlerpEvent &event, bool postChatNotice = true);

    const std::deque<BlerpEvent> &history() const;
    void clearHistory();

    /// Simulates a test blerp event to verify audio / chat notice
    void simulateTestBlerp();

    /// Format and inject inline notice into target channel
    void postInlineNotice(const BlerpEvent &event);

Q_SIGNALS:
    void eventRecorded(const BlerpEvent &event);
    void historyCleared();

private:
    std::shared_ptr<Channel> resolveTargetChannel(const QString &hintChannel);
    void playLocalChime();

    BlerpClient client_;
    std::deque<BlerpEvent> history_;
    static constexpr size_t MAX_HISTORY = 200;

    mutable std::mutex mutex_;
    std::unordered_map<QString, std::chrono::steady_clock::time_point>
        recentDedupeKeys_;
};

}  // namespace chatterino
