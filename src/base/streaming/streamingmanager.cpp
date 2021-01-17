#include "streamingmanager.h"

#include <QDir>
#include <QProcess>
#include <QTimer>

#include "base/bittorrent/session.h"
#include "base/http/connection.h"
#include "base/http/httperror.h"
#include "base/http/types.h"
#include "base/logger.h"
#include "streamfile.h"

namespace
{
    struct Range
    {
        qint64 firstBytePos, lastBytePos;
        static Range fromHttpRangeField(const QString &value, qint64 fileSize, bool &ok)
        {
            // range value field should be in form -> "<unit>=<range>"
            // where unit is "bytes"
            // range is "<firstBytePos>-<lastBytePos>"
            ok = false;
            const int unitRangeSeparatorIndex = value.indexOf('=');
            if (unitRangeSeparatorIndex == -1)
            {
                return {};
            }

            const QString unit = value.left(unitRangeSeparatorIndex);
            const QString acceptedUnit = QLatin1String("bytes");
            if (unit != acceptedUnit)
            {
                return {};
            }

            const int rangeSeparatorIndex = value.indexOf('-', (unitRangeSeparatorIndex + 1));
            if (rangeSeparatorIndex == -1)
            {
                return {};
            }

            bool isValidInt;
            const qint64 firstBytePos = value.midRef(unitRangeSeparatorIndex + 1, (rangeSeparatorIndex - unitRangeSeparatorIndex - 1))
                                            .toULongLong(&isValidInt);
            if (!isValidInt)
            {
                return {};
            }

            qint64 lastBytePos =
                value.midRef(rangeSeparatorIndex + 1).toULongLong(&isValidInt);
            if (!isValidInt)
                // lastBytePos is optional, if not provided we should assume lastBytePos is at the end of the file
                lastBytePos = fileSize - 1;

            if (firstBytePos > lastBytePos)
            {
                return {};
            }

            ok = true;
            return {firstBytePos, lastBytePos};
        }

        qint64 size() const
        {
            return lastBytePos - firstBytePos + 1;
        }
    };

    QString vlcPath()
    {
        const auto tryVar = [](const char *var) {
            const QString expandedEnv = qgetenv(var);
            if (expandedEnv.isEmpty())
                return QString {};
            const QDir dir {expandedEnv};
            if (!dir.exists() || !dir.exists("VideoLAN/VLC/vlc.exe"))
                return QString {};
            return dir.absoluteFilePath("VideoLAN/VLC/vlc.exe");
        };
        const QString vlc = tryVar("PROGRAMFILES");
        return vlc.isEmpty() ? tryVar("ProgramFiles(x86)") : vlc;
    }
}

StreamingManager *StreamingManager::m_instance = nullptr;

void StreamingManager::initInstance()
{
    if (!m_instance)
        m_instance = new StreamingManager;
}

void StreamingManager::freeInstance()
{
    if (m_instance)
        delete m_instance;
}

StreamingManager *StreamingManager::instance()
{
    return m_instance;
}

StreamingManager::StreamingManager(QObject *parent)
    : QObject {parent}
    , m_server {this, this}
{
    startListening();
    connect(BitTorrent::Session::instance(), &BitTorrent::Session::torrentAboutToBeRemoved
            , this, &StreamingManager::removeServingTorrent);
}

void StreamingManager::playFile(int fileIndex, BitTorrent::Torrent *torrent)
{
    Q_ASSERT(m_server.isListening());

    StreamFile *file {findFile(fileIndex, torrent)};
    if (!file)
    {
        file = new StreamFile(fileIndex, torrent, this);
        m_files.push_back(file);
    }

    QProcess::startDetached(vlcPath(), {url(file)});
}

void StreamingManager::removeServingTorrent(const BitTorrent::Torrent *torrent)
{
    for (auto iter = m_files.begin(); iter != m_files.end();)
    {
        if ((*iter)->torrent() != torrent)
        {
            iter++;
            continue;
        }

        // remove torrent
        delete (*iter);
        iter = m_files.erase(iter);
    }
}

QString StreamingManager::url(const StreamFile *file) const
{
    return file ? QString("http://localhost:%1/%2")
                      .arg(QString::number(m_server.serverPort()),
                          QString(file->name().toUtf8().toPercentEncoding()))
                : QString {};
}

