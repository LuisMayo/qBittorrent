/*
 * Bittorrent Client using Qt4 and libtorrent.
 * Copyright (C) 2006  Christophe Dumez
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
 * Contact : chris@qbittorrent.org
 */
#include "downloadingTorrents.h"
#include "misc.h"
#include "properties_imp.h"
#include "bittorrent.h"
#include "allocationDlg.h"
#include "DLListDelegate.h"
#include "GUI.h"

#include <QFile>
#include <QSettings>
#include <QStandardItemModel>
#include <QHeaderView>
#include <QTime>
#include <QMenu>

DownloadingTorrents::DownloadingTorrents(QObject *parent, bittorrent *BTSession) : parent(parent), BTSession(BTSession), delayedSorting(false), nbTorrents(0) {
  setupUi(this);
  // Setting icons
  actionStart->setIcon(QIcon(QString::fromUtf8(":/Icons/skin/play.png")));
  actionPause->setIcon(QIcon(QString::fromUtf8(":/Icons/skin/pause.png")));
  actionDelete->setIcon(QIcon(QString::fromUtf8(":/Icons/skin/delete.png")));
  actionClearLog->setIcon(QIcon(QString::fromUtf8(":/Icons/skin/delete.png")));
  actionPreview_file->setIcon(QIcon(QString::fromUtf8(":/Icons/skin/preview.png")));
  actionSet_upload_limit->setIcon(QIcon(QString::fromUtf8(":/Icons/skin/seeding.png")));
  actionSet_download_limit->setIcon(QIcon(QString::fromUtf8(":/Icons/skin/downloading.png")));
  actionDelete_Permanently->setIcon(QIcon(QString::fromUtf8(":/Icons/skin/delete_perm.png")));
  actionTorrent_Properties->setIcon(QIcon(QString::fromUtf8(":/Icons/skin/properties.png")));
//   tabBottom->setTabIcon(0, QIcon(QString::fromUtf8(":/Icons/log.png")));
//   tabBottom->setTabIcon(1, QIcon(QString::fromUtf8(":/Icons/filter.png")));

  // Set Download list model
  DLListModel = new QStandardItemModel(0,10);
  DLListModel->setHeaderData(NAME, Qt::Horizontal, tr("Name", "i.e: file name"));
  DLListModel->setHeaderData(SIZE, Qt::Horizontal, tr("Size", "i.e: file size"));
  DLListModel->setHeaderData(PROGRESS, Qt::Horizontal, tr("Progress", "i.e: % downloaded"));
  DLListModel->setHeaderData(DLSPEED, Qt::Horizontal, tr("DL Speed", "i.e: Download speed"));
  DLListModel->setHeaderData(UPSPEED, Qt::Horizontal, tr("UP Speed", "i.e: Upload speed"));
  DLListModel->setHeaderData(SEEDSLEECH, Qt::Horizontal, tr("Seeds/Leechs", "i.e: full/partial sources"));
  DLListModel->setHeaderData(RATIO, Qt::Horizontal, tr("Ratio"));
  DLListModel->setHeaderData(ETA, Qt::Horizontal, tr("ETA", "i.e: Estimated Time of Arrival / Time left"));
  DLListModel->setHeaderData(PRIORITY, Qt::Horizontal, tr("Priority"));
  downloadList->setModel(DLListModel);
  DLDelegate = new DLListDelegate(downloadList);
  downloadList->setItemDelegate(DLDelegate);
  // Hide priority column
  downloadList->hideColumn(PRIORITY);
  // Hide hash column
  downloadList->hideColumn(HASH);
  loadHiddenColumns();

  connect(BTSession, SIGNAL(addedTorrent(QString, QTorrentHandle&, bool)), this, SLOT(torrentAdded(QString, QTorrentHandle&, bool)));
  connect(BTSession, SIGNAL(duplicateTorrent(QString)), this, SLOT(torrentDuplicate(QString)));
  connect(BTSession, SIGNAL(invalidTorrent(QString)), this, SLOT(torrentCorrupted(QString)));
  connect(BTSession, SIGNAL(portListeningFailure()), this, SLOT(portListeningFailure()));
  connect(BTSession, SIGNAL(peerBlocked(QString)), this, SLOT(addLogPeerBlocked(const QString)));
  connect(BTSession, SIGNAL(fastResumeDataRejected(QString)), this, SLOT(addFastResumeRejectedAlert(QString)));
  connect(BTSession, SIGNAL(aboutToDownloadFromUrl(QString)), this, SLOT(displayDownloadingUrlInfos(QString)));
  connect(BTSession, SIGNAL(urlSeedProblem(QString, QString)), this, SLOT(addUrlSeedError(QString, QString)));
  connect(BTSession, SIGNAL(UPnPError(QString)), this, SLOT(displayUPnPError(QString)));
  connect(BTSession, SIGNAL(UPnPSuccess(QString)), this, SLOT(displayUPnPSuccess(QString)));

  // Load last columns width for download list
  if(!loadColWidthDLList()) {
    downloadList->header()->resizeSection(0, 200);
  }
  // Make download list header clickable for sorting
  downloadList->header()->setClickable(true);
  downloadList->header()->setSortIndicatorShown(true);
  // Connecting Actions to slots
  connect(downloadList, SIGNAL(doubleClicked(const QModelIndex&)), this, SLOT(notifyTorrentDoubleClicked(const QModelIndex&)));
  connect(downloadList->header(), SIGNAL(sectionPressed(int)), this, SLOT(sortDownloadList(int)));
  connect(downloadList, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(displayDLListMenu(const QPoint&)));
  downloadList->header()->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(downloadList->header(), SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(displayDLHoSMenu(const QPoint&)));
  connect(infoBar, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(displayInfoBarMenu(const QPoint&)));
  // Actions
  connect(actionPause, SIGNAL(triggered()), (GUI*)parent, SLOT(on_actionPause_triggered()));
  connect(actionStart, SIGNAL(triggered()), (GUI*)parent, SLOT(on_actionStart_triggered()));
  connect(actionDelete, SIGNAL(triggered()), (GUI*)parent, SLOT(on_actionDelete_triggered()));
  connect(actionPreview_file, SIGNAL(triggered()), (GUI*)parent, SLOT(on_actionPreview_file_triggered()));
  connect(actionDelete_Permanently, SIGNAL(triggered()), (GUI*)parent, SLOT(on_actionDelete_Permanently_triggered()));
  connect(actionOpen_destination_folder, SIGNAL(triggered()), (GUI*)parent, SLOT(openDestinationFolder()));
  connect(actionTorrent_Properties, SIGNAL(triggered()), this, SLOT(propertiesSelection()));
  connect(actionBuy_it, SIGNAL(triggered()), (GUI*)parent, SLOT(goBuyPage()));

  connect(actionHOSColName, SIGNAL(triggered()), this, SLOT(hideOrShowColumnName()));
  connect(actionHOSColSize, SIGNAL(triggered()), this, SLOT(hideOrShowColumnSize()));
  connect(actionHOSColProgress, SIGNAL(triggered()), this, SLOT(hideOrShowColumnProgress()));
  connect(actionHOSColDownSpeed, SIGNAL(triggered()), this, SLOT(hideOrShowColumnDownSpeed()));
  connect(actionHOSColUpSpeed, SIGNAL(triggered()), this, SLOT(hideOrShowColumnUpSpeed()));
  connect(actionHOSColSeedersLeechers, SIGNAL(triggered()), this, SLOT(hideOrShowColumnSeedersLeechers()));
  connect(actionHOSColRatio, SIGNAL(triggered()), this, SLOT(hideOrShowColumnRatio()));
  connect(actionHOSColEta, SIGNAL(triggered()), this, SLOT(hideOrShowColumnEta()));
  connect(actionHOSColPriority, SIGNAL(triggered()), this, SLOT(hideOrShowColumnPriority()));

  // Set info Bar infos
  setInfoBar(tr("qBittorrent %1 started.", "e.g: qBittorrent v0.x started.").arg(QString::fromUtf8(""VERSION)));
  qDebug("Download tab built");
}

