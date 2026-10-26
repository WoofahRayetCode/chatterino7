// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "controllers/social/LivePostTemplate.hpp"

#include <QDialog>
#include <QLineEdit>

class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;

namespace chatterino {

class SocialPostController;

class LivePostConfirmDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LivePostConfirmDialog(const LiveStreamContext &ctx,
                                   const QString &initialText,
                                   SocialPostController *controller,
                                   QWidget *parent = nullptr);

    void updateStreamMetadata(const QString &title, const QString &game);

Q_SIGNALS:
    void closed();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void updateCounters();
    void sendPost();
    void refreshFromStream();
    void rerenderTemplate();
    void checkAlreadyPosted();

    LiveStreamContext context_;
    SocialPostController *controller_{};

    QLabel *headerLabel_{};
    QLabel *alreadyPostedLabel_{};
    QLineEdit *titleEdit_{};
    QLineEdit *gameEdit_{};
    QPushButton *refreshButton_{};
    QPushButton *rerenderButton_{};

    QPlainTextEdit *editor_{};
    QLabel *counterLabel_{};
    QLabel *statusLabel_{};
    QCheckBox *blueskyCheck_{};
    QCheckBox *twitterCheck_{};
    QPushButton *postButton_{};
    QPushButton *skipButton_{};
};

}  // namespace chatterino
