#include <QFile>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <atomic>
#include <memory>
#include <thread>
#include "storage/database.h"
#include "ui/singleinstance.h"

namespace {
void writePdf(const QString &path, const QString &text)
{
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    QPainter p(&writer);
    p.drawText(QRectF(0, 400, writer.width(), 1200), Qt::AlignHCenter, text);
}
}

class TstSingleInstance : public QObject {
    Q_OBJECT
private slots:
    // Exec=lumen %U: files as paths or file:// URLs; flags and their values are not files.
    void filesFromTheCommandLine()
    {
        QTemporaryDir dir;
        const QString pdf = dir.filePath("paper.pdf"), book = dir.filePath("maths.lumen");
        for (const QString &f : {pdf, book}) { QFile out(f); QVERIFY(out.open(QIODevice::WriteOnly)); out.write("x"); }
        const QString before = QDir::currentPath();
        QDir::setCurrent(dir.path());
        const QList<QUrl> files = SingleInstance::filesIn({"lumen", "--screenshot", "paper.pdf", "--shot-js", "x()", "--uitest", "paper.pdf",
                                                           QUrl::fromLocalFile(book).toString(), "missing.pdf", "https://example.com/a.pdf"});
        QDir::setCurrent(before);
        QCOMPARE(files, (QList<QUrl>{QUrl::fromLocalFile(pdf), QUrl::fromLocalFile(book)}));
    }

