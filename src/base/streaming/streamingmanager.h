
#pragma once

#include <QObject>
#include <QHash>

#include "base/http/irequesthandler.h"
#include "base/http/server.h"

class StreamFile;

namespace BitTorrent
{
    class Torrent;
}

class StreamingManager : public QObject, public Http::IRequestHandler
{
    Q_DISABLE_COPY(StreamingManager)

public:
    static void initInstance();
    static void freeInstance();
    static StreamingManager *instance();

    StreamingManager(QObject *parent = nullptr);
    void playFile(int fileIndex, BitTorrent::Torrent *torrent);

private slots:
    void removeServingTorrent(const BitTorrent::Torrent *handle);

private:
    void handleRequest(const Http::Request &request, Http::Connection *connection) override;
    void doHead(const Http::Request &request, Http::Connection *connection);
    void doGet(const Http::Request &request, Http::Connection *connection);
    void startListening();

    StreamFile *findFile(int fileIndex, BitTorrent::Torrent *torrent) const;
    StreamFile *findFile(const QString &path) const;
    QString url(const StreamFile *) const;

    static StreamingManager *m_instance;
    QVector<StreamFile *> m_files;
    Http::Server m_server;
};