void StreamingManager::handleRequest(const Http::Request &request, Http::Connection *connection)
{
    try
    {
        if (request.method == Http::HEADER_REQUEST_METHOD_HEAD)
            doHead(request, connection);
        else if (request.method == Http::HEADER_REQUEST_METHOD_GET)
            doGet(request, connection);
        else
            throw MethodNotAllowedHTTPError();
    }
    catch (const HTTPError &error)
    {
        connection->sendStatus(
            {static_cast<uint>(error.statusCode()), error.statusText()});
        if (!error.message().isEmpty())
        {
            connection->sendHeaders({{Http::HEADER_CONTENT_TYPE, Http::CONTENT_TYPE_TXT},
                {Http::HEADER_CONTENT_LENGTH,
                    QString::number(error.message().length())}});
            connection->sendContent(error.message().toUtf8());
        }
        connection->close();
    }
}

void StreamingManager::doHead(const Http::Request &request, Http::Connection *connection)
{
    const StreamFile *file = findFile(request.path);
    if (!file)
        throw NotFoundHTTPError();

    connection->sendStatus({200, "Ok"});
    connection->sendHeaders(
    {
        {"accept-ranges", "bytes"},
        {"connection", "close"},
        {"content-length", QString::number(file->size())},
        {Http::HEADER_CONTENT_TYPE, file->mimeType()}
    });

    connection->close();
}

void StreamingManager::doGet(const Http::Request &request, Http::Connection *connection)
{
    const QString rangeValue = request.headers.value("range");
    if (rangeValue.isEmpty())
        doHead(request, connection);

    StreamFile *file = findFile(request.path);
    if (!file)
        throw NotFoundHTTPError();

    bool isValidRange = true;
    const Range range = Range::fromHttpRangeField(rangeValue, file->size(), isValidRange);
    if (!isValidRange)
        throw InvalidRangeHTTPError();

    connection->sendStatus({206, "Partial Content"});
    connection->sendHeaders(
    {
        {"accept-ranges", "bytes"},
        {"content-length", QString::number(range.size())},
        {Http::HEADER_CONTENT_TYPE, file->mimeType()},
        {"content-range", QString("bytes %1-%2/%3")
                              .arg(QString::number(range.firstBytePos)
                                   , QString::number(range.lastBytePos)
                                   , QString::number(file->size()))
        }
    });

    ReadRequest *readRequest {file->read(range.firstBytePos, range.size())};
    readRequest->setParent(connection);

    connect(readRequest, &ReadRequest::bytesRead, connection, [connection](const QByteArray &data, const bool isLastBlock)
    {
        connection->sendContent(data);
        if (isLastBlock)
            connection->close();
    });

    connect(connection, &Http::Connection::bytesWritten, readRequest, [readRequest, connection, pieceLength = file->pieceLength()]()
    {
        if ((connection->bytesToWrite() < pieceLength)
            && readRequest->outstandingRead())
            readRequest->notifyBlockReceived();
    });

    connect(readRequest, &ReadRequest::error, connection, [connection, range, readRequest](const QString &message)
    {
        LogMsg(tr("Failed to serve request in range [%1,%2]. Reason: %3")
                   .arg(QString::number(range.firstBytePos)
                        , QString::number(range.lastBytePos)
                        , message)
                , Log::CRITICAL);

        connection->close();
        readRequest->deleteLater();
    });

    connect(file, &QObject::destroyed, connection,
            [connection]() { connection->close(); });
}

void StreamingManager::startListening()
{
    const QHostAddress ip = QHostAddress::Any;
    const int port = 0;

    if (m_server.isListening())
    {
        if (m_server.serverPort() == port)
            // Already listening on the right port, just return
            return;

        // Wrong port, closing the server
        m_server.close();
    }

    // Listen on the predefined port
    const bool listenSuccess = m_server.listen(ip, port);

    if (listenSuccess)
    {
        LogMsg(tr("Torrent streaming server: Now listening on IP: %1, port: %2")
                   .arg(ip.toString(), QString::number(m_server.serverPort()))
                   , Log::INFO);
    }
    else
    {
        LogMsg(tr("Torrent streaming server: Unable to bind to IP: %1, port: %2. Reason: %3")
                   .arg(ip.toString(), QString::number(port), m_server.errorString()),
               Log::WARNING);
    }
}

StreamFile *StreamingManager::findFile(int fileIndex, BitTorrent::Torrent *torrent) const
{
    const auto fileIter =
        std::find_if(m_files.begin(), m_files.end(), [fileIndex, torrent](const StreamFile *file)
    {
        return file->fileIndex() == fileIndex && file->torrent() == torrent;
    });

    return (fileIter == m_files.end()) ? nullptr : *fileIter;
}

StreamFile *StreamingManager::findFile(const QString &path) const
{
    const QString pathWithoutSep = path.startsWith('/') ? path.mid(1) : path;
    const auto fileIter =
        std::find_if(m_files.begin(), m_files.end(), [pathWithoutSep](const StreamFile *file)
    {
        return file->name() == pathWithoutSep;
    });

    return (fileIter == m_files.end()) ? nullptr : *fileIter;
}
