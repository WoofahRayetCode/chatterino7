// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "controllers/notifications/ViewerSoundRule.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QLineEdit>
#include <QSlider>
#include <QSpinBox>

namespace chatterino {

class ViewerSoundDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ViewerSoundDialog(const ViewerSoundRule &initialRule,
                              bool isNewRule,
                              const QString &currentChannel = QString(),
                              QWidget *parent = nullptr);

    ViewerSoundRule rule() const;
    bool wasRemoved() const;

private:
    void populateSoundCombo(QComboBox *combo, const QString &currentSoundId);
    QString getSoundIdFromCombo(QComboBox *combo) const;
    void testSound(const QString &soundId, int volume);

    QLineEdit *usernameEdit_ = nullptr;
    QLineEdit *channelEdit_ = nullptr;
    QCheckBox *enabledCheck_ = nullptr;
    QCheckBox *playOnJoinCheck_ = nullptr;
    QComboBox *joinSoundCombo_ = nullptr;
    QCheckBox *playOnChatCheck_ = nullptr;
    QComboBox *chatSoundCombo_ = nullptr;
    QSlider *volumeSlider_ = nullptr;
    QSpinBox *cooldownSpin_ = nullptr;

    ViewerSoundRule rule_;
    bool wasRemoved_ = false;
};

}  // namespace chatterino
