// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "controllers/blerp/BlerpClient.hpp"

#include "util/PostToThread.hpp"

#include <QPointer>
#include <QUrl>

namespace chatterino {

struct BlerpWebSocketListener : public WebSocketListener {
    explicit BlerpWebSocketListener(QPointer<BlerpClient> client)
        : client_(std::move(client))
    {
    }

    void onOpen() override
    {
        runInGuiThread([client = this->client_]() {
            if (client)
            {
                client->handleOpen();
            }
        });
    }

    void onTextMessage(QByteArray data) override
    {
        runInGuiThread([client = this->client_, data = std::move(data)]() {
            if (client)
            {
                client->handleTextMessage(data);
            }
        });
    }

    void onBinaryMessage(QByteArray data) override
    {
        // Engine.IO binary messages not used for Blerp alerts
        (void)data;
    }

    void onClose(std::unique_ptr<WebSocketListener> /* self */) override
    {
        runInGuiThread([client = this->client_]() {
            if (client)
            {
                client->handleClose();
            }
        });
    }

    QPointer<BlerpClient> client_;
};

BlerpClient::BlerpClient(QObject *parent)
    : QObject(parent)
{
    this->reconnectTimer_.setSingleShot(true);
    connect(&this->reconnectTimer_, &QTimer::timeout, this,
            &BlerpClient::connectSocket);
}

BlerpClient::~BlerpClient()
{
    this->disconnectSocket();
}

void BlerpClient::setBrowserSourceUrl(const QString &urlOrToken)
{
    QString token = BlerpPayloadParser::extractRoomToken(urlOrToken);
    if (this->roomToken_ == token)
    {
        return;
    }

    this->roomToken_ = token;
    this->reconnectAttempts_ = 0;
    this->reconnectTimer_.stop();
    this->disconnectSocket();

    if (!this->roomToken_.isEmpty())
    {
        this->connectSocket();
    }
}

void BlerpClient::connectSocket()
{
    if (this->roomToken_.isEmpty())
    {
        this->state_ = State::Disconnected;
        Q_EMIT this->stateChanged(this->state_, "No Blerp room token set");
        return;
    }

    this->disconnectSocket();

    this->state_ = State::Connecting;
    Q_EMIT this->stateChanged(this->state_, "Connecting to obs.blerp.com...");

    QString rawUrl =
        QString("wss://obs.blerp.com/socket.io/?roomId=%1&EIO=4&transport=websocket")
            .arg(this->roomToken_);

    WebSocketOptions opts;
    opts.url = QUrl(rawUrl);

    auto listener = std::make_unique<BlerpWebSocketListener>(this);
    this->socket_ = this->pool_.createSocket(opts, std::move(listener));
}

void BlerpClient::disconnectSocket()
{
    this->socket_.close();
    if (this->state_ != State::Disconnected)
    {
        this->state_ = State::Disconnected;
        Q_EMIT this->stateChanged(this->state_, "Disconnected");
    }
}

BlerpClient::State BlerpClient::state() const
{
    return this->state_;
}

QString BlerpClient::currentRoomToken() const
{
    return this->roomToken_;
}

void BlerpClient::handleOpen()
{
    this->reconnectAttempts_ = 0;
    // Send Socket.IO namespace connection
    this->socket_.sendText("40");
}

void BlerpClient::handleTextMessage(const QByteArray &data)
{
    if (data.isEmpty())
    {
        return;
    }

    // Engine.IO Open packet
    if (data[0] == '0')
    {
        this->socket_.sendText("40");
        this->state_ = State::Connected;
        Q_EMIT this->stateChanged(this->state_, "Connected to Blerp overlay room");
        return;
    }

    // Engine.IO Ping packet -> reply Pong
    if (data[0] == '2')
    {
        this->socket_.sendText("3");
        return;
    }

    // Socket.IO Connect ACK
    if (data.startsWith("40"))
    {
        this->state_ = State::Connected;
        Q_EMIT this->stateChanged(this->state_, "Connected to Blerp overlay room");
        return;
    }

    // Socket.IO Event
    if (data.startsWith("42"))
    {
        // Check for heartbeat ack requirement (e.g. "4215[...]")
        int idx = 2;
        QByteArray ackId;
        while (idx < data.size() && data[idx] >= '0' && data[idx] <= '9')
        {
            ackId.append(data[idx]);
            idx++;
        }
        if (!ackId.isEmpty() && data.contains("heart_beat_check"))
        {
            this->socket_.sendText("43" + ackId + "[]");
        }

        auto ev = BlerpPayloadParser::parseEngineIoPacket(data);
        if (ev.has_value())
        {
            Q_EMIT this->blerpReceived(*ev);
        }
    }
}

void BlerpClient::handleClose()
{
    if (this->state_ != State::Disconnected)
    {
        this->state_ = State::Disconnected;
        Q_EMIT this->stateChanged(this->state_, "Connection closed");
    }

    this->scheduleReconnect();
}

void BlerpClient::scheduleReconnect()
{
    if (this->roomToken_.isEmpty())
    {
        return;
    }

    // Exponential backoff: 2s, 5s, 15s, 30s
    int delays[] = {2000, 5000, 15000, 30000};
    int delay = delays[std::min(this->reconnectAttempts_, 3)];
    this->reconnectAttempts_++;

    this->reconnectTimer_.start(delay);
}

}  // namespace chatterino
