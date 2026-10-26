// SPDX-FileCopyrightText: 2024 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common/SignalVectorModel.hpp"

#include <QObject>

namespace chatterino {

class NotificationPage;

class ChatSoundChannelModel : public SignalVectorModel<QString>
{
public:
    explicit ChatSoundChannelModel(QObject *parent);

protected:
    // turn a vector item into a model row
    QString getItemFromRow(std::vector<QStandardItem *> &row,
                           const QString &original,
                           std::optional<QModelIndex> index,
                           std::optional<int> role) override;

    // turns a row in the model into a vector item
    void getRowFromItem(const QString &item,
                        std::vector<QStandardItem *> &row) override;

    friend class NotificationPage;
};

}  // namespace chatterino
