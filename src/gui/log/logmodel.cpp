/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2020  Prince Gupta <jagannatharjun11@gmail.com>
 * Copyright (C) 2019  sledgehammer999 <hammered999@gmail.com>
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

#include "logmodel.h"

#include <QApplication>
#include <QDateTime>
#include <QColor>
#include <QPalette>

#include <base/logger.h>

BaseLogModel::BaseLogModel(int initialSize, QObject *parent)
    : QAbstractListModel(parent)
    , m_msgCount(initialSize)
    , m_cache(69)
{
}

int BaseLogModel::rowCount(const QModelIndex &) const 
{
    return m_msgCount;
}

int BaseLogModel::columnCount(const QModelIndex &) const
{
    return 1;
}

QVariant BaseLogModel::data(const QModelIndex &index, const int role) const
{
    static int hit = 0, miss = 0;
    if (!index.isValid() || m_msgCount == 0)
        return {};

    const int i = m_msgCount - index.row() + m_startIndex - 1;

    Item *item = m_cache[i];
    if (!item) {
        item = new Item{rowData(i, Qt::DisplayRole), rowData(i, Qt::ForegroundRole), rowData(i, Qt::UserRole)};
        m_cache.insert(i, item, 1);
    }

    switch (role) {
    case Qt::DisplayRole:
        return item->displayRole;
    case Qt::ForegroundRole:
        return item->foregroundRole;
    case Qt::UserRole:
        return item->userRole;
    default:
        return {};
    }
}

void BaseLogModel::addNewMessage() 
{
    beginInsertRows(QModelIndex(), 0, 0);
    ++m_msgCount;
    const int i = m_msgCount + m_startIndex - 1;
    Item *item = new Item{rowData(i, Qt::DisplayRole), rowData(i, Qt::ForegroundRole), rowData(i, Qt::UserRole)};
    m_cache.insert(i, item, 1);
    endInsertRows();

    const int count = rowCount();
    if (count > MAX_LOG_MESSAGES) {
        const int lastMessage = count - 1;
        beginRemoveRows(QModelIndex(), lastMessage, lastMessage);
        --m_msgCount;
        endRemoveRows();
    }
}

void BaseLogModel::reset()
{
    beginResetModel();
    m_startIndex = m_msgCount;
    m_msgCount = 0;
    endResetModel();
}

LogModel::LogModel(QObject * parent) : BaseLogModel(Logger::instance()->messageCount(), parent) 
{
    connect(Logger::instance(), &Logger::newLogMessage, this, &LogModel::handleNewMessage);
}

void LogModel::handleNewMessage(const Log::Msg &)
{
    addNewMessage();
}

QVariant LogModel::rowData(int id, int role) const
{
    const Log::Msg msg = Logger::instance()->message(id);
    switch (role) {
    case Qt::DisplayRole: {
        const QDateTime time = QDateTime::fromMSecsSinceEpoch(msg.timestamp);
        return QString("%1 - %2").arg(time.toString(Qt::SystemLocaleShortDate), msg.message);
    }
    case Qt::ForegroundRole:
        switch (msg.type) {
        // The RGB QColor constructor is used for performance
        case Log::INFO:
            return QColor(0, 0, 255); // blue
        case Log::WARNING:
            return QColor(255, 165, 0); // orange
        case Log::CRITICAL:
            return QColor(255, 0, 0); // red
        default:
            return QApplication::palette().color(QPalette::WindowText);
        }
    case Qt::UserRole:
        return msg.type;
    }

    return {};
}


LogPeerModel::LogPeerModel(QObject * parent) : BaseLogModel(Logger::instance()->peerCount(), parent) 
{
    connect(Logger::instance(), &Logger::newLogPeer, this, &LogPeerModel::handleNewMessage);
}

void LogPeerModel::handleNewMessage(const Log::Peer &)
{
    addNewMessage();
}

QVariant LogPeerModel::rowData(int id, int role) const
{
    const Log::Peer peer = Logger::instance()->peer(id);

    if (role == Qt::DisplayRole) {
        const QDateTime time = QDateTime::fromMSecsSinceEpoch(peer.timestamp);
        QString text = QString("%1 - ").arg(time.toString(Qt::SystemLocaleShortDate));

        if (peer.blocked)
            text.append(tr("%1 was blocked %2", "x.y.z.w was blocked").arg(peer.ip, peer.reason));
        else
            text.append(tr("%1 was banned", "x.y.z.w was banned").arg(peer.ip));

        return text;
    }

    return {};
}