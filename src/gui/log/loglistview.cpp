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

#include "loglistview.h"

#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QMenu>

#include "uithememanager.h"

LogListView::LogListView(QWidget *parent)
    : QListView(parent)
{
    setUniformItemSizes(true);

    setSelectionMode(QAbstractItemView::ExtendedSelection);

#if defined(Q_OS_MAC)
    setAttribute(Qt::WA_MacShowFocusRect, false);
#endif

    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, &LogListView::displayListMenu);
}

void LogListView::displayListMenu(const QPoint &pos)
{
    QMenu *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    // don't show copy action if no row is selected
    if (currentIndex().isValid()) {
        const QAction *copyAct = menu->addAction(UIThemeManager::instance()->getIcon("edit-copy"), tr("Copy"));
        connect(copyAct, &QAction::triggered, this, &LogListView::copySelection);
    }

    const QAction *clearAct = menu->addAction(UIThemeManager::instance()->getIcon("edit-clear"), tr("Clear"));
    connect(clearAct, &QAction::triggered, this, &LogListView::resetModel);

    menu->popup(mapToGlobal(pos));
}

void LogListView::keyPressEvent(QKeyEvent *event)
{
    if (event->matches(QKeySequence::Copy))
        copySelection();
    else
        QListView::keyPressEvent(event);
}

void LogListView::copySelection() const
{
    QStringList list;
    const QModelIndexList selectedIndexes = selectionModel()->selectedRows();
    for (const QModelIndex &index : selectedIndexes)
        list.append(index.data().toString());

    QApplication::clipboard()->setText(list.join('\n'));
}
