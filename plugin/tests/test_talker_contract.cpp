// SPDX-FileCopyrightText: 2026 by Christian McHugh <mchugh19@users.noreply.github.com>
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Contract tests for ImmichTalker against a stub HTTP server:
// the exact request shapes the Immich API demands and the
// response parsing the window relies on. Every API-shape bug
// this project has hit (a 400 for a wrongly-shaped tag body,
// say) belongs to the class these tests pin down, and they run
// without a server or a network.
//
// The stub records each request (method, path, content type,
// JSON body) and answers from canned routes, so a test reads:
// given this API call, the talker must send exactly this, and
// given this reply, it must emit exactly that.

#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>

#include "immichsession.h"
#include "immichtalker.h"

using namespace DigikamGenericImmichPlugin;

class StubServer : public QObject
{
    Q_OBJECT

public:

    struct Request
    {
        QString    method;
        QString    path;
        QString    contentType;
        QJsonObject body;
    };

    QList<Request> requests;
    QMap<QString, QPair<int, QByteArray> > routes;   // "PUT /api/tags" -> (status, body)

    explicit StubServer(QObject* parent = nullptr)
        : QObject(parent)
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            QTcpSocket* const socket = m_server.nextPendingConnection();
            m_buffers.insert(socket, QByteArray());
            connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
                QByteArray& buf = m_buffers[socket];
                buf += socket->readAll();
                const int headerEnd = buf.indexOf("\r\n\r\n");

                if (headerEnd < 0)
                {
                    return;
                }

                const QString head = QString::fromLatin1(buf.left(headerEnd));
                int contentLength = 0;

                QString contentType;

                for (const QString& line : head.split(QStringLiteral("\r\n")))
                {
                    if (line.startsWith(QStringLiteral("Content-Length:"), Qt::CaseInsensitive))
                    {
                        contentLength = line.mid(15).trimmed().toInt();
                    }

                    if (line.startsWith(QStringLiteral("Content-Type:"), Qt::CaseInsensitive))
                    {
                        contentType = line.mid(13).trimmed();
                    }
                }

                if (buf.size() < headerEnd + 4 + contentLength)
                {
                    return;   // body still arriving
                }

                const QByteArray body = buf.mid(headerEnd + 4, contentLength);
                const QStringList firstLine = head.split(QStringLiteral("\r\n"))
                                                  .value(0).split(QLatin1Char(' '));
                Request req;
                req.method      = firstLine.value(0);
                req.path        = firstLine.value(1);
                req.contentType = contentType;
                req.body        = QJsonDocument::fromJson(body).object();
                requests.append(req);

                const QPair<int, QByteArray> route = routes.value(
                    req.method + QLatin1Char(' ') + req.path, qMakePair(404, QByteArray("{}")));
                const QByteArray payload = route.second;
                QByteArray response = QStringLiteral("HTTP/1.1 %1\r\nContent-Type: application/json\r\n"
                                                     "Content-Length: %2\r\nConnection: close\r\n\r\n")
                                          .arg(route.first).arg(payload.size()).toLatin1();
                response += payload;
                socket->write(response);
                socket->disconnectFromHost();
                m_buffers.remove(socket);
            });
        });
    }

    bool listen()
    {
        return m_server.listen(QHostAddress::LocalHost, 0);
    }

    quint16 port() const
    {
        return m_server.serverPort();
    }

private:

    QTcpServer m_server;
    QHash<QTcpSocket*, QByteArray> m_buffers;
};

class TestTalkerContract : public QObject
{
    Q_OBJECT

private:

    StubServer*  m_stub = nullptr;
    ImmichSession* m_session = nullptr;
    ImmichTalker*  m_talker = nullptr;

    void makeTalker()
    {
        m_stub = new StubServer(this);
        QVERIFY(m_stub->listen());
        m_session = new ImmichSession;
        m_session->setUrl(QStringLiteral("http://127.0.0.1:%1").arg(m_stub->port()));
        m_talker = new ImmichTalker(m_session, this);
    }

private Q_SLOTS:

    void init()
    {
        makeTalker();
    }

