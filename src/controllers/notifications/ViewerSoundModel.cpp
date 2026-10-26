// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "controllers/notifications/ViewerSoundModel.hpp"

#include "util/StandardItemHelper.hpp"

namespace chatterino {

ViewerSoundModel::ViewerSoundModel(QObject *parent)
    : SignalVectorModel<ViewerSoundRule>(ColCount, parent)
{
}

ViewerSoundRule ViewerSoundModel::getItemFromRow(
    std::vector<QStandardItem *> &row, const ViewerSoundRule &original,
    std::optional<QModelIndex> index, std::optional<int> role)
{
    ViewerSoundRule rule = original;

    rule.enabled = row[ColEnabled]->checkState() == Qt::Checked;
    rule.username = row[ColUsername]->data(Qt::EditRole).toString().trimmed();
    rule.channelFilter =
        row[ColChannel]->data(Qt::EditRole).toString().trimmed();
    rule.playOnJoin = row[ColPlayOnJoin]->checkState() == Qt::Checked;
    rule.joinSound = row[ColJoinSound]->data(Qt::EditRole).toString().trimmed();
    rule.playOnChat = row[ColPlayOnChat]->checkState() == Qt::Checked;
    rule.chatSound = row[ColChatSound]->data(Qt::EditRole).toString().trimmed();

    bool ok = false;
    int vol = row[ColVolume]->data(Qt::EditRole).toInt(&ok);
    if (ok)
    {
        rule.volume = std::clamp(vol, 0, 100);
    }

    int cd = row[ColCooldown]->data(Qt::EditRole).toInt(&ok);
    if (ok)
    {
        rule.cooldownSecs = std::clamp(cd, 0, 3600);
    }

    return rule;
}

void ViewerSoundModel::getRowFromItem(const ViewerSoundRule &item,
                                     std::vector<QStandardItem *> &row)
{
    setBoolItem(row[ColEnabled], item.enabled);
    setStringItem(row[ColUsername], item.username, false);
    setStringItem(row[ColChannel],
                  item.channelFilter.isEmpty() ? "Any" : item.channelFilter,
                  false);
    setBoolItem(row[ColPlayOnJoin], item.playOnJoin, false);
    setStringItem(row[ColJoinSound], item.joinSound, false);
    setBoolItem(row[ColPlayOnChat], item.playOnChat, false);
    setStringItem(row[ColChatSound], item.chatSound, false);
    setStringItem(row[ColVolume], QString::number(item.volume) + "%", false);
    setStringItem(row[ColCooldown], QString::number(item.cooldownSecs) + "s",
                  false);
}

}  // namespace chatterino