DownloadingTorrents::~DownloadingTorrents() {
  saveColWidthDLList();
  saveHiddenColumns();
  delete DLDelegate;
  delete DLListModel;
}

void DownloadingTorrents::enablePriorityColumn(bool enable) {
  if(enable) {
    downloadList->showColumn(PRIORITY);
  } else {
    downloadList->hideColumn(PRIORITY);
  }
}

void DownloadingTorrents::notifyTorrentDoubleClicked(const QModelIndex& index) {
  unsigned int row = index.row();
  QString hash = getHashFromRow(row);
  emit torrentDoubleClicked(hash, false);
}

void DownloadingTorrents::addLogPeerBlocked(QString ip) {
  static unsigned int nbLines = 0;
  ++nbLines;
  if(nbLines > 200) {
    textBlockedUsers->clear();
    nbLines = 1;
  }
  textBlockedUsers->append(QString::fromUtf8("<font color='grey'>")+ QTime::currentTime().toString(QString::fromUtf8("hh:mm:ss")) + QString::fromUtf8("</font> - ")+tr("<font color='red'>%1</font> <i>was blocked</i>", "x.y.z.w was blocked").arg(ip));
}

unsigned int DownloadingTorrents::getNbTorrentsInList() const {
  return nbTorrents;
}

// Note: do not actually pause the torrent in BT session
void DownloadingTorrents::pauseTorrent(QString hash) {
  int row = getRowFromHash(hash);
  if(row == -1)
    return;
  DLListModel->setData(DLListModel->index(row, DLSPEED), QVariant((double)0.0));
  DLListModel->setData(DLListModel->index(row, UPSPEED), QVariant((double)0.0));
  DLListModel->setData(DLListModel->index(row, ETA), QVariant((qlonglong)-1));
  DLListModel->setData(DLListModel->index(row, NAME), QIcon(QString::fromUtf8(":/Icons/skin/paused.png")), Qt::DecorationRole);
  DLListModel->setData(DLListModel->index(row, SEEDSLEECH), QVariant(QString::fromUtf8("0/0")));
  QTorrentHandle h = BTSession->getTorrentHandle(hash);
  DLListModel->setData(DLListModel->index(row, PROGRESS), QVariant((double)h.progress()));
  setRowColor(row, QString::fromUtf8("red"));
}

QString DownloadingTorrents::getHashFromRow(unsigned int row) const {
  Q_ASSERT(row < (unsigned int)DLListModel->rowCount());
  return DLListModel->data(DLListModel->index(row, HASH)).toString();
}

void DownloadingTorrents::setBottomTabEnabled(unsigned int index, bool b){
  if(index and !b)
    tabBottom->setCurrentIndex(0);
  tabBottom->setTabEnabled(index, b);
}

// Show torrent properties dialog
void DownloadingTorrents::showProperties(const QModelIndex &index) {
  showPropertiesFromHash(DLListModel->data(DLListModel->index(index.row(), HASH)).toString());
}

