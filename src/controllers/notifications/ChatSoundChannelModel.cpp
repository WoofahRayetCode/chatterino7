// SPDX-FileCopyrightText: 2024 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "controllers/notifications/ChatSoundChannelModel.hpp"

#include "util/StandardItemHelper.hpp"

namespace chatterino {

ChatSoundChannelModel::ChatSoundChannelModel(QObject *parent)
    : SignalVectorModel<QString>(1, parent)
{
}

QString ChatSoundChannelModel::getItemFromRow(
    std::vector<QStandardItem *> &row, const QString & /*original*/,
    std::optional<QModelIndex> index, std::optional<int> role)
{
    return row[0]->data(Qt::DisplayRole).toString();
}

void ChatSoundChannelModel::getRowFromItem(const QString &item,
                                           std::vector<QStandardItem *> &row)
{
    setStringItem(row[0], item);
}

}  // namespace chatterino
