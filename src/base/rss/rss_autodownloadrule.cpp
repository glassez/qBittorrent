/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2017-2023  Vladimir Golovnev <glassez@yandex.ru>
 * Copyright (C) 2010  Christophe Dumez <chris@qbittorrent.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * In addition, as a special exception, the copyright holders give permission to
 * link this program with the OpenSSL project's "OpenSSL" library (or with
 * modified versions of it that use the same license as the "OpenSSL" library),
 * and distribute the linked executables. You must obey the GNU General Public
 * License in all respects for all of the code used other than "OpenSSL".  If you
 * modify file(s), you may extend this exception to your version of the file(s),
 * but you are not obligated to do so. If you do not wish to do so, delete this
 * exception statement from your version.
 */

#include "rss_autodownloadrule.h"

#include <algorithm>

#include <QDebug>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSharedData>
#include <QString>
#include <QStringList>

#include "base/global.h"
#include "base/path.h"
#include "base/preferences.h"
#include "base/utils/fs.h"
#include "base/utils/string.h"
#include "rss_article.h"
#include "rss_autodownloader.h"
#include "rss_feed.h"

namespace
{
    std::optional<bool> toOptionalBool(const QJsonValue &jsonVal)
    {
        if (jsonVal.isBool())
            return jsonVal.toBool();

        return std::nullopt;
    }

    QJsonValue toJsonValue(const std::optional<bool> boolValue)
    {
        return boolValue.has_value() ? *boolValue : QJsonValue {};
    }

    std::optional<BitTorrent::TorrentContentLayout> jsonValueToContentLayout(const QJsonValue &jsonVal)
    {
        const QString str = jsonVal.toString();
        if (str.isEmpty())
            return std::nullopt;
        return Utils::String::toEnum(str, BitTorrent::TorrentContentLayout::Original);
    }

    QJsonValue contentLayoutToJsonValue(const std::optional<BitTorrent::TorrentContentLayout> contentLayout)
    {
        if (!contentLayout)
            return {};
        return Utils::String::fromEnum(*contentLayout);
    }
}

const QString S_NAME = u"name"_s;
const QString S_ENABLED = u"enabled"_s;
const QString S_PRIORITY = u"priority"_s;
const QString S_USE_REGEX = u"useRegex"_s;
const QString S_MUST_CONTAIN = u"mustContain"_s;
const QString S_MUST_NOT_CONTAIN = u"mustNotContain"_s;
const QString S_EPISODE_FILTER = u"episodeFilter"_s;
const QString S_AFFECTED_FEEDS = u"affectedFeeds"_s;
const QString S_LAST_MATCH = u"lastMatch"_s;
const QString S_IGNORE_DAYS = u"ignoreDays"_s;
const QString S_SMART_FILTER = u"smartFilter"_s;
const QString S_PREVIOUSLY_MATCHED = u"previouslyMatchedEpisodes"_s;

const QString S_SAVE_PATH = u"savePath"_s;
const QString S_ASSIGNED_CATEGORY = u"assignedCategory"_s;
const QString S_ADD_PAUSED = u"addPaused"_s;
const QString S_CONTENT_LAYOUT = u"torrentContentLayout"_s;

const QString S_TORRENT_PARAMS = u"torrentParams"_s;

namespace
{
    QString computeEpisodeName(const QString &article)
    {
        const QRegularExpression episodeRegex = RSS::AutoDownloader::instance()->smartEpisodeRegex();
        const QRegularExpressionMatch match = episodeRegex.match(article);

        // See if we can extract an season/episode number or date from the title
        if (!match.hasMatch())
            return {};

        QStringList ret;
        for (int i = 1; i <= match.lastCapturedIndex(); ++i)
        {
            const QString cap = match.captured(i);
            if (cap.isEmpty())
                continue;

            bool isInt = false;
            const int x = cap.toInt(&isInt);

            ret.append(isInt ? QString::number(x) : cap);
        }

        return ret.join(u'x');
    }
}

RSS::AutoDownloadRule::AutoDownloadRule() = default;

QRegularExpression RSS::AutoDownloadRule::cachedRegex(const QString &expression, const bool isRegex) const
{
    // Use a cache of regexes so we don't have to continually recompile - big performance increase.
    // The cache is cleared whenever the regex/wildcard, must or must not contain fields or
    // episode filter are modified.
    Q_ASSERT(!expression.isEmpty());

    QRegularExpression &regex = m_cachedRegexes[expression];
    if (regex.pattern().isEmpty())
    {
        const QString pattern = (isRegex ? expression : Utils::String::wildcardToRegexPattern(expression));
        regex = QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption);
    }

    return regex;
}