void DownloadingTorrents::showPropertiesFromHash(QString hash) {
  QTorrentHandle h = BTSession->getTorrentHandle(hash);
  properties *prop = new properties(this, BTSession, h);
  connect(prop, SIGNAL(filteredFilesChanged(QString)), this, SLOT(updateFileSizeAndProgress(QString)));
  connect(prop, SIGNAL(trackersChanged(QString)), BTSession, SLOT(saveTrackerFile(QString)));
  prop->show();
}

void DownloadingTorrents::resumeTorrent(QString hash){
    int row = getRowFromHash(hash);
    Q_ASSERT(row != -1);
    DLListModel->setData(DLListModel->index(row, NAME), QVariant(QIcon(QString::fromUtf8(":/Icons/skin/connecting.png"))), Qt::DecorationRole);
    setRowColor(row, QString::fromUtf8("grey"));
}

// Remove a torrent from the download list but NOT from the BT Session
void DownloadingTorrents::deleteTorrent(QString hash) {
  int row = getRowFromHash(hash);
  if(row == -1){
    qDebug("torrent is not in download list, nothing to delete");
    return;
  }
  DLListModel->removeRow(row);
  --nbTorrents;
  emit unfinishedTorrentsNumberChanged(nbTorrents);
}

void DownloadingTorrents::displayUPnPError(QString msg) {
  setInfoBar(tr("UPnP/NAT-PMP: Port mapping failure, message: %1").arg(msg), QColor("red"));
}

void DownloadingTorrents::displayUPnPSuccess(QString msg) {
  DownloadingTorrents::setInfoBar(tr("UPnP/NAT-PMP: Port mapping successful, message: %1").arg(msg), QColor("blue"));
}

// Update Info Bar information
void DownloadingTorrents::setInfoBar(QString info, QColor color) {
  static unsigned int nbLines = 0;
  ++nbLines;
  // Check log size, clear it if too big
  if(nbLines > 200) {
    infoBar->clear();
    nbLines = 1;
  }
  infoBar->append(QString::fromUtf8("<font color='grey'>")+ QTime::currentTime().toString(QString::fromUtf8("hh:mm:ss")) + QString::fromUtf8("</font> - <font color='") + color.name() +QString::fromUtf8("'><i>") + info + QString::fromUtf8("</i></font>"));
}

void DownloadingTorrents::addFastResumeRejectedAlert(QString name) {
  setInfoBar(tr("Fast resume data was rejected for torrent %1, checking again...").arg(name), QString::fromUtf8("red"));
}

void DownloadingTorrents::addUrlSeedError(QString url, QString msg) {
  setInfoBar(tr("Url seed lookup failed for url: %1, message: %2").arg(url).arg(msg), QString::fromUtf8("red"));
}

void DownloadingTorrents::on_actionSet_download_limit_triggered() {
  QModelIndexList selectedIndexes = downloadList->selectionModel()->selectedIndexes();
  QModelIndex index;
  QStringList hashes;
  foreach(index, selectedIndexes) {
    if(index.column() == NAME) {
      // Get the file hash
      hashes << DLListModel->data(DLListModel->index(index.row(), HASH)).toString();
    }
  }
  Q_ASSERT(hashes.size() > 0);
  new BandwidthAllocationDialog(this, false, BTSession, hashes);
}

void DownloadingTorrents::on_actionSet_upload_limit_triggered() {
  QModelIndexList selectedIndexes = downloadList->selectionModel()->selectedIndexes();
  QModelIndex index;
  QStringList hashes;
  foreach(index, selectedIndexes) {
    if(index.column() == NAME) {
      // Get the file hash
      hashes << DLListModel->data(DLListModel->index(index.row(), HASH)).toString();
    }
  }
  Q_ASSERT(hashes.size() > 0);
  new BandwidthAllocationDialog(this, true, BTSession, hashes);
}

// display properties of selected items
void DownloadingTorrents::propertiesSelection(){
  QModelIndexList selectedIndexes = downloadList->selectionModel()->selectedIndexes();
  QModelIndex index;
  foreach(index, selectedIndexes){
    if(index.column() == NAME){
      showProperties(index);
    }
  }
}

void DownloadingTorrents::displayDLListMenu(const QPoint& pos) {
  QMenu myDLLlistMenu(this);
  QModelIndex index;
  // Enable/disable pause/start action given the DL state
  QModelIndexList selectedIndexes = downloadList->selectionModel()->selectedIndexes();
  bool has_pause = false, has_start = false, has_preview = false;
  foreach(index, selectedIndexes) {
    if(index.column() == NAME) {
      // Get the file name
      QString hash = DLListModel->data(DLListModel->index(index.row(), HASH)).toString();
      // Get handle and pause the torrent
      QTorrentHandle h = BTSession->getTorrentHandle(hash);
      if(!h.is_valid()) continue;
      if(h.is_paused()) {
        if(!has_start) {
          myDLLlistMenu.addAction(actionStart);
          has_start = true;
        }
      }else{
        if(!has_pause) {
          myDLLlistMenu.addAction(actionPause);
          has_pause = true;
        }
      }
      if(BTSession->isFilePreviewPossible(hash) && !has_preview) {
         myDLLlistMenu.addAction(actionPreview_file);
         has_preview = true;
      }
      if(has_pause && has_start && has_preview) break;
    }
  }
  myDLLlistMenu.addSeparator();
  myDLLlistMenu.addAction(actionDelete);
  myDLLlistMenu.addAction(actionDelete_Permanently);
  myDLLlistMenu.addSeparator();
  myDLLlistMenu.addAction(actionSet_download_limit);
  myDLLlistMenu.addAction(actionSet_upload_limit);
  myDLLlistMenu.addSeparator();
  myDLLlistMenu.addAction(actionOpen_destination_folder);
  myDLLlistMenu.addAction(actionTorrent_Properties);
  myDLLlistMenu.addSeparator();
  myDLLlistMenu.addAction(actionBuy_it);
  // Call menu
  // XXX: why mapToGlobal() is not enough?
  myDLLlistMenu.exec(mapToGlobal(pos)+QPoint(10,35));
}


