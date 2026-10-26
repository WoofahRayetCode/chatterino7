// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "controllers/blerp/BlerpEvent.hpp"
#include "controllers/notifications/ViewerSoundRule.hpp"
#include "Test.hpp"

#include <pajlada/serialize.hpp>
#include <rapidjson/document.h>

using namespace chatterino;

TEST(ViewerSounds, RuleSerialization)
{
    ViewerSoundRule rule{
        .username = "testviewer",
        .channelFilter = "forsen",
        .enabled = true,
        .playOnJoin = true,
        .joinSound = "custom/join.mp3",
        .playOnChat = true,
        .chatSound = "custom/chat.mp3",
        .volume = 75,
        .cooldownSecs = 10,
    };

    rapidjson::Document doc;
    auto val = pajlada::Serialize<ViewerSoundRule>::get(rule, doc.GetAllocator());

    bool error = false;
    ViewerSoundRule deserialized =
        pajlada::Deserialize<ViewerSoundRule>::get(val, &error);

    EXPECT_FALSE(error);
    EXPECT_EQ(deserialized.username, "testviewer");
    EXPECT_EQ(deserialized.channelFilter, "forsen");
    EXPECT_TRUE(deserialized.enabled);
    EXPECT_TRUE(deserialized.playOnJoin);
    EXPECT_EQ(deserialized.joinSound, "custom/join.mp3");
    EXPECT_TRUE(deserialized.playOnChat);
    EXPECT_EQ(deserialized.chatSound, "custom/chat.mp3");
    EXPECT_EQ(deserialized.volume, 75);
    EXPECT_EQ(deserialized.cooldownSecs, 10);
}

TEST(ViewerSounds, ChannelMatching)
{
    ViewerSoundRule globalRule{
        .username = "xqc",
        .channelFilter = "",
    };

    EXPECT_TRUE(globalRule.matchesChannel("twitch", "forsen"));
    EXPECT_TRUE(globalRule.matchesChannel("kick", "trainwreckstv"));

    ViewerSoundRule chanRule{
        .username = "xqc",
        .channelFilter = "forsen",
    };

    EXPECT_TRUE(chanRule.matchesChannel("twitch", "forsen"));
    EXPECT_FALSE(chanRule.matchesChannel("twitch", "shroud"));

    ViewerSoundRule kickPrefixRule{
        .username = "xqc",
        .channelFilter = "kick:xqc",
    };

    EXPECT_TRUE(kickPrefixRule.matchesChannel("kick", "xqc"));
    EXPECT_FALSE(kickPrefixRule.matchesChannel("twitch", "xqc"));
}

TEST(Blerp, ExtractRoomToken)
{
    EXPECT_EQ(BlerpPayloadParser::extractRoomToken(
                  "https://obs.blerp.com/pwn/vuswqsztc7o8"),
              "vuswqsztc7o8");
    EXPECT_EQ(BlerpPayloadParser::extractRoomToken(
                  "https://obs.blerp.com/pwn/vuswqsztc7o8?preview=true"),
              "vuswqsztc7o8");
    EXPECT_EQ(BlerpPayloadParser::extractRoomToken(
                  "wss://obs.blerp.com/socket.io/?roomId=vuswqsztc7o8&EIO=4"),
              "vuswqsztc7o8");
    EXPECT_EQ(BlerpPayloadParser::extractRoomToken("vuswqsztc7o8"),
              "vuswqsztc7o8");
    EXPECT_EQ(BlerpPayloadParser::extractRoomToken("   "), "");
}

