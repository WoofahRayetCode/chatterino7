// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common/SignalVectorModel.hpp"
#include "controllers/notifications/ViewerSoundRule.hpp"

#include <QObject>

namespace chatterino {

class ViewerSoundModel : public SignalVectorModel<ViewerSoundRule>
{
public:
    explicit ViewerSoundModel(QObject *parent);

    enum Column {
        ColEnabled = 0,
        ColUsername = 1,
        ColChannel = 2,
        ColPlayOnJoin = 3,
        ColJoinSound = 4,
        ColPlayOnChat = 5,
        ColChatSound = 6,
        ColVolume = 7,
        ColCooldown = 8,
        ColCount = 9,
    };

protected:
    ViewerSoundRule getItemFromRow(std::vector<QStandardItem *> &row,
                                  const ViewerSoundRule &original,
                                  std::optional<QModelIndex> index,
                                  std::optional<int> role) override;

    void getRowFromItem(const ViewerSoundRule &item,
                        std::vector<QStandardItem *> &row) override;
};

}  // namespace chatterino
