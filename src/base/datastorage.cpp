/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2025  Vladimir Golovnev <glassez@yandex.ru>
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

#include "datastorage.h"

#include <concepts>
#include <memory>
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

#include "exceptions.h"
#include "global.h" // IWYU pragma: keep
#include "logger.h"
#include "path.h"
#include "profile.h"
#include "utils/variant.h"

const QString DB_TABLE_NAME = u"datastorage"_s;
const QString DB_COLUMN_ID = u"id"_s;
const QString DB_COLUMN_KEY = u"key"_s;
const QString DB_COLUMN_VALUE = u"value"_s;

namespace
{
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

    class StoreValueJob final : public Job
    {
    public:
        StoreValueJob(const QString &key, QVariant data);
        void perform(QSqlDatabase db) override;

    private:
        QString m_key;
        QVariant m_value;
    };

    class FetchValueJob final : public Job
    {
    public:
        explicit FetchValueJob(const QString &key, QPromise<std::optional<QVariant>> &&promise);
        void perform(QSqlDatabase db) override;

    private:
        QString m_key;
        QPromise<std::optional<QVariant>> m_promise;
    };

    class RemoveValueJob final : public Job
    {
    public:
        explicit RemoveValueJob(const QString &key);
        void perform(QSqlDatabase db) override;

    private:
        QString m_key;
    };
}

class DataStorage::Worker final : public QThread
{
    Q_DISABLE_COPY_MOVE(Worker)

public:
    explicit Worker(const QString &storageName, QObject *parent = nullptr);

    void run() override;
    void requestInterruption();

    void storeValue(const QString &key, QVariant &&value);
    void fetchValue(const QString &key, QPromise<std::optional<QVariant>> &&promise);
    void removeValue(const QString &key);

private:
    void addJob(std::unique_ptr<Job> job);

    QString m_storageName;
    QReadWriteLock m_dbLock;

    std::queue<std::unique_ptr<Job>> m_jobs;
    QMutex m_jobsMutex;
    QWaitCondition m_waitCondition;
};

DataStorage *DataStorage::m_instance = nullptr;

DataStorage::DataStorage(const QString &storageName)
{
    m_asyncWorker = new Worker(storageName, this);
    m_asyncWorker->start();
}

DataStorage::~DataStorage()
{
    m_asyncWorker->requestInterruption();
    m_asyncWorker->wait();
}

void DataStorage::initInstance()
{
    if (!m_instance)
        m_instance = new DataStorage(u"qBittorrent_data"_s);
}

void DataStorage::freeInstance()
{
    delete m_instance;
    m_instance = nullptr;
}

DataStorage *DataStorage::instance()
{
    return m_instance;
}

QFuture<std::optional<QVariant>> DataStorage::fetchValueImpl(const QString &key) const
{
    QPromise<std::optional<QVariant>> promise;
    const auto future = promise.future();
    m_asyncWorker->fetchValue(key, std::move(promise));
    return future;
}

void DataStorage::storeValueImpl(const QString &key, const QVariant &value)
{
    storeValueImpl(key, QVariant(value));
}

void DataStorage::storeValueImpl(const QString &key, QVariant &&value)
{
    m_asyncWorker->storeValue(key, std::move(value));
}

void DataStorage::removeValue(const QString &key)
{
    m_asyncWorker->removeValue(key);
}

DataStorage::Worker::Worker(const QString &storageName, QObject *parent)
    : QThread(parent)
    , m_storageName {storageName}
{
}

void DataStorage::Worker::run()
{
    const QString connectionName = u"DataSorage_" + QString::fromLatin1(toHex(qHash(this)));
    Q_ASSERT(!QSqlDatabase::database(connectionName, false).isValid());

    const Path dbPath = specialFolderLocation(SpecialFolder::Data) / Path(m_storageName + u".db");

    {
        auto db = QSqlDatabase::addDatabase(u"QSQLITE"_s, connectionName);
        db.setDatabaseName(dbPath.data());

        if (!db.open())
            throw RuntimeError(db.lastError().text());

        {
            QSqlQuery query {db};

            const QString createTableQuery = u"CREATE TABLE IF NOT EXISTS `%1` (%2 INTEGER PRIMARY KEY, %3 TEXT NOT NULL UNIQUE, %4 BLOB)"_s
                    .arg(DB_TABLE_NAME, DB_COLUMN_ID, DB_COLUMN_KEY, DB_COLUMN_VALUE);
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

                    qDebug() << "Data storage changes are committed. Transacted jobs:" << transactedJobsCount;
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

                m_dbLock.lockForWrite();
                if (!db.transaction())
                {
                    LogMsg(tr("Couldn't begin transaction. Error: %1").arg(db.lastError().text()), Log::WARNING);
                    m_dbLock.unlock();
                    break;
                }
            }
            std::unique_ptr<Job> job = std::move(m_jobs.front());
            m_jobs.pop();
            m_jobsMutex.unlock();

            job->perform(db);
            ++transactedJobsCount;
        }

        db.close();
    }

    QSqlDatabase::removeDatabase(connectionName);
}