/*
 * Hiding Columns functions
 */

// hide/show columns menu
void DownloadingTorrents::displayDLHoSMenu(const QPoint& pos){
  QMenu hideshowColumn(this);
  hideshowColumn.setTitle(tr("Hide or Show Column"));
  int lastCol;
  if(BTSession->isQueueingEnabled()) {
    lastCol = PRIORITY;
  } else {
    lastCol = ETA;
  }
  for(int i=0; i <= lastCol; ++i) {
    hideshowColumn.addAction(getActionHoSCol(i));
  }
  // Call menu
  hideshowColumn.exec(mapToGlobal(pos)+QPoint(10,10));
}

// toggle hide/show a column
void DownloadingTorrents::hideOrShowColumn(int index) {
  unsigned int nbVisibleColumns = 0;
  unsigned int nbCols = DLListModel->columnCount();
  // Count visible columns
  for(unsigned int i=0; i<nbCols; ++i) {
    if(!downloadList->isColumnHidden(i))
      ++nbVisibleColumns;
  }
  if(!downloadList->isColumnHidden(index)) {
    // User wants to hide the column
    // Is there at least one other visible column?
    if(nbVisibleColumns <= 1) return;
    // User can hide the column, do it.
    downloadList->setColumnHidden(index, true);
    getActionHoSCol(index)->setIcon(QIcon(QString::fromUtf8(":/Icons/button_cancel.png")));
    --nbVisibleColumns;
    if(index == ETA) {
      BTSession->setETACalculation(false);
      qDebug("Disable ETA calculation");
    }
  } else {
    // User want to display the column
    downloadList->setColumnHidden(index, false);
    getActionHoSCol(index)->setIcon(QIcon(QString::fromUtf8(":/Icons/button_ok.png")));
    ++nbVisibleColumns;
    if(index == ETA) {
      BTSession->setETACalculation(true);
      qDebug("Enable ETA calculation");
    }
  }
  //resize all others non-hidden columns
  for(unsigned int i=0; i<nbCols; ++i) {
    if(!downloadList->isColumnHidden(i)) {
      downloadList->setColumnWidth(i, (int)ceil(downloadList->columnWidth(i)+(downloadList->columnWidth(index)/nbVisibleColumns)));
    }
  }
}

void DownloadingTorrents::hidePriorityColumn(bool hide) {
  downloadList->setColumnHidden(PRIORITY, hide);
}

// save the hidden columns in settings
void DownloadingTorrents::saveHiddenColumns() {
  QSettings settings("qBittorrent", "qBittorrent");
  QStringList ishidden_list;
  short nbColumns = DLListModel->columnCount()-1;

  for(short i=0; i<nbColumns; ++i){
    if(downloadList->isColumnHidden(i)) {
      ishidden_list << QString::fromUtf8(misc::toString(0).c_str());
    } else {
      ishidden_list << QString::fromUtf8(misc::toString(1).c_str());
    }
  }
  settings.setValue("DownloadListColsHoS", ishidden_list.join(" "));
}

// load the previous settings, and hide the columns
bool DownloadingTorrents::loadHiddenColumns() {
  bool loaded = false;
  QSettings settings("qBittorrent", "qBittorrent");
  QString line = settings.value("DownloadListColsHoS", QString()).toString();
  QStringList ishidden_list;
  if(!line.isEmpty()) {
    ishidden_list = line.split(' ');
    if(ishidden_list.size() == DLListModel->columnCount()-1) {
      unsigned int listSize = ishidden_list.size();
      for(unsigned int i=0; i<listSize; ++i){
            downloadList->header()->resizeSection(i, ishidden_list.at(i).toInt());
      }
      loaded = true;
    }
  }
  for(int i=0; i<DLListModel->columnCount()-1; i++) {
    if(loaded && ishidden_list.at(i) == "0") {
      downloadList->setColumnHidden(i, true);
      getActionHoSCol(i)->setIcon(QIcon(QString::fromUtf8(":/Icons/button_cancel.png")));
    } else {
      getActionHoSCol(i)->setIcon(QIcon(QString::fromUtf8(":/Icons/button_ok.png")));
    }
  }
  return loaded;
}

void DownloadingTorrents::hideOrShowColumnName() {
  hideOrShowColumn(NAME);
}

void DownloadingTorrents::hideOrShowColumnSize() {
  hideOrShowColumn(SIZE);
}

void DownloadingTorrents::hideOrShowColumnProgress() {
  hideOrShowColumn(PROGRESS);
}

void DownloadingTorrents::hideOrShowColumnDownSpeed() {
  hideOrShowColumn(DLSPEED);
}

void DownloadingTorrents::hideOrShowColumnUpSpeed() {
  hideOrShowColumn(UPSPEED);
}

void DownloadingTorrents::hideOrShowColumnSeedersLeechers() {
  hideOrShowColumn(SEEDSLEECH);
}

void DownloadingTorrents::hideOrShowColumnRatio() {
  hideOrShowColumn(RATIO);
}

void DownloadingTorrents::hideOrShowColumnEta() {
  hideOrShowColumn(ETA);
}

void DownloadingTorrents::hideOrShowColumnPriority() {
  hideOrShowColumn(PRIORITY);
}

void DownloadingTorrents::on_actionClearLog_triggered() {
  infoBar->clear();
}

