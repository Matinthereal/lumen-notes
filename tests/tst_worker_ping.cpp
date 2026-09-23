#include <QSignalSpy>
#include <QtTest>
#include "workers/workersupervisor.h"

static bool waitReady(WorkerSupervisor &w, int ms = 20000)
{
    QElapsedTimer t; t.start();
    while (t.elapsed() < ms) {
        if (w.state() == WorkerSupervisor::State::Ready) return true;
        QTest::qWait(20);
    }
    return false;
}

class TstWorkerPing : public QObject {
    Q_OBJECT
private slots:
    void pingRoundTrip() {
        WorkerSupervisor w("ping");
        w.start();
        QVERIFY2(waitReady(w), "worker never became ready (is python3 on PATH?)");
        QSignalSpy spy(&w, &WorkerSupervisor::response);
        const int id = w.request("ping");
        QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1, 10000);
        const auto args = spy.takeFirst();
        QCOMPARE(args.at(0).toInt(), id);
        QVERIFY(args.at(2).toJsonObject().isEmpty());
        QVERIFY(args.at(1).toJsonObject().value("pong").toBool());
    }
    void echoRoundTripsParams() {
        WorkerSupervisor w("ping");
        w.start();
        QVERIFY(waitReady(w));
        QSignalSpy spy(&w, &WorkerSupervisor::response);
        w.request("echo", QJsonObject{{"text", "ünïcode ok"}, {"n", 42}});
        QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1, 10000);
        const QJsonObject r = spy.takeFirst().at(1).toJsonObject();
        QCOMPARE(r.value("text").toString(), QStringLiteral("ünïcode ok"));
        QCOMPARE(r.value("n").toInt(), 42);
    }
    void unknownMethodIsAnError() {
        WorkerSupervisor w("ping");
        w.start();
        QVERIFY(waitReady(w));
        QSignalSpy spy(&w, &WorkerSupervisor::response);
        w.request("does-not-exist");
        QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1, 10000);
        const QJsonObject err = spy.takeFirst().at(2).toJsonObject();
        QCOMPARE(err.value("code").toInt(), -32601);
        QCOMPARE(w.state(), WorkerSupervisor::State::Ready); // still alive
    }
    void notReadyAnswersWithAnErrorNotAHang() {
        WorkerSupervisor w("ping"); // never started
        QSignalSpy spy(&w, &WorkerSupervisor::response);
        w.request("ping");
        QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1, 2000);
        QVERIFY(!spy.takeFirst().at(2).toJsonObject().isEmpty());
    }
    void crashFailsTheRequestsItTookWithIt() {
        WorkerSupervisor w("ping");
        w.start();
        QVERIFY(waitReady(w));
        QSignalSpy spy(&w, &WorkerSupervisor::response);
        const int dying = w.request("crash");          // the worker exits before it can answer this
        const int after = w.request("ping");           // written to the same socket right behind it
        QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 2, 15000);
        QSet<int> failed;
        for (const auto &args : spy) {
            if (!args.at(2).toJsonObject().isEmpty()) failed.insert(args.at(0).toInt());
        }
        QVERIFY2(failed.contains(dying), "the request the worker died on must fail, not hang");
        QVERIFY2(failed.contains(after), "a request queued behind the crash must fail too");
    }
    void progressIsReportedAndClears() {
        WorkerSupervisor w("ping");
        w.start();
        QVERIFY(waitReady(w));
        QSignalSpy progress(&w, &WorkerSupervisor::progressed);
        QSignalSpy spy(&w, &WorkerSupervisor::response);
        const int id = w.request("slow", QJsonObject{{"steps", 6}, {"delay", 0.1}});
        QTRY_VERIFY_WITH_TIMEOUT(progress.count() >= 2, 10000);
        QCOMPARE(progress.first().at(0).toInt(), id);
        QVERIFY(w.busy());
        QVERIFY(w.progress() >= 0 && w.progress() < 1);
        QVERIFY(w.activity().startsWith(QLatin1String("step ")));
        QCOMPARE(w.status(), QStringLiteral("busy"));
        QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1, 10000);
        QVERIFY(spy.first().at(2).toJsonObject().isEmpty());
        QVERIFY(!w.busy());
        QCOMPARE(w.progress(), -1.0);
        QCOMPARE(w.status(), QStringLiteral("ready"));
    }
    void cancelStopsALongRequest() {
        WorkerSupervisor w("ping");
        w.start();
        QVERIFY(waitReady(w));
        QSignalSpy progress(&w, &WorkerSupervisor::progressed);
        QSignalSpy spy(&w, &WorkerSupervisor::response);
        const int id = w.request("slow", QJsonObject{{"steps", 100}, {"delay", 0.1}});
        QTRY_VERIFY_WITH_TIMEOUT(progress.count() >= 1, 10000);
        QElapsedTimer t; t.start();
        w.cancel(id);
        QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1, 3000);
        QVERIFY2(t.elapsed() < 2000, "a cancelled request must stop at its next step, not run out its ten seconds");
        QCOMPARE(spy.first().at(0).toInt(), id);
        QCOMPARE(spy.first().at(2).toJsonObject().value("code").toInt(), WorkerSupervisor::CancelledCode);
        spy.clear();
        w.request("ping");                             // and the worker is still there for the next one
        QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1, 5000);
        QVERIFY(spy.first().at(2).toJsonObject().isEmpty());
    }
    void cancellingAQueuedRequestDropsIt() {
        WorkerSupervisor w("ping");
        w.setAutoStart(true);                          // not started: the request waits in the queue
        QSignalSpy spy(&w, &WorkerSupervisor::response);
        const int id = w.request("slow", QJsonObject{{"steps", 100}, {"delay", 0.1}});
        w.cancel(id);
        QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1, 3000);
        QCOMPARE(spy.first().at(2).toJsonObject().value("code").toInt(), WorkerSupervisor::CancelledCode);
        w.stop();
    }
    void aMissingPackageIsNotInstalledNotACrashLoop() {
        qputenv("LUMEN_PING_IMPORT", "lumen_no_such_package");
        WorkerSupervisor w("ping");
        w.start();
        qunsetenv("LUMEN_PING_IMPORT");
        QTRY_VERIFY_WITH_TIMEOUT(w.state() == WorkerSupervisor::State::NotInstalled, 15000);
        QCOMPARE(w.missing(), QStringList{QStringLiteral("lumen_no_such_package")});
        QCOMPARE(w.status(), QStringLiteral("not installed"));
        QTest::qWait(1500);                            // past the first restart backoff
        QCOMPARE(w.state(), WorkerSupervisor::State::NotInstalled);
        QCOMPARE(w.restarts(), 0);
        QSignalSpy spy(&w, &WorkerSupervisor::response);
        w.request("ping");
        QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1, 1000);
        QCOMPARE(spy.first().at(2).toJsonObject().value("code").toInt(), -4);
        w.restart();                                   // installed now (the hook is gone): it comes up
        QVERIFY(waitReady(w));
        QVERIFY(w.missing().isEmpty());
    }
    void helloNamesMissingPackages() {
        qputenv("LUMEN_PING_REQUIRES", "lumen_absent_package,json");
        WorkerSupervisor w("ping");
        w.start();
        qunsetenv("LUMEN_PING_REQUIRES");
        QVERIFY(waitReady(w));
        QTRY_COMPARE_WITH_TIMEOUT(w.missing(), QStringList{QStringLiteral("lumen_absent_package")}, 5000);
        QCOMPARE(w.status(), QStringLiteral("not installed"));
    }
    void requestsDuringRestartBackoffAreQueuedNotRejected() {
        WorkerSupervisor w("ping");
        w.start();
        QVERIFY(waitReady(w));
        w.request("crash");
        QTRY_VERIFY_WITH_TIMEOUT(w.state() != WorkerSupervisor::State::Ready, 10000);
        QSignalSpy spy(&w, &WorkerSupervisor::response);
        const int id = w.request("ping");              // sent while the restart timer is pending
        QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1, 30000);
        const auto args = spy.takeFirst();
        QCOMPARE(args.at(0).toInt(), id);
        QVERIFY2(args.at(2).toJsonObject().isEmpty(), "should have been answered by the restarted worker");
        QVERIFY(args.at(1).toJsonObject().value("pong").toBool());
    }
    void restartsAfterCrash() {
        WorkerSupervisor w("ping");
        w.start();
        QVERIFY(waitReady(w));
        w.request("crash");
        QTRY_VERIFY_WITH_TIMEOUT(w.state() != WorkerSupervisor::State::Ready, 10000);
        QVERIFY2(waitReady(w, 30000), "worker was not restarted after a crash");
        QCOMPARE(w.restarts(), 1);
        QSignalSpy spy(&w, &WorkerSupervisor::response);
        w.request("ping");
        QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1, 10000);
        QVERIFY(spy.takeFirst().at(1).toJsonObject().value("pong").toBool());
    }
};
QTEST_GUILESS_MAIN(TstWorkerPing)
#include "tst_worker_ping.moc"