TEST(Blerp, ParseEngineIoPacket)
{
    // Valid Socket.IO event frame
    QByteArray frame =
        R"(42["alert",{"title":"Emotional Damage","username":"Gamer123","bits":100,"url":"https://blerp.com/soundbites/123"}])";
    auto ev = BlerpPayloadParser::parseEngineIoPacket(frame, "forsen");

    ASSERT_TRUE(ev.has_value());
    EXPECT_EQ(ev->soundTitle, "Emotional Damage");
    EXPECT_EQ(ev->username, "Gamer123");
    EXPECT_EQ(ev->triggerSource, "100 Bits");
    EXPECT_EQ(ev->blerpUrl, "https://blerp.com/soundbites/123");
    EXPECT_EQ(ev->channelName, "forsen");

    // Engine.IO frame with ack id
    QByteArray frameWithAck =
        R"(4299["soundPlay",{"soundTitle":"Bruh","user":"MemeLord","type":"Channel Points"}])";
    auto ev2 = BlerpPayloadParser::parseEngineIoPacket(frameWithAck);
    ASSERT_TRUE(ev2.has_value());
    EXPECT_EQ(ev2->soundTitle, "Bruh");
    EXPECT_EQ(ev2->username, "MemeLord");
    EXPECT_EQ(ev2->triggerSource, "Channel Points");

    // Heartbeat frame should be ignored
    QByteArray heartbeat = R"(4215["heart_beat_check_server_client_callback",{}])";
    EXPECT_FALSE(BlerpPayloadParser::parseEngineIoPacket(heartbeat).has_value());
}

TEST(Blerp, ParseBotChatMessage)
{
    QString botList = "blerp,blerpbot";

    // Played alert
    auto ev1 = BlerpPayloadParser::parseBotChatMessage(
        "blerp", "@Forsen played \"Victory Royale\" sound alert! 📢", "forsen",
        botList);
    ASSERT_TRUE(ev1.has_value());
    EXPECT_EQ(ev1->username, "Forsen");
    EXPECT_EQ(ev1->soundTitle, "Victory Royale");

    // Cheered bits alert
    auto ev2 = BlerpPayloadParser::parseBotChatMessage(
        "blerp", "@CoolViewer cheered 500 Bits and played \"Airhorn\"! 🔥",
        "streamer", botList);
    ASSERT_TRUE(ev2.has_value());
    EXPECT_EQ(ev2->username, "CoolViewer");
    EXPECT_EQ(ev2->triggerSource, "500 Bits");
    EXPECT_EQ(ev2->soundTitle, "Airhorn");

    // Channel point redeem
    auto ev3 = BlerpPayloadParser::parseBotChatMessage(
        "blerp", "@Viewer redeemed channel point reward: \"Noot Noot\"",
        "streamer", botList);
    ASSERT_TRUE(ev3.has_value());
    EXPECT_EQ(ev3->username, "Viewer");
    EXPECT_EQ(ev3->triggerSource, "Channel Points");
    EXPECT_EQ(ev3->soundTitle, "Noot Noot");

    // Walk-On entrance
    auto ev4 = BlerpPayloadParser::parseBotChatMessage(
        "blerp", "👋 @VipUser joined the stream with their WalkOn sound! \"Boss Music\"",
        "streamer", botList);
    ASSERT_TRUE(ev4.has_value());
    EXPECT_EQ(ev4->username, "VipUser");
    EXPECT_EQ(ev4->triggerSource, "Walk-On");
    EXPECT_EQ(ev4->soundTitle, "Boss Music");

    // Shared blerp link from a normal user
    auto ev5 = BlerpPayloadParser::parseBotChatMessage(
        "randomchatter", "check this sound out https://blerp.com/x/funny-sound",
        "streamer", botList);
    ASSERT_TRUE(ev5.has_value());
    EXPECT_EQ(ev5->username, "randomchatter");
    EXPECT_EQ(ev5->blerpUrl, "https://blerp.com/x/funny-sound");

    // Unrelated chat message should not parse
    auto ev6 = BlerpPayloadParser::parseBotChatMessage(
        "randomchatter", "hello world what is up", "streamer", botList);
    EXPECT_FALSE(ev6.has_value());
}

TEST(Blerp, MatchRewardKeywords)
{
    QString keywords = "blerp,sound alert,walk-on,walkon";

    EXPECT_TRUE(BlerpPayloadParser::matchesRewardKeywords(
        "Play Blerp Sound (Random)", keywords));
    EXPECT_TRUE(BlerpPayloadParser::matchesRewardKeywords(
        "Custom Sound Alert 500", keywords));
    EXPECT_TRUE(BlerpPayloadParser::matchesRewardKeywords(
        "Walk-On Entrance", keywords));
    EXPECT_FALSE(BlerpPayloadParser::matchesRewardKeywords(
        "Hydrate 250pts", keywords));
}
