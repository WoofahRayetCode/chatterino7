// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/dialogs/BlerpHistoryDialog.hpp"

#include "Application.hpp"
#include "controllers/blerp/BlerpController.hpp"
#include "controllers/notifications/NotificationController.hpp"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace chatterino {

BlerpHistoryDialog::BlerpHistoryDialog(QWidget *parent)
    : QDialog(parent)
{
    this->setWindowTitle("Recent Blerp Alerts");
    this->resize(700, 420);

    auto *layout = new QVBoxLayout(this);

    // Top control bar
    auto *topBar = new QHBoxLayout();
    this->filterEdit_ = new QLineEdit();
    this->filterEdit_->setPlaceholderText("Filter viewer, sound title, or channel...");
    connect(this->filterEdit_, &QLineEdit::textChanged, this,
            &BlerpHistoryDialog::rebuildTable);
    topBar->addWidget(this->filterEdit_, 1);

    auto *simBtn = new QPushButton("Simulate Test Blerp");
    connect(simBtn, &QPushButton::clicked, []() {
        if (auto *app = getApp())
        {
            if (auto *notifs = app->getNotifications())
            {
                if (auto *bc = notifs->blerp())
                {
                    bc->simulateTestBlerp();
                }
            }
        }
    });
    topBar->addWidget(simBtn);

    auto *clearBtn = new QPushButton("Clear");
    connect(clearBtn, &QPushButton::clicked, [this]() {
        if (auto *app = getApp())
        {
            if (auto *notifs = app->getNotifications())
            {
                if (auto *bc = notifs->blerp())
                {
                    bc->clearHistory();
                }
            }
        }
        this->rebuildTable();
    });
    topBar->addWidget(clearBtn);

    layout->addLayout(topBar);

    // Table view
    this->tableView_ = new QTableView();
    this->model_ = new QStandardItemModel(this);
    this->model_->setHorizontalHeaderLabels(
        {"Time", "Platform", "Channel", "Viewer", "Sound Title", "Source"});
    this->tableView_->setModel(this->model_);
    this->tableView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    this->tableView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    this->tableView_->horizontalHeader()->setStretchLastSection(true);
    this->tableView_->horizontalHeader()->setSectionResizeMode(
        4, QHeaderView::Stretch);

    connect(this->tableView_, &QTableView::doubleClicked,
            [this](const QModelIndex &idx) {
                if (!idx.isValid())
                {
                    return;
                }
                auto urlItem = this->model_->item(idx.row(), 4);
                if (urlItem)
                {
                    QString url = urlItem->data(Qt::UserRole).toString();
                    if (!url.isEmpty())
                    {
                        QDesktopServices::openUrl(QUrl(url));
                    }
                }
            });

    layout->addWidget(this->tableView_, 1);

    // Bottom close button
    auto *bottomBar = new QHBoxLayout();
    auto *statusLabel = new QLabel();

    if (auto *app = getApp())
    {
        if (auto *notifs = app->getNotifications())
        {
            if (auto *bc = notifs->blerp())
            {
                auto state = bc->client().state();
                QString stateStr = (state == BlerpClient::State::Connected)
                                       ? "● Connected to obs.blerp.com"
                                       : "○ Disconnected from obs.blerp.com";
                statusLabel->setText(stateStr);
            }
        }
    }
    bottomBar->addWidget(statusLabel);
    bottomBar->addStretch(1);

    auto *closeBtn = new QPushButton("Close");
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    bottomBar->addWidget(closeBtn);

    layout->addLayout(bottomBar);

    // Live update when new blerps arrive
    if (auto *app = getApp())
    {
        if (auto *notifs = app->getNotifications())
        {
            if (auto *bc = notifs->blerp())
            {
                connect(bc, &BlerpController::eventRecorded, this,
                        &BlerpHistoryDialog::rebuildTable);
            }
        }
    }

    this->rebuildTable();
}

void BlerpHistoryDialog::rebuildTable()
{
    this->model_->removeRows(0, this->model_->rowCount());

    if (auto *app = getApp())
    {
        if (auto *notifs = app->getNotifications())
        {
            if (auto *bc = notifs->blerp())
            {
                QString filter = this->filterEdit_->text().trimmed().toLower();
                for (const auto &ev : bc->history())
                {
                    if (!filter.isEmpty())
                    {
                        bool match =
                            ev.username.toLower().contains(filter) ||
                            ev.soundTitle.toLower().contains(filter) ||
                            ev.channelName.toLower().contains(filter);
                        if (!match)
                        {
                            continue;
                        }
                    }
                    this->addEventRow(ev);
                }
            }
        }
    }
}

void BlerpHistoryDialog::addEventRow(const BlerpEvent &ev)
{
    QList<QStandardItem *> items;

    items.append(new QStandardItem(ev.timestamp.toString("hh:mm:ss")));
    items.append(new QStandardItem(ev.platform));
    items.append(new QStandardItem(ev.channelName));
    items.append(new QStandardItem(ev.username));

    auto *titleItem = new QStandardItem(ev.soundTitle);
    if (!ev.blerpUrl.isEmpty())
    {
        titleItem->setData(ev.blerpUrl, Qt::UserRole);
        titleItem->setToolTip("Double-click to open: " + ev.blerpUrl);
    }
    items.append(titleItem);

    items.append(new QStandardItem(ev.triggerSource));

    this->model_->appendRow(items);
}

}  // namespace chatterino
