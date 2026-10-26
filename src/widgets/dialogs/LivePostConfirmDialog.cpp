// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/dialogs/LivePostConfirmDialog.hpp"

#include "controllers/social/SocialPostController.hpp"
#include "singletons/Settings.hpp"

#include <QCheckBox>
#include <QDateTime>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

namespace chatterino {

LivePostConfirmDialog::LivePostConfirmDialog(const LiveStreamContext &ctx,
                                             const QString &initialText,
                                             SocialPostController *controller,
                                             QWidget *parent)
    : QDialog(parent)
    , context_(ctx)
    , controller_(controller)
{
    this->setWindowTitle(QStringLiteral("Going Live Announcement — %1 (%2)")
                             .arg(ctx.displayName, ctx.platform));
    this->resize(560, 420);
    this->setAttribute(Qt::WA_DeleteOnClose);

    auto *layout = new QVBoxLayout(this);

    // Stream info header
    this->headerLabel_ = new QLabel(
        QStringLiteral("<b>🔴 Stream Announcement:</b> %1 on %2")
            .arg(ctx.displayName.toHtmlEscaped(), ctx.platform.toHtmlEscaped()),
        this);
    this->headerLabel_->setTextFormat(Qt::RichText);
    layout->addWidget(this->headerLabel_);

    this->alreadyPostedLabel_ = new QLabel(
        QStringLiteral("<i>🔍 Checking if already posted to Bluesky / X...</i>"),
        this);
    this->alreadyPostedLabel_->setTextFormat(Qt::RichText);
    this->alreadyPostedLabel_->setWordWrap(true);
    layout->addWidget(this->alreadyPostedLabel_);

    // Stream metadata fields
    auto *metaForm = new QFormLayout();
    metaForm->setLabelAlignment(Qt::AlignRight);

    this->titleEdit_ = new QLineEdit(ctx.title, this);
    this->titleEdit_->setPlaceholderText(QStringLiteral("Stream title"));
    metaForm->addRow(QStringLiteral("Title:"), this->titleEdit_);

    auto *gameRowLayout = new QHBoxLayout();
    this->gameEdit_ = new QLineEdit(ctx.game, this);
    this->gameEdit_->setPlaceholderText(QStringLiteral("Game or Category"));
    gameRowLayout->addWidget(this->gameEdit_, 1);

    this->refreshButton_ =
        new QPushButton(QStringLiteral("🔄 Refresh from Stream"), this);
    this->refreshButton_->setToolTip(
        QStringLiteral("Fetch current title and game from the channel stream"));
    gameRowLayout->addWidget(this->refreshButton_);

    this->rerenderButton_ =
        new QPushButton(QStringLiteral("Re-apply Template"), this);
    this->rerenderButton_->setToolTip(
        QStringLiteral("Re-generate post text from template with current title & game"));
    gameRowLayout->addWidget(this->rerenderButton_);

    metaForm->addRow(QStringLiteral("Game:"), gameRowLayout);
    layout->addLayout(metaForm);

    layout->addSpacing(4);

    // Editor
    this->editor_ = new QPlainTextEdit(this);
    this->editor_->setPlainText(initialText);
    layout->addWidget(this->editor_);

    // Counters and destination options
    auto *optionsLayout = new QHBoxLayout();

    this->blueskyCheck_ = new QCheckBox("Post to Bluesky", this);
    this->blueskyCheck_->setChecked(getSettings()->socialPostBlueskyEnabled);

    this->twitterCheck_ = new QCheckBox(
        getSettings()->socialPostTwitterMode == 0
            ? "Post to X (Web Intent)"
            : "Post to X (API v2)",
        this);
    this->twitterCheck_->setChecked(getSettings()->socialPostTwitterEnabled);

    optionsLayout->addWidget(this->blueskyCheck_);
    optionsLayout->addWidget(this->twitterCheck_);
    optionsLayout->addStretch(1);

    this->counterLabel_ = new QLabel(this);
    optionsLayout->addWidget(this->counterLabel_);

    layout->addLayout(optionsLayout);

    // Status label for API responses / errors
    this->statusLabel_ = new QLabel(this);
    this->statusLabel_->setStyleSheet("color: #e06c75; font-weight: bold;");
    this->statusLabel_->setWordWrap(true);
    this->statusLabel_->hide();
    layout->addWidget(this->statusLabel_);

    // Button bar
    auto *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch(1);

    this->skipButton_ = new QPushButton("Skip Announcement", this);
    buttonLayout->addWidget(this->skipButton_);

    this->postButton_ = new QPushButton("Post Announcement", this);
    this->postButton_->setDefault(true);
    buttonLayout->addWidget(this->postButton_);

    layout->addLayout(buttonLayout);

    // Connections
    QObject::connect(this->editor_, &QPlainTextEdit::textChanged, this,
                     &LivePostConfirmDialog::updateCounters);
    QObject::connect(this->blueskyCheck_, &QCheckBox::toggled, this,
                     &LivePostConfirmDialog::updateCounters);
    QObject::connect(this->twitterCheck_, &QCheckBox::toggled, this,
                     &LivePostConfirmDialog::updateCounters);
    QObject::connect(this->refreshButton_, &QPushButton::clicked, this,
                     &LivePostConfirmDialog::refreshFromStream);
    QObject::connect(this->rerenderButton_, &QPushButton::clicked, this,
                     &LivePostConfirmDialog::rerenderTemplate);
    QObject::connect(this->skipButton_, &QPushButton::clicked, this,
                     &QDialog::reject);
    QObject::connect(this->postButton_, &QPushButton::clicked, this,
                     &LivePostConfirmDialog::sendPost);

    this->updateCounters();
    this->checkAlreadyPosted();
}

void LivePostConfirmDialog::checkAlreadyPosted()
{
    if (!this->controller_)
    {
        return;
    }

    QPointer<LivePostConfirmDialog> self(this);
    this->controller_->checkPostStatus(
        this->context_,
        [self](const SocialPostController::PostStatusReport &report) {
            if (!self)
            {
                return;
            }
            QString bskyColor =
                report.blueskyAlreadyPosted ? "#98c379" : "#abb2bf";
            QString xColor =
                report.twitterAlreadyPosted ? "#98c379" : "#abb2bf";

            self->alreadyPostedLabel_->setText(
                QStringLiteral(
                    "<b>Status:</b> <span style='color:%1'>Bluesky: %2 %3</span>"
                    " &nbsp;|&nbsp; <span style='color:%4'>X: %5 %6</span>")
                    .arg(bskyColor,
                         report.blueskyAlreadyPosted ? "✅" : "⚪",
                         report.blueskyDetail.toHtmlEscaped(), xColor,
                         report.twitterAlreadyPosted ? "✅" : "⚪",
                         report.twitterDetail.toHtmlEscaped()));

            if (report.blueskyAlreadyPosted && self->blueskyCheck_)
            {
                self->blueskyCheck_->setChecked(false);
            }
            if (report.twitterAlreadyPosted && self->twitterCheck_)
            {
                self->twitterCheck_->setChecked(false);
            }
            self->updateCounters();
        });
}

void LivePostConfirmDialog::closeEvent(QCloseEvent *event)
{
    Q_EMIT this->closed();
    QDialog::closeEvent(event);
}

void LivePostConfirmDialog::updateCounters()
{
    QString text = this->editor_->toPlainText();
    int bskyGraphemes = LivePostTemplate::countBlueskyGraphemes(text);
    int xWeighted = LivePostTemplate::countTwitterWeighted(text);

    QString bskyColor = bskyGraphemes > 300 ? "#e06c75" : "#abb2bf";
    QString xColor = xWeighted > 280 ? "#e06c75" : "#abb2bf";

    this->counterLabel_->setText(
        QStringLiteral("<span style='color:%1'>X: %2/280</span> | "
                       "<span style='color:%3'>Bluesky: %4/300</span>")
            .arg(xColor, QString::number(xWeighted), bskyColor,
                 QString::number(bskyGraphemes)));

    bool canPost = (this->blueskyCheck_->isChecked() ||
                    this->twitterCheck_->isChecked()) &&
                   !text.trimmed().isEmpty();
    this->postButton_->setEnabled(canPost);
}

void LivePostConfirmDialog::updateStreamMetadata(const QString &title,
                                                const QString &game)
{
    if (!title.isEmpty())
    {
        this->context_.title = title;
        if (this->titleEdit_)
        {
            this->titleEdit_->setText(title);
        }
    }

    if (!game.isEmpty())
    {
        this->context_.game = game;
        if (this->gameEdit_)
        {
            this->gameEdit_->setText(game);
        }
    }

    this->rerenderTemplate();
}

void LivePostConfirmDialog::refreshFromStream()
{
    if (!this->controller_)
    {
        return;
    }

    this->refreshButton_->setEnabled(false);
    this->refreshButton_->setText(QStringLiteral("Fetching..."));

    QPointer<LivePostConfirmDialog> self(this);
    this->controller_->fetchLiveStreamMetadata(
        this->context_.platform, this->context_.channelName,
        [self](const QString &title, const QString &game) {
            if (!self)
            {
                return;
            }
            self->refreshButton_->setEnabled(true);
            self->refreshButton_->setText(
                QStringLiteral("🔄 Refresh from Stream"));

            if (!title.isEmpty() || !game.isEmpty())
            {
                self->updateStreamMetadata(title, game);
            }
        });
}

void LivePostConfirmDialog::rerenderTemplate()
{
    if (this->titleEdit_)
    {
        this->context_.title = this->titleEdit_->text().trimmed();
    }
    if (this->gameEdit_)
    {
        this->context_.game = this->gameEdit_->text().trimmed();
    }

    QString rendered = LivePostTemplate::render(
        getSettings()->socialPostTemplate.getValue(), this->context_);
    this->editor_->setPlainText(rendered);
}

void LivePostConfirmDialog::sendPost()
{
    if (this->titleEdit_)
    {
        this->context_.title = this->titleEdit_->text().trimmed();
    }
    if (this->gameEdit_)
    {
        this->context_.game = this->gameEdit_->text().trimmed();
    }

    QString text = this->editor_->toPlainText().trimmed();
    bool postBsky = this->blueskyCheck_->isChecked();
    bool postX = this->twitterCheck_->isChecked();

    if (!postBsky && !postX)
    {
        return;
    }

    this->postButton_->setEnabled(false);
    this->skipButton_->setEnabled(false);
    this->statusLabel_->setStyleSheet("color: #61afef; font-weight: normal;");
    this->statusLabel_->setText("Publishing announcement...");
    this->statusLabel_->show();

    this->controller_->publishAll(
        text, postBsky, postX, this->context_,
        [this](bool bskyOk, const QString &bskyMsg, bool xOk,
               const QString &xMsg) {
            bool allSucceeded = true;
            QStringList errors;

            if (this->blueskyCheck_->isChecked())
            {
                if (!bskyOk)
                {
                    allSucceeded = false;
                    errors.append(bskyMsg);
                }
            }

            if (this->twitterCheck_->isChecked())
            {
                if (!xOk)
                {
                    allSucceeded = false;
                    errors.append(xMsg);
                }
            }

            if (allSucceeded)
            {
                // Update persistent deduplication state
                if (!this->context_.streamId.isEmpty())
                {
                    getSettings()->socialPostLastTwitchStreamId.setValue(
                        this->context_.streamId);
                }
                getSettings()->socialPostLastPostedEpochSec.setValue(
                    static_cast<int>(QDateTime::currentSecsSinceEpoch()));

                this->accept();
            }
            else
            {
                this->postButton_->setEnabled(true);
                this->skipButton_->setEnabled(true);
                this->statusLabel_->setStyleSheet(
                    "color: #e06c75; font-weight: bold;");
                this->statusLabel_->setText("Errors: " + errors.join(" | "));
                this->statusLabel_->show();
            }
        });
}

}  // namespace chatterino