// getter, return the action hide or show whose id is index
QAction* DownloadingTorrents::getActionHoSCol(int index) {
  switch(index) {
    case NAME :
      return actionHOSColName;
    break;
    case SIZE :
      return actionHOSColSize;
    break;
    case PROGRESS :
      return actionHOSColProgress;
    break;
    case DLSPEED :
      return actionHOSColDownSpeed;
    break;
    case UPSPEED :
      return actionHOSColUpSpeed;
    break;
    case SEEDSLEECH :
      return actionHOSColSeedersLeechers;
    break;
    case RATIO :
      return actionHOSColRatio;
    break;
    case ETA :
      return actionHOSColEta;
    break;
    case PRIORITY :
      return actionHOSColPriority;
      break;
    default :
      return NULL;
  }
}

QStringList DownloadingTorrents::getSelectedTorrents(bool only_one) const{
  QStringList res;
  QModelIndex index;
  QModelIndexList selectedIndexes = downloadList->selectionModel()->selectedIndexes();
  foreach(index, selectedIndexes) {
    if(index.column() == NAME) {
      // Get the file hash
      QString hash = DLListModel->data(DLListModel->index(index.row(), HASH)).toString();
      res << hash;
      if(only_one) break;
    }
  }
  return res;
}

void DownloadingTorrents::displayInfoBarMenu(const QPoint& pos) {
  // Log Menu
  QMenu myLogMenu(this);
  myLogMenu.addAction(actionClearLog);
  // XXX: Why mapToGlobal() is not enough?
  myLogMenu.exec(mapToGlobal(pos)+QPoint(44,305));
}

void DownloadingTorrents::sortProgressColumnDelayed() {
    if(delayedSorting) {
      sortDownloadListFloat(PROGRESS, delayedSortingOrder);
      qDebug("Delayed sorting of progress column");
    }
}

// get information from torrent handles and
// update download list accordingly
void DownloadingTorrents::updateDlList() {
  // browse handles
  QStringList unfinishedTorrents = BTSession->getUnfinishedTorrents();
  QString hash;
  foreach(hash, unfinishedTorrents) {
    QTorrentHandle h = BTSession->getTorrentHandle(hash);
    if(!h.is_valid()){
      qDebug("We have an invalid handle for: %s", qPrintable(hash));
      continue;
    }
    try{
      QString hash = h.hash();
      int row = getRowFromHash(hash);
      if(row == -1) {
        qDebug("Info: Could not find filename in download list, adding it...");
        addTorrent(hash);
        row = getRowFromHash(hash);
      }
      Q_ASSERT(row != -1);
      // Update Priority
      if(BTSession->isQueueingEnabled()) {
        DLListModel->setData(DLListModel->index(row, PRIORITY), QVariant((int)BTSession->getDlTorrentPriority(hash)));
        if(h.is_paused() && BTSession->isDownloadQueued(hash)) {
          DLListModel->setData(DLListModel->index(row, NAME), QVariant(QIcon(QString::fromUtf8(":/Icons/skin/queued.png"))), Qt::DecorationRole);
          if(!downloadList->isColumnHidden(ETA)) {
            DLListModel->setData(DLListModel->index(row, ETA), QVariant((qlonglong)-1));
          }
          setRowColor(row, QString::fromUtf8("grey"));
        }
      }
      // No need to update a paused torrent
      if(h.is_paused()) continue;
      if(BTSession->getTorrentsToPauseAfterChecking().indexOf(hash) != -1) {
        if(!downloadList->isColumnHidden(PROGRESS)) {
          DLListModel->setData(DLListModel->index(row, PROGRESS), QVariant((double)h.progress()));
        }
         continue;
      }
      // Parse download state
      // Setting download state
      switch(h.state()) {
        case torrent_status::finished:
        case torrent_status::seeding:
          qDebug("A torrent that was in download tab just finished, moving it to finished tab");
          BTSession->setUnfinishedTorrent(hash);
          emit torrentFinished(hash);
          deleteTorrent(hash);
          continue;
        case torrent_status::checking_files:
        case torrent_status::queued_for_checking:
          DLListModel->setData(DLListModel->index(row, NAME), QVariant(QIcon(QString::fromUtf8(":/Icons/time.png"))), Qt::DecorationRole);
          setRowColor(row, QString::fromUtf8("grey"));
          if(!downloadList->isColumnHidden(PROGRESS)) {
            DLListModel->setData(DLListModel->index(row, PROGRESS), QVariant((double)h.progress()));
          }
          break;
        case torrent_status::connecting_to_tracker:
          if(h.download_payload_rate() > 0) {
            // Display "Downloading" status when connecting if download speed > 0
            if(!downloadList->isColumnHidden(ETA)) {
              DLListModel->setData(DLListModel->index(row, ETA), QVariant((qlonglong)BTSession->getETA(hash)));
            }
            DLListModel->setData(DLListModel->index(row, NAME), QVariant(QIcon(QString::fromUtf8(":/Icons/skin/downloading.png"))), Qt::DecorationRole);
            setRowColor(row, QString::fromUtf8("green"));
          }else{
            if(!downloadList->isColumnHidden(ETA)) {
              DLListModel->setData(DLListModel->index(row, ETA), QVariant((qlonglong)-1));
            }
            DLListModel->setData(DLListModel->index(row, NAME), QVariant(QIcon(QString::fromUtf8(":/Icons/skin/connecting.png"))), Qt::DecorationRole);
            setRowColor(row, QString::fromUtf8("grey"));
          }
          if(!downloadList->isColumnHidden(PROGRESS)) {
            DLListModel->setData(DLListModel->index(row, PROGRESS), QVariant((double)h.progress()));
          }
          if(!downloadList->isColumnHidden(DLSPEED)) {
            DLListModel->setData(DLListModel->index(row, DLSPEED), QVariant((double)h.download_payload_rate()));
          }
          if(!downloadList->isColumnHidden(UPSPEED)) {
            DLListModel->setData(DLListModel->index(row, UPSPEED), QVariant((double)h.upload_payload_rate()));
          }
          break;
        case torrent_status::downloading:
        case torrent_status::downloading_metadata:
          if(h.download_payload_rate() > 0) {
            DLListModel->setData(DLListModel->index(row, NAME), QVariant(QIcon(QString::fromUtf8(":/Icons/skin/downloading.png"))), Qt::DecorationRole);
            if(!downloadList->isColumnHidden(ETA)) {
              DLListModel->setData(DLListModel->index(row, ETA), QVariant((qlonglong)BTSession->getETA(hash)));
            }
            setRowColor(row, QString::fromUtf8("green"));
          }else{
            DLListModel->setData(DLListModel->index(row, NAME), QVariant(QIcon(QString::fromUtf8(":/Icons/skin/stalled.png"))), Qt::DecorationRole);
            if(!downloadList->isColumnHidden(ETA)) {
              DLListModel->setData(DLListModel->index(row, ETA), QVariant((qlonglong)-1));
            }
            setRowColor(row, QApplication::palette().color(QPalette::WindowText));
          }
          if(!downloadList->isColumnHidden(PROGRESS)) {
            DLListModel->setData(DLListModel->index(row, PROGRESS), QVariant((double)h.progress()));
          }
          if(!downloadList->isColumnHidden(DLSPEED)) {
            DLListModel->setData(DLListModel->index(row, DLSPEED), QVariant((double)h.download_payload_rate()));
          }
          if(!downloadList->isColumnHidden(UPSPEED)) {
            DLListModel->setData(DLListModel->index(row, UPSPEED), QVariant((double)h.upload_payload_rate()));
          }
          break;
        default:
          if(!downloadList->isColumnHidden(ETA)) {
            DLListModel->setData(DLListModel->index(row, ETA), QVariant((qlonglong)-1));
          }
      }
      if(!downloadList->isColumnHidden(SEEDSLEECH)) {
        DLListModel->setData(DLListModel->index(row, SEEDSLEECH), QVariant(misc::toQString(h.num_seeds(), true)+QString::fromUtf8("/")+misc::toQString(h.num_peers() - h.num_seeds(), true)));
      }
      if(!downloadList->isColumnHidden(RATIO)) {
        DLListModel->setData(DLListModel->index(row, RATIO), QVariant(misc::toQString(BTSession->getRealRatio(hash))));
      }
    }catch(invalid_handle e) {
      continue;
    }
  }
}

