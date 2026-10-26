// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common/websockets/WebSocketPool.hpp"
#include "controllers/blerp/BlerpEvent.hpp"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTimer>

#include <memory>

namespace chatterino {

class BlerpClient : public QObject
{
    Q_OBJECT

public:
    enum class State {
        Disconnected,
        Connecting,
        Connected,
        Error,
    };
    Q_ENUM(State)

    explicit BlerpClient(QObject *parent = nullptr);
    ~BlerpClient() override;

    void setBrowserSourceUrl(const QString &urlOrToken);
    void connectSocket();
    void disconnectSocket();

    State state() const;
    QString currentRoomToken() const;

Q_SIGNALS:
    void stateChanged(BlerpClient::State newState, const QString &message);
    void blerpReceived(const BlerpEvent &event);

private:
    void handleOpen();
    void handleTextMessage(const QByteArray &data);
    void handleClose();
    void scheduleReconnect();

    friend struct BlerpWebSocketListener;

    WebSocketPool pool_{"blerp"};
    WebSocketHandle socket_;
    State state_ = State::Disconnected;
    QString roomToken_;
    QTimer reconnectTimer_;
    int reconnectAttempts_ = 0;
};

}  // namespace chatterino
