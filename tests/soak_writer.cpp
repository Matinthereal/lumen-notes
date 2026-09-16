// Writes strokes through PageStore as fast as it can and prints "ack N" after each one is journaled.
// tst_soak kills it with SIGKILL at a random moment and checks that every acknowledged stroke survived.
#include <QCoreApplication>
#include <QTimer>
#include <cstdio>
#include "ink/inkdocument.h"
#include "storage/database.h"
#include "storage/library.h"
#include "storage/pagestore.h"
#include "storage/schema.h"

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 3) { fprintf(stderr, "usage: soak_writer <datadir> <count>\n"); return 2; }
    const QString dir = QString::fromLocal8Bit(argv[1]);
    const int count = atoi(argv[2]);
    Database db; if (!db.open(dir + "/lumen.db") || ensureSchema(db) == 0) return 3;
    Library lib(db); lib.seedDefaults();
    const qint64 pid = lib.firstPageId();
    InkDocument doc; PageStore store(db, dir + "/journal"); store.setIdleMs(300); store.attach(&doc);
    if (!store.load(pid)) return 4;
    printf("page %lld start %d\n", (long long)pid, doc.count()); fflush(stdout);
    int n = 0;
    QTimer t; t.setInterval(0);
    QObject::connect(&t, &QTimer::timeout, [&] {
        if (n >= count) { store.flush(); printf("done %d\n", n); fflush(stdout); app.quit(); return; }
        Stroke s; s.width = 1.5f;
        const float x = float(n % 700), y = float((n * 7) % 1000);
        for (int k = 0; k < 12; ++k) s.points.append({x + k, y + (k % 3), 0.5f, 0, 0, quint32(k * 3)});
        doc.addStroke(s);
        ++n;
        if (store.journalFailures() == 0) { printf("ack %d\n", n); fflush(stdout); }
    });
    t.start();
    return app.exec();
}
