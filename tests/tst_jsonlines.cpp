#include <QtTest>
#include "ipc/jsonlines.h"

class TstJsonLines : public QObject {
    Q_OBJECT
private slots:
    void splitAcrossChunks() {
        JsonLineBuffer b;
        b.append("{\"a\":1}\n{\"b\":");
        QJsonObject o;
        QVERIFY(b.next(o)); QCOMPARE(o.value("a").toInt(), 1);
        QVERIFY(!b.next(o));
        b.append("2}\n");
        QVERIFY(b.next(o)); QCOMPARE(o.value("b").toInt(), 2);
        QVERIFY(!b.next(o));
    }
    void manyPerChunk() {
        JsonLineBuffer b;
        b.append("{\"n\":1}\n\n{\"n\":2}\n{\"n\":3}\n");
        QJsonObject o; int seen = 0;
        while (b.next(o)) ++seen;
        QCOMPARE(seen, 3);
    }
    void invalidLinesAreDroppedNotFatal() {
        JsonLineBuffer b;
        b.append("not json\n[1,2]\n{\"ok\":true}\n");
        QJsonObject o;
        QVERIFY(b.next(o));
        QVERIFY(o.value("ok").toBool());
        QCOMPARE(b.droppedLines(), 2);
    }
    void encodeRoundTrip() {
        const QJsonObject in{{"id", 7}, {"method", "ping"}, {"params", QJsonObject{{"x", "y"}}}};
        const QByteArray bytes = JsonLineBuffer::encode(in);
        QVERIFY(bytes.endsWith('\n'));
        QVERIFY(!bytes.chopped(1).contains('\n'));
        JsonLineBuffer b; b.append(bytes);
        QJsonObject out; QVERIFY(b.next(out));
        QCOMPARE(out, in);
    }
};
QTEST_GUILESS_MAIN(TstJsonLines)
#include "tst_jsonlines.moc"
