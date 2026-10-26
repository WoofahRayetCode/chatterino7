// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/settingspages/NotificationPage.hpp"

#include "Application.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "controllers/blerp/BlerpController.hpp"
#include "controllers/highlights/Sounds.hpp"
#include "controllers/notifications/ChatSoundChannelModel.hpp"
#include "controllers/notifications/NotificationController.hpp"
#include "controllers/notifications/NotificationModel.hpp"
#include "controllers/notifications/ViewerSoundController.hpp"
#include "controllers/notifications/ViewerSoundModel.hpp"
#include "controllers/social/SocialPostController.hpp"
#include "controllers/sound/ISoundController.hpp"
#include "widgets/dialogs/BlerpHistoryDialog.hpp"
#include "widgets/dialogs/ViewerSoundDialog.hpp"
#include "singletons/Settings.hpp"
#include "singletons/Toasts.hpp"
#include "util/LayoutCreator.hpp"
#include "util/Twitch.hpp"
#include "widgets/helper/EditableModelView.hpp"

#include <QCheckBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QTableView>
#include <QTimer>

namespace chatterino {

NotificationPage::NotificationPage()
{
    LayoutCreator<NotificationPage> layoutCreator(this);
    auto layout = layoutCreator.emplace<QVBoxLayout>().withoutMargin();
    {
        auto tabs = layout.emplace<QTabWidget>();
        {
            auto settings = tabs.appendTab(new QVBoxLayout, "Live Options");
            {
                settings.emplace<QLabel>(
                    "You can be informed when certain channels go live. You "
                    "must be logged in to receive live notifications."
                    "\nYou can be informed in multiple ways:");

                settings.append(this->createCheckBox(
                    "Flash taskbar", getSettings()->notificationFlashTaskbar));
                settings.append(
                    this->createCheckBox("Play sound for selected channels",
                                         getSettings()->notificationPlaySound));
                settings.append(this->createCheckBox(
                    "Play sound for any channel going live",
                    getSettings()->notificationOnAnyChannel));

                settings.append(this->createCheckBox(
                    "Suppress live notifications on startup",
                    getSettings()->suppressInitialLiveNotification));
#if defined(Q_OS_WIN) || defined(CHATTERINO_WITH_LIBNOTIFY)
                settings.append(this->createCheckBox(
                    "Show notification", getSettings()->notificationToast));
#endif
#ifdef Q_OS_WIN
                settings.append(this->createCheckBox(
                    "Create start menu shortcut (requires "
                    "restart)",
                    getSettings()->createShortcutForToasts,
                    "When enabled, a shortcut will be created inside your "
                    "start menu folder if needed by live notifications."
                    "\n(On portable mode, this is disabled by "
                    "default)"));
#endif
#if defined(Q_OS_WIN) || defined(CHATTERINO_WITH_LIBNOTIFY)
                auto openIn = settings.emplace<QHBoxLayout>().withoutMargin();
                {
                    openIn
                        .emplace<QLabel>(
                            "Action when clicking on a notification:  ")
                        ->setSizePolicy(QSizePolicy::Maximum,
                                        QSizePolicy::Preferred);

                    // implementation of custom combobox done
                    // because addComboBox only can handle strings-settings
                    // int setting for the ToastReaction is desired
                    openIn.append(this->createToastReactionComboBox())
                        ->setSizePolicy(QSizePolicy::Maximum,
                                        QSizePolicy::Preferred);
                }
                openIn->setContentsMargins(40, 0, 0, 0);
                openIn->setSizeConstraint(QLayout::SetMaximumSize);
#endif
                auto customSound =
                    layout.emplace<QHBoxLayout>().withoutMargin();
                {
                    customSound.append(this->createCheckBox(
                        "Custom sound",
                        getSettings()->notificationCustomSound));
                    auto selectFile = customSound.emplace<QPushButton>(
                        "Select custom sound file");
                    QObject::connect(
                        selectFile.getElement(), &QPushButton::clicked, this,
                        [this] {
                            auto fileName = QFileDialog::getOpenFileName(
                                this, tr("Open Sound"), "",
                                tr("Audio Files (*.mp3 *.wav)"));
                            getSettings()->notificationPathSound = fileName;
                        });
                }

                settings->addStretch(1);
            }
            auto twitchChannels =
                tabs.appendTab(new QVBoxLayout, "Live Channels");
            {
                twitchChannels.emplace<QLabel>(
                    "These are the channels for which you will be informed "
                    "when they go live:");

                EditableModelView *view =
                    twitchChannels
                        .emplace<EditableModelView>(
                            getApp()->getNotifications()->createModel(
                                nullptr, Platform::Twitch))
                        .getElement();
                view->setTitles({"Twitch channels"});
                view->setValidationRegexp(twitchUserNameRegexp());

                view->getTableView()->horizontalHeader()->setSectionResizeMode(
                    QHeaderView::Fixed);
                view->getTableView()->horizontalHeader()->setSectionResizeMode(
                    0, QHeaderView::Stretch);

                QTimer::singleShot(1, [view] {
                    view->getTableView()->resizeColumnsToContents();
                    view->getTableView()->setColumnWidth(0, 200);
                });

                // We can safely ignore this signal connection since we own the view
                std::ignore = view->addButtonPressed.connect([] {
                    getApp()->getNotifications()->addChannelNotification(
                        "channel", Platform::Twitch);
                });
            }
            auto chatSounds = tabs.appendTab(new QVBoxLayout, "Chat Sounds");
            {
                chatSounds.emplace<QLabel>(
                    "Play sound notifications for incoming chat messages in "
                    "Twitch and Kick channels.");

                chatSounds.append(this->createCheckBox(
                    "Enable chat message sounds",
                    getSettings()->chatSoundEnabled));
                chatSounds.append(this->createCheckBox(
                    "Play sound for all channels",
                    getSettings()->chatSoundAllChannels));
                chatSounds.append(this->createCheckBox(
                    "Play sound even when Chatterino is focused",
                    getSettings()->chatSoundAlwaysPlay));
                chatSounds.append(this->createCheckBox(
                    "Ignore own messages", getSettings()->chatSoundIgnoreSelf));

                auto soundLayout =
                    chatSounds.emplace<QHBoxLayout>().withoutMargin();
                {
                    soundLayout
                        .emplace<QLabel>("Sound: ")
                        ->setSizePolicy(QSizePolicy::Maximum,
                                        QSizePolicy::Preferred);

                    auto *soundCombo = new QComboBox();
                    for (const auto &[soundID, defaultSound] :
                         highlights::defaultSounds())
                    {
                        soundCombo->addItem(defaultSound.displayName,
                                            QVariant::fromValue(defaultSound));
                    }
                    soundCombo->addItem("Custom sound...");

                    auto currentSoundId = getSettings()->chatSoundId.getValue();
                    if (auto d = highlights::resolveDefaultSound(currentSoundId))
                    {
                        soundCombo->setCurrentText(d->displayName);
                    }
                    else if (!currentSoundId.isEmpty())
                    {
                        QUrl currentSoundUrl(currentSoundId);
                        soundCombo->addItem(currentSoundUrl.fileName());
                        soundCombo->setCurrentText(currentSoundUrl.fileName());
                    }

                    int baseNumRows =
                        static_cast<int>(highlights::defaultSounds().size()) + 1;

                    auto *testSound = new QPushButton("Test sound");

                    QObject::connect(
                        soundCombo,
                        QOverload<int>::of(&QComboBox::currentIndexChanged),
                        [this, baseNumRows, soundCombo](int index) {
                            auto data = soundCombo->currentData();
                            if (data.canConvert<highlights::DefaultSound>())
                            {
                                auto defaultSound =
                                    data.value<highlights::DefaultSound>();
                                getSettings()->chatSoundId.setValue(
                                    defaultSound.id);
                                if (soundCombo->count() > baseNumRows)
                                {
                                    soundCombo->removeItem(baseNumRows);
                                }
                            }
                            else if (data.isNull())
                            {
                                auto fileUrl = QFileDialog::getOpenFileUrl(
                                    this, tr("Open Sound"), QUrl(),
                                    tr("Audio Files (*.mp3 *.wav)"));
                                if (fileUrl.isValid())
                                {
                                    getSettings()->chatSoundId.setValue(
                                        fileUrl.toString());
                                    QSignalBlocker block(soundCombo);
                                    if (soundCombo->count() > baseNumRows)
                                    {
                                        soundCombo->removeItem(baseNumRows);
                                    }
                                    soundCombo->addItem(fileUrl.fileName());
                                    soundCombo->setCurrentIndex(baseNumRows);
                                }
                                else
                                {
                                    auto current =
                                        getSettings()->chatSoundId.getValue();
                                    if (auto d = highlights::resolveDefaultSound(
                                            current))
                                    {
                                        soundCombo->setCurrentText(
                                            d->displayName);
                                    }
                                    else if (soundCombo->count() > baseNumRows)
                                    {
                                        soundCombo->setCurrentIndex(baseNumRows);
                                    }
                                }
                            }
                        });

                    QObject::connect(testSound, &QPushButton::clicked, [this]() {
                        auto current = getSettings()->chatSoundId.getValue();
                        QUrl url;
                        if (auto d = highlights::resolveDefaultSound(current))
                        {
                            url = QUrl(d->resourcePath);
                        }
                        else
                        {
                            url = QUrl::fromUserInput(current);
                        }
                        if (!url.isEmpty())
                        {
                            float vol =
                                std::clamp(
                                    getSettings()->chatSoundVolume.getValue(), 0,
                                    100) /
                                100.0F;
                            getApp()->getSound()->play(url, vol);
                        }
                    });

                    soundLayout.append(soundCombo);
                    soundLayout.append(testSound);
                }

                auto volumeLayout =
                    chatSounds.emplace<QHBoxLayout>().withoutMargin();
                {
                    volumeLayout
                        .emplace<QLabel>("Volume: ")
                        ->setSizePolicy(QSizePolicy::Maximum,
                                        QSizePolicy::Preferred);

                    auto *slider = new QSlider(Qt::Horizontal);
                    slider->setRange(0, 100);
                    slider->setValue(getSettings()->chatSoundVolume.getValue());

                    auto *volumeLabel =
                        new QLabel(QString::number(slider->value()) + "%");
                    volumeLabel->setFixedWidth(40);

                    QObject::connect(
                        slider, &QSlider::valueChanged, [volumeLabel](int val) {
                            getSettings()->chatSoundVolume.setValue(val);
                            volumeLabel->setText(QString::number(val) + "%");
                        });

                    volumeLayout.append(slider);
                    volumeLayout.append(volumeLabel);
                }

                auto cooldownLayout =
                    chatSounds.emplace<QHBoxLayout>().withoutMargin();
                {
                    cooldownLayout
                        .emplace<QLabel>("Spam cooldown (ms): ")
                        ->setSizePolicy(QSizePolicy::Maximum,
                                        QSizePolicy::Preferred);

                    auto *spin = new QSpinBox();
                    spin->setRange(0, 10000);
                    spin->setSingleStep(50);
                    spin->setValue(
                        getSettings()->chatSoundCooldownMs.getValue());

                    QObject::connect(
                        spin, QOverload<int>::of(&QSpinBox::valueChanged),
                        [](int val) {
                            getSettings()->chatSoundCooldownMs.setValue(val);
                        });

                    cooldownLayout.append(spin);
                    cooldownLayout->addStretch(1);
                }

                chatSounds.emplace<QLabel>(
                    "Channels with chat sounds enabled (when not set to all "
                    "channels):");

                EditableModelView *view =
                    chatSounds
                        .emplace<EditableModelView>(
                            (new ChatSoundChannelModel(nullptr))
                                ->initialized(
                                    &getSettings()->chatSoundChannels))
                        .getElement();
                view->setTitles({"Channel"});
                view->getTableView()->horizontalHeader()->setSectionResizeMode(
                    QHeaderView::Fixed);
                view->getTableView()->horizontalHeader()->setSectionResizeMode(
                    0, QHeaderView::Stretch);

                QTimer::singleShot(1, [view] {
                    view->getTableView()->resizeColumnsToContents();
                    view->getTableView()->setColumnWidth(0, 200);
                });

                std::ignore = view->addButtonPressed.connect([] {
                    getSettings()->chatSoundChannels.append("channel");
                });
            }

            auto goLiveTab =
                tabs.appendTab(new QVBoxLayout, "Go-Live Posts");
            {
                auto *scroll = new QScrollArea();
                scroll->setWidgetResizable(true);
                scroll->setFrameShape(QFrame::NoFrame);

                auto *contentWidget = new QWidget();
                auto *goLiveLayout = new QVBoxLayout(contentWidget);
                goLiveLayout->setContentsMargins(0, 0, 0, 0);
                goLiveLayout->setSpacing(12);

                goLiveLayout->addWidget(new QLabel(
                    "Automatically publish an announcement to Bluesky and/or X "
                    "(Twitter) when you go live on Twitch or Kick."));

                // General Settings Group
                auto *generalGroup = new QGroupBox("General Options");
                auto *generalLayout = new QVBoxLayout(generalGroup);

                generalLayout->addWidget(this->createCheckBox(
                    "Enable go-live social announcements",
                    getSettings()->socialPostEnabled));

                auto *triggerModeLayout = new QHBoxLayout();
                triggerModeLayout->addWidget(new QLabel("Action when live:"));
                auto *triggerCombo = new QComboBox();
                triggerCombo->addItem(
                    "Review & edit announcement popup before posting");
                triggerCombo->addItem("Post automatically in background");
                triggerCombo->setCurrentIndex(
                    getSettings()->socialPostTriggerMode.getValue());
                QObject::connect(
                    triggerCombo,
                    QOverload<int>::of(&QComboBox::currentIndexChanged),
                    [](int idx) {
                        getSettings()->socialPostTriggerMode.setValue(idx);
                    });
                triggerModeLayout->addWidget(triggerCombo);
                triggerModeLayout->addStretch(1);
                generalLayout->addLayout(triggerModeLayout);

                auto *cooldownLayout = new QHBoxLayout();
                cooldownLayout->addWidget(
                    new QLabel("Session cooldown (minutes):"));
                auto *cooldownSpin = new QSpinBox();
                cooldownSpin->setRange(0, 1440);
                cooldownSpin->setSingleStep(5);
                cooldownSpin->setValue(
                    getSettings()->socialPostCooldownMinutes.getValue());
                QObject::connect(
                    cooldownSpin,
                    QOverload<int>::of(&QSpinBox::valueChanged),
                    [](int val) {
                        getSettings()->socialPostCooldownMinutes.setValue(val);
                    });
                cooldownLayout->addWidget(cooldownSpin);
                cooldownLayout->addWidget(new QLabel(
                    "(prevents re-posting if OBS drops or when multistreaming)"));
                cooldownLayout->addStretch(1);
                generalLayout->addLayout(cooldownLayout);

                auto *channelTargetLayout = new QFormLayout();
                auto *twitchOverrideEdit = new QLineEdit();
                twitchOverrideEdit->setPlaceholderText(
                    "Auto-detect current logged-in Twitch user");
                twitchOverrideEdit->setText(
                    getSettings()->socialPostTwitchChannelOverride.getValue());
                QObject::connect(
                    twitchOverrideEdit, &QLineEdit::textChanged, [](const QString &t) {
                        getSettings()->socialPostTwitchChannelOverride.setValue(
                            t.trimmed());
                    });
                channelTargetLayout->addRow("Twitch Channel Override:",
                                            twitchOverrideEdit);

                auto *kickOverrideEdit = new QLineEdit();
                kickOverrideEdit->setPlaceholderText(
                    "Auto-detect current logged-in Kick user");
                kickOverrideEdit->setText(
                    getSettings()->socialPostKickChannelOverride.getValue());
                QObject::connect(
                    kickOverrideEdit, &QLineEdit::textChanged, [](const QString &t) {
                        getSettings()->socialPostKickChannelOverride.setValue(
                            t.trimmed());
                    });
                channelTargetLayout->addRow("Kick Channel Override:",
                                            kickOverrideEdit);
                generalLayout->addLayout(channelTargetLayout);

                goLiveLayout->addWidget(generalGroup);

                // Template Group
                auto *templateGroup = new QGroupBox("Announcement Template");
                auto *templateLayout = new QVBoxLayout(templateGroup);

                auto *templateEdit = new QPlainTextEdit();
                templateEdit->setPlainText(
                    getSettings()->socialPostTemplate.getValue());
                templateEdit->setMaximumHeight(90);
                QObject::connect(
                    templateEdit, &QPlainTextEdit::textChanged,
                    [templateEdit]() {
                        getSettings()->socialPostTemplate.setValue(
                            templateEdit->toPlainText());
                    });
                templateLayout->addWidget(templateEdit);

                auto *varLabel = new QLabel(
                    "Variables: <b>{platform}</b>, <b>{channel}</b>, "
                    "<b>{title}</b>, <b>{game}</b>, <b>{url}</b>, "
                    "<b>{twitch_url}</b>, <b>{kick_url}</b>");
                varLabel->setTextFormat(Qt::RichText);
                templateLayout->addWidget(varLabel);

                auto *btnRow = new QHBoxLayout();
                auto *bgPostBtn =
                    new QPushButton("Post Now in Background");
                auto *checkPostedBtn =
                    new QPushButton("🔍 Check If Already Posted");
                auto *previewBtn =
                    new QPushButton("Preview / Edit Dialog...");
                auto *bgStatusLabel = new QLabel();
                bgStatusLabel->setWordWrap(true);
                bgStatusLabel->hide();

                QObject::connect(
                    checkPostedBtn, &QPushButton::clicked,
                    [checkPostedBtn, bgStatusLabel]() {
                        if (auto *sp =
                                getApp()->getNotifications()->socialPosts())
                        {
                            checkPostedBtn->setEnabled(false);
                            bgStatusLabel->setStyleSheet("color: #61afef;");
                            bgStatusLabel->setText(
                                "Checking Bluesky & X post history...");
                            bgStatusLabel->show();

                            LiveStreamContext ctx;
                            QString twitchTarget = sp->resolveTwitchTarget();
                            QString kickTarget = sp->resolveKickTarget();
                            ctx.platform =
                                !twitchTarget.isEmpty() ? "Twitch" : "Kick";
                            ctx.channelName = !twitchTarget.isEmpty()
                                                  ? twitchTarget
                                                  : kickTarget;
                            ctx.url = ctx.platform == "Twitch"
                                          ? ("https://twitch.tv/" +
                                             ctx.channelName)
                                          : ("https://kick.com/" +
                                             ctx.channelName);

                            sp->checkPostStatus(
                                ctx,
                                [checkPostedBtn, bgStatusLabel](
                                    const SocialPostController::PostStatusReport
                                        &report) {
                                    checkPostedBtn->setEnabled(true);
                                    bool anyPosted =
                                        report.blueskyAlreadyPosted ||
                                        report.twitterAlreadyPosted;
                                    bgStatusLabel->setStyleSheet(
                                        anyPosted ? "color: #98c379; font-weight: bold;"
                                                  : "color: #e5c07b;");
                                    bgStatusLabel->setText(
                                        QStringLiteral(
                                            "Bluesky: %1 %2  |  X / Twitter: %3 %4")
                                            .arg(report.blueskyAlreadyPosted
                                                     ? "✅"
                                                     : "⚪",
                                                 report.blueskyDetail,
                                                 report.twitterAlreadyPosted
                                                     ? "✅"
                                                     : "⚪",
                                                 report.twitterDetail));
                                });
                        }
                    });

                QObject::connect(
                    bgPostBtn, &QPushButton::clicked,
                    [bgPostBtn, bgStatusLabel]() {
                        if (auto *sp =
                                getApp()->getNotifications()->socialPosts())
                        {
                            bgPostBtn->setEnabled(false);
                            bgStatusLabel->setStyleSheet("color: #61afef;");
                            bgStatusLabel->setText(
                                "Fetching live stream metadata & posting in background...");
                            bgStatusLabel->show();

                            QString twitchTarget = sp->resolveTwitchTarget();
                            QString kickTarget = sp->resolveKickTarget();
                            QString platform =
                                !twitchTarget.isEmpty() ? "Twitch" : "Kick";
                            QString chan = !twitchTarget.isEmpty()
                                               ? twitchTarget
                                               : kickTarget;

                            sp->fetchLiveStreamMetadata(
                                platform, chan,
                                [sp, platform, chan, twitchTarget, kickTarget,
                                 bgPostBtn, bgStatusLabel](
                                    const QString &title, const QString &game) {
                                    LiveStreamContext ctx;
                                    ctx.platform = platform;
                                    ctx.channelName = chan;
                                    ctx.displayName = chan;
                                    ctx.title = title;
                                    ctx.game = game;
                                    ctx.twitchUrl =
                                        !twitchTarget.isEmpty()
                                            ? "https://twitch.tv/" +
                                                  twitchTarget
                                            : "";
                                    ctx.kickUrl =
                                        !kickTarget.isEmpty()
                                            ? "https://kick.com/" + kickTarget
                                            : "";
                                    ctx.url = platform == "Twitch"
                                                  ? ctx.twitchUrl
                                                  : ctx.kickUrl;

                                    QString rendered = LivePostTemplate::render(
                                        getSettings()
                                            ->socialPostTemplate.getValue(),
                                        ctx);

                                    sp->checkPostStatus(
                                        ctx,
                                        [sp, ctx, rendered, bgPostBtn,
                                         bgStatusLabel, title, game](
                                            const SocialPostController::
                                                PostStatusReport &report) {
                                            bool wantBsky =
                                                getSettings()
                                                    ->socialPostBlueskyEnabled;
                                            bool wantX =
                                                getSettings()
                                                    ->socialPostTwitterEnabled;
                                            bool doBsky =
                                                wantBsky &&
                                                !report.blueskyAlreadyPosted;
                                            bool doX =
                                                wantX &&
                                                !report.twitterAlreadyPosted;

                                            if (!doBsky && !doX)
                                            {
                                                bgPostBtn->setEnabled(true);
                                                bgStatusLabel->setStyleSheet(
                                                    "color: #e5c07b; font-weight: bold;");
                                                bgStatusLabel->setText(
                                                    QStringLiteral(
                                                        "Skipped duplicate post — Bluesky: %1 | X: %2")
                                                        .arg(report
                                                                 .blueskyDetail,
                                                             report
                                                                 .twitterDetail));
                                                return;
                                            }

                                            sp->publishAll(
                                                rendered, doBsky, doX, ctx,
                                                [bgPostBtn, bgStatusLabel,
                                                 title, game, report, wantBsky,
                                                 wantX, doBsky, doX](
                                                    bool bOk,
                                                    const QString &bMsg,
                                                    bool xOk,
                                                    const QString &xMsg) {
                                                    bgPostBtn->setEnabled(true);
                                                    bool ok = bOk && xOk;
                                                    bgStatusLabel->setStyleSheet(
                                                        ok ? "color: #98c379;"
                                                           : "color: #e06c75;");
                                                    QStringList parts;
                                                    if (wantBsky && !doBsky)
                                                        parts << ("Bluesky: Skipped (" +
                                                                  report.blueskyDetail +
                                                                  ")");
                                                    else if (!bMsg.isEmpty())
                                                        parts << ("Bluesky: " +
                                                                  bMsg);

                                                    if (wantX && !doX)
                                                        parts << ("X: Skipped (" +
                                                                  report.twitterDetail +
                                                                  ")");
                                                    else if (!xMsg.isEmpty())
                                                        parts << ("X: " + xMsg);

                                                    bgStatusLabel->setText(
                                                        QStringLiteral(
                                                            "%1 (%2 — %3)")
                                                            .arg(parts.join(
                                                                     " | "),
                                                                 title, game));
                                                });
                                        });
                                });
                        }
                    });

                QObject::connect(previewBtn, &QPushButton::clicked, []() {
                    if (getApp()->getNotifications()->socialPosts())
                    {
                        getApp()
                            ->getNotifications()
                            ->socialPosts()
                            ->openManualPostDialog();
                    }
                });
                btnRow->addWidget(bgPostBtn);
                btnRow->addWidget(checkPostedBtn);
                btnRow->addWidget(previewBtn);
                templateLayout->addLayout(btnRow);
                templateLayout->addWidget(bgStatusLabel);

                goLiveLayout->addWidget(templateGroup);

                // Bluesky Group
                auto *bskyGroup = new QGroupBox("Bluesky (AT Protocol)");
                auto *bskyLayout = new QVBoxLayout(bskyGroup);

                bskyLayout->addWidget(this->createCheckBox(
                    "Enable Bluesky announcements",
                    getSettings()->socialPostBlueskyEnabled));

                auto *bskyForm = new QFormLayout();

                auto *bskyHandleEdit = new QLineEdit();
                bskyHandleEdit->setPlaceholderText("user.bsky.social");
                bskyHandleEdit->setText(
                    getSettings()->socialPostBlueskyHandle.getValue());
                QObject::connect(
                    bskyHandleEdit, &QLineEdit::textChanged, [](const QString &t) {
                        getSettings()->socialPostBlueskyHandle.setValue(
                            t.trimmed());
                    });
                bskyForm->addRow("Bluesky Handle:", bskyHandleEdit);

                auto *bskyPassEdit = new QLineEdit();
                bskyPassEdit->setEchoMode(QLineEdit::Password);
                bskyPassEdit->setPlaceholderText(
                    "Generated in Bluesky Settings > App Passwords");
                bskyPassEdit->setText(
                    getSettings()->socialPostBlueskyAppPassword.getValue());
                QObject::connect(
                    bskyPassEdit, &QLineEdit::textChanged, [](const QString &t) {
                        getSettings()->socialPostBlueskyAppPassword.setValue(
                            t.trimmed());
                    });
                bskyForm->addRow("App Password:", bskyPassEdit);

                auto *bskyPdsEdit = new QLineEdit();
                bskyPdsEdit->setText(
                    getSettings()->socialPostBlueskyPdsUrl.getValue());
                QObject::connect(
                    bskyPdsEdit, &QLineEdit::textChanged, [](const QString &t) {
                        getSettings()->socialPostBlueskyPdsUrl.setValue(
                            t.trimmed());
                    });
                bskyForm->addRow("PDS Server URL:", bskyPdsEdit);

                bskyLayout->addLayout(bskyForm);

                bskyLayout->addWidget(this->createCheckBox(
                    "Attach stream preview link card",
                    getSettings()->socialPostBlueskyEmbedCard));

                auto *bskyTestLayout = new QHBoxLayout();
                auto *testBskyBtn = new QPushButton("Test Bluesky Login");
                auto *bskyStatusLabel = new QLabel();
                QObject::connect(
                    testBskyBtn, &QPushButton::clicked,
                    [bskyStatusLabel]() {
                        bskyStatusLabel->setText("Connecting to Bluesky...");
                        auto *sp =
                            getApp()->getNotifications()->socialPosts();
                        if (sp)
                        {
                            sp->bluesky().verifyCredentials(
                                getSettings()
                                    ->socialPostBlueskyPdsUrl.getValue(),
                                getSettings()
                                    ->socialPostBlueskyHandle.getValue(),
                                getSettings()
                                    ->socialPostBlueskyAppPassword.getValue(),
                                [bskyStatusLabel](bool ok, const QString &msg) {
                                    bskyStatusLabel->setText(
                                        (ok ? "✓ " : "✗ ") + msg);
                                });
                        }
                    });
                bskyTestLayout->addWidget(testBskyBtn);
                bskyTestLayout->addWidget(bskyStatusLabel);
                bskyTestLayout->addStretch(1);
                bskyLayout->addLayout(bskyTestLayout);

                goLiveLayout->addWidget(bskyGroup);

                // Twitter / X Group
                auto *xGroup = new QGroupBox("Twitter / X");
                auto *xLayout = new QVBoxLayout(xGroup);

                xLayout->addWidget(this->createCheckBox(
                    "Enable Twitter / X announcements",
                    getSettings()->socialPostTwitterEnabled));

                auto *xHandleForm = new QFormLayout();
                auto *xHandleEdit = new QLineEdit();
                xHandleEdit->setPlaceholderText("KingManiacTech (without @)");
                if (auto *sp = getApp()->getNotifications()->socialPosts())
                {
                    xHandleEdit->setText(sp->twitterHandle());
                }
                QObject::connect(
                    xHandleEdit, &QLineEdit::textChanged,
                    [](const QString &t) {
                        if (auto *sp =
                                getApp()->getNotifications()->socialPosts())
                        {
                            sp->setTwitterHandle(t);
                        }
                    });
                xHandleForm->addRow("X Handle (for post verification):",
                                    xHandleEdit);
                xLayout->addLayout(xHandleForm);

                auto *xModeLayout = new QHBoxLayout();
                xModeLayout->addWidget(new QLabel("Mode:"));
                auto *xModeCombo = new QComboBox();
                xModeCombo->addItem(
                    "Free Browser Web Intent (no API keys required)");
                xModeCombo->addItem("Direct X API v2 (requires OAuth 1.0a keys)");
                xModeCombo->setCurrentIndex(
                    getSettings()->socialPostTwitterMode.getValue());
                xModeLayout->addWidget(xModeCombo);
                xModeLayout->addStretch(1);
                xLayout->addLayout(xModeLayout);

                auto *xKeyWidget = new QWidget();
                auto *xKeyForm = new QFormLayout(xKeyWidget);
                xKeyForm->setContentsMargins(0, 0, 0, 0);

                auto *apiKeyEdit = new QLineEdit();
                apiKeyEdit->setText(
                    getSettings()->socialPostTwitterApiKey.getValue());
                QObject::connect(apiKeyEdit, &QLineEdit::textChanged,
                                 [](const QString &t) {
                                     getSettings()->socialPostTwitterApiKey.setValue(
                                         t.trimmed());
                                 });
                xKeyForm->addRow("API Key (Consumer Key):", apiKeyEdit);

                auto *apiSecEdit = new QLineEdit();
                apiSecEdit->setEchoMode(QLineEdit::Password);
                apiSecEdit->setText(
                    getSettings()->socialPostTwitterApiSecret.getValue());
                QObject::connect(apiSecEdit, &QLineEdit::textChanged,
                                 [](const QString &t) {
                                     getSettings()->socialPostTwitterApiSecret.setValue(
                                         t.trimmed());
                                 });
                xKeyForm->addRow("API Secret (Consumer Secret):", apiSecEdit);

                auto *tokenEdit = new QLineEdit();
                tokenEdit->setText(
                    getSettings()->socialPostTwitterAccessToken.getValue());
                QObject::connect(tokenEdit, &QLineEdit::textChanged,
                                 [](const QString &t) {
                                     getSettings()->socialPostTwitterAccessToken.setValue(
                                         t.trimmed());
                                 });
                xKeyForm->addRow("Access Token:", tokenEdit);

                auto *tokenSecEdit = new QLineEdit();
                tokenSecEdit->setEchoMode(QLineEdit::Password);
                tokenSecEdit->setText(
                    getSettings()->socialPostTwitterAccessSecret.getValue());
                QObject::connect(tokenSecEdit, &QLineEdit::textChanged,
                                 [](const QString &t) {
                                     getSettings()->socialPostTwitterAccessSecret.setValue(
                                         t.trimmed());
                                 });
                xKeyForm->addRow("Access Token Secret:", tokenSecEdit);

                xLayout->addWidget(xKeyWidget);

                auto updateXKeyVisibility = [xKeyWidget](int mode) {
                    xKeyWidget->setVisible(mode == 1);
                };
                updateXKeyVisibility(
                    getSettings()->socialPostTwitterMode.getValue());

                QObject::connect(
                    xModeCombo,
                    QOverload<int>::of(&QComboBox::currentIndexChanged),
                    [updateXKeyVisibility](int idx) {
                        getSettings()->socialPostTwitterMode.setValue(idx);
                        updateXKeyVisibility(idx);
                    });

                goLiveLayout->addWidget(xGroup);

                goLiveLayout->addStretch(1);
                scroll->setWidget(contentWidget);
                goLiveTab.append(scroll);
            }
            auto viewerSoundsTab =
                tabs.appendTab(new QVBoxLayout, "Viewer Sounds");
            {
                viewerSoundsTab.emplace<QLabel>(
                    "Configure specific sounds for specific viewers when they "
                    "join and/or type in chat.");

                viewerSoundsTab.append(this->createCheckBox(
                    "Enable viewer-specific sounds",
                    getSettings()->viewerSoundsEnabled));
                viewerSoundsTab.append(this->createCheckBox(
                    "Enable First-Message (Walk-On) fallback when IRC JOIN is "
                    "not available (e.g. Kick or >1k chatters)",
                    getSettings()->viewerSoundsWalkOnFallback));
                viewerSoundsTab.append(this->createCheckBox(
                    "Play viewer sounds even when Chatterino is not focused",
                    getSettings()->viewerSoundsAlwaysPlay));

                auto *model = new ViewerSoundModel(this);
                model->initialize(&getSettings()->viewerSoundRules);

                auto *table = new QTableView();
                table->setModel(model);
                table->setSelectionBehavior(QAbstractItemView::SelectRows);
                table->setSelectionMode(QAbstractItemView::SingleSelection);
                table->horizontalHeader()->setStretchLastSection(true);
                table->horizontalHeader()->setSectionResizeMode(
                    QHeaderView::ResizeToContents);

                viewerSoundsTab.append(table);

                auto *btnLayout = new QHBoxLayout();
                auto *addBtn = new QPushButton("Add Viewer Rule...");
                auto *editBtn = new QPushButton("Edit Selected...");
                auto *removeBtn = new QPushButton("Remove Selected");
                auto *resetBtn = new QPushButton("Reset Session Walk-Ons");

                btnLayout->addWidget(addBtn);
                btnLayout->addWidget(editBtn);
                btnLayout->addWidget(removeBtn);
                btnLayout->addStretch(1);
                btnLayout->addWidget(resetBtn);

                QObject::connect(addBtn, &QPushButton::clicked, [this]() {
                    ViewerSoundRule rule;
                    ViewerSoundDialog dlg(rule, true, "", this);
                    if (dlg.exec() == QDialog::Accepted)
                    {
                        getSettings()->viewerSoundRules.append(dlg.rule());
                    }
                });

                auto editSelected = [this, table]() {
                    auto selection = table->selectionModel()->selectedRows();
                    if (selection.isEmpty())
                    {
                        return;
                    }
                    int row = selection.first().row();
                    auto rules = getSettings()->viewerSoundRules.raw();
                    if (row >= 0 && row < static_cast<int>(rules.size()))
                    {
                        ViewerSoundRule rule = rules[row];
                        ViewerSoundDialog dlg(rule, false, rule.channelFilter,
                                              this);
                        if (dlg.exec() == QDialog::Accepted)
                        {
                            if (dlg.wasRemoved())
                            {
                                getSettings()->viewerSoundRules.removeAt(row);
                            }
                            else
                            {
                                getSettings()->viewerSoundRules.removeAt(row);
                                getSettings()->viewerSoundRules.insert(
                                    dlg.rule(), row);
                            }
                        }
                    }
                };

                QObject::connect(editBtn, &QPushButton::clicked, editSelected);
                QObject::connect(table, &QTableView::doubleClicked,
                                 editSelected);

                QObject::connect(removeBtn, &QPushButton::clicked, [table]() {
                    auto selection = table->selectionModel()->selectedRows();
                    if (!selection.isEmpty())
                    {
                        int row = selection.first().row();
                        auto rules = getSettings()->viewerSoundRules.raw();
                        if (row >= 0 && row < static_cast<int>(rules.size()))
                        {
                            getSettings()->viewerSoundRules.removeAt(row);
                        }
                    }
                });

                QObject::connect(resetBtn, &QPushButton::clicked, []() {
                    if (auto *app = getApp())
                    {
                        if (auto *notifs = app->getNotifications())
                        {
                            if (auto *vs = notifs->viewerSounds())
                            {
                                vs->resetSessionState();
                            }
                        }
                    }
                });

                viewerSoundsTab.append(btnLayout);
            }
            auto blerpTab = tabs.appendTab(new QVBoxLayout, "Blerp");
            {
                auto *scroll = new QScrollArea();
                scroll->setWidgetResizable(true);
                scroll->setFrameShape(QFrame::NoFrame);

                auto *contentWidget = new QWidget();
                auto *blerpLayout = new QVBoxLayout(contentWidget);

                blerpLayout->addWidget(new QLabel(
                    "Surface Blerp sound alerts in real time through OBS "
                    "Browser Source WebSocket and chat/redemption triggers."));

                blerpLayout->addWidget(this->createCheckBox(
                    "Enable Blerp integration", getSettings()->blerpEnabled));

                // Direct Browser Source Group
                auto *directGroup = new QGroupBox(
                    "Streamer Setup (Direct OBS Browser Source WebSocket)");
                auto *directForm = new QFormLayout(directGroup);

                directForm->addRow(new QLabel(
                    "Paste your Blerp OBS Browser Source URL below to capture "
                    "every Blerp played on your stream (including bits, beets, "
                    "and extension clicks)."));

                auto *urlRow = new QHBoxLayout();
                auto *urlEdit = new QLineEdit();
                urlEdit->setEchoMode(QLineEdit::Password);
                urlEdit->setText(
                    getSettings()->blerpBrowserSourceUrl.getValue());
                urlEdit->setPlaceholderText("https://obs.blerp.com/pwn/<token>");

                auto *showBtn = new QPushButton("Show");
                showBtn->setCheckable(true);
                QObject::connect(showBtn, &QPushButton::toggled,
                                 [urlEdit, showBtn](bool checked) {
                                     urlEdit->setEchoMode(
                                         checked ? QLineEdit::Normal
                                                 : QLineEdit::Password);
                                     showBtn->setText(checked ? "Hide"
                                                              : "Show");
                                 });

                QObject::connect(
                    urlEdit, &QLineEdit::textChanged, [](const QString &t) {
                        getSettings()->blerpBrowserSourceUrl.setValue(
                            t.trimmed());
                    });

                urlRow->addWidget(urlEdit, 1);
                urlRow->addWidget(showBtn);
                directForm->addRow("Browser Source URL:", urlRow);

                auto *statusLabel = new QLabel("○ Disconnected");
                directForm->addRow("Connection Status:", statusLabel);

                if (auto *app = getApp())
                {
                    if (auto *notifs = app->getNotifications())
                    {
                        if (auto *bc = notifs->blerp())
                        {
                            auto updateStatus =
                                [statusLabel](BlerpClient::State s,
                                              const QString &msg) {
                                    if (s == BlerpClient::State::Connected)
                                    {
                                        statusLabel->setText(
                                            "● Connected to obs.blerp.com");
                                        statusLabel->setStyleSheet(
                                            "color: #44bb44;");
                                    }
                                    else if (s ==
                                             BlerpClient::State::Connecting)
                                    {
                                        statusLabel->setText(
                                            "◌ Connecting to obs.blerp.com...");
                                        statusLabel->setStyleSheet(
                                            "color: #ddaa00;");
                                    }
                                    else
                                    {
                                        statusLabel->setText("○ Disconnected (" +
                                                             msg + ")");
                                        statusLabel->setStyleSheet(
                                            "color: #888888;");
                                    }
                                };

                            updateStatus(bc->client().state(), "");
                            QObject::connect(&bc->client(),
                                             &BlerpClient::stateChanged,
                                             statusLabel, updateStatus);
                        }
                    }
                }

                auto *chanOverrideEdit = new QLineEdit();
                chanOverrideEdit->setText(
                    getSettings()->blerpTargetChannelOverride.getValue());
                chanOverrideEdit->setPlaceholderText(
                    "e.g. forsen or kick:forsen (leave empty for current channel)");
                QObject::connect(chanOverrideEdit, &QLineEdit::textChanged,
                                 [](const QString &t) {
                                     getSettings()
                                         ->blerpTargetChannelOverride.setValue(
                                             t.trimmed());
                                 });
                directForm->addRow("Target Channel Override:", chanOverrideEdit);
                blerpLayout->addWidget(directGroup);

                // Chat & Redemption Group
                auto *detectGroup = new QGroupBox(
                    "Chat Bot & Reward Detection (Any Channel)");
                auto *detectLayout = new QVBoxLayout(detectGroup);

                detectLayout->addWidget(this->createCheckBox(
                    "Detect @blerp bot chat announcements and reward redemptions",
                    getSettings()->blerpDetectChatAndRedemptions));

                auto *detectForm = new QFormLayout();
                auto *botNamesEdit = new QLineEdit(
                    getSettings()->blerpBotUsernames.getValue());
                QObject::connect(
                    botNamesEdit, &QLineEdit::textChanged, [](const QString &t) {
                        getSettings()->blerpBotUsernames.setValue(t.trimmed());
                    });
                detectForm->addRow("Bot Usernames (CSV):", botNamesEdit);

                auto *keywordsEdit = new QLineEdit(
                    getSettings()->blerpRewardKeywords.getValue());
                QObject::connect(
                    keywordsEdit, &QLineEdit::textChanged, [](const QString &t) {
                        getSettings()->blerpRewardKeywords.setValue(t.trimmed());
                    });
                detectForm->addRow("Reward Keywords (CSV):", keywordsEdit);
                detectLayout->addLayout(detectForm);
                blerpLayout->addWidget(detectGroup);

                // Chime Options
                auto *chimeGroup = new QGroupBox("Sound Alert (Local)");
                auto *chimeForm = new QFormLayout(chimeGroup);

                chimeForm->addRow(this->createCheckBox(
                    "Play subtle local chime when a Blerp alert is detected",
                    getSettings()->blerpPlayLocalChime));

                auto *chimeVolSlider = new QSlider(Qt::Horizontal);
                chimeVolSlider->setRange(0, 100);
                chimeVolSlider->setValue(
                    getSettings()->blerpChimeVolume.getValue());
                auto *chimeVolLabel = new QLabel(
                    QString::number(
                        getSettings()->blerpChimeVolume.getValue()) +
                    "%");
                QObject::connect(
                    chimeVolSlider, &QSlider::valueChanged,
                    [chimeVolLabel](int val) {
                        getSettings()->blerpChimeVolume.setValue(val);
                        chimeVolLabel->setText(QString::number(val) + "%");
                    });
                auto *chimeVolRow = new QHBoxLayout();
                chimeVolRow->addWidget(chimeVolSlider, 1);
                chimeVolRow->addWidget(chimeVolLabel);
                chimeForm->addRow("Chime Volume:", chimeVolRow);

                blerpLayout->addWidget(chimeGroup);

                // Action buttons
                auto *actionRow = new QHBoxLayout();
                auto *testNoticeBtn = new QPushButton("Test Blerp Notice");
                QObject::connect(testNoticeBtn, &QPushButton::clicked, []() {
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

                auto *viewHistoryBtn =
                    new QPushButton("View Recent Blerps (/blerps)");
                QObject::connect(viewHistoryBtn, &QPushButton::clicked, []() {
                    auto *dlg = new BlerpHistoryDialog();
                    dlg->setAttribute(Qt::WA_DeleteOnClose);
                    dlg->show();
                });

                actionRow->addWidget(testNoticeBtn);
                actionRow->addWidget(viewHistoryBtn);
                actionRow->addStretch(1);
                blerpLayout->addLayout(actionRow);

                blerpLayout->addStretch(1);
                scroll->setWidget(contentWidget);
                blerpTab.append(scroll);
            }
        }
    }
}
QComboBox *NotificationPage::createToastReactionComboBox()
{
    QComboBox *toastReactionOptions = new QComboBox();

    for (int i = 0; i <= static_cast<int>(ToastReaction::OpenInCustomPlayer);
         i++)
    {
        toastReactionOptions->insertItem(
            i, Toasts::findStringFromReaction(static_cast<ToastReaction>(i)));
    }

    // update when setting changes
    pajlada::Settings::Setting<int> setting = getSettings()->openFromToast;
    setting.connect(
        [toastReactionOptions](const int &index, auto) {
            toastReactionOptions->setCurrentIndex(index);
        },
        this->managedConnections_);

    QObject::connect(toastReactionOptions,
                     QOverload<int>::of(&QComboBox::currentIndexChanged),
                     [](const int &newValue) {
                         getSettings()->openFromToast.setValue(newValue);
                     });

    return toastReactionOptions;
}
}  // namespace chatterino
