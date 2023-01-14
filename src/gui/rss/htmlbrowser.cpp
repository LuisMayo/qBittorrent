/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2013  Mladen Milinkovic <max@smoothware.net>
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

#include "htmlbrowser.h"

#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QImageReader>
#include <QBuffer>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QScrollBar>
#include <QStyle>
#include <QTimer>

#include "base/global.h"
#include "base/path.h"
#include "base/profile.h"

namespace
{
    QImage fitImage(QIODevice *device, const QSize &maxSize)
    {
        QImageReader reader;
        reader.setDevice(device);

        const auto expectedSize = reader.size();
        if (expectedSize.width() > maxSize.width())
        {
            reader.setScaledSize(expectedSize.scaled(maxSize, Qt::KeepAspectRatio));
        }

        const auto image = reader.read();
        if (image.width() <= maxSize.width())
            return image;

        return image.scaled(maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

}

NetImageLoader::NetImageLoader(QObject *parent)
    : QObject {parent}
    , m_netManager {std::make_unique<QNetworkAccessManager>(this)}
{
    m_netManager->setAutoDeleteReplies(true);
    connect(m_netManager.get(), &QNetworkAccessManager::finished, this, &NetImageLoader::handleReplyFinished);
}

void NetImageLoader::load(const QUrl &url)
{
    QMetaObject::invokeMethod(this, [this, url]() { _loadImpl(url); });
}

const QSize &NetImageLoader::maxLoadSize() const
{
    QMutexLocker locker(&m_lock);
    return m_maxLoadSize;
}

void NetImageLoader::setMaxLoadSize(const QSize &newMaxLoadSize)
{
    QMutexLocker locker(&m_lock);
    m_maxLoadSize = newMaxLoadSize;
}

void NetImageLoader::_loadImpl(const QUrl &url)
{
    if (m_activeRequests.contains(url))
        return;

    qDebug() << "NetImageLoader::load() get " << url.toString();
    m_activeRequests.insert(url);

    auto reply = m_netManager->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::downloadProgress, this, &NetImageLoader::handleProgressUpdated);

    connect(this, &NetImageLoader::abortDownloads, reply, [this, reply]()
    {
        qDebug() << "NetImageLoader::abortDownloads abort" << reply->request().url();

        m_activeRequests.remove(reply->request().url());
        m_dirty.remove(reply);

        reply->abort();
    });
}

void NetImageLoader::handleReplyFinished(QNetworkReply *reply)
{
    m_dirty.remove(reply);
    if (reply->error() != QNetworkReply::NoError)
    {
        qDebug() << "NetImageLoader::handleReplyFinished, error" << reply->error();
        return;
    }

    QSize loadSize = maxLoadSize();
    m_activeRequests.remove(reply->url());

    emit finished(reply->url(), fitImage(reply, loadSize));
}

void NetImageLoader::handleProgressUpdated()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    assert(reply);

    m_dirty.insert(reply);
    if (m_readIncompleteImagesEnqueued)
        return;

    m_readIncompleteImagesEnqueued = true;
    QTimer::singleShot(500, this, &NetImageLoader::readIncompleteImages);
}

void NetImageLoader::readIncompleteImages()
{
    m_readIncompleteImagesEnqueued = false;

    auto loadSize = maxLoadSize();

    QByteArray buf;
    for (auto reply : qAsConst(m_dirty))
    {
        buf.resize(reply->bytesAvailable());
        auto read = reply->peek(buf.data(), reply->bytesAvailable());
        buf.resize(read);

        QBuffer ioBuffer;
        ioBuffer.setBuffer(&buf);

        qDebug() << "NetImageLoader::readIncompleteImages" << reply->url() << buf.size();
        emit updated(reply->url(), fitImage(&ioBuffer, loadSize));
    }

    m_dirty.clear();
}

class HtmlBrowser::ImageCache : public QObject
{
    const int CLEAN_TIMEOUT = 50000;
    const int MAX_SIZE_IN_BYTES = 100 * 1024 * 1024;

public:
    ImageCache()
    {
        auto timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, &ImageCache::clean);
        timer->start(CLEAN_TIMEOUT);
    }

    void insert(QUrl url, QImage image, bool pending)
    {
        if (m_map.contains(url))
        {
            m_sizeInBytes -= m_map[url]->image.sizeInBytes();

            m_list.erase(m_map[url]);
            m_map.remove(url);
        }

        data d;
        d.url = url;
        d.image = image;
        d.pending = pending;
        d.timer.start();

        m_sizeInBytes += image.sizeInBytes();

        m_list.push_front(d);
        m_map[url] = m_list.begin();
    }

    std::pair<QImage, bool> value(QUrl url)
    {
        if (!m_map.contains(url))
            return {};

        auto iter = m_map[url];
        auto data = *iter;
        m_list.erase(iter); // NOTE: iter is not invalidated

        data.timer.restart();
        m_list.push_front(data);
        m_map[url] = m_list.begin();
        return {data.image, data.pending};
    }

    bool contains(const QUrl &url) const
    {
        return m_map.contains(url);
    }

