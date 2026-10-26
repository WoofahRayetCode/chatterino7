// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "controllers/social/LivePostTemplate.hpp"

#include <QRegularExpression>
#include <QTextBoundaryFinder>

using namespace Qt::StringLiterals;

namespace chatterino {

namespace {

const QRegularExpression &urlRegex()
{
    static const QRegularExpression regex(u"https?://[^\\s]+"_s);
    return regex;
}

}  // namespace

QString LivePostTemplate::render(const QString &templateStr,
                                 const LiveStreamContext &ctx)
{
    QString result = templateStr;

    result.replace(u"{platform}"_s, ctx.platform.trimmed());
    result.replace(u"{channel}"_s,
                   ctx.displayName.isEmpty() ? ctx.channelName.trimmed()
                                             : ctx.displayName.trimmed());
    result.replace(u"{title}"_s, ctx.title.trimmed());
    result.replace(u"{game}"_s, ctx.game.trimmed());
    result.replace(u"{category}"_s, ctx.game.trimmed());
    result.replace(u"{url}"_s, ctx.url.trimmed());
    result.replace(u"{twitch_url}"_s, ctx.twitchUrl.trimmed());
    result.replace(u"{kick_url}"_s, ctx.kickUrl.trimmed());

    return result.trimmed();
}

int LivePostTemplate::countBlueskyGraphemes(const QString &text)
{
    if (text.isEmpty())
    {
        return 0;
    }

    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, text);
    int count = 0;
    while (finder.toNextBoundary() != -1)
    {
        ++count;
    }
    return count;
}

int LivePostTemplate::countTwitterWeighted(const QString &text)
{
    if (text.isEmpty())
    {
        return 0;
    }

    int weightedLength = 0;
    int lastPos = 0;

    auto it = urlRegex().globalMatch(text);
    while (it.hasNext())
    {
        auto match = it.next();
        // Count non-URL segment before this match
        QString prefix = text.mid(lastPos, match.capturedStart() - lastPos);
        weightedLength += prefix.length();

        // Each URL on X is transformed to a t.co link (23 characters)
        weightedLength += 23;

        lastPos = match.capturedEnd();
    }

    if (lastPos < text.length())
    {
        weightedLength += text.mid(lastPos).length();
    }

    return weightedLength;
}

QString LivePostTemplate::clampForBluesky(const QString &text, int maxGraphemes)
{
    if (countBlueskyGraphemes(text) <= maxGraphemes)
    {
        return text;
    }

    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, text);
    int count = 0;
    int cutIndex = 0;
    const int target = maxGraphemes - 1;  // Leave room for ellipsis

    while (finder.toNextBoundary() != -1)
    {
        if (++count > target)
        {
            break;
        }
        cutIndex = finder.position();
    }

    return text.left(cutIndex).trimmed() + u"…"_s;
}

QString LivePostTemplate::clampForTwitter(const QString &text,
                                         int maxWeightedChars)
{
    if (countTwitterWeighted(text) <= maxWeightedChars)
    {
        return text;
    }

    // Try shortening title/text while keeping URLs intact if possible
    QString clamped = text;
    while (!clamped.isEmpty() &&
           countTwitterWeighted(clamped + u"…"_s) > maxWeightedChars)
    {
        clamped.chop(1);
    }

    return clamped.trimmed() + u"…"_s;
}

}  // namespace chatterino
