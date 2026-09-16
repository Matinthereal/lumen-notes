#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <QProcess>
#include <QStandardPaths>
#include "storage/paths.h"
#include "workers/workersupervisor.h"

// The audio worker without a microphone: ffmpeg's lavfi sine as the input, tiny.en for speed,
// mpv with a null audio output. Proves capture → Opus, the live loop, the re-pass and playback.
class TstAudio : public QObject {
    Q_OBJECT
    static QJsonObject callSync(WorkerSupervisor &w, const QString &m, const QJsonObject &p, int ms = 120000) {
        QSignalSpy spy(&w, &WorkerSupervisor::response);
        const int id = w.request(m, p);
        QElapsedTimer t; t.start();
        while (t.elapsed() < ms) {
            QTest::qWait(20);
            for (int i = 0; i < spy.count(); ++i) if (spy[i].at(0).toInt() == id) {
                const QJsonObject err = spy[i].at(2).toJsonObject();
                if (!err.isEmpty()) qWarning() << m << "error:" << err;
                return spy[i].at(1).toJsonObject();
            }
        }
        qWarning() << m << "timed out"; return {};
    }
private slots:
    void captureTranscribePlay() {
        qputenv("LUMEN_MPV_AO", "null");
        QTemporaryDir dir;
        WorkerSupervisor w("audio");
        w.start();
        QTRY_VERIFY_WITH_TIMEOUT(w.state() == WorkerSupervisor::State::Ready, 60000);
        QJsonObject r = callSync(w, "sources", {});
        QVERIFY(r.value("sources").toArray().size() >= 1);
        r = callSync(w, "prepare", {{"models_dir", paths::modelsDir()}, {"live_model", "tiny.en"}}, 600000);   // first run downloads ~75 MB
        QVERIFY2(r.value("ok").toBool(), "tiny.en could not be prepared (network?)");
        const QString opus = dir.path() + "/rec.opus";
        r = callSync(w, "start", {{"out_path", opus}, {"source", "default"}, {"model", "tiny.en"}, {"live", true},
                                  {"input_args", QJsonArray{"-f", "lavfi", "-i", "sine=frequency=440:duration=3"}}});
        QVERIFY(r.value("ok").toBool());
        QTest::qWait(3600);
        r = callSync(w, "stop", {}, 90000);
        QVERIFY(r.value("ok").toBool());
        QVERIFY(QFileInfo(opus).size() > 2000);
        QVERIFY2(r.value("duration_ms").toInt() >= 2500 && r.value("duration_ms").toInt() <= 3600, qPrintable(QString::number(r.value("duration_ms").toInt())));
        r = callSync(w, "probe_duration", {{"path", opus}});
        QVERIFY(r.value("duration_ms").toInt() >= 2500);
        r = callSync(w, "transcribe_file", {{"path", opus}, {"model", "tiny.en"}}, 300000);
        QVERIFY(r.contains("segments"));                       // a sine wave has no words; the pipeline must still complete
        r = callSync(w, "play", {{"path", opus}, {"start_ms", 500}});
        QVERIFY(r.value("ok").toBool());
        QTest::qWait(900);
        r = callSync(w, "position", {});
        QVERIFY2(r.value("position_ms").toInt() >= 900, qPrintable(QString::number(r.value("position_ms").toInt())));
        r = callSync(w, "pause", {});
        QVERIFY(r.value("ok").toBool());
        const int paused = r.value("position_ms").toInt();
        QTest::qWait(400);
        r = callSync(w, "position", {});
        QVERIFY(std::abs(r.value("position_ms").toInt() - paused) < 150);   // really paused
        r = callSync(w, "seek", {{"ms", 2000}});
        QVERIFY(r.value("ok").toBool());
        callSync(w, "stop_playback", {});
    }
    void liveTranscriptionHearsSpeech() {
        const QString espeak = QStandardPaths::findExecutable("espeak-ng");
        if (espeak.isEmpty()) QSKIP("espeak-ng not installed");
        QTemporaryDir dir;
        const QString wav = dir.path() + "/speech.wav";
        QProcess::execute(espeak, {"-v", "en-gb", "-s", "150", "-w", wav, "The integral of x squared from zero to one is one third. Newton's second law states that force equals mass times acceleration."});
        QVERIFY(QFileInfo(wav).size() > 10000);
        WorkerSupervisor w("audio");
        w.start();
        QTRY_VERIFY_WITH_TIMEOUT(w.state() == WorkerSupervisor::State::Ready, 60000);
        // small.en — what the app ships with; tiny.en garbles espeak's synthetic voice
        QJsonObject r = callSync(w, "prepare", {{"models_dir", paths::modelsDir()}, {"live_model", "small.en"}}, 900000);
        QVERIFY(r.value("ok").toBool());
        QSignalSpy notes(&w, &WorkerSupervisor::notification);
        const QString opus = dir.path() + "/live.opus";
        r = callSync(w, "start", {{"out_path", opus}, {"source", "default"}, {"model", "small.en"}, {"live", true}, {"input_args", QJsonArray{"-re", "-i", wav}}});
        QVERIFY(r.value("ok").toBool());
        // segments must arrive WHILE recording, not only after stop
        QElapsedTimer t; t.start();
        QStringList liveText;
        while (t.elapsed() < 20000) {
            QTest::qWait(100);
            for (int i = 0; i < notes.count(); ++i) if (notes[i].at(0).toString() == "segments")
                for (const QJsonValue &v : notes[i].at(1).toJsonObject().value("segments").toArray()) { const QString tx = v.toObject().value("text").toString(); if (!liveText.contains(tx)) liveText << tx; }
            if (t.elapsed() > 11000 && !liveText.isEmpty()) break;
        }
        r = callSync(w, "stop", {}, 90000);
        for (int i = 0; i < notes.count(); ++i) if (notes[i].at(0).toString() == "segments")
            for (const QJsonValue &v : notes[i].at(1).toJsonObject().value("segments").toArray()) { const QString tx = v.toObject().value("text").toString(); if (!liveText.contains(tx)) liveText << tx; }
        const QString all = liveText.join(' ').toLower();
        qInfo() << "live heard:" << all;
        QVERIFY2(all.contains("integral") || all.contains("squared") || all.contains("third"), qPrintable(all));
        QVERIFY2(all.contains("newton") || all.contains("mass") || all.contains("acceleration"), qPrintable(all));
        bool listened = false;
        for (int i = 0; i < notes.count(); ++i) if (notes[i].at(0).toString() == "status" && notes[i].at(1).toJsonObject().value("text").toString() == "listening") listened = true;
        QVERIFY(listened);
        r = callSync(w, "transcribe_file", {{"path", opus}, {"model", "small.en"}}, 300000);
        QVERIFY(r.value("segments").toArray().size() >= 1);       // the re-pass sees speech too
        r = callSync(w, "sources", {});
        const QJsonArray srcs = r.value("sources").toArray();
        QVERIFY(srcs.size() >= 1);
        QVERIFY(srcs.at(0).toObject().contains("available"));
        QVERIFY(srcs.at(0).toObject().value("default").toBool());          // best first
        for (const QJsonValue &v : srcs) if (!v.toObject().value("available").toBool()) QVERIFY(v.toObject().value("description").toString().contains("nothing plugged in"));
        r = callSync(w, "check", {{"input_args", QJsonArray{"-f", "lavfi", "-i", "sine=frequency=440:duration=2"}}});
        QCOMPARE(r.value("verdict").toString(), QStringLiteral("alive"));
        r = callSync(w, "check", {{"input_args", QJsonArray{"-f", "lavfi", "-i", "anullsrc=r=16000:cl=mono", "-t", "2"}}});
        QVERIFY(r.value("rms_db").toDouble() < -70);
    }
};
QTEST_GUILESS_MAIN(TstAudio)
#include "tst_audio.moc"
