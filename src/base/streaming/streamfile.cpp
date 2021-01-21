#include "streamfile.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QTimer>
#include <QMimeType>
#include <QMimeDatabase>

#include "base/bittorrent/torrent.h"
#include "base/bittorrent/torrentinfo.h"

namespace
{
    const int MIN_DEADLINE_TIME = 32;
    const int MAX_DEADLINE_TIME = 320;
    const int BUFFER_SIZE = 32 * 1024 * 1024; // 64 MiB

    struct PieceRange
    {
        int start = -1, end = -1;
    };
}

class ReadRequestPrivate : public ReadRequest
{
public:
    ReadRequestPrivate(quint64 initialPosition, quint64 maxSize, QObject *parent = nullptr)
        : ReadRequest(parent)
        , m_currentPosition(initialPosition)
        , m_leftSize(maxSize)
    {
        m_timeSinceLastFeed.start();
    }

    ~ReadRequestPrivate()
    {
        if (leftSize() != 0)
            emit cancelled();
    }

    void feed(const QByteArray &data)
    {
        Q_ASSERT(data.size() <= m_leftSize);
        Q_ASSERT(leftSize() != 0);

        m_currentPosition += data.size();
        m_leftSize -= data.size();
        m_isBlockPending = true;
        m_timeSinceLastFeed.restart();
        emit bytesRead(data, (m_leftSize == 0));
    }

    void setAdvanceRange(const PieceRange &range)
    {
        m_advanceRange = range;
    }

    PieceRange advanceRange()
    {
        return m_advanceRange;
    }

    quint64 currentPosition() const
    {
        return m_currentPosition;
    }

    quint64 leftSize() const
    {
        return m_leftSize;
    }

    void notifyError(const QString &message)
    {
        emit error(message);
    }

    qint64 timeSinceLastFeed() const
    {
        return m_timeSinceLastFeed.elapsed();
    }

private:
    PieceRange m_advanceRange;
    quint64 m_currentPosition {};
    quint64 m_leftSize {};
    QElapsedTimer m_timeSinceLastFeed;
};

bool ReadRequest::outstandingRead() const
{
    return m_isBlockPending;
}

void ReadRequest::notifyBlockReceived()
{
    Q_ASSERT(m_isBlockPending);
    m_isBlockPending = false;
    emit received();
}

StreamFile::StreamFile(int fileIndex, BitTorrent::Torrent *torrent, QObject *parent)
    : QObject(parent)
    , m_torrent(torrent)
    , m_fileIndex(fileIndex)
{
    const BitTorrent::TorrentInfo info = m_torrent->info();
    m_name = info.name() + '/' + info.fileName(fileIndex);
    m_mimeType = QMimeDatabase().mimeTypeForFile(m_name).name();
    m_size = info.fileSize(fileIndex);
    m_lastPiece = info.filePieces(fileIndex).last();
    m_pieceLength = info.pieceLength();
}

qint64 StreamFile::size() const
{
    return m_size;
}

QString StreamFile::name() const
{
    return m_name;
}

QString StreamFile::mimeType() const
{
    return m_mimeType;
}

BitTorrent::Torrent *StreamFile::torrent() const
{
    return m_torrent;
}

ReadRequest *StreamFile::read(const quint64 position, const quint64 size)
{
    ReadRequestPrivate *request = new ReadRequestPrivate(position, size, this);
    connect(request, &ReadRequest::received, this,
            [this, request]() { doRead(request); });

    connect(request, &ReadRequest::cancelled, this, [this, request]()
    {
        const auto range = request->advanceRange();
        if (range.start == -1 || range.end == -1)
            return;
        for (int i = range.start; i <= range.end; i++)
            m_torrent->resetPieceDeadline(i);
    });

    doRead(request);
    return request;
}

void StreamFile::doRead(ReadRequestPrivate *request)
{
    if (request->leftSize() == 0)
    {
        request->deleteLater();
        return;
    }

    const int deadlineTime = std::min<int>(std::max<int>(request->timeSinceLastFeed(), MIN_DEADLINE_TIME), MAX_DEADLINE_TIME);
    const BitTorrent::PieceFileInfo pieceInfo = m_torrent->info().mapFile(
        m_fileIndex, request->currentPosition(), std::min<quint64>(request->leftSize(), m_pieceLength));
    BitTorrent::PieceRequest *pieceRequest =
        m_torrent->havePiece(pieceInfo.index)
            ? m_torrent->readPiece(pieceInfo.index)
            : m_torrent->setPieceDeadline(pieceInfo.index, deadlineTime, true);

    Q_ASSERT(pieceRequest);
    pieceRequest->setParent(request);

    connect(pieceRequest, &BitTorrent::PieceRequest::complete, request
            , [request, pieceInfo, pieceRequest](const QByteArray &data)
    {
        request->feed(data.mid(pieceInfo.start, pieceInfo.length));
        pieceRequest->deleteLater();
    });

    connect(pieceRequest, &BitTorrent::PieceRequest::error, request, [pieceRequest, request](const QString &error)
    {
        request->notifyError(error);
        pieceRequest->deleteLater();
    });

    if (pieceInfo.index < m_lastPiece)
    {
        PieceRange advanceRange;
        advanceRange.start = pieceInfo.index + 1;
        advanceRange.end = qMin<int>(pieceInfo.index + std::ceil(BUFFER_SIZE / static_cast<double>(m_pieceLength)), m_lastPiece);
        Q_ASSERT(advanceRange.start <= advanceRange.end);

        for (int i = 1, pieceIndex = advanceRange.start;
             pieceIndex <= advanceRange.end; pieceIndex++, i++)
        {
            if (!m_torrent->havePiece(pieceIndex))
                m_torrent->setPieceDeadline(pieceIndex, deadlineTime * (i + 1), false); // prepare for next read
        }
        request->setAdvanceRange(advanceRange);
    }
}

int StreamFile::fileIndex() const
{
    return m_fileIndex;
}

int StreamFile::pieceLength() const
{
    return m_pieceLength;
}