    // A second launch on the same library hands its files (and the launcher's activation token) to
    // the first and is told to exit; a launch on another library is not.
    void secondLaunchHandsOver()
    {
        QTemporaryDir library, other;
        SingleInstance first(library.path());
        QVERIFY(!first.handOver({}, {}, 500));          // nobody running yet
        QVERIFY(first.listen());
        QSignalSpy spy(&first, &SingleInstance::handedOver);
        const QUrl file = QUrl::fromLocalFile(library.filePath("June 2016 paper.pdf"));
        std::atomic<int> handed{-1}, elsewhere{-1};
        std::thread second([&] {                        // blocking calls: off this thread, which serves the first
            SingleInstance again(library.path());
            handed = again.handOver({file}, "token-1") ? 1 : 0;
            SingleInstance another(other.path());
            elsewhere = another.handOver({file}, {}, 500) ? 1 : 0;
        });
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);
        second.join();
        QCOMPARE(handed.load(), 1);
        QCOMPARE(elsewhere.load(), 0);
        QCOMPARE(spy.first().at(0).value<QList<QUrl>>(), QList<QUrl>{file});
        QCOMPARE(spy.first().at(1).toString(), QStringLiteral("token-1"));
    }

    // A crash leaves the socket file behind: the next launch must not hand over to nobody.
    void staleSocketIsReplaced()
    {
        QTemporaryDir library;
        SingleInstance crashed(library.path());
        { QFile stale(crashed.serverName()); QVERIFY(stale.open(QIODevice::WriteOnly)); }
        SingleInstance next(library.path());
        QVERIFY(!next.handOver({}, {}, 500));
        QVERIFY(next.listen());
        QSignalSpy spy(&next, &SingleInstance::handedOver);
        std::atomic<int> handed{-1};
        std::thread later([&] { SingleInstance again(library.path()); handed = again.handOver({}) ? 1 : 0; });
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);
        later.join();
        QCOMPARE(handed.load(), 1);
    }

    // A launch that finds a Lumen listening leaves its socket alone, so later launches still reach it.
    void liveSocketIsKept()
    {
        QTemporaryDir library;
        SingleInstance first(library.path());
        QVERIFY(first.listen());
        SingleInstance second(library.path());
        QVERIFY(!second.listen());
        QSignalSpy spy(&first, &SingleInstance::handedOver);
        std::atomic<int> handed{-1};
        std::thread later([&] { SingleInstance again(library.path()); handed = again.handOver({}) ? 1 : 0; });
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);
        later.join();
        QCOMPARE(handed.load(), 1);
    }

    // Two launches at the same moment after a crash: one replaces the socket, the other finds it
    // listening. Before the lock both could see nobody answer, and the second removed the first's.
    void staleSocketGoesToOneOfTwoLaunches()
    {
        QTemporaryDir library;
        const QString socket = SingleInstance(library.path()).serverName();
        { QFile stale(socket); QVERIFY(stale.open(QIODevice::WriteOnly)); }
        std::atomic<int> listening{0}, done{0};
        std::atomic<bool> leave{false};
        auto launch = [&] {
            SingleInstance instance(library.path());
            if (instance.listen()) ++listening;
            ++done;
            while (!leave) QThread::msleep(10);         // the winner keeps listening until both have tried
        };
        std::unique_ptr<QThread> a(QThread::create(launch)), b(QThread::create(launch));
        a->start();
        b->start();
        QTRY_COMPARE_WITH_TIMEOUT(done.load(), 2, 15000);
        const int winners = listening.load();
        leave = true;
        QVERIFY(a->wait(5000));
        QVERIFY(b->wait(5000));
        QCOMPARE(winners, 1);
    }

    // The real app, offscreen: started with a PDF it imports it, and "Open with Lumen" on another
    // PDF while it runs gives that one to the same window and exits. The running one is a plain
    // launch: a --screenshot or --smoke run stays out of the single-instance guard.
    void openWithLumen()
    {
#ifdef Q_OS_WIN
        QSKIP("waits for the socket file, and a Windows pipe has none");
#endif
        QTemporaryDir data, files;
        const QString first = files.filePath("first handout.pdf"), second = files.filePath("second handout.pdf");
        writePdf(first, "First");
        writePdf(second, "Second");
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("QT_QPA_PLATFORM", "offscreen");
        env.insert("LUMEN_DATA_DIR", data.path());
        env.insert("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent");
        QProcess running;
        running.setProcessEnvironment(env);
        running.setProcessChannelMode(QProcess::ForwardedChannels);
        running.start(LUMEN_APP, {first});
        QVERIFY(running.waitForStarted());
        const QString socket = SingleInstance(data.path()).serverName();
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(socket), 30000);
        QProcess opener;
        opener.setProcessEnvironment(env);
        opener.setProcessChannelMode(QProcess::ForwardedChannels);
        QElapsedTimer took; took.start();
        opener.start(LUMEN_APP, {second});
        QVERIFY(opener.waitForFinished(30000));
        QCOMPARE(opener.exitStatus(), QProcess::NormalExit);
        QCOMPARE(opener.exitCode(), 0);
        QVERIFY2(took.elapsed() < 10000, "the second launch should hand over and leave, not run");
        const auto stop = [&] {
            running.terminate();
            if (!running.waitForFinished(10000)) running.kill();
            running.waitForFinished(5000);
            QFile::remove(socket);                      // ended by a signal, it never tidied its own
        };
        // The imports need the PDF helper, which CI does not install (its ctest line leaves out pdf and papers too).
        if (qEnvironmentVariableIsSet("CI")) { stop(); QSKIP("no PDF helper on CI: the hand-over is checked, the import is not"); }
        // The window stays open, so read its library while it runs. It has answered the opener, so
        // the schema is there.
        Database db;
        QVERIFY(db.open(data.filePath("lumen.db")));
        auto sections = [&db] {
            QStringList names;
            Database::Query q(db, "SELECT name FROM section WHERE deleted_at IS NULL");
            while (q.step()) names << q.text(0);
            return names;
        };
        QTRY_VERIFY2_WITH_TIMEOUT(sections().contains("first handout") && sections().contains("second handout"),
                                  qPrintable(sections().join(", ")), 60000);
        stop();
    }

    // A test or diagnostic run beside an open Lumen runs on its own instead of handing over and
    // exiting, and does not take the socket either.
    void diagnosticRunsStayOutOfTheGuard()
    {
        QTemporaryDir data;
        SingleInstance open(data.path());
        QVERIFY(open.listen());
        QSignalSpy spy(&open, &SingleInstance::handedOver);
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("QT_QPA_PLATFORM", "offscreen");
        env.insert("LUMEN_DATA_DIR", data.path());
        env.insert("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent");
        QProcess smoke;
        smoke.setProcessEnvironment(env);
        smoke.setProcessChannelMode(QProcess::ForwardedChannels);
        smoke.start(LUMEN_APP, {"--smoke"});
        QVERIFY(smoke.waitForFinished(60000));          // not spinning our event loop: a hand-over would wait, then show below
        QCOMPARE(smoke.exitStatus(), QProcess::NormalExit);
        QCOMPARE(smoke.exitCode(), 0);
        QTest::qWait(200);
        QCOMPARE(spy.count(), 0);
        QVERIFY2(QFileInfo::exists(data.filePath("lumen.db")), "the smoke run should have opened the library itself");
    }
};
QTEST_MAIN(TstSingleInstance)
#include "tst_singleinstance.moc"
