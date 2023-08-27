/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2017  Vladimir Golovnev <glassez@yandex.ru>
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

#include "bandwidthscheduler.h"

#include <chrono>
#include <utility>

#include <QDate>
#include <QTime>

#include "base/application.h"
#include "base/preferences.h"

using namespace std::chrono_literals;

BandwidthScheduler::BandwidthScheduler(QObject *parent)
    : QObject(parent)
{
    connect(&m_timer, &QTimer::timeout, this, &BandwidthScheduler::onTimeout);
}

void BandwidthScheduler::start()
{
    m_lastAlternative = isTimeForAlternative();
    emit bandwidthLimitRequested(m_lastAlternative);

    // Timeout regularly to accommodate for external system clock changes
    // eg from the user or from a timesync utility
    m_timer.start(30s);
}

bool BandwidthScheduler::isTimeForAlternative() const
{
    const Preferences *const pref = qBt->preferences();

    QTime start = pref->getSchedulerStartTime();
    QTime end = pref->getSchedulerEndTime();
    const QTime now = QTime::currentTime();
    const Scheduler::Days schedulerDays = pref->getSchedulerDays();
    const int day = QDate::currentDate().dayOfWeek();
    bool alternative = false;

    if (start > end)
    {
        std::swap(start, end);
        alternative = true;
    }

    if ((start <= now) && (end >= now))
    {
        switch (schedulerDays)
        {
        case Scheduler::Days::EveryDay:
            alternative = !alternative;
            break;
        case Scheduler::Days::Monday:
        case Scheduler::Days::Tuesday:
        case Scheduler::Days::Wednesday:
        case Scheduler::Days::Thursday:
        case Scheduler::Days::Friday:
        case Scheduler::Days::Saturday:
        case Scheduler::Days::Sunday:
            {
                const int offset = static_cast<int>(Scheduler::Days::Monday) - 1;
                const int dayOfWeek = static_cast<int>(schedulerDays) - offset;
                if (day == dayOfWeek)
                    alternative = !alternative;
            }
            break;
        case Scheduler::Days::Weekday:
            if ((day >= 1) && (day <= 5))
                alternative = !alternative;
            break;
        case Scheduler::Days::Weekend:
            if ((day == 6) || (day == 7))
                alternative = !alternative;
            break;
        default:
            Q_ASSERT(false);
            break;
        }
    }

    return alternative;
}

void BandwidthScheduler::onTimeout()
{
    const bool alternative = isTimeForAlternative();

    if (alternative != m_lastAlternative)
    {
        m_lastAlternative = alternative;
        emit bandwidthLimitRequested(alternative);
    }
}
