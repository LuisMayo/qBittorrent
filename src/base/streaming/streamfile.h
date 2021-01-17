#pragma once

#include <QObject>
#include <QQueue>

namespace BitTorrent
{
    class Torrent;
}

class ReadRequestPrivate;
class ReadRequest : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(ReadRequest)

public:
    using QObject::QObject;

    void notifyBlockReceived();
    bool outstandingRead() const;

signals:
    void bytesRead(const QByteArray &data, bool isLastBlock);
    void error(const QString &message);
    void received();
    void cancelled();

protected:
    bool m_isBlockPending = false;
};

class StreamFile : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(StreamFile)

public:
    StreamFile(int fileIndex, BitTorrent::Torrent *torrentHandle, QObject *parent = nullptr);

    qint64 size() const;
    QString name() const;
    QString mimeType() const;
    BitTorrent::Torrent *torrent() const;
    int pieceLength() const;
    int fileIndex() const;

    // ReadRequest is owned by callee
    ReadRequest *read(quint64 position, quint64 size);

private:
    void doRead(ReadRequestPrivate *request);

    BitTorrent::Torrent *m_torrent;
    int m_fileIndex;
    QString m_name;
    QString m_mimeType;
    qlonglong m_size;
    int m_lastPiece;
    int m_pieceLength;
};
