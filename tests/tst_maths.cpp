#include <QSignalSpy>
#include <QtTest>
#include "workers/workersupervisor.h"

// Drives the maths worker the way MathsService does. Everything here is a line a student would
// actually write; the answers are the ones a marker would expect.
class TstMaths : public QObject {
    Q_OBJECT
    static QJsonObject callSync(WorkerSupervisor &w, const QString &method, const QJsonObject &params, int ms = 30000) {
        QSignalSpy spy(&w, &WorkerSupervisor::response);
        const int id = w.request(method, params);
        QElapsedTimer t; t.start();
        while (t.elapsed() < ms) {
            QTest::qWait(10);
            for (int i = 0; i < spy.count(); ++i)
                if (spy[i].at(0).toInt() == id) {
                    if (!spy[i].at(2).toJsonObject().isEmpty()) qWarning() << method << "error:" << spy[i].at(2).toJsonObject();
                    return spy[i].at(1).toJsonObject();
                }
        }
        qWarning() << method << "timed out"; return {};
    }
private slots:
    void solvesWhatAStudentWrites() {
        WorkerSupervisor w("maths");
        w.start();
        QTRY_VERIFY_WITH_TIMEOUT(w.state() == WorkerSupervisor::State::Ready, 60000);

        struct Case { const char *input; const char *expect; };
        const QVector<Case> cases{
            {"2+3\\times4=", "14"},                       // arithmetic, times sign and all
            {"\\frac{3}{4}+\\frac{1}{4}=", "1"},          // fractions from pix2tex
            {"\\frac{1}{3}=", "1/3"},                     // exact, not 0.333…
            {"\\sqrt{144}=", "12"},
            {"3x + 2 = 11", "x = 3"},                     // solve for the unknown
            {"x^2 - 5x + 6 = 0", "x = 2, 3"},             // both roots
            {"\\sin(0)+\\cos(0)=", "1"},
        };
        for (const Case &c : cases) {
            const QJsonObject r = callSync(w, "solve", {{"expression", QString::fromUtf8(c.input)}});
            QVERIFY2(r.value("ok").toBool(), qPrintable(QStringLiteral("%1: %2").arg(c.input, r.value("reason").toString())));
            QCOMPARE(r.value("result").toString(), QString::fromUtf8(c.expect));
        }

        // E is energy here, not Euler's number — and earlier lines on the page supply the values
        QJsonObject r = callSync(w, "solve", {{"expression", "E = m c^2"}, {"context", QJsonArray{"m = 2", "c = 3"}}});
        QVERIFY(r.value("ok").toBool());
        QCOMPARE(r.value("result").toString(), QStringLiteral("E = 18"));

        // a value defined earlier is used later
        r = callSync(w, "solve", {{"expression", "2v"}, {"context", QJsonArray{"v = 3"}}});
        QCOMPARE(r.value("result").toString(), QStringLiteral("6"));

        // both sides known: it says whether it is true
        r = callSync(w, "solve", {{"expression", "2+2 = 5"}});
        QCOMPARE(r.value("result").toString(), QStringLiteral("false"));

        // and it refuses rather than inventing an answer
        r = callSync(w, "solve", {{"expression", ""}});
        QVERIFY(!r.value("ok").toBool());

        r = callSync(w, "define", {{"lines", QJsonArray{"v = 3", "a = 9.81", "not maths"}}});
        const QJsonObject vars = r.value("variables").toObject();
        QCOMPARE(vars.value("v").toString(), QStringLiteral("3"));
        QCOMPARE(vars.value("a").toString(), QStringLiteral("9.81"));
        QCOMPARE(vars.size(), 2);
    }
};
QTEST_MAIN(TstMaths)
#include "tst_maths.moc"