    void bulkTagBodyUsesTagIdsArray()
    {
        m_stub->routes.insert(QStringLiteral("PUT /api/tags/assets"), qMakePair(200, QByteArray("{}")));
        QSignalSpy spy(m_talker, &ImmichTalker::signalBulkTagged);
        m_talker->bulkTagAssets(QStringLiteral("t1"), { QStringLiteral("a1"), QStringLiteral("a2") });
        QVERIFY(spy.wait(5000));
        QCOMPARE(spy.at(0).at(1).toInt(), 200);

        QCOMPARE(m_stub->requests.size(), 1);
        const StubServer::Request req = m_stub->requests.at(0);
        QCOMPARE(req.method, QStringLiteral("PUT"));
        QCOMPARE(req.path, QStringLiteral("/api/tags/assets"));
        // 3.3.3's TagBulkAssetsDto: tagIds is an array; a lone
        // "tagId" field is a 400 against the real server.
        const QJsonArray expectedTags { QStringLiteral("t1") };
        const QJsonArray expectedAssets { QStringLiteral("a1"), QStringLiteral("a2") };
        QVERIFY(!req.body.contains(QStringLiteral("tagId")));
        QCOMPARE(req.body.value(QStringLiteral("tagIds")).toArray(), expectedTags);
        QCOMPARE(req.body.value(QStringLiteral("assetIds")).toArray(), expectedAssets);
    }

    void createAlbumShapeAndParse()
    {
        m_stub->routes.insert(QStringLiteral("POST /api/albums"),
                              qMakePair(201, QByteArray("{\"id\":\"alb1\",\"albumName\":\"Holidays\","
                                                        "\"assetCount\":0}")));
        ImmichAlbum created;
        bool gotCreated = false;
        connect(m_talker, &ImmichTalker::signalAlbumCreated, this,
                [&created, &gotCreated](const ImmichAlbum& album) {
                    created    = album;
                    gotCreated = true;
                });
        m_talker->createAlbum(QStringLiteral("Holidays"));
        QTRY_VERIFY(gotCreated);
        QCOMPARE(created.id, QStringLiteral("alb1"));
        QCOMPARE(created.name, QStringLiteral("Holidays"));

        QCOMPARE(m_stub->requests.size(), 1);
        const StubServer::Request req = m_stub->requests.at(0);
        QCOMPARE(req.method, QStringLiteral("POST"));
        QCOMPARE(req.path, QStringLiteral("/api/albums"));
        // CreateAlbumDto (3.3.3): albumName is the one required
        // field, and the only one this call sends.
        QCOMPARE(req.body.size(), 1);
        QCOMPARE(req.body.value(QStringLiteral("albumName")).toString(),
                 QStringLiteral("Holidays"));
        // The body must also arrive AS JSON. Without the content
        // type Qt posts form-urlencoded, the server's JSON parser
        // never runs, albumName is missing at validation, and
        // the real server answers 400. This is the assertion the
        // original code failed.
        QVERIFY(req.contentType.startsWith(QStringLiteral("application/json")));
    }

    void errorSurfacesServerMessage()
    {
        m_stub->routes.insert(QStringLiteral("POST /api/albums"),
                              qMakePair(400, QByteArray("{\"message\":\"albumName must be a string\","
                                                        "\"error\":\"Bad Request\",\"statusCode\":400}")));
        QSignalSpy spy(m_talker, &ImmichTalker::signalError);
        m_talker->createAlbum(QStringLiteral("Holidays"));
        QVERIFY(spy.wait(5000));
        // Qt's own text only reports the status code; the reason
        // the server sent in the body's message field must reach
        // the error the window displays.
        QVERIFY(spy.at(0).at(0).toString().contains(QStringLiteral("albumName must be a string")));
    }

    void deleteAssetsBodyShape()
    {
        m_stub->routes.insert(QStringLiteral("DELETE /api/assets"), qMakePair(204, QByteArray()));
        QSignalSpy spy(m_talker, &ImmichTalker::signalAssetsDeleted);
        m_talker->deleteAssets({ QStringLiteral("a1"), QStringLiteral("a2") });
        QVERIFY(spy.wait(5000));
        QCOMPARE(spy.at(0).at(0).toInt(), 204);

        const QJsonArray expectedIds { QStringLiteral("a1"), QStringLiteral("a2") };
        const StubServer::Request req = m_stub->requests.at(0);
        QCOMPARE(req.method, QStringLiteral("DELETE"));
        QCOMPARE(req.body.value(QStringLiteral("ids")).toArray(), expectedIds);
    }