void DataStorage::Worker::requestInterruption()
{
    QThread::requestInterruption();
    m_waitCondition.wakeAll();
}

void DataStorage::Worker::storeValue(const QString &key, QVariant &&value)
{
    addJob(std::make_unique<StoreValueJob>(key, std::move(value)));
}

void DataStorage::Worker::fetchValue(const QString &key, QPromise<std::optional<QVariant>> &&promise)
{
    addJob(std::make_unique<FetchValueJob>(key, std::move(promise)));
}

void DataStorage::Worker::removeValue(const QString &key)
{
    addJob(std::make_unique<RemoveValueJob>(key));
}

void DataStorage::Worker::addJob(std::unique_ptr<Job> job)
{
    m_jobsMutex.lock();
    m_jobs.push(std::move(job));
    m_jobsMutex.unlock();

    m_waitCondition.wakeAll();
}

namespace
{
    StoreValueJob::StoreValueJob(const QString &key, QVariant value)
        : m_key {key}
        , m_value {std::move(value)}
    {
    }

    void StoreValueJob::perform(QSqlDatabase db)
    {
        const QString insertStatement = u"INSERT INTO `%1` (%2, %3) VALUES (:%2, :%3) ON CONFLICT (%2) DO UPDATE SET (%2, %3) = (:%2, :%3)"_s
                .arg(DB_TABLE_NAME, DB_COLUMN_KEY, DB_COLUMN_VALUE);
        QSqlQuery query {db};

        try
        {
            if (!query.prepare(insertStatement))
                throw RuntimeError(query.lastError().text());

            query.bindValue(u":" + DB_COLUMN_KEY, m_key);
            query.bindValue(u":" + DB_COLUMN_VALUE, Utils::Variant::serialize(m_value));

            if (!query.exec())
                throw RuntimeError(query.lastError().text());
        }
        catch (const RuntimeError &err)
        {
            LogMsg(DataStorage::tr("Couldn't store value for key '%1'. Error: %2")
                    .arg(m_key, err.message()), Log::CRITICAL);
        }
    }

    FetchValueJob::FetchValueJob(const QString &key, QPromise<std::optional<QVariant>> &&promise)
        : m_key {key}
        , m_promise {std::move(promise)}
    {
    }

    void FetchValueJob::perform(QSqlDatabase db)
    {
        m_promise.start();

        const QString selectStatement = u"SELECT `%1` FROM `%2` WHERE `%3` = :%3;"_s
                .arg(DB_COLUMN_VALUE, DB_TABLE_NAME, DB_COLUMN_KEY);

        QSqlQuery query {db};
        try
        {
            if (!query.prepare(selectStatement))
                throw RuntimeError(query.lastError().text());

            query.bindValue(u":" + DB_COLUMN_KEY, m_key);
            if (!query.exec())
                throw RuntimeError(query.lastError().text());

            if (query.next())
                m_promise.addResult(Utils::Variant::deserialize(query.value(DB_COLUMN_VALUE).toByteArray()));
            else
                m_promise.addResult(std::nullopt);

            m_promise.finish();
        }
        catch (const RuntimeError &)
        {
            m_promise.setException(std::current_exception());
        }
    }

    RemoveValueJob::RemoveValueJob(const QString &key)
        : m_key {key}
    {
    }

    void RemoveValueJob::perform(QSqlDatabase db)
    {
        const auto deleteStatement = u"DELETE FROM `%1` WHERE `%2` = :%2;"_s
                .arg(DB_TABLE_NAME, DB_COLUMN_KEY);

        QSqlQuery query {db};
        try
        {
            if (!query.prepare(deleteStatement))
                throw RuntimeError(query.lastError().text());

            query.bindValue(u":" + DB_COLUMN_KEY, m_key);

            if (!query.exec())
                throw RuntimeError(query.lastError().text());
        }
        catch (const RuntimeError &err)
        {
            LogMsg(DataStorage::tr("Couldn't delete entry '%1'. Error: %2")
                    .arg(m_key, err.message()), Log::CRITICAL);
        }
    }
}
