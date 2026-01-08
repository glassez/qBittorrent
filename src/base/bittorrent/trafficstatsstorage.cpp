/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2026  Vladimir Golovnev <glassez@yandex.ru>
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

#include "trafficstatsstorage.h"

#include <queue>

#include <QDataStream>
#include <QFile>
#include <QHash>
#include <QMutex>
#include <QPoint>
#include <QPromise>
#include <QReadWriteLock>
#include <QRect>
#include <QSize>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QWaitCondition>

#include "base/exceptions.h"
#include "base/global.h" // IWYU pragma: keep
#include "base/logger.h"
#include "base/path.h"
#include "base/profile.h"

const QString DB_FILE_NAME = u"trafficstats.db"_s;
const QString DB_TABLE_NAME = u"trafficstatsstorage"_s;
const QString DB_COLUMN_ID = u"id"_s;
const QString DB_COLUMN_TIMESTAMP = u"date"_s;
const QString DB_COLUMN_DOWNLOADED = u"downloaded"_s;
const QString DB_COLUMN_UPLOADED = u"uploaded"_s;

namespace
{
    using namespace BitTorrent;

    struct Entry
    {
        QDate timestamp;
        qint64 downloaded = 0;
        qint64 uploaded = 0;
    };

    template <std::integral T>
    QByteArray toHex(const T value)
    {
        return QByteArray(reinterpret_cast<const char *>(&value), sizeof(value)).toHex();
    }

    class Job
    {
    public:
        virtual ~Job() = default;
        virtual void perform(QSqlDatabase db) = 0;
    };

    class StoreEntryJob final : public Job
    {
    public:
        StoreEntryJob(const Entry &entry);
        void perform(QSqlDatabase db) override;

    private:
        Entry m_entry;
    };

    class FetchEntryJob final : public Job
    {
    public:
        explicit FetchEntryJob(const QDate &timestamp, QPromise<std::optional<Entry>> &&promise);
        void perform(QSqlDatabase db) override;

    private:
        QDate m_timestamp;
        QPromise<std::optional<Entry>> m_promise;
    };

    class FetchLastEntryJob final : public Job
    {
    public:
        explicit FetchLastEntryJob(QPromise<std::optional<Entry>> &&promise);
        void perform(QSqlDatabase db) override;

    private:
        QPromise<std::optional<Entry>> m_promise;
    };

    class RemoveEntryJob final : public Job
    {
    public:
        explicit RemoveEntryJob(const QDate &timestamp);
        void perform(QSqlDatabase db) override;

    private:
        QDate m_timestamp;
    };
}

class BitTorrent::TrafficStatsStorage::Worker final : public QThread
{
    Q_DISABLE_COPY_MOVE(Worker)

public:
    explicit Worker(QObject *parent = nullptr);

    void run() override;
    void requestInterruption();

    void storeEntry(const Entry &entry);
    QFuture<std::optional<Entry>> fetchEntry(const QDate &timestamp);
    QFuture<std::optional<Entry>> fetchLastEntry();
    void removeEntry(const QDate &timestamp);

private:
    void addJob(std::unique_ptr<Job> job);

    QReadWriteLock m_dbLock;

    std::queue<std::unique_ptr<Job>> m_jobs;
    QMutex m_jobsMutex;
    QWaitCondition m_waitCondition;
};

BitTorrent::TrafficStatsStorage::TrafficStatsStorage(QObject *parent)
    : QObject(parent)
    , m_asyncWorker {new Worker(this)}
{
    m_asyncWorker->start();

    QFuture<std::optional<Entry>> lastEntryFuture = m_asyncWorker->fetchLastEntry();
    lastEntryFuture.waitForFinished();
    const Entry lastEntry = lastEntryFuture.result().value_or({});

    const QDate currentDate = QDate::currentDate();

    if (lastEntry.timestamp.isValid())
    {
        m_lastTimestamp = lastEntry.timestamp;
        const bool isSameDay = (m_lastTimestamp.daysTo(QDate::currentDate()) == 0);
        if (isSameDay)
        {
            m_downloadedDelta = lastEntry.downloaded;
            m_uploadedDelta = lastEntry.uploaded;
        }

        qDebug() << "Last statistics date:" << m_lastTimestamp.toString();
        qDebug() << "Downloaded on that day:" << lastEntry.downloaded;
        qDebug() << "Uploaded on that day:" << lastEntry.uploaded;
        qDebug() << "Current date:" << currentDate.toString();
        qDebug() << "Same day:" << (isSameDay ? "YES" : "NO");
    }
    else
    {
        m_lastTimestamp = currentDate;
    }
}

BitTorrent::TrafficStatsStorage::~TrafficStatsStorage()
{
    m_asyncWorker->requestInterruption();
    m_asyncWorker->wait();
}