    void createStackShapeAndParse()
    {
        m_stub->routes.insert(QStringLiteral("POST /api/stacks"),
                              qMakePair(201, QByteArray("{\"id\":\"s1\",\"primaryAssetId\":\"n1\"}")));
        QSignalSpy spy(m_talker, &ImmichTalker::signalStackCreated);
        m_talker->createStack({ QStringLiteral("n1"), QStringLiteral("o1") });
        QVERIFY(spy.wait(5000));
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("n1"));   // first id
        QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("s1"));
        QCOMPARE(spy.at(0).at(2).toInt(), 201);

        const QJsonArray expectedIds { QStringLiteral("n1"), QStringLiteral("o1") };
        const StubServer::Request req = m_stub->requests.at(0);
        QCOMPARE(req.body.value(QStringLiteral("assetIds")).toArray(), expectedIds);
    }

    void fetchStackParsesMembers()
    {
        m_stub->routes.insert(QStringLiteral("GET /api/stacks/s1"),
                              qMakePair(200, QByteArray("{\"id\":\"s1\",\"assets\":"
                                                        "[{\"id\":\"n1\"},{\"id\":\"o1\"},{\"id\":\"orig\"}]}")));
        QSignalSpy spy(m_talker, &ImmichTalker::signalStackFetched);
        m_talker->fetchStack(QStringLiteral("s1"));
        QVERIFY(spy.wait(5000));
        QCOMPARE(spy.at(0).at(1).toStringList(),
                 QStringList({ QStringLiteral("n1"), QStringLiteral("o1"), QStringLiteral("orig") }));
    }

    void searchByNameScopesToAlbum()
    {
        m_stub->routes.insert(QStringLiteral("POST /api/search/metadata"),
                              qMakePair(200, QByteArray("{\"assets\":{\"items\":[{\"id\":\"a1\"}],\"total\":1}}")));
        QSignalSpy spy(m_talker, &ImmichTalker::signalIdentitySearch);

        const QJsonArray expectedAlbums { QStringLiteral("album1") };
        m_talker->searchByFileName(QStringLiteral("x.jpg"), QStringLiteral("album1"));
        QVERIFY(spy.wait(5000));
        QCOMPARE(spy.at(0).at(1).value<QJsonArray>().size(), 1);

        StubServer::Request req = m_stub->requests.at(0);
        QCOMPARE(req.body.value(QStringLiteral("originalFileName")).toString(), QStringLiteral("x.jpg"));
        QCOMPARE(req.body.value(QStringLiteral("albumIds")).toArray(), expectedAlbums);

        m_stub->requests.clear();
        m_talker->searchByFileName(QStringLiteral("x.jpg"));
        QVERIFY(spy.wait(5000));
        // The stub records the request before replying, so the successful
        // spy wait guarantees exactly one new request was recorded.
        QCOMPARE(m_stub->requests.size(), 1);
        req = m_stub->requests.at(0);   // cppcheck-suppress containerOutOfBounds
        QVERIFY(!req.body.contains(QStringLiteral("albumIds")));
    }

    void syncRecordPassesStatusThrough()
    {
        m_stub->routes.insert(QStringLiteral("GET /api/assets/a1"), qMakePair(403, QByteArray("{}")));
        QSignalSpy spy(m_talker, &ImmichTalker::signalSyncRecord);
        m_talker->fetchAssetForSync(QStringLiteral("a1"));
        QVERIFY(spy.wait(5000));
        // A 403 is data for the sync (permissions), not an error.
        QCOMPARE(spy.at(0).at(2).toInt(), 403);
    }

    void bulkCheckParseAndShape()
    {
        m_stub->routes.insert(QStringLiteral("POST /api/assets/bulk-upload-check"),
                              qMakePair(200, QByteArray("{\"results\":["
                                                        "{\"id\":\"/p1\",\"action\":\"reject\",\"assetId\":\"srv1\"},"
                                                        "{\"id\":\"/p2\",\"action\":\"accept\"}]}")));
        QSignalSpy spy(m_talker, &ImmichTalker::signalUploadCheck);
        m_talker->checkUploads({ { QStringLiteral("/p1"), QStringLiteral("sum1") },
                                 { QStringLiteral("/p2"), QStringLiteral("sum2") } });
        QVERIFY(spy.wait(5000));

        const QMap<QString, QString> dups = spy.at(0).at(0).value<QMap<QString, QString> >();
        QCOMPARE(dups.size(), 1);
        QCOMPARE(dups.value(QStringLiteral("/p1")), QStringLiteral("srv1"));

        const QJsonArray sent = m_stub->requests.at(0).body.value(QStringLiteral("assets")).toArray();
        QCOMPARE(sent.size(), 2);
        QCOMPARE(sent.at(0).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("/p1"));
        QCOMPARE(sent.at(0).toObject().value(QStringLiteral("checksum")).toString(), QStringLiteral("sum1"));
    }
};

int main(int argc, char** argv)
{
    qputenv("XDG_CONFIG_HOME", "/tmp/immich-talker-test-config");
    QCoreApplication app(argc, argv);
    TestTalkerContract test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_talker_contract.moc"