bool RSS::AutoDownloadRule::matchesExpression(const QString &articleTitle, const QString &expression) const
{
    const QRegularExpression whitespace {u"\\s+"_s};

    if (expression.isEmpty())
    {
        // A regex of the form "expr|" will always match, so do the same for wildcards
        return true;
    }

    if (m_useRegex)
    {
        const QRegularExpression reg = cachedRegex(expression);
        return reg.match(articleTitle).hasMatch();
    }

    // Only match if every wildcard token (separated by spaces) is present in the article name.
    // Order of wildcard tokens is unimportant (if order is important, they should have used *).
    const QStringList wildcards {expression.split(whitespace, Qt::SkipEmptyParts)};
    for (const QString &wildcard : wildcards)
    {
        const QRegularExpression reg = cachedRegex(wildcard, false);
        if (!reg.match(articleTitle).hasMatch())
            return false;
    }

    return true;
}

bool RSS::AutoDownloadRule::matchesMustContainExpression(const QString &articleTitle) const
{
    if (m_mustContain.empty())
        return true;

    // Each expression is either a regex, or a set of wildcards separated by whitespace.
    // Accept if any complete expression matches.
    return std::ranges::any_of(asConst(m_mustContain), [this, &articleTitle](const QString &expression)
    {
        // A regex of the form "expr|" will always match, so do the same for wildcards
        return matchesExpression(articleTitle, expression);
    });
}

bool RSS::AutoDownloadRule::matchesMustNotContainExpression(const QString &articleTitle) const
{
    if (m_mustNotContain.empty())
        return true;

    // Each expression is either a regex, or a set of wildcards separated by whitespace.
    // Reject if any complete expression matches.
    return std::ranges::none_of(asConst(m_mustNotContain), [this, &articleTitle](const QString &expression)
    {
        // A regex of the form "expr|" will always match, so do the same for wildcards
        return matchesExpression(articleTitle, expression);
    });
}

bool RSS::AutoDownloadRule::matchesEpisodeFilterExpression(const QString &articleTitle) const
{
    // Reset the lastComputedEpisode, we don't want to leak it between matches
    m_lastComputedEpisodes.clear();

    if (m_episodeFilter.isEmpty())
        return true;

    const QRegularExpression filterRegex = cachedRegex(u"(^\\d{1,4})x(.*;$)"_s);
    const QRegularExpressionMatch matcher = filterRegex.match(m_episodeFilter);
    if (!matcher.hasMatch())
        return false;

    const QStringView season {matcher.capturedView(1)};
    const QList<QStringView> episodes {matcher.capturedView(2).split(u';')};
    const int seasonOurs {season.toInt()};

    for (QStringView episode : episodes)
    {
        if (episode.isEmpty())
            continue;

        // We need to trim leading zeroes, but if it's all zeros then we want episode zero.
        while ((episode.size() > 1) && episode.startsWith(u'0'))
        {
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
            episode.slice(1);
#else
            episode = episode.sliced(1);
#endif
        }

        if (episode.contains(u'-'))
        { // Range detected
            const QString partialPattern1 {u"\\bs0?(\\d{1,4})[ -_\\.]?e(0?\\d{1,4})(?:\\D|\\b)"_s};
            const QString partialPattern2 {u"\\b(\\d{1,4})x(0?\\d{1,4})(?:\\D|\\b)"_s};

            // Extract partial match from article and compare as digits
            QRegularExpressionMatch matcher = cachedRegex(partialPattern1).match(articleTitle);
            bool matched = matcher.hasMatch();

            if (!matched)
            {
                matcher = cachedRegex(partialPattern2).match(articleTitle);
                matched = matcher.hasMatch();
            }

            if (matched)
            {
                const int seasonTheirs {matcher.capturedView(1).toInt()};
                const int episodeTheirs {matcher.capturedView(2).toInt()};

                if (episode.endsWith(u'-'))
                { // Infinite range
                    const int episodeOurs {QStringView(episode).chopped(1).toInt()};
                    if (((seasonTheirs == seasonOurs) && (episodeTheirs >= episodeOurs)) || (seasonTheirs > seasonOurs))
                        return true;
                }
                else
                { // Normal range
                    const QList<QStringView> range {episode.split(u'-')};
                    Q_ASSERT(range.size() == 2);

                    const int episodeOursFirst {range.first().toInt()};
                    const int episodeOursLast {range.last().toInt()};
                    if (episodeOursFirst > episodeOursLast)
                        continue; // Ignore this subrule completely

                    if ((seasonTheirs == seasonOurs) && ((episodeOursFirst <= episodeTheirs) && (episodeOursLast >= episodeTheirs)))
                        return true;
                }
            }
        }
        else
        { // Single number
            const QString expStr {u"\\b(?:s0?%1[ -_\\.]?e0?%2|%1x0?%2)(?:\\D|\\b)"_s.arg(season, episode)};
            if (cachedRegex(expStr).match(articleTitle).hasMatch())
                return true;
        }
    }

    return false;
}