void BitTorrent::TrafficStatsStorage::updateSessionStats(const QDateTime &timestamp, const qint64 sessionDownloaded, const qint64 sessionUploaded)
{
    const QDate timestampDate = timestamp.date();
    const bool isSameDay = (m_lastTimestamp.daysTo(timestampDate) == 0);
    qDebug() << "Last statistics date:" << m_lastTimestamp.toString();
    qDebug() << "Current statistics datetime:" << timestamp.toString();
    qDebug() << "Same day:" << (isSameDay ? "YES" : "NO");

    if (!isSameDay)
    {
        m_downloadedDelta -= m_thisDayDownloaded;
        m_uploadedDelta -= m_thisDayUploaded;
    }

    m_thisDayDownloaded = sessionDownloaded + m_downloadedDelta;
    m_thisDayUploaded = sessionUploaded + m_uploadedDelta;
    qDebug() << "Downloaded on this day:" << m_thisDayDownloaded;
    qDebug() << "Uploaded on this day:" << m_thisDayUploaded;

    m_asyncWorker->storeEntry({.timestamp = timestampDate, .downloaded = m_thisDayDownloaded, .uploaded = m_thisDayUploaded});

    m_lastTimestamp = timestampDate;
}

BitTorrent::TrafficStatsStorage::Worker::Worker(QObject *parent)
    : QThread(parent)
{
}

void BitTorrent::TrafficStatsStorage::Worker::run()
{
    const QString connectionName = u"TrafficStatsSorage_" + QString::fromLatin1(toHex(qHash(this)));
    Q_ASSERT(!QSqlDatabase::database(connectionName, false).isValid());

    const Path dbPath = specialFolderLocation(SpecialFolder::Data) / Path(DB_FILE_NAME);

    {
        auto db = QSqlDatabase::addDatabase(u"QSQLITE"_s, connectionName);
        db.setDatabaseName(dbPath.data());

        if (!db.open())
            throw RuntimeError(db.lastError().text());

        {
            QSqlQuery query {db};

            const QString createTableQuery = u"CREATE TABLE IF NOT EXISTS `%1` (%2 INTEGER PRIMARY KEY, %3 DATE NOT NULL UNIQUE, %4 INT NOT NULL, %5 INT NOT NULL)"_s
                .arg(DB_TABLE_NAME, DB_COLUMN_ID, DB_COLUMN_TIMESTAMP, DB_COLUMN_DOWNLOADED, DB_COLUMN_UPLOADED);
            if (!query.exec(createTableQuery))
                throw RuntimeError(query.lastError().text());
        }

        int64_t transactedJobsCount = 0;
        while (true)
        {
            m_jobsMutex.lock();
            if (m_jobs.empty())
            {
                if (transactedJobsCount > 0)
                {
                    db.commit();
                    m_dbLock.unlock();

                    qDebug() << "Traffic stats storage changes are committed. Transacted jobs:" << transactedJobsCount;
                    transactedJobsCount = 0;
                }

                if (isInterruptionRequested())
                {
                    m_jobsMutex.unlock();
                    break;
                }

                m_waitCondition.wait(&m_jobsMutex);
                if (isInterruptionRequested())
                {
                    m_jobsMutex.unlock();
                    break;
                }
            }

            std::unique_ptr<Job> job = std::move(m_jobs.front());
            m_jobs.pop();
            m_jobsMutex.unlock();

            if (transactedJobsCount == 0)
            {
                m_dbLock.lockForWrite();
                if (!db.transaction())
                {
                    LogMsg(tr("Traffic stats storage cannot begin transaction. Error: %1").arg(db.lastError().text()), Log::WARNING);
                    m_dbLock.unlock();
                    break;
                }
            }

            job->perform(db);
            ++transactedJobsCount;
        }

        db.close();
    }

    QSqlDatabase::removeDatabase(connectionName);
}

void BitTorrent::TrafficStatsStorage::Worker::requestInterruption()
{
    QThread::requestInterruption();
    m_waitCondition.wakeAll();
}

void BitTorrent::TrafficStatsStorage::Worker::storeEntry(const Entry &entry)
{
    addJob(std::make_unique<StoreEntryJob>(entry));
}

QFuture<std::optional<Entry>> BitTorrent::TrafficStatsStorage::Worker::fetchEntry(const QDate &timestamp)
{
    QPromise<std::optional<Entry>> promise;
    const auto future = promise.future();
    addJob(std::make_unique<FetchEntryJob>(timestamp, std::move(promise)));

    return future;
}

QFuture<std::optional<Entry>> TrafficStatsStorage::Worker::fetchLastEntry()
{
    QPromise<std::optional<Entry>> promise;
    const auto future = promise.future();
    addJob(std::make_unique<FetchLastEntryJob>(std::move(promise)));

    return future;
}

void BitTorrent::TrafficStatsStorage::Worker::removeEntry(const QDate &timestamp)
{
    addJob(std::make_unique<RemoveEntryJob>(timestamp));
}

void BitTorrent::TrafficStatsStorage::Worker::addJob(std::unique_ptr<Job> job)
{
    m_jobsMutex.lock();
    m_jobs.push(std::move(job));
    m_jobsMutex.unlock();

    m_waitCondition.wakeAll();
}

namespace
{
    StoreEntryJob::StoreEntryJob(const Entry &entry)
        : m_entry {entry}
    {
    }

