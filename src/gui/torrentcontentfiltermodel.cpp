/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2006-2012  Christophe Dumez <chris@qbittorrent.org>
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

#include "torrentcontentfiltermodel.h"

#include "base/utils/string.h"
#include "torrentcontentmodel.h"

TorrentContentFilterModel::TorrentContentFilterModel(QObject *parent)
    : QSortFilterProxyModel {parent}
{
    // Filter settings
    setFilterKeyColumn(TorrentContentModelItem::COL_NAME);
    setFilterRole(TorrentContentModel::UnderlyingDataRole);
    setDynamicSortFilter(true);
    setSortCaseSensitivity(Qt::CaseInsensitive);
    setSortRole(TorrentContentModel::UnderlyingDataRole);
}

TorrentContentModel *TorrentContentFilterModel::sourceModel() const
{
    return qobject_cast<TorrentContentModel *>(QSortFilterProxyModel::sourceModel());
}

void TorrentContentFilterModel::setSourceModel(QAbstractItemModel *sourceModel)
{
    auto contentModel = qobject_cast<TorrentContentModel *>(sourceModel);
    Q_ASSERT(contentModel);

    disconnect(this->sourceModel());
    QSortFilterProxyModel::setSourceModel(sourceModel);
}

TorrentContentModelItem::ItemType TorrentContentFilterModel::getItemType(const QModelIndex &index) const
{
    return sourceModel()->getItemType(mapToSource(index));
}

int TorrentContentFilterModel::getFileIndex(const QModelIndex &index) const
{
    return sourceModel()->getFileIndex(mapToSource(index));
}

QModelIndex TorrentContentFilterModel::parent(const QModelIndex &child) const
{
    if (!child.isValid()) return {};

    QModelIndex sourceParent = sourceModel()->parent(mapToSource(child));
    if (!sourceParent.isValid()) return {};

    return mapFromSource(sourceParent);
}

bool TorrentContentFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    if (sourceModel()->getItemType(sourceModel()->index(sourceRow, 0, sourceParent)) == TorrentContentModelItem::FolderType)
    {
        // accept folders if they have at least one filtered item
        return hasFiltered(sourceModel()->index(sourceRow, 0, sourceParent));
    }

    return QSortFilterProxyModel::filterAcceptsRow(sourceRow, sourceParent);
}

bool TorrentContentFilterModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    switch (sortColumn())
    {
    case TorrentContentModelItem::COL_NAME:
        {
            const TorrentContentModelItem::ItemType leftType = sourceModel()->getItemType(sourceModel()->index(left.row(), 0, left.parent()));
            const TorrentContentModelItem::ItemType rightType = sourceModel()->getItemType(sourceModel()->index(right.row(), 0, right.parent()));

            if (leftType == rightType)
            {
                const QString strL = left.data().toString();
                const QString strR = right.data().toString();
                return Utils::String::naturalLessThan<Qt::CaseInsensitive>(strL, strR);
            }
            if ((leftType == TorrentContentModelItem::FolderType) && (sortOrder() == Qt::AscendingOrder))
            {
                return true;
            }

            return false;
        }
    default:
        return QSortFilterProxyModel::lessThan(left, right);
    };
}

void TorrentContentFilterModel::selectAll()
{
    for (int i = 0; i < rowCount(); ++i)
        setData(index(i, 0), Qt::Checked, Qt::CheckStateRole);

    emit dataChanged(index(0, 0), index((rowCount() - 1), (columnCount() - 1)));
}

void TorrentContentFilterModel::selectNone()
{
    for (int i = 0; i < rowCount(); ++i)
        setData(index(i, 0), Qt::Unchecked, Qt::CheckStateRole);

    emit dataChanged(index(0, 0), index((rowCount() - 1), (columnCount() - 1)));
}

bool TorrentContentFilterModel::hasFiltered(const QModelIndex &folder) const
{
    // this should be called only with folders
    // check if the folder name itself matches the filter string
    QString name = folder.data().toString();
    if (name.contains(filterRegExp()))
        return true;
    for (int child = 0; child < sourceModel()->rowCount(folder); ++child)
    {
        QModelIndex childIndex = sourceModel()->index(child, 0, folder);
        if (sourceModel()->hasChildren(childIndex))
        {
            if (hasFiltered(childIndex))
                return true;
            continue;
        }
        name = childIndex.data().toString();
        if (name.contains(filterRegExp()))
            return true;
    }

    return false;
}