bool RSS::AutoDownloadRule::matchesSmartEpisodeFilter(const QString &articleTitle) const
{
    if (!useSmartFilter())
        return true;

    const QString episodeStr = computeEpisodeName(articleTitle);
    if (episodeStr.isEmpty())
        return false; // Don't accept articles with unrecognized episode number

    // See if this episode has been downloaded before
    const bool previouslyMatched = m_previouslyMatchedEpisodes.contains(episodeStr);
    if (previouslyMatched)
    {
        if (!AutoDownloader::instance()->downloadRepacks())
            return false;

        // Now see if we've downloaded this particular repack/proper combination
        const bool isRepack = articleTitle.contains(u"REPACK", Qt::CaseInsensitive);
        const bool isProper = articleTitle.contains(u"PROPER", Qt::CaseInsensitive);

        if (!isRepack && !isProper)
            return false;

        const QString fullEpisodeStr = u"%1%2%3"_s.arg(episodeStr, (isRepack ? u"-REPACK" : u""), (isProper ? u"-PROPER" : u""));
        const bool previouslyMatchedFull = m_previouslyMatchedEpisodes.contains(fullEpisodeStr);
        if (previouslyMatchedFull)
            return false;

        m_lastComputedEpisodes.append(fullEpisodeStr);

        // If this is a REPACK and PROPER download, add the individual entries to the list
        // so we don't download those
        if (isRepack && isProper)
        {
            m_lastComputedEpisodes.append(episodeStr + u"-REPACK");
            m_lastComputedEpisodes.append(episodeStr + u"-PROPER");
        }

        return true;
    }

    m_lastComputedEpisodes.append(episodeStr);
    return true;
}

bool RSS::AutoDownloadRule::matches(const QVariantHash &articleData) const
{
    const QDateTime articleDate {articleData[Article::KeyDate].toDateTime()};
    if (ignoreDays() > 0)
    {
        if (lastMatch().isValid() && (articleDate < lastMatch().addDays(ignoreDays())))
            return false;
    }

    const QString articleTitle {articleData[Article::KeyTitle].toString()};
    if (!matchesMustContainExpression(articleTitle))
        return false;
    if (!matchesMustNotContainExpression(articleTitle))
        return false;
    if (!matchesEpisodeFilterExpression(articleTitle))
        return false;
    if (!matchesSmartEpisodeFilter(articleTitle))
        return false;

    return true;
}

bool RSS::AutoDownloadRule::accepts(const QVariantHash &articleData)
{
    if (!matches(articleData))
        return false;

    setLastMatch(articleData[Article::KeyDate].toDateTime());

    // If there's a matched episode string, add that to the previously matched list
    if (!m_lastComputedEpisodes.isEmpty())
    {
        m_previouslyMatchedEpisodes.append(m_lastComputedEpisodes);
        m_lastComputedEpisodes.clear();
    }

    return true;
}

QJsonObject RSS::AutoDownloadRule::toJsonObject() const
{
    const BitTorrent::AddTorrentParams &addTorrentParams = m_addTorrentParams;

    return {
        {S_ENABLED, isEnabled()},
        {S_PRIORITY, priority()},
        {S_USE_REGEX, useRegex()},
        {S_MUST_CONTAIN, mustContain()},
        {S_MUST_NOT_CONTAIN, mustNotContain()},
        {S_EPISODE_FILTER, episodeFilter()},
        {S_AFFECTED_FEEDS, QJsonArray::fromStringList(feedURLs())},
        {S_LAST_MATCH, lastMatch().toString(Qt::RFC2822Date)},
        {S_IGNORE_DAYS, ignoreDays()},
        {S_SMART_FILTER, useSmartFilter()},
        {S_PREVIOUSLY_MATCHED, QJsonArray::fromStringList(previouslyMatchedEpisodes())},
        {S_TORRENT_PARAMS, BitTorrent::serializeAddTorrentParams(addTorrentParams)}
    };
}

