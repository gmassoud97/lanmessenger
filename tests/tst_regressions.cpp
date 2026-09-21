#include <QtTest>
#include <QDataStream>
#include <QFile>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>

#include "filemodelview.h"
#include "netstreamer.h"

class RegressionTests : public QObject {
    Q_OBJECT

private slots:
    void zeroByteSenderCompletes();
    void zeroByteReceiverCreatesFileAndCompletes();
    void fragmentedMessageFrameIsReassembled();
    void transferDateSurvivesSaveAndLoad();
    void legacyTransferDateIsMigrated();
};

void RegressionTests::zeroByteSenderCompletes() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString sourcePath = temp.filePath("empty.txt");
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    source.close();

    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));

    const QString transferId("0123456789abcdef0123456789abcdef");
    const QString localId("sender-id");
    FileSender sender(transferId, localId, "receiver-id", sourcePath,
        "empty.txt", 0, "127.0.0.1", server.serverPort(), FT_Normal);

    int completed = 0;
    int errors = 0;
    connect(&sender, &FileSender::progressUpdated,
        [&](FileMode mode, FileOp op, FileType, QString*, QString*, QString*) {
            if(mode == FM_Send && op == FO_Complete)
                ++completed;
            if(op == FO_Error)
                ++errors;
        });

    sender.init();
    QTRY_VERIFY(server.hasPendingConnections());
    QScopedPointer<QTcpSocket> peer(server.nextPendingConnection());
    QVERIFY(peer);

    const QByteArray expectedHeader = QByteArray("FILE") + transferId.toLocal8Bit() + localId.toLocal8Bit();
    QTRY_COMPARE(peer->bytesAvailable(), qint64(expectedHeader.size()));
    QCOMPARE(peer->readAll(), expectedHeader);
    QCOMPARE(peer->write("START"), qint64(5));
    QVERIFY(peer->flush());

    QTRY_COMPARE(completed, 1);
    QCOMPARE(errors, 0);
    QCOMPARE(QFileInfo(sourcePath).size(), qint64(0));
}

void RegressionTests::zeroByteReceiverCreatesFileAndCompletes() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString destinationPath = temp.filePath("received/empty.txt");

    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QTcpSocket client;
    client.connectToHost(QHostAddress::LocalHost, server.serverPort());
    QVERIFY(client.waitForConnected(2000));
    QVERIFY(server.waitForNewConnection(2000));
    QTcpSocket* accepted = server.nextPendingConnection();
    QVERIFY(accepted);

    FileReceiver receiver("0123456789abcdef0123456789abcdef", "sender-id",
        destinationPath, "empty.txt", 0, "127.0.0.1", server.serverPort(), FT_Normal);
    int completed = 0;
    int errors = 0;
    connect(&receiver, &FileReceiver::progressUpdated,
        [&](FileMode mode, FileOp op, FileType, QString*, QString*, QString*) {
            if(mode == FM_Receive && op == FO_Complete)
                ++completed;
            if(op == FO_Error)
                ++errors;
        });

    receiver.init(accepted);
    QTRY_COMPARE(completed, 1);
    QCOMPARE(errors, 0);
    QTRY_COMPARE(client.bytesAvailable(), qint64(5));
    QCOMPARE(client.readAll(), QByteArray("START"));
    QVERIFY(QFileInfo::exists(destinationPath));
    QCOMPARE(QFileInfo(destinationPath).size(), qint64(0));
}

void RegressionTests::fragmentedMessageFrameIsReassembled() {
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QTcpSocket client;
    client.connectToHost(QHostAddress::LocalHost, server.serverPort());
    QVERIFY(client.waitForConnected(2000));
    QVERIFY(server.waitForNewConnection(2000));
    QTcpSocket* accepted = server.nextPendingConnection();
    QVERIFY(accepted);

    MsgStream stream("local-id", "peer-id", "127.0.0.1", server.serverPort());
    QByteArray received;
    int receivedCount = 0;
    connect(&stream, &MsgStream::messageReceived,
        [&](QString*, QString*, QByteArray& data) {
            received = data;
            ++receivedCount;
        });
    stream.init(accepted);

    QByteArray payload(130000, '\0');
    for(int i = 0; i < payload.size(); ++i)
        payload[i] = char(i % 251);
    QByteArray framed;
    QDataStream out(&framed, QIODevice::WriteOnly);
    out << quint32(payload.size());
    out.writeRawData(payload.constData(), payload.size());

    const QList<int> chunks = QList<int>() << 2 << 1 << 7 << 4096 << 30000 << 65535;
    int offset = 0;
    for(int chunk : chunks) {
        if(offset >= framed.size())
            break;
        const int length = qMin(chunk, framed.size() - offset);
        QCOMPARE(client.write(framed.constData() + offset, length), qint64(length));
        QVERIFY(client.flush());
        offset += length;
        QTest::qWait(5);
    }
    if(offset < framed.size()) {
        const int length = framed.size() - offset;
        QCOMPARE(client.write(framed.constData() + offset, length), qint64(length));
        QVERIFY(client.flush());
    }

    QTRY_COMPARE(receivedCount, 1);
    QCOMPARE(received, payload);
}

void RegressionTests::transferDateSurvivesSaveAndLoad() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString historyPath = temp.filePath("transfers.lst");
    const QDateTime date(QDate(2026, 9, 15), QTime(17, 17));

    FileModel source;
    FileView view("dated-transfer");
    view.mode = FileView::TM_Send;
    view.state = FileView::TS_Complete;
    view.filePath = "C:/tmp/empty.txt";
    view.fileDisplay = "empty.txt (0 bytes)";
    view.startTime = date;
    source.insertItem(0, &view);
    source.saveData(historyPath);

    FileModel loaded;
    loaded.loadData(historyPath);
    QCOMPARE(loaded.rowCount(), 1);
    QVERIFY(loaded.item(0));
    QCOMPARE(loaded.item(0)->startTime, date);
    QCOMPARE(loaded.item(0)->fileDisplay, QString("empty.txt (0 bytes)"));
    QCOMPARE(loaded.item(0)->sizeHint().height(), 74);
}

void RegressionTests::legacyTransferDateIsMigrated() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString historyPath = temp.filePath("legacy-transfers.lst");
    QFile file(historyPath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QDataStream out(&file);
    out << quint32(1)
        << QString("legacy-transfer")
        << qint32(FileView::TM_Receive)
        << QString("C:/tmp/empty.txt")
        << QString("Issa")
        << QString("empty.txt (0 bytes) - 2026-09-15 17:17")
        << qint32(FileView::TS_Complete)
        << QPixmap();
    file.close();

    FileModel loaded;
    loaded.loadData(historyPath);
    QCOMPARE(loaded.rowCount(), 1);
    QVERIFY(loaded.item(0));
    QCOMPARE(loaded.item(0)->fileDisplay, QString("empty.txt (0 bytes)"));
    QCOMPARE(loaded.item(0)->startTime,
        QDateTime(QDate(2026, 9, 15), QTime(17, 17)));
}

QTEST_MAIN(RegressionTests)
#include "tst_regressions.moc"
