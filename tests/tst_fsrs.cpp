#include <QtTest>
#include "cards/fsrs.h"

using namespace fsrs;

class TstFsrs : public QObject {
    Q_OBJECT
private slots:
    void retrievabilityIsAnchoredAndDecreasing() {
        QCOMPARE(retrievability(0, 10), 1.0);
        QVERIFY(std::abs(retrievability(10, 10) - 0.9) < 1e-9);         // R(S, S) = 0.9 by construction
        QVERIFY(retrievability(20, 10) < retrievability(10, 10));
        QVERIFY(std::abs(intervalFor(10, 0.9) - 10.0) < 1e-9);          // and the interval for 90 % is S
        QVERIFY(intervalFor(10, 0.95) < intervalFor(10, 0.9));
    }
    void firstRatingsOrderStability() {
        Params p;
        QVERIFY(initialStability(p, Again) < initialStability(p, Hard));
        QVERIFY(initialStability(p, Hard) < initialStability(p, Good));
        QVERIFY(initialStability(p, Good) < initialStability(p, Easy));
        for (int g = 1; g <= 4; ++g) { const double d = initialDifficulty(p, g); QVERIFY(d >= 1 && d <= 10); }
        QVERIFY(initialDifficulty(p, Again) > initialDifficulty(p, Easy));
    }
    void goodStreakGrowsIntervalsAndAgainResets() {
        Params p; CardState s;
        Outcome o = review(p, s, Good, 0);
        QVERIFY(o.intervalDays >= 1 && o.intervalDays <= 10);
        double last = o.intervalDays; s = o.next;
        for (int i = 0; i < 6; ++i) {
            o = review(p, s, Good, last);          // reviewed exactly when due
            QVERIFY2(o.intervalDays > last, qPrintable(QStringLiteral("interval %1 after %2").arg(o.intervalDays).arg(last)));
            QVERIFY(o.next.difficulty >= 1 && o.next.difficulty <= 10);
            last = o.intervalDays; s = o.next;
        }
        QVERIFY(last > 30);
        const Outcome again = review(p, s, Again, last);
        QCOMPARE(again.intervalDays, 0.0);
        QVERIFY(again.next.stability < s.stability);
        QCOMPARE(again.next.lapses, 1);
        QCOMPARE(int(again.next.state), int(State::Relearning));
        // rating order on a mature-but-not-saturated card (the streak above may sit at the interval cap)
        CardState m; m.state = State::Review; m.stability = 12; m.difficulty = 5; m.reps = 4;
        QVERIFY(review(p, m, Easy, 12).intervalDays > review(p, m, Good, 12).intervalDays);
        QVERIFY(review(p, m, Hard, 12).intervalDays < review(p, m, Good, 12).intervalDays);
        QVERIFY(review(p, s, Good, last).intervalDays <= p.maxIntervalDays);
    }
    void sameDayReviewUsesShortTermRule() {
        Params p; CardState s; s.state = State::Review; s.stability = 5; s.difficulty = 5; s.reps = 3;
        const Outcome good = review(p, s, Good, 0.01);
        QVERIFY(good.next.stability > s.stability);
        const Outcome again = review(p, s, Again, 0.01);
        QVERIFY(again.next.stability < s.stability);
    }
    void deterministic() {
        Params p; CardState s;
        const Outcome a = review(p, s, Good, 0), b = review(p, s, Good, 0);
        QCOMPARE(a.next.stability, b.next.stability); QCOMPARE(a.intervalDays, b.intervalDays);
    }
};
QTEST_GUILESS_MAIN(TstFsrs)
#include "tst_fsrs.moc"
