// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/dialogs/ViewerSoundDialog.hpp"

#include "Application.hpp"
#include "controllers/highlights/Sounds.hpp"
#include "controllers/notifications/ViewerSoundController.hpp"
#include "controllers/sound/ISoundController.hpp"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace chatterino {

ViewerSoundDialog::ViewerSoundDialog(const ViewerSoundRule &initialRule,
                                   bool isNewRule,
                                   const QString &currentChannel,
                                   QWidget *parent)
    : QDialog(parent)
    , rule_(initialRule)
{
    this->setWindowTitle(isNewRule ? "Add Viewer Sound Rule"
                                   : "Edit Viewer Sound Rule");
    this->setMinimumWidth(440);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout();

    this->usernameEdit_ = new QLineEdit(this->rule_.username);
    this->usernameEdit_->setPlaceholderText("Viewer username (e.g. forsen)");
    form->addRow("Username:", this->usernameEdit_);

    auto *channelRow = new QHBoxLayout();
    this->channelEdit_ = new QLineEdit(this->rule_.channelFilter);
    this->channelEdit_->setPlaceholderText("Empty = all channels");
    channelRow->addWidget(this->channelEdit_);

    if (!currentChannel.isEmpty())
    {
        auto *thisChanBtn = new QPushButton("Use Current Channel");
        connect(thisChanBtn, &QPushButton::clicked, [this, currentChannel]() {
            this->channelEdit_->setText(currentChannel);
        });
        channelRow->addWidget(thisChanBtn);
    }
    form->addRow("Channel Filter:", channelRow);

    this->enabledCheck_ = new QCheckBox("Rule Enabled");
    this->enabledCheck_->setChecked(this->rule_.enabled);
    form->addRow("", this->enabledCheck_);

    // Join Sound row
    auto *joinRow = new QHBoxLayout();
    this->playOnJoinCheck_ = new QCheckBox("Play on Join / Walk-On");
    this->playOnJoinCheck_->setChecked(this->rule_.playOnJoin);
    this->joinSoundCombo_ = new QComboBox();
    this->populateSoundCombo(this->joinSoundCombo_, this->rule_.joinSound);
    auto *testJoinBtn = new QPushButton("Test");
    connect(testJoinBtn, &QPushButton::clicked, [this]() {
        this->testSound(this->getSoundIdFromCombo(this->joinSoundCombo_),
                        this->volumeSlider_->value());
    });
    joinRow->addWidget(this->playOnJoinCheck_);
    joinRow->addWidget(this->joinSoundCombo_, 1);
    joinRow->addWidget(testJoinBtn);
    form->addRow("Join Sound:", joinRow);

    // Chat Sound row
    auto *chatRow = new QHBoxLayout();
    this->playOnChatCheck_ = new QCheckBox("Play on Chat Message");
    this->playOnChatCheck_->setChecked(this->rule_.playOnChat);
    this->chatSoundCombo_ = new QComboBox();
    this->populateSoundCombo(this->chatSoundCombo_, this->rule_.chatSound);
    auto *testChatBtn = new QPushButton("Test");
    connect(testChatBtn, &QPushButton::clicked, [this]() {
        this->testSound(this->getSoundIdFromCombo(this->chatSoundCombo_),
                        this->volumeSlider_->value());
    });
    chatRow->addWidget(this->playOnChatCheck_);
    chatRow->addWidget(this->chatSoundCombo_, 1);
    chatRow->addWidget(testChatBtn);
    form->addRow("Chat Sound:", chatRow);

    // Volume row
    auto *volRow = new QHBoxLayout();
    this->volumeSlider_ = new QSlider(Qt::Horizontal);
    this->volumeSlider_->setRange(0, 100);
    this->volumeSlider_->setValue(this->rule_.volume);
    auto *volLabel =
        new QLabel(QString::number(this->rule_.volume) + "%");
    connect(this->volumeSlider_, &QSlider::valueChanged, [volLabel](int val) {
        volLabel->setText(QString::number(val) + "%");
    });
    volRow->addWidget(this->volumeSlider_, 1);
    volRow->addWidget(volLabel);
    form->addRow("Volume:", volRow);

    // Cooldown row
    this->cooldownSpin_ = new QSpinBox();
    this->cooldownSpin_->setRange(0, 3600);
    this->cooldownSpin_->setSuffix(" seconds");
    this->cooldownSpin_->setValue(this->rule_.cooldownSecs);
    form->addRow("Chat Sound Cooldown:", this->cooldownSpin_);

    layout->addLayout(form);

    // Buttons
    auto *btnBox = new QHBoxLayout();
    if (!isNewRule)
    {
        auto *removeBtn = new QPushButton("Remove Rule");
        removeBtn->setStyleSheet("color: #ff5555;");
        connect(removeBtn, &QPushButton::clicked, [this]() {
            this->wasRemoved_ = true;
            this->accept();
        });
        btnBox->addWidget(removeBtn);
    }

    btnBox->addStretch(1);

    auto *saveBtn = new QPushButton("Save");
    saveBtn->setDefault(true);
    connect(saveBtn, &QPushButton::clicked, [this]() {
        this->rule_.username = this->usernameEdit_->text().trimmed();
        if (this->rule_.username.startsWith('@'))
        {
            this->rule_.username = this->rule_.username.mid(1);
        }
        this->rule_.channelFilter = this->channelEdit_->text().trimmed();
        this->rule_.enabled = this->enabledCheck_->isChecked();
        this->rule_.playOnJoin = this->playOnJoinCheck_->isChecked();
        this->rule_.joinSound =
            this->getSoundIdFromCombo(this->joinSoundCombo_);
        this->rule_.playOnChat = this->playOnChatCheck_->isChecked();
        this->rule_.chatSound =
            this->getSoundIdFromCombo(this->chatSoundCombo_);
        this->rule_.volume = this->volumeSlider_->value();
        this->rule_.cooldownSecs = this->cooldownSpin_->value();
        this->accept();
    });

    auto *cancelBtn = new QPushButton("Cancel");
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    btnBox->addWidget(saveBtn);
    btnBox->addWidget(cancelBtn);

    layout->addLayout(btnBox);
}