    void StoreEntryJob::perform(QSqlDatabase db)
    {
        const QString insertStatement =
            u"INSERT INTO `%1` (%2, %3, %4) VALUES (:%2, :%3, :%4) ON CONFLICT (%2) DO UPDATE SET (%2, %3, %4) = (:%2, :%3, :%4)"_s
                .arg(DB_TABLE_NAME, DB_COLUMN_TIMESTAMP, DB_COLUMN_DOWNLOADED, DB_COLUMN_UPLOADED);
        QSqlQuery query {db};

        try
        {
            if (!query.prepare(insertStatement))
                throw RuntimeError(query.lastError().text());

            query.bindValue(u":" + DB_COLUMN_TIMESTAMP, m_entry.timestamp);
            query.bindValue(u":" + DB_COLUMN_DOWNLOADED, m_entry.downloaded);
            query.bindValue(u":" + DB_COLUMN_UPLOADED, m_entry.uploaded);

            if (!query.exec())
                throw RuntimeError(query.lastError().text());
        }
        catch (const RuntimeError &err)
        {
            LogMsg(BitTorrent::TrafficStatsStorage::tr("Traffic stats storage cannot store value for key '%1'. Error: %2")
                    .arg(m_entry.timestamp.toString(), err.message()), Log::CRITICAL);
        }
    }

    FetchEntryJob::FetchEntryJob(const QDate &timestamp, QPromise<std::optional<Entry>> &&promise)
        : m_timestamp {timestamp}
        , m_promise {std::move(promise)}
    {
    }

    void FetchEntryJob::perform(QSqlDatabase db)
    {
        m_promise.start();

        const QString selectStatement = u"SELECT `%1`, `%2` FROM `%3` WHERE `%4` = :%4;"_s
            .arg(DB_COLUMN_DOWNLOADED, DB_COLUMN_UPLOADED, DB_TABLE_NAME, DB_COLUMN_TIMESTAMP);

        QSqlQuery query {db};
        try
        {
            if (!query.prepare(selectStatement))
                throw RuntimeError(query.lastError().text());

            query.bindValue(u":" + DB_COLUMN_TIMESTAMP, m_timestamp);
            if (!query.exec())
                throw RuntimeError(query.lastError().text());

            if (query.next())
            {
                m_promise.addResult(Entry {
                    .timestamp = m_timestamp,
                    .downloaded = query.value(DB_COLUMN_DOWNLOADED).toLongLong(),
                    .uploaded = query.value(DB_COLUMN_UPLOADED).toLongLong()
                });
            }
            else
            {
                m_promise.addResult(std::nullopt);
            }

            m_promise.finish();
        }
        catch (const RuntimeError &)
        {
            m_promise.setException(std::current_exception());
        }
    }

    FetchLastEntryJob::FetchLastEntryJob(QPromise<std::optional<Entry>> &&promise)
        : m_promise {std::move(promise)}
    {
    }

    void FetchLastEntryJob::perform(QSqlDatabase db)
    {
        m_promise.start();

        const QString selectStatement = u"SELECT `%1`, `%2`, `%3` FROM `%4` WHERE `%1` = (SELECT MAX(`%1`) FROM `%4`);"_s
                .arg(DB_COLUMN_TIMESTAMP, DB_COLUMN_DOWNLOADED, DB_COLUMN_UPLOADED, DB_TABLE_NAME);

        QSqlQuery query {db};
        try
        {
            if (!query.prepare(selectStatement))
                throw RuntimeError(query.lastError().text());

            if (!query.exec())
                throw RuntimeError(query.lastError().text());

            if (query.next())
            {
                m_promise.addResult(Entry {
                    .timestamp = query.value(DB_COLUMN_TIMESTAMP).toDate(),
                    .downloaded = query.value(DB_COLUMN_DOWNLOADED).toLongLong(),
                    .uploaded = query.value(DB_COLUMN_UPLOADED).toLongLong()
                });
            }
            else
            {
                m_promise.addResult(std::nullopt);
            }

            m_promise.finish();
        }
        catch (const RuntimeError &)
        {
            m_promise.setException(std::current_exception());
        }
    }

    RemoveEntryJob::RemoveEntryJob(const QDate &timestamp)
        : m_timestamp {timestamp}
    {
    }

    void RemoveEntryJob::perform(QSqlDatabase db)
    {
        const auto deleteStatement = u"DELETE FROM `%1` WHERE `%2` = :%2;"_s.arg(DB_TABLE_NAME, DB_COLUMN_TIMESTAMP);

        QSqlQuery query {db};
        try
        {
            if (!query.prepare(deleteStatement))
                throw RuntimeError(query.lastError().text());

            query.bindValue(u":" + DB_COLUMN_TIMESTAMP, m_timestamp);

            if (!query.exec())
                throw RuntimeError(query.lastError().text());
        }
        catch (const RuntimeError &err)
        {
            LogMsg(BitTorrent::TrafficStatsStorage::tr("Traffic stats storage cannot delete entry '%1'. Error: %2")
                    .arg(m_timestamp.toString(), err.message()), Log::CRITICAL);
        }
    }
}