void DownloadingTorrents::addTorrent(QString hash) {
  if(BTSession->isFinished(hash)){
    BTSession->setUnfinishedTorrent(hash);
  }
  QTorrentHandle h = BTSession->getTorrentHandle(hash);
  int row = getRowFromHash(hash);
  if(row != -1) return;
  row = DLListModel->rowCount();
  // Adding torrent to download list
  DLListModel->insertRow(row);
  DLListModel->setData(DLListModel->index(row, NAME), QVariant(h.name()));
  DLListModel->setData(DLListModel->index(row, SIZE), QVariant((qlonglong)h.actual_size()));
  DLListModel->setData(DLListModel->index(row, DLSPEED), QVariant((double)0.));
  DLListModel->setData(DLListModel->index(row, UPSPEED), QVariant((double)0.));
  DLListModel->setData(DLListModel->index(row, SEEDSLEECH), QVariant(QString::fromUtf8("0/0")));
  DLListModel->setData(DLListModel->index(row, ETA), QVariant((qlonglong)-1));
  if(BTSession->isQueueingEnabled())
    DLListModel->setData(DLListModel->index(row, PRIORITY), QVariant((int)BTSession->getDlTorrentPriority(hash)));
  DLListModel->setData(DLListModel->index(row, HASH), QVariant(hash));
  // Pause torrent if it was paused last time
  if(BTSession->isPaused(hash)) {
    DLListModel->setData(DLListModel->index(row, NAME), QVariant(QIcon(QString::fromUtf8(":/Icons/skin/paused.png"))), Qt::DecorationRole);
    setRowColor(row, QString::fromUtf8("red"));
  }else{
    DLListModel->setData(DLListModel->index(row, NAME), QVariant(QIcon(QString::fromUtf8(":/Icons/skin/connecting.png"))), Qt::DecorationRole);
    setRowColor(row, QString::fromUtf8("grey"));
  }
  ++nbTorrents;
  emit unfinishedTorrentsNumberChanged(nbTorrents);
}

void DownloadingTorrents::sortDownloadListFloat(int index, Qt::SortOrder sortOrder) {
  QList<QPair<int, double> > lines;
  // insertion sorting
  unsigned int nbRows = DLListModel->rowCount();
  for(unsigned int i=0; i<nbRows; ++i) {
    misc::insertSort(lines, QPair<int,double>(i, DLListModel->data(DLListModel->index(i, index)).toDouble()), sortOrder);
  }
  // Insert items in new model, in correct order
  unsigned int nbRows_old = lines.size();
  for(unsigned int row=0; row<nbRows_old; ++row) {
    DLListModel->insertRow(DLListModel->rowCount());
    unsigned int sourceRow = lines[row].first;
    unsigned int nbColumns = DLListModel->columnCount();
    for(unsigned int col=0; col<nbColumns; ++col) {
      DLListModel->setData(DLListModel->index(nbRows_old+row, col), DLListModel->data(DLListModel->index(sourceRow, col)));
      DLListModel->setData(DLListModel->index(nbRows_old+row, col), DLListModel->data(DLListModel->index(sourceRow, col), Qt::DecorationRole), Qt::DecorationRole);
      DLListModel->setData(DLListModel->index(nbRows_old+row, col), DLListModel->data(DLListModel->index(sourceRow, col), Qt::ForegroundRole), Qt::ForegroundRole);
    }
  }
  // Remove old rows
  DLListModel->removeRows(0, nbRows_old);
}

