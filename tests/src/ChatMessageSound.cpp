// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "singletons/Settings.hpp"
#include "singletons/Updates.hpp"
#include "controllers/sound/ISoundController.hpp"
#include "controllers/sound/NullBackend.hpp"
#include "mocks/EmptyApplication.hpp"
#include "Test.hpp"

using namespace chatterino;

namespace {

class ChatSoundTestApp : public mock::EmptyApplication
{
public:
    ChatSoundTestApp()
        : mock::EmptyApplication("{}")
        , settings(this->modes_, this->args_, this->settingsDir.path(),
                   {
                       .isTest = true,
                       .runMigrations = false,
                       .runCleanup = false,
                   })
        , updates(this->modes_, this->paths_, this->settings)
    {
    }

    Updates &getUpdates() override
    {
        return this->updates;
    }

    Settings settings;
    Updates updates;
};

}  // namespace

TEST(ChatMessageSound, DefaultSettings)
{
    ChatSoundTestApp app;

    EXPECT_TRUE(app.settings.chatSoundEnabled.getValue());
    EXPECT_FALSE(app.settings.chatSoundAllChannels.getValue());
    EXPECT_TRUE(app.settings.chatSoundAlwaysPlay.getValue());
    EXPECT_TRUE(app.settings.chatSoundIgnoreSelf.getValue());
    EXPECT_EQ(app.settings.chatSoundId.getValue(), "004-yfjesse-notification");
    EXPECT_EQ(app.settings.chatSoundVolume.getValue(), 50);
    EXPECT_EQ(app.settings.chatSoundCooldownMs.getValue(), 750);
}

TEST(ChatMessageSound, ChannelToggling)
{
    ChatSoundTestApp app;

    EXPECT_FALSE(app.settings.isChatSoundChannel("forsen"));
    EXPECT_FALSE(app.settings.isChatSoundChannel("FORSEN"));

    // Toggle on
    EXPECT_TRUE(app.settings.toggleChatSoundChannel("forsen"));
    EXPECT_TRUE(app.settings.isChatSoundChannel("forsen"));
    EXPECT_TRUE(app.settings.isChatSoundChannel("FORSEN"));

    // Toggle off
    EXPECT_FALSE(app.settings.toggleChatSoundChannel("FORSEN"));
    EXPECT_FALSE(app.settings.isChatSoundChannel("forsen"));

    // Explicit set on
    app.settings.setChatSoundChannel("xqc", true);
    EXPECT_TRUE(app.settings.isChatSoundChannel("xqc"));
    EXPECT_TRUE(app.settings.isChatSoundChannel("XQC"));

    // Explicit set off
    app.settings.setChatSoundChannel("XQC", false);
    EXPECT_FALSE(app.settings.isChatSoundChannel("xqc"));
}

TEST(ChatMessageSound, NullSoundBackendVolume)
{
    NullBackend backend;
    // Volume parameter should compile and execute cleanly
    backend.play(QUrl("qrc:/sounds/ping2.wav"), 0.5F);
    backend.play(QUrl("qrc:/sounds/ping2.wav"));
}