void ViewerSoundDialog::populateSoundCombo(QComboBox *combo,
                                           const QString &currentSoundId)
{
    combo->clear();
    for (const auto &[id, defaultSound] : highlights::defaultSounds())
    {
        combo->addItem(defaultSound.displayName,
                       QVariant::fromValue(defaultSound));
    }
    combo->addItem("Custom sound...");

    if (auto d = highlights::resolveDefaultSound(currentSoundId))
    {
        combo->setCurrentText(d->displayName);
    }
    else if (!currentSoundId.isEmpty())
    {
        QUrl url(currentSoundId);
        combo->addItem(url.fileName(), currentSoundId);
        combo->setCurrentText(url.fileName());
    }

    int baseNumRows = static_cast<int>(highlights::defaultSounds().size()) + 1;

    connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [this, combo, baseNumRows](int index) {
                auto data = combo->currentData();
                if (data.isNull() && combo->currentIndex() == baseNumRows - 1)
                {
                    auto fileUrl = QFileDialog::getOpenFileUrl(
                        this, "Open Sound File", QUrl(),
                        "Sound Files (*.wav *.mp3 *.ogg *.flac)");
                    if (!fileUrl.isEmpty())
                    {
                        QString path = fileUrl.toLocalFile();
                        combo->addItem(fileUrl.fileName(), path);
                        combo->setCurrentIndex(combo->count() - 1);
                    }
                }
            });
}

QString ViewerSoundDialog::getSoundIdFromCombo(QComboBox *combo) const
{
    auto data = combo->currentData();
    if (data.canConvert<highlights::DefaultSound>())
    {
        return data.value<highlights::DefaultSound>().id;
    }
    if (data.canConvert<QString>())
    {
        return data.toString();
    }
    return "004-yfjesse-notification";
}

void ViewerSoundDialog::testSound(const QString &soundId, int volume)
{
    QUrl url = ViewerSoundController::resolveSoundUrl(soundId);
    if (url.isEmpty())
    {
        return;
    }
    float vol = std::clamp(volume / 100.0F, 0.0F, 1.0F);
    if (auto *app = getApp())
    {
        if (auto *sound = app->getSound())
        {
            sound->play(url, vol);
        }
    }
}

ViewerSoundRule ViewerSoundDialog::rule() const
{
    return this->rule_;
}

bool ViewerSoundDialog::wasRemoved() const
{
    return this->wasRemoved_;
}

}  // namespace chatterino