void DownloadingTorrents::sortDownloadListString(int index, Qt::SortOrder sortOrder) {
  QList<QPair<int, QString> > lines;
  // Insertion sorting
  unsigned int nbRows = DLListModel->rowCount();
  for(unsigned int i=0; i<nbRows; ++i) {
    misc::insertSortString(lines, QPair<int, QString>(i, DLListModel->data(DLListModel->index(i, index)).toString()), sortOrder);
  }
  // Insert items in new model, in correct order
  unsigned int nbRows_old = lines.size();
  for(unsigned int row=0; row<nbRows_old; ++row) {
    DLListModel->insertRow(DLListModel->rowCount());
    unsigned int sourceRow = lines[row].first;
    unsigned int nbColumns = DLListModel->columnCount();
    for(unsigned int col=0; col<nbColumns; ++col) {
      DLListModel->setData(DLListModel->index(nbRows_old+row, col), DLListModel->data(DLListModel->index(sourceRow, col)));
      DLListModel->setData(DLListModel->index(nbRows_old+row, col), DLListModel->data(DLListModel->index(sourceRow, col), Qt::DecorationRole), Qt::DecorationRole);
      DLListModel->setData(DLListModel->index(nbRows_old+row, col), DLListModel->data(DLListModel->index(sourceRow, col), Qt::ForegroundRole), Qt::ForegroundRole);
    }
  }
  // Remove old rows
  DLListModel->removeRows(0, nbRows_old);
}

void DownloadingTorrents::sortDownloadList(int index, Qt::SortOrder startSortOrder, bool fromLoadColWidth) {
  qDebug("Called sort download list");
  static Qt::SortOrder sortOrder = startSortOrder;
  if(!fromLoadColWidth && downloadList->header()->sortIndicatorSection() == index) {
    if(sortOrder == Qt::AscendingOrder) {
      sortOrder = Qt::DescendingOrder;
    }else{
      sortOrder = Qt::AscendingOrder;
    }
  }
  QString sortOrderLetter;
  if(sortOrder == Qt::AscendingOrder)
    sortOrderLetter = QString::fromUtf8("a");
  else
    sortOrderLetter = QString::fromUtf8("d");
  if(fromLoadColWidth) {
    // XXX: Why is this needed?
    if(sortOrder == Qt::DescendingOrder)
      downloadList->header()->setSortIndicator(index, Qt::AscendingOrder);
    else
      downloadList->header()->setSortIndicator(index, Qt::DescendingOrder);
  } else {
    downloadList->header()->setSortIndicator(index, sortOrder);
  }
  switch(index) {
    case SIZE:
    case ETA:
    case UPSPEED:
    case DLSPEED:
      sortDownloadListFloat(index, sortOrder);
      break;
    case PROGRESS:
      if(fromLoadColWidth) {
        // Progress sorting must be delayed until files are checked (on startup)
        delayedSorting = true;
        qDebug("Delayed sorting of the progress column");
        delayedSortingOrder = sortOrder;
      }else{
        sortDownloadListFloat(index, sortOrder);
      }
      break;
    default:
      sortDownloadListString(index, sortOrder);
  }
  QSettings settings(QString::fromUtf8("qBittorrent"), QString::fromUtf8("qBittorrent"));
  settings.setValue(QString::fromUtf8("DownloadListSortedCol"), misc::toQString(index)+sortOrderLetter);
}

// Save columns width in a file to remember them
// (download list)
void DownloadingTorrents::saveColWidthDLList() const{
  qDebug("Saving columns width in download list");
  QSettings settings(QString::fromUtf8("qBittorrent"), QString::fromUtf8("qBittorrent"));
  QStringList width_list;
  QStringList new_width_list;
  short nbColumns = DLListModel->columnCount()-1;
  QString line = settings.value("DownloadListColsWidth", QString()).toString();
  if(!line.isEmpty()) {
    width_list = line.split(' ');
  }
  for(short i=0; i<nbColumns; ++i){
    if(downloadList->columnWidth(i)<1 && width_list.size() == DLListModel->columnCount()-1 && width_list.at(i).toInt()>=1) {
      // load the former width
      new_width_list << width_list.at(i);
    } else if(downloadList->columnWidth(i)>=1) { 
      // usual case, save the current width
      new_width_list << QString::fromUtf8(misc::toString(downloadList->columnWidth(i)).c_str());
    } else { 
      // default width
      downloadList->resizeColumnToContents(i);
      new_width_list << QString::fromUtf8(misc::toString(downloadList->columnWidth(i)).c_str());
    }
  }
  settings.setValue(QString::fromUtf8("DownloadListColsWidth"), new_width_list.join(QString::fromUtf8(" ")));
  qDebug("Download list columns width saved");
}

