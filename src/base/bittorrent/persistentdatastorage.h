/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2021  Vladimir Golovnev <glassez@yandex.ru>
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

#pragma once

#include <libtorrent/add_torrent_params.hpp>

#include <QObject>
#include <QSet>
#include <QVariant>

#include "torrent.h"
#include "torrentcontentlayout.h"

namespace BitTorrent
{
    struct LoadTorrentParams;

    class PersistentDataStorage : public QObject
    {
        Q_OBJECT
        Q_DISABLE_COPY(PersistentDataStorage)

    public:
        enum class ItemID
        {
            Name,
            SavePath,
            Category,
            Tags,
            RatioLimit,
            SeedingTimeLimit,
            HasSeedStatus,
            HasFirstLastPiecePriority,
            IsStopped,
            OperatingMode,
            ContentLayout,
            StorageMode,
            TotalDownloaded,
            TotalUploaded,
            ActiveTime,
            FinishedTime,
            SeedingTime,
            LastSeenComplete,
            LastDownload,
            LastUpload,
            NumComplete,
            NumIncomplete,
            NumDownloaded,
            SeedMode,
            UploadMode,
            ShareMode,
            ApplyIpFilter,
            SuperSeeding,
            SequentialDownload,
            StopWhenReady,
            DisableDHT,
            DisableLSD,
            DisablePEX,
            AddedTime,
            CompletedTime,
            StorageLocation,
            UploadRateLimit,
            DownloadRateLimit,
            MaxConnections,
            MaxUploads,
            MerkleTree,
            URLSeeds,
            HTTPSeeds,
            Pieces,
            UnfinishedPieces,
            Trackers,
            RenamedFiles,
            Peers,
            BannedPeers,
            FilePriorities,
            PiecePriorities
        };
        Q_ENUM(ItemID)

        explicit PersistentDataStorage(const QString &torrentID);

        QString torrentID() const;
        bool hasChangedData() const;

        virtual QVariant loadData(ItemID itemID) const = 0;
        virtual void storeData(ItemID itemID, const QVariant &value) = 0;

        virtual lt::add_torrent_params getLTAddTorrentParams() const = 0;
        virtual std::shared_ptr<const lt::torrent_info> getLTTorrentInfo() const = 0;
        virtual void setLTTorrentInfo(std::shared_ptr<const lt::torrent_info> torrentInfo) = 0;

    signals:
        void dataChanged();

    protected:
        void setDataChanged();
        void acceptChangedData();

    private:
        const QString m_torrentID;
        bool m_hasChangedData = false;
    };

    class BencodeDataStorage : public PersistentDataStorage
    {
        Q_DISABLE_COPY(BencodeDataStorage)

    public:
        BencodeDataStorage(const QString &torrentID, const LoadTorrentParams &data);

        QVariant loadData(ItemID itemID) const override;
        void storeData(ItemID itemID, const QVariant &value) override;

        lt::add_torrent_params getLTAddTorrentParams() const override;
        std::shared_ptr<const lt::torrent_info> getLTTorrentInfo() const override;
        void setLTTorrentInfo(std::shared_ptr<const lt::torrent_info> torrentInfo) override;

        std::shared_ptr<lt::entry> takeChangedData();

    private:
        lt::add_torrent_params m_nativeParams;
        std::shared_ptr<const lt::torrent_info> m_nativeTorrentInfo;
        QString m_name;
        QString m_category;
        QSet<QString> m_tags;
        QString m_savePath;
        TorrentContentLayout m_contentLayout;
        TorrentOperatingMode m_operatingMode;
        bool m_firstLastPiecePriority;
        bool m_hasSeedStatus;
        bool m_isStopped;
        qreal m_ratioLimit;
        int m_seedingTimeLimit;
    };

    template <typename T>
    class PersistentDataItem
    {
    public:
        PersistentDataItem(PersistentDataStorage *storage, PersistentDataStorage::ItemID itemID)
            : m_itemID {itemID}
            , m_storage {storage}
        {
            load();
        }

        const T &get() const
        {
            return m_value;
        }

        void set(const T &value)
        {
            if (m_value != value)
            {
                m_value = value;
                store();
            }
        }

        operator T() const
        {
            return get();
        }

        PersistentDataItem<T> &operator=(const T &value)
        {
            set(value);
            return *this;
        }

    private:
        void load()
        {
            if constexpr (std::is_enum_v<T>)
            {
                const auto value = m_storage->loadData(m_itemID).toString();
                m_value = Utils::String::template toEnum<T>(value, {});
            }
            else
            {
                const QVariant value = m_storage->loadData(m_itemID);
                m_value = value.template value<T>();
            }
        }

        void store()
        {
            if constexpr (std::is_enum_v<T>)
                m_storage->storeData(m_itemID, Utils::String::fromEnum(m_value));
            else
                m_storage->storeData(m_itemID, QVariant::fromValue<T>(m_value));
        }

        const PersistentDataStorage::ItemID m_itemID;
        T m_value;
        PersistentDataStorage *m_storage;
    };
}

Q_DECLARE_METATYPE(std::string);
Q_DECLARE_METATYPE(lt::storage_mode_t);

using StringVectorType = std::vector<std::string>;
Q_DECLARE_METATYPE(StringVectorType);

using EndpointVectorType = std::vector<lt::tcp::endpoint>;
Q_DECLARE_METATYPE(EndpointVectorType);

using PriorityVectorType = std::vector<lt::download_priority_t>;
Q_DECLARE_METATYPE(PriorityVectorType);

using Sha1HashVectorType = std::vector<lt::sha1_hash>;
Q_DECLARE_METATYPE(Sha1HashVectorType);

using FileMapType = std::map<lt::file_index_t, std::string>;
Q_DECLARE_METATYPE(FileMapType);

using PieceMapType = std::map<lt::piece_index_t, lt::bitfield>;
Q_DECLARE_METATYPE(PieceMapType);

using TrackersType = std::pair<std::vector<std::string>, std::vector<int>>;
Q_DECLARE_METATYPE(TrackersType);

using PiecesType = std::pair<lt::typed_bitfield<lt::piece_index_t>, lt::typed_bitfield<lt::piece_index_t>>;
Q_DECLARE_METATYPE(PiecesType);