RSS::AutoDownloadRule *RSS::AutoDownloadRule::fromJsonObject(const QJsonObject &jsonObj)
{
    auto *rule = new AutoDownloadRule;

    rule->setEnabled(jsonObj.value(S_ENABLED).toBool(true));
    rule->setPriority(jsonObj.value(S_PRIORITY).toInt(0));

    rule->setUseRegex(jsonObj.value(S_USE_REGEX).toBool(false));
    rule->setMustContain(jsonObj.value(S_MUST_CONTAIN).toString());
    rule->setMustNotContain(jsonObj.value(S_MUST_NOT_CONTAIN).toString());
    rule->setEpisodeFilter(jsonObj.value(S_EPISODE_FILTER).toString());
    rule->setLastMatch(QDateTime::fromString(jsonObj.value(S_LAST_MATCH).toString(), Qt::RFC2822Date));
    rule->setIgnoreDays(jsonObj.value(S_IGNORE_DAYS).toInt());
    rule->setUseSmartFilter(jsonObj.value(S_SMART_FILTER).toBool(false));

    const QJsonValue feedsVal = jsonObj.value(S_AFFECTED_FEEDS);
    QStringList feedURLs;
    if (feedsVal.isString())
        feedURLs << feedsVal.toString();
    else for (const QJsonValue &urlVal : asConst(feedsVal.toArray()))
        feedURLs << urlVal.toString();
    rule->setFeedURLs(feedURLs);

    const QJsonValue previouslyMatchedVal = jsonObj.value(S_PREVIOUSLY_MATCHED);
    QStringList previouslyMatched;
    if (previouslyMatchedVal.isString())
    {
        previouslyMatched << previouslyMatchedVal.toString();
    }
    else
    {
        for (const QJsonValue &val : asConst(previouslyMatchedVal.toArray()))
            previouslyMatched << val.toString();
    }
    rule->setPreviouslyMatchedEpisodes(previouslyMatched);
    rule->setAddTorrentParams(BitTorrent::parseAddTorrentParams(jsonObj.value(S_TORRENT_PARAMS).toObject()));

    return rule;
}

void RSS::AutoDownloadRule::setMustContain(const QString &tokens)
{
    m_cachedRegexes.clear();

    if (m_useRegex)
        m_mustContain = QStringList() << tokens;
    else
        m_mustContain = tokens.split(u'|');

    // Check for single empty string - if so, no condition
    if ((m_mustContain.size() == 1) && m_mustContain[0].isEmpty())
        m_mustContain.clear();
}

void RSS::AutoDownloadRule::setMustNotContain(const QString &tokens)
{
    m_cachedRegexes.clear();

    if (m_useRegex)
        m_mustNotContain = QStringList() << tokens;
    else
        m_mustNotContain = tokens.split(u'|');

    // Check for single empty string - if so, no condition
    if ((m_mustNotContain.size() == 1) && m_mustNotContain[0].isEmpty())
        m_mustNotContain.clear();
}

QStringList RSS::AutoDownloadRule::feedURLs() const
{
    return m_feedURLs;
}

void RSS::AutoDownloadRule::setFeedURLs(const QStringList &urls)
{
    m_feedURLs = urls;
}

BitTorrent::AddTorrentParams RSS::AutoDownloadRule::addTorrentParams() const
{
    return m_addTorrentParams;
}

void RSS::AutoDownloadRule::setAddTorrentParams(BitTorrent::AddTorrentParams addTorrentParams)
{
    m_addTorrentParams = std::move(addTorrentParams);
}

bool RSS::AutoDownloadRule::isEnabled() const
{
    return m_enabled;
}

void RSS::AutoDownloadRule::setEnabled(const bool enable)
{
    m_enabled = enable;
}

int RSS::AutoDownloadRule::priority() const
{
    return m_priority;
}

void RSS::AutoDownloadRule::setPriority(const int value)
{
    m_priority = value;
}

QDateTime RSS::AutoDownloadRule::lastMatch() const
{
    return m_lastMatch;
}

void RSS::AutoDownloadRule::setLastMatch(const QDateTime &lastMatch)
{
    m_lastMatch = lastMatch;
}

void RSS::AutoDownloadRule::setIgnoreDays(const int d)
{
    m_ignoreDays = d;
}

int RSS::AutoDownloadRule::ignoreDays() const
{
    return m_ignoreDays;
}

QString RSS::AutoDownloadRule::mustContain() const
{
    return m_mustContain.join(u'|');
}

QString RSS::AutoDownloadRule::mustNotContain() const
{
    return m_mustNotContain.join(u'|');
}

bool RSS::AutoDownloadRule::useSmartFilter() const
{
    return m_useSmartFilter;
}

void RSS::AutoDownloadRule::setUseSmartFilter(const bool enabled)
{
    m_useSmartFilter = enabled;
}

bool RSS::AutoDownloadRule::useRegex() const
{
    return m_useRegex;
}

void RSS::AutoDownloadRule::setUseRegex(const bool enabled)
{
    m_useRegex = enabled;
    m_cachedRegexes.clear();
}

QStringList RSS::AutoDownloadRule::previouslyMatchedEpisodes() const
{
    return m_previouslyMatchedEpisodes;
}

void RSS::AutoDownloadRule::setPreviouslyMatchedEpisodes(const QStringList &previouslyMatchedEpisodes)
{
    m_previouslyMatchedEpisodes = previouslyMatchedEpisodes;
}

QString RSS::AutoDownloadRule::episodeFilter() const
{
    return m_episodeFilter;
}

void RSS::AutoDownloadRule::setEpisodeFilter(const QString &e)
{
    m_episodeFilter = e;
    m_cachedRegexes.clear();
}