// Load columns width in a file that were saved previously
// (download list)
bool DownloadingTorrents::loadColWidthDLList() {
  qDebug("Loading columns width for download list");
  QSettings settings(QString::fromUtf8("qBittorrent"), QString::fromUtf8("qBittorrent"));
  QString line = settings.value(QString::fromUtf8("DownloadListColsWidth"), QString()).toString();
  if(line.isEmpty())
    return false;
  QStringList width_list = line.split(QString::fromUtf8(" "));
  if(width_list.size() != DLListModel->columnCount()-1) {
    qDebug("Corrupted values for download list columns sizes");
    return false;
  }
  unsigned int listSize = width_list.size();
  for(unsigned int i=0; i<listSize; ++i) {
        downloadList->header()->resizeSection(i, width_list.at(i).toInt());
  }
  // Loading last sorted column
  QString sortedCol = settings.value(QString::fromUtf8("DownloadListSortedCol"), QString()).toString();
  if(!sortedCol.isEmpty()) {
    Qt::SortOrder sortOrder;
    if(sortedCol.endsWith(QString::fromUtf8("d")))
      sortOrder = Qt::DescendingOrder;
    else
      sortOrder = Qt::AscendingOrder;
    sortedCol = sortedCol.left(sortedCol.size()-1);
    int index = sortedCol.toInt();
    sortDownloadList(index, sortOrder, true);
  }
  qDebug("Download list columns width loaded");
  return true;
}

// Called when a torrent is added
void DownloadingTorrents::torrentAdded(QString path, QTorrentHandle& h, bool fastResume) {
  QString hash = h.hash();
  if(BTSession->isFinished(hash)) {
    return;
  }
  int row = DLListModel->rowCount();
  // Adding torrent to download list
  DLListModel->insertRow(row);
  DLListModel->setData(DLListModel->index(row, NAME), QVariant(h.name()));
  DLListModel->setData(DLListModel->index(row, SIZE), QVariant((qlonglong)h.actual_size()));
  DLListModel->setData(DLListModel->index(row, DLSPEED), QVariant((double)0.));
  DLListModel->setData(DLListModel->index(row, UPSPEED), QVariant((double)0.));
  DLListModel->setData(DLListModel->index(row, SEEDSLEECH), QVariant(QString::fromUtf8("0/0")));
  DLListModel->setData(DLListModel->index(row, RATIO), QVariant(misc::toQString(BTSession->getRealRatio(hash))));
  DLListModel->setData(DLListModel->index(row, ETA), QVariant((qlonglong)-1));
  DLListModel->setData(DLListModel->index(row, HASH), QVariant(hash));
  // Pause torrent if it was paused last time
  // Not using isPaused function because torrents are paused after checking now
  if(QFile::exists(misc::qBittorrentPath()+QString::fromUtf8("BT_backup")+QDir::separator()+hash+QString::fromUtf8(".paused"))) {
    DLListModel->setData(DLListModel->index(row, NAME), QVariant(QIcon(QString::fromUtf8(":/Icons/skin/paused.png"))), Qt::DecorationRole);
    setRowColor(row, QString::fromUtf8("red"));
  }else{
    DLListModel->setData(DLListModel->index(row, NAME), QVariant(QIcon(QString::fromUtf8(":/Icons/skin/connecting.png"))), Qt::DecorationRole);
    setRowColor(row, QString::fromUtf8("grey"));
  }
  if(!fastResume) {
    setInfoBar(tr("'%1' added to download list.", "'/home/y/xxx.torrent' was added to download list.").arg(path));
  }else{
    setInfoBar(tr("'%1' resumed. (fast resume)", "'/home/y/xxx.torrent' was resumed. (fast resume)").arg(path));
  }
  ++nbTorrents;
  emit unfinishedTorrentsNumberChanged(nbTorrents);
}

// Called when trying to add a duplicate torrent
void DownloadingTorrents::torrentDuplicate(QString path) {
  setInfoBar(tr("'%1' is already in download list.", "e.g: 'xxx.avi' is already in download list.").arg(path));
}

void DownloadingTorrents::torrentCorrupted(QString path) {
  setInfoBar(tr("Unable to decode torrent file: '%1'", "e.g: Unable to decode torrent file: '/home/y/xxx.torrent'").arg(path), QString::fromUtf8("red"));
  setInfoBar(tr("This file is either corrupted or this isn't a torrent."),QString::fromUtf8("red"));
}

void DownloadingTorrents::updateFileSizeAndProgress(QString hash) {
  int row = getRowFromHash(hash);
  Q_ASSERT(row != -1);
  QTorrentHandle h = BTSession->getTorrentHandle(hash);
  DLListModel->setData(DLListModel->index(row, SIZE), QVariant((qlonglong)h.actual_size()));
  DLListModel->setData(DLListModel->index(row, PROGRESS), QVariant((double)h.progress()));
}

// Called when we couldn't listen on any port
// in the given range.
void DownloadingTorrents::portListeningFailure() {
  setInfoBar(tr("Couldn't listen on any of the given ports."), QString::fromUtf8("red"));
}

// Set the color of a row in data model
void DownloadingTorrents::setRowColor(int row, QColor color) {
  unsigned int nbColumns = DLListModel->columnCount()-1;
  for(unsigned int i=0; i<nbColumns; ++i) {
    DLListModel->setData(DLListModel->index(row, i), QVariant(color), Qt::ForegroundRole);
  }
}

// return the row of in data model
// corresponding to the given the hash
int DownloadingTorrents::getRowFromHash(QString hash) const{
  unsigned int nbRows = DLListModel->rowCount();
  for(unsigned int i=0; i<nbRows; ++i) {
    if(DLListModel->data(DLListModel->index(i, HASH)) == hash) {
      return i;
    }
  }
  return -1;
}

void DownloadingTorrents::displayDownloadingUrlInfos(QString url) {
  setInfoBar(tr("Downloading '%1', please wait...", "e.g: Downloading 'xxx.torrent', please wait...").arg(url), QPalette::WindowText);
}