private:
    void clean()
    {
        while (!m_list.empty()
               && (m_sizeInBytes >= MAX_SIZE_IN_BYTES
                   || m_list.back().timer.hasExpired(CLEAN_TIMEOUT)))
        {
            auto data = m_list.rbegin();

            m_sizeInBytes -= data->image.sizeInBytes();
            assert(m_sizeInBytes >= 0);

            qDebug() << "HTMLBrowser::ImageCache clean" << data->url << "new sizeInBytes" << (m_sizeInBytes / (1024. * 1024.));

            m_map.remove(data->url);
            m_list.pop_back();
        }
    }

    struct data
    {
        QUrl url;
        QImage image;
        bool pending = false;
        QElapsedTimer timer {};
    };

    QHash<QUrl, std::list<data>::iterator> m_map;
    std::list<data> m_list;
    int64_t m_sizeInBytes = 0;
};



HtmlBrowser::HtmlBrowser(QWidget *parent)
    : QTextBrowser(parent)
    , m_imageLoader {std::make_unique<NetImageLoader>()}
    , m_imageCache {std::make_unique<ImageCache>()}
{
    m_workerThread.start();

    m_imageLoader->moveToThread(&m_workerThread);

    connect(m_imageLoader.get(), &NetImageLoader::finished, this, [this](const QUrl &url, QImage image)
    {
        resourceLoaded(url, image, false);
    });

    connect(m_imageLoader.get(), &NetImageLoader::updated, this, [this](const QUrl &url, QImage image)
    {
        resourceLoaded(url, image, true);
    });
}

HtmlBrowser::~HtmlBrowser()
{
    m_imageLoader.release();

    m_workerThread.quit();
    m_workerThread.wait();
}

void HtmlBrowser::setContentHTML(const QString &html)
{
    emit m_imageLoader->abortDownloads();
    QTextBrowser::setHtml(html);
}

QVariant HtmlBrowser::loadResource(int type, const QUrl &name)
{
    if (type == QTextDocument::ImageResource)
    {
        QUrl url(name);
        if (url.scheme().isEmpty())
            url.setScheme(u"http"_qs);

        // TODO add support for gif files
        if (Path(url.path()).hasExtension(u".gif"_qs))
            return {};

        std::pair<QImage, bool> data {{}, true};
        if (m_imageCache->contains(url))
            data = m_imageCache->value(url);

        // the cached image is pending, request to download
        if (data.second)
            m_imageLoader->load(url);

        return data.first;
    }

    return QTextBrowser::loadResource(type, name);
}

void HtmlBrowser::resourceLoaded(const QUrl &url, QImage image, bool pending)
{
    if (image.isNull())
    {
        // If resource failed to load, replace it with warning icon and store it in cache
        image = QApplication::style()->standardIcon(QStyle::SP_MessageBoxWarning).pixmap(32, 32).toImage();
    }

    m_imageCache->insert(url, image, pending);
    enqueueRefresh();
}

void HtmlBrowser::resizeEvent(QResizeEvent *event)
{
    auto scrollBarWidth = style()->pixelMetric(QStyle::PM_ScrollBarExtent);
    QSize loadSize = size().shrunkBy(QMargins(0, 0, scrollBarWidth + 24, 0));
    m_imageLoader->setMaxLoadSize(loadSize);

    QTextBrowser::resizeEvent(event);
}

void HtmlBrowser::enqueueRefresh()
{
    if (m_refreshEnqueued)
        return;

    m_refreshEnqueued = true;

    QTimer::singleShot(200, this, [this]()
    {
        m_refreshEnqueued = false;

        // Refresh the document display and keep scrollbars where they are
        int sx = horizontalScrollBar()->value();
        int sy = verticalScrollBar()->value();
        document()->setHtml(document()->toHtml());
        horizontalScrollBar()->setValue(sx);
        verticalScrollBar()->setValue(sy);

        setReadOnly(true);
    });
}

