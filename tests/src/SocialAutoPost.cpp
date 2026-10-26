// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "controllers/social/BlueskyClient.hpp"
#include "controllers/social/LivePostTemplate.hpp"
#include "controllers/social/TwitterClient.hpp"
#include "Test.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

using namespace chatterino;

TEST(SocialAutoPost, TemplateRender)
{
    LiveStreamContext ctx{
        .platform = "Twitch",
        .channelName = "forsen",
        .displayName = "Forsen",
        .streamId = "12345",
        .title = "God Gamer Returns",
        .game = "Minecraft",
        .url = "https://twitch.tv/forsen",
        .twitchUrl = "https://twitch.tv/forsen",
        .kickUrl = "https://kick.com/forsen",
    };

    QString tpl =
        "🔴 LIVE on {platform}! {title}\nPlaying: {game}\nTwitch: {twitch_url} | Kick: {kick_url}";
    QString rendered = LivePostTemplate::render(tpl, ctx);

    EXPECT_EQ(
        rendered,
        "🔴 LIVE on Twitch! God Gamer Returns\nPlaying: Minecraft\nTwitch: https://twitch.tv/forsen | Kick: https://kick.com/forsen");
}

TEST(SocialAutoPost, BlueskyFacetExtraction)
{
    // String with leading multi-byte UTF-8 emoji (🔴 = 4 bytes) + space (1 byte) -> URL starts at byte 5
    QString text = "🔴 https://twitch.tv/streamer is live! #stream #gaming";
    QJsonArray facets = BlueskyClient::extractFacets(text);

    ASSERT_EQ(facets.size(), 3);

    // Link facet
    QJsonObject fLink = facets[0].toObject();
    QJsonObject idxLink = fLink["index"].toObject();
    EXPECT_EQ(idxLink["byteStart"].toInt(), 5);
    EXPECT_EQ(idxLink["byteEnd"].toInt(), 5 + 26);  // "https://twitch.tv/streamer" is 26 bytes

    QJsonArray featsLink = fLink["features"].toArray();
    ASSERT_EQ(featsLink.size(), 1);
    EXPECT_EQ(featsLink[0].toObject()["$type"].toString(),
              "app.bsky.richtext.facet#link");
    EXPECT_EQ(featsLink[0].toObject()["uri"].toString(),
              "https://twitch.tv/streamer");

    // Hashtag 1: #stream
    QJsonObject fTag1 = facets[1].toObject();
    QJsonObject idxTag1 = fTag1["index"].toObject();
    QJsonArray featsTag1 = fTag1["features"].toArray();
    EXPECT_EQ(featsTag1[0].toObject()["$type"].toString(),
              "app.bsky.richtext.facet#tag");
    EXPECT_EQ(featsTag1[0].toObject()["tag"].toString(), "stream");

    // Hashtag 2: #gaming
    QJsonObject fTag2 = facets[2].toObject();
    QJsonArray featsTag2 = fTag2["features"].toArray();
    EXPECT_EQ(featsTag2[0].toObject()["$type"].toString(),
              "app.bsky.richtext.facet#tag");
    EXPECT_EQ(featsTag2[0].toObject()["tag"].toString(), "gaming");
}

TEST(SocialAutoPost, TrailingPunctuationStripping)
{
    QString text = "Check out https://twitch.tv/streamer, it is awesome!";
    QJsonArray facets = BlueskyClient::extractFacets(text);

    ASSERT_EQ(facets.size(), 1);
    QJsonObject fLink = facets[0].toObject();
    QJsonArray featsLink = fLink["features"].toArray();
    EXPECT_EQ(featsLink[0].toObject()["uri"].toString(),
              "https://twitch.tv/streamer");
}

TEST(SocialAutoPost, OAuth1SignatureHeader)
{
    QString header = OAuth1Signer::buildAuthorizationHeader(
        "POST", "https://api.x.com/2/tweets", "test_consumer_key",
        "test_consumer_secret", "test_access_token", "test_token_secret",
        "fixed_nonce_123", 1700000000);

    EXPECT_TRUE(header.startsWith("OAuth "));
    EXPECT_TRUE(header.contains("oauth_consumer_key=\"test_consumer_key\""));
    EXPECT_TRUE(header.contains("oauth_nonce=\"fixed_nonce_123\""));
    EXPECT_TRUE(header.contains("oauth_signature_method=\"HMAC-SHA1\""));
    EXPECT_TRUE(header.contains("oauth_timestamp=\"1700000000\""));
    EXPECT_TRUE(header.contains("oauth_token=\"test_access_token\""));
    EXPECT_TRUE(header.contains("oauth_version=\"1.0\""));
    EXPECT_TRUE(header.contains("oauth_signature="));
}

TEST(SocialAutoPost, DynamicTitleAndCategoryReflection)
{
    LiveStreamContext ctx{
        .platform = "Kick",
        .channelName = "xqc",
        .displayName = "xQc",
        .streamId = "kick_999",
        .title = "  VARIETY GAMING TODAY | CHAT DECIDES  \n",
        .game = "  Grand Theft Auto V  ",
        .url = "https://kick.com/xqc",
        .twitchUrl = "https://twitch.tv/xqc",
        .kickUrl = "https://kick.com/xqc",
    };

    QString tpl =
        "🔴 LIVE on {platform}! Playing {category}: {title} | Watch at {url}";
    QString rendered = LivePostTemplate::render(tpl, ctx);

    EXPECT_EQ(
        rendered,
        "🔴 LIVE on Kick! Playing Grand Theft Auto V: VARIETY GAMING TODAY | CHAT DECIDES | Watch at https://kick.com/xqc");
}

TEST(SocialAutoPost, UTF8TitleAndGame)
{
    LiveStreamContext ctx{
        .platform = "Twitch",
        .channelName = "vtuber",
        .displayName = "VTuber",
        .streamId = "789",
        .title = "【歌枠】Singing Stream! 🎵 楽しい歌を歌うよ！",
        .game = "Music & Performing Arts",
        .url = "https://twitch.tv/vtuber",
        .twitchUrl = "https://twitch.tv/vtuber",
        .kickUrl = "",
    };

    QString tpl = "🔴 {channel} is live with {title} in {game}! {url}";
    QString rendered = LivePostTemplate::render(tpl, ctx);

    EXPECT_EQ(
        rendered,
        "🔴 VTuber is live with 【歌枠】Singing Stream! 🎵 楽しい歌を歌うよ！ in Music & Performing Arts! https://twitch.tv/vtuber");
    EXPECT_LE(LivePostTemplate::countBlueskyGraphemes(rendered), 300);
}

TEST(SocialAutoPost, Clamping)
{
    QString longText = "🔴 " + QString(350, 'A') + " https://twitch.tv/test";
    QString bskyClamped = LivePostTemplate::clampForBluesky(longText, 300);
    EXPECT_LE(LivePostTemplate::countBlueskyGraphemes(bskyClamped), 300);
    EXPECT_TRUE(bskyClamped.endsWith("…"));

    QString xClamped = LivePostTemplate::clampForTwitter(longText, 280);
    EXPECT_LE(LivePostTemplate::countTwitterWeighted(xClamped), 280);
    EXPECT_TRUE(xClamped.endsWith("…"));
}
