// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "controllers/blerp/BlerpEvent.hpp"

#include <QDialog>
#include <QLineEdit>
#include <QStandardItemModel>
#include <QTableView>

namespace chatterino {

class BlerpHistoryDialog : public QDialog
{
    Q_OBJECT

public:
    explicit BlerpHistoryDialog(QWidget *parent = nullptr);

private:
    void rebuildTable();
    void addEventRow(const BlerpEvent &ev);

    QLineEdit *filterEdit_ = nullptr;
    QTableView *tableView_ = nullptr;
    QStandardItemModel *model_ = nullptr;
};

}  // namespace chatterino
