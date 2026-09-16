#include <QProcess>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QtTest>
#include <signal.h>
#include "ink/inkdocument.h"
#include "storage/database.h"
#include "storage/library.h"
#include "storage/pagestore.h"
#include "storage/schema.h"

class TstSoak : public QObject {
    Q_OBJECT
    static int countStrokes(const QString &dir) {
        Database db; if (!db.open(dir + "/lumen.db") || ensureSchema(db) == 0) return -1;
        Library lib(db); const qint64 pid = lib.firstPageId();
        InkDocument doc; PageStore store(db, dir + "/journal"); store.attach(&doc);
        if (!store.load(pid)) return -1;
        return doc.count();
    }
private slots:
    void tenThousandStrokesPersistWithoutLoss() {
        QTemporaryDir dir;
        QProcess p; p.setProcessChannelMode(QProcess::ForwardedErrorChannel);
        p.start(QCoreApplication::applicationDirPath() + "/soak_writer", {dir.path(), "10000"});
        QVERIFY(p.waitForStarted(5000));
        QVERIFY2(p.waitForFinished(120000), "writer did not finish");
        const QString out = QString::fromUtf8(p.readAllStandardOutput());
        QVERIFY(out.contains("done 10000"));
        QCOMPARE(countStrokes(dir.path()), 10000);
    }
    void killMinusNineLosesNothingAcknowledged_data() {
        QTest::addColumn<int>("delayMs");
        QTest::newRow("early") << 150; QTest::newRow("mid") << 600; QTest::newRow("late") << 1500;
    }
    void killMinusNineLosesNothingAcknowledged() {
        QFETCH(int, delayMs);
        QTemporaryDir dir;
        QProcess p;
        p.start(QCoreApplication::applicationDirPath() + "/soak_writer", {dir.path(), "1000000"});
        QVERIFY(p.waitForStarted(5000));
        QTest::qWait(delayMs + QRandomGenerator::global()->bounded(200));
        const qint64 pid = p.processId();
        ::kill(pid_t(pid), SIGKILL);
        p.waitForFinished(5000);
        int lastAck = -1;
        for (const QByteArray &line : p.readAllStandardOutput().split('\n'))
            if (line.startsWith("ack ")) lastAck = line.mid(4).trimmed().toInt();
        QVERIFY2(lastAck > 0, "writer never acknowledged anything");
        const int recovered = countStrokes(dir.path());
        qInfo() << "killed after" << delayMs << "ms; acknowledged" << lastAck << "recovered" << recovered;
        QVERIFY2(recovered >= lastAck, qPrintable(QStringLiteral("lost strokes: acked %1, recovered %2").arg(lastAck).arg(recovered)));
        QVERIFY(recovered <= lastAck + 1);   // at most the one stroke whose ack line never printed
    }
};
QTEST_GUILESS_MAIN(TstSoak)
#include "tst_soak.moc"
