#include "audioservice.h"
#include "storage/attachments.h"
#include "storage/database.h"
#include "storage/library.h"
#include "storage/paths.h"
#include "workers/workersupervisor.h"
#include <QDateTime>
#include <algorithm>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QLoggingCategory>
#include <QEventLoop>
#include <QDir>
#include <QTimer>

Q_LOGGING_CATEGORY(lcAudio, "lumen.audio")

AudioService::AudioService(Database &db, Library &lib, WorkerSupervisor *worker, QObject *parent)
    : QObject(parent), m_db(db), m_lib(lib), m_worker(worker)
{
    connect(worker, &WorkerSupervisor::response, this, [this](int id, const QJsonObject &r, const QJsonObject &e) {
        const Callback cb = m_pending.take(id);
        if (cb) cb(r, e);
    });
    connect(worker, &WorkerSupervisor::notification, this, &AudioService::onNotification);
    connect(worker, &WorkerSupervisor::progressed, this, [this](int id, const QString &stage, double fraction, const QString &text) {
        if (id != m_repassRequest) return;
        m_progress = fraction;
        setStatus(stage == QLatin1String("loading") ? text + QStringLiteral("…") : QStringLiteral("re-transcribing with the larger model…"));
        emit stateChanged();
    });
    connect(worker, &WorkerSupervisor::stateChanged, this, [this] {
        if (m_worker->state() == WorkerSupervisor::State::Ready) {
            refreshSources();
            if (!m_recovered) { m_recovered = true; recoverOrphans(); }
        } else if (m_recordingId && m_worker->state() == WorkerSupervisor::State::Crashed) {
            // The worker died under a recording: the file is on disk; recover it when the worker returns.
            qCWarning(lcAudio) << "audio worker crashed while recording" << m_recordingId;
            emit error(QStringLiteral("The audio worker crashed while recording; the audio so far is being recovered."));
            m_recordingId = 0; m_tick.stop(); m_recovered = false; emit stateChanged(); emit recordingsChanged();
        }
        emit stateChanged();
    });
    m_tick.setInterval(200);
    connect(&m_tick, &QTimer::timeout, this, &AudioService::tick);
    m_poll.setInterval(250);
    connect(&m_poll, &QTimer::timeout, this, [this] {
        call("position", {}, [this](const QJsonObject &r, const QJsonObject &) {
            const bool playing = r.value("playing").toBool();
            m_playbackMs = r.value("position_ms").toInteger();
            if (!playing && m_playing) { m_playing = false; m_poll.stop(); }
            emit playbackChanged();
        });
    });
    m_source = lib.setting("audio.source", "default");
    m_backend = lib.setting("audio.backend", "cpu");
}

int AudioService::call(const QString &method, const QJsonObject &params, Callback cb)
{
    const int id = m_worker->request(method, params);
    if (cb) m_pending.insert(id, std::move(cb));
    return id;
}

void AudioService::setStatus(const QString &s) { if (m_status == s) return; m_status = s; emit stateChanged(); }

qint64 AudioService::elapsedMs() const { return m_recordingId ? QDateTime::currentMSecsSinceEpoch() - m_epochMs : 0; }

void AudioService::setSource(const QString &s) { m_source = s; m_lib.setSetting("audio.source", s); m_micVerdict.clear(); emit sourcesChanged(); if (m_monitoring) monitor(true); checkSource(); }

void AudioService::checkSource()
{
    if (m_worker->state() != WorkerSupervisor::State::Ready || m_recordingId) return;
    call("check", {{"source", m_source}}, [this](const QJsonObject &r, const QJsonObject &e) {
        m_micVerdict = e.isEmpty() ? r.value("verdict").toString() : e.value("message").toString();
        qCInfo(lcAudio) << "mic check" << m_source << m_micVerdict << r.value("rms_db").toDouble();
        emit sourcesChanged();
    });
}

void AudioService::monitor(bool on)
{
    m_monitoring = on;
    if (m_worker->state() != WorkerSupervisor::State::Ready || m_recordingId) return;
    if (on) call("monitor", {{"source", m_source}});
    else { call("monitor_stop", {}); m_level = 0; emit levelChanged(); }
}

void AudioService::refreshSources()
{
    call("sources", {}, [this](const QJsonObject &r, const QJsonObject &) {
        m_sources = r.value("sources").toArray().toVariantList();
        bool have = false, usable = false;
        QString desc;
        for (const QVariant &v : m_sources) { const QVariantMap m = v.toMap(); if (m.value("name").toString() == m_source) { have = true; usable = m.value("available").toBool(); desc = m.value("description").toString(); } }
        const QString def = r.value("default").toString();
        if ((!have || !usable || m_source == "default") && !def.isEmpty() && def != m_source) {
            const QString old = desc;
            m_source = def;
            m_lib.setSetting("audio.source", def);
            QString newDesc; for (const QVariant &v : m_sources) if (v.toMap().value("name").toString() == def) newDesc = v.toMap().value("description").toString();
            if (have && !usable) emit error(QStringLiteral("Microphone switched to \"%1\" — \"%2\" has nothing plugged in.").arg(newDesc, old));
        }
        emit sourcesChanged();
        if (m_monitoring) monitor(true);
        checkSource();
    });
}

void AudioService::prepareModels()
{
    if (m_modelReady || m_preparing) return;
    m_preparing = true;
    setStatus(QStringLiteral("preparing the transcription model…"));
    call("prepare", {{"models_dir", paths::modelsDir()}, {"live_model", m_lib.setting("audio.liveModel", "small.en")}},
         [this](const QJsonObject &r, const QJsonObject &e) {
             m_preparing = false;
             m_modelReady = e.isEmpty() && r.value("ok").toBool();
             const bool noAddOn = e.value("code").toInt() == -4 || m_worker->missingOptional().contains(QLatin1String("faster_whisper"));
             setStatus(m_modelReady ? QString()
                       : noAddOn ? QStringLiteral("transcription needs the AI add-on")
                                 : QStringLiteral("model not available: ") + e.value("message").toString());
             emit stateChanged();
         });
}

bool AudioService::startRecording(qint64 sectionId, qint64 pageId)
{
    if (m_recordingId) return false;
    if (m_worker->state() != WorkerSupervisor::State::Ready) { emit error("audio worker is not ready"); return false; }
    QDir().mkpath(paths::dataDir() + "/recordings");
    Database::Query q(m_db, "INSERT INTO recording(section_id, page_id, started_at, title) VALUES (?,?,?,?)");
    const qint64 started = QDateTime::currentSecsSinceEpoch();
    q.bind(1, sectionId).bind(2, pageId).bind(3, started).bind(4, QDateTime::currentDateTime().toString("ddd d MMM, HH:mm"));
    if (!q.run()) { emit error(m_db.lastError()); return false; }
    m_recordingId = m_db.lastInsertId();
    m_tempPath = QStringLiteral("%1/recordings/rec-%2.opus").arg(paths::dataDir()).arg(m_recordingId);
    m_epochMs = QDateTime::currentMSecsSinceEpoch();
    m_listening = false;
    m_level = 0;
    call("start", {{"out_path", m_tempPath}, {"source", m_source}, {"model", m_lib.setting("audio.liveModel", "small.en")}, {"live", true}},
         [this](const QJsonObject &r, const QJsonObject &e) {
             if (!e.isEmpty() || !r.value("ok").toBool()) {
                 emit error(QStringLiteral("could not start recording: ") + (e.isEmpty() ? r.value("error").toString() : e.value("message").toString()));
                 Database::Query d(m_db, "DELETE FROM recording WHERE id=?"); d.bind(1, m_recordingId); d.run();
                 m_recordingId = 0; m_tick.stop(); emit stateChanged();
             }
         });
    m_tick.start();
    setStatus(QStringLiteral("recording"));
    emit stateChanged();
    emit recordingsChanged();
    return true;
}

void AudioService::stopRecording()
{
    if (!m_recordingId) return;
    const qint64 rid = m_recordingId;
    const QString temp = m_tempPath;
    m_stoppingId = rid;
    m_recordingId = 0;
    m_listening = false;
    m_tick.stop();
    m_level = 0; emit levelChanged();
    setStatus(QStringLiteral("finishing…"));
    emit stateChanged();
    call("stop", {}, [this, rid, temp](const QJsonObject &r, const QJsonObject &e) {
        m_stoppingId = 0;
        if (!e.isEmpty()) { emit error(e.value("message").toString()); setStatus({}); return; }
        const qint64 duration = r.value("duration_ms").toInteger();
        QString err;
        const QString sha = attachments::store(m_db, temp, "audio/ogg", &err);
        if (!sha.isEmpty()) QFile::remove(temp); else emit error(err);
        Database::Query u(m_db, "UPDATE recording SET attachment=?, duration_ms=? WHERE id=?");
        u.bind(1, sha).bind(2, duration).bind(3, rid); u.run();
        emit recordingsChanged();
        if (m_monitoring) monitor(true);
        retranscribe(rid);
    });
}

void AudioService::retranscribe(qint64 rid)
{
    const QString path = audioPath(rid);
    if (path.isEmpty()) { setStatus({}); return; }
    if (m_worker->missingOptional().contains(QLatin1String("faster_whisper"))) { setStatus({}); return; }   // no add-on: the recording is kept, untranscribed
    setStatus(QStringLiteral("re-transcribing with the larger model…"));
    m_progress = -1;
    m_repassRequest = call("transcribe_file", {{"path", path}, {"model", m_lib.setting("audio.repassModel", "large-v3-turbo")}},
         [this, rid](const QJsonObject &r, const QJsonObject &e) {
             m_repassRequest = 0;
             m_progress = -1;
             if (e.isEmpty()) storeSegments(rid, r.value("segments").toArray(), "repass", true);
             else if (e.value("code").toInt() != WorkerSupervisor::CancelledCode) emit error(QStringLiteral("re-pass failed: ") + e.value("message").toString());
             setStatus({});
             emit stateChanged();
         });
    emit stateChanged();
}

// The live transcript stays; only the slower, better second pass is dropped.
void AudioService::cancelRetranscribe()
{
    if (m_repassRequest) m_worker->cancel(m_repassRequest);
}

// Called from aboutToQuit: the worker must not be killed with a recording open.
void AudioService::finishForQuit()
{
    if (!m_recordingId) return;
    qCInfo(lcAudio) << "quit while recording" << m_recordingId << "— finalising";
    const qint64 rid = m_recordingId;
    QEventLoop loop;
    bool done = false;
    auto conn = connect(this, &AudioService::recordingsChanged, &loop, [&] { if (!m_recordingId) { done = true; loop.quit(); } });
    QTimer::singleShot(20000, &loop, &QEventLoop::quit);
    stopRecording();
    if (!done) loop.exec();
    disconnect(conn);
    Q_UNUSED(rid);
}

int AudioService::recoverOrphans()
{
    int n = 0;
    Database::Query q(m_db, "SELECT id FROM recording WHERE attachment IS NULL AND duration_ms=0");
    QVector<qint64> ids;
    while (q.step()) ids.append(q.i64(0));
    for (qint64 rid : ids) {
        const QString temp = QStringLiteral("%1/recordings/rec-%2.opus").arg(paths::dataDir()).arg(rid);
        if (!QFileInfo::exists(temp) || QFileInfo(temp).size() < 1000) {
            Database::Query d(m_db, "DELETE FROM recording WHERE id=?"); d.bind(1, rid); d.run();   // nothing was captured
            QFile::remove(temp);
            continue;
        }
        QString err;
        const QString sha = attachments::store(m_db, temp, "audio/ogg", &err);
        if (sha.isEmpty()) { qCWarning(lcAudio) << "orphan" << rid << err; continue; }
        QFile::remove(temp);
        Database::Query u(m_db, "UPDATE recording SET attachment=?, duration_ms=1, title=title||' (recovered)' WHERE id=?");
        u.bind(1, sha).bind(2, rid); u.run();
        ++n;
        call("probe_duration", {{"path", attachments::pathFor(sha, "audio/ogg")}}, [this, rid](const QJsonObject &r, const QJsonObject &) {
            Database::Query u(m_db, "UPDATE recording SET duration_ms=? WHERE id=?"); u.bind(1, std::max<qint64>(1, r.value("duration_ms").toInteger())).bind(2, rid); u.run();
            emit recordingsChanged();
            retranscribe(rid);
        });
    }
    if (n) { qCInfo(lcAudio) << "recovered" << n << "recording(s) from a previous run"; emit recordingsChanged(); }
    return n;
}

void AudioService::onNotification(const QString &method, const QJsonObject &params)
{
    if (method == "status") qCInfo(lcAudio) << "worker:" << params.value("text").toString();
    if (method == "level") { m_level = params.value("rms").toDouble(); emit levelChanged(); }
    else if (method == "segments") {
        // Segments can still arrive after the maker pressed stop (the last window is flushed then).
        const qint64 rid = m_recordingId ? m_recordingId : m_stoppingId;
        if (rid) storeSegments(rid, params.value("segments").toArray(), params.value("pass").toString("live"), false);
    } else if (method == "status") {
        const QString t = params.value("text").toString();
        if (t == "listening") { m_listening = true; emit stateChanged(); }
        else setStatus(t);
    }
}

void AudioService::storeSegments(qint64 rid, const QJsonArray &segs, const QString &pass, bool replace)
{
    m_db.begin();
    if (replace) { Database::Query d(m_db, "DELETE FROM transcript_segment WHERE recording_id=?"); d.bind(1, rid); d.run(); }
    qint64 pageId = 0;
    { Database::Query p(m_db, "SELECT page_id FROM recording WHERE id=?"); p.bind(1, rid); if (p.step()) pageId = p.i64(0); }
    if (replace) m_lib.unindex("transcript", pageId);
    Database::Query q(m_db, "INSERT INTO transcript_segment(recording_id, t0_ms, t1_ms, text, pass) VALUES (?,?,?,?,?)");
    for (const QJsonValue &v : segs) {
        const QJsonObject s = v.toObject();
        q.reset();
        q.bind(1, rid).bind(2, s.value("t0").toInteger()).bind(3, s.value("t1").toInteger()).bind(4, s.value("text").toString()).bind(5, pass);
        q.run();
        m_lib.indexText("transcript", pageId, m_db.lastInsertId(), s.value("text").toString());
    }
    m_db.commit();
    emit segmentsChanged(rid);
}

QString AudioService::audioPath(qint64 rid) const
{
    Database::Query q(m_db, "SELECT attachment, audio_deleted FROM recording WHERE id=?");
    q.bind(1, rid);
    if (!q.step() || q.isNull(0) || q.i32(1)) return {};
    return attachments::pathFor(q.text(0), "audio/ogg");
}

QVariantList AudioService::recordings(qint64 sectionId) const
{
    QVariantList out;
    Database::Query q(m_db, "SELECT id, title, started_at, duration_ms, audio_deleted, page_id, (SELECT COUNT(*) FROM transcript_segment t WHERE t.recording_id=r.id)"
                            " FROM recording r WHERE section_id=? ORDER BY started_at DESC");
    q.bind(1, sectionId);
    while (q.step())
        out.append(QVariantMap{{"id", q.i64(0)}, {"title", q.text(1)}, {"startedAt", q.i64(2)}, {"durationMs", q.i64(3)}, {"audioDeleted", q.i32(4) != 0}, {"pageId", q.i64(5)}, {"segmentCount", q.i32(6)}});
    return out;
}

QVariantMap AudioService::recordingInfo(qint64 id) const
{
    Database::Query q(m_db, "SELECT id, title, started_at, duration_ms, audio_deleted, page_id, section_id FROM recording WHERE id=?");
    q.bind(1, id);
    if (!q.step()) return {};
    return QVariantMap{{"id", q.i64(0)}, {"title", q.text(1)}, {"startedAt", q.i64(2)}, {"durationMs", q.i64(3)}, {"audioDeleted", q.i32(4) != 0}, {"pageId", q.i64(5)}, {"sectionId", q.i64(6)}};
}

QVariantList AudioService::segments(qint64 rid) const
{
    QVariantList out;
    Database::Query q(m_db, "SELECT id, t0_ms, t1_ms, text, pass FROM transcript_segment WHERE recording_id=? ORDER BY t0_ms");
    q.bind(1, rid);
    while (q.step()) out.append(QVariantMap{{"id", q.i64(0)}, {"t0", q.i64(1)}, {"t1", q.i64(2)}, {"text", q.text(3)}, {"pass", q.text(4)}});
    return out;
}

void AudioService::play(qint64 rid, qint64 ms)
{
    const QString path = audioPath(rid);
    if (path.isEmpty()) { emit error("this recording has no audio"); return; }
    m_playbackRecordingId = rid;
    m_playbackDurationMs = recordingInfo(rid).value("durationMs").toLongLong();
    call("play", {{"path", path}, {"start_ms", ms}}, [this, ms](const QJsonObject &, const QJsonObject &e) {
        if (!e.isEmpty()) { emit error(e.value("message").toString()); return; }
        m_playing = true; m_playbackMs = ms; m_poll.start(); emit playbackChanged();
    });
}

void AudioService::pause()
{
    call("pause", {}, [this](const QJsonObject &r, const QJsonObject &) {
        m_playing = false; m_playbackMs = r.value("position_ms").toInteger(); m_poll.stop(); emit playbackChanged();
    });
}

void AudioService::seek(qint64 ms)
{
    if (!m_playbackRecordingId) return;
    if (!m_playing) { play(m_playbackRecordingId, ms); return; }
    call("seek", {{"ms", ms}}, [this, ms](const QJsonObject &, const QJsonObject &) { m_playbackMs = ms; emit playbackChanged(); });
}

static QString trashDir()
{
    const QString dir = paths::dataDir() + QStringLiteral("/trash");
    QDir().mkpath(dir);
    return dir;
}

QString AudioService::attachmentSha(qint64 rid) const
{
    Database::Query q(m_db, "SELECT attachment FROM recording WHERE id=?");
    q.bind(1, rid);
    return (q.step() && !q.isNull(0)) ? q.text(0) : QString();
}

void AudioService::deleteAudio(qint64 rid)
{
    if (m_playbackRecordingId == rid) { call("stop_playback", {}); m_playing = false; m_poll.stop(); m_playbackRecordingId = 0; emit playbackChanged(); }
    // A lecture recording cannot be re-made: it goes to the trash, where undo (and 30 days) can reach it.
    const QString sha = attachmentSha(rid);
    if (!sha.isEmpty()) {
        const QString src = attachments::pathFor(sha, QStringLiteral("audio/ogg"));
        const QString dst = trashDir() + QLatin1Char('/') + sha + QStringLiteral(".opus");
        if (QFile::exists(src)) { QFile::remove(dst); QFile::rename(src, dst); }
    }
    Database::Query q(m_db, "UPDATE recording SET audio_deleted=1 WHERE id=?"); q.bind(1, rid); q.run();
    emit recordingsChanged();
}

bool AudioService::audioInTrash(qint64 rid) const
{
    const QString sha = attachmentSha(rid);
    return !sha.isEmpty() && QFile::exists(paths::dataDir() + QStringLiteral("/trash/") + sha + QStringLiteral(".opus"));
}

bool AudioService::restoreAudio(qint64 rid)
{
    const QString sha = attachmentSha(rid);
    if (sha.isEmpty()) return false;
    const QString src = paths::dataDir() + QStringLiteral("/trash/") + sha + QStringLiteral(".opus");
    if (!QFile::exists(src)) return false;
    const QString dst = attachments::pathFor(sha, QStringLiteral("audio/ogg"));
    QFile::remove(dst);
    if (!QFile::rename(src, dst)) return false;
    Database::Query q(m_db, "UPDATE recording SET audio_deleted=0 WHERE id=?"); q.bind(1, rid); q.run();
    emit recordingsChanged();
    return true;
}

void AudioService::purgeTrash(int olderThanDays)
{
    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-olderThanDays);
    const QDir dir(paths::dataDir() + QStringLiteral("/trash"));
    if (!dir.exists()) return;
    for (const QFileInfo &fi : dir.entryInfoList(QDir::Files))
        if (fi.lastModified() < cutoff) QFile::remove(fi.absoluteFilePath());
}

void AudioService::deleteRecording(qint64 rid)
{
    deleteAudio(rid);
    qint64 pageId = 0;
    { Database::Query p(m_db, "SELECT page_id FROM recording WHERE id=?"); p.bind(1, rid); if (p.step()) pageId = p.i64(0); }
    m_lib.unindex("transcript", pageId);
    Database::Query q(m_db, "DELETE FROM recording WHERE id=?"); q.bind(1, rid); q.run();
    emit recordingsChanged();
}

qint64 AudioService::addMark(qint64 rid, qint64 tMs, const QString &label)
{
    if (!rid) return 0;
    Database::Query q(m_db, "INSERT INTO recording_mark(recording_id, t_ms, label) VALUES (?,?,?)");
    q.bind(1, rid).bind(2, tMs).bind(3, label);
    if (!q.run()) return 0;
    emit marksChanged(rid);
    return m_db.lastInsertId();
}

QVariantList AudioService::marks(qint64 rid) const
{
    QVariantList out;
    Database::Query q(m_db, "SELECT id, t_ms, label FROM recording_mark WHERE recording_id=? ORDER BY t_ms");
    q.bind(1, rid);
    while (q.step()) out.append(QVariantMap{{"id", q.i64(0)}, {"tMs", q.i64(1)}, {"label", q.text(2)}});
    return out;
}

void AudioService::removeMark(qint64 markId)
{
    qint64 rid = 0;
    { Database::Query q(m_db, "SELECT recording_id FROM recording_mark WHERE id=?"); q.bind(1, markId); if (q.step()) rid = q.i64(0); }
    Database::Query q(m_db, "DELETE FROM recording_mark WHERE id=?");
    q.bind(1, markId);
    if (q.run() && rid) emit marksChanged(rid);
}

void AudioService::setPlaybackSpeed(double speed)
{
    m_playbackSpeed = std::clamp(speed, 0.5, 3.0);
    call(QStringLiteral("playback_speed"), QJsonObject{{QStringLiteral("speed"), m_playbackSpeed}});
    emit playbackChanged();
}

void AudioService::deleteTranscript(qint64 rid)
{
    qint64 pageId = 0;
    { Database::Query p(m_db, "SELECT page_id FROM recording WHERE id=?"); p.bind(1, rid); if (p.step()) pageId = p.i64(0); }
    m_lib.unindex("transcript", pageId);
    Database::Query q(m_db, "DELETE FROM transcript_segment WHERE recording_id=?"); q.bind(1, rid); q.run();
    emit segmentsChanged(rid);
    emit recordingsChanged();
}

void AudioService::renameRecording(qint64 id, const QString &title)
{
    Database::Query q(m_db, "UPDATE recording SET title=? WHERE id=?"); q.bind(1, title).bind(2, id); q.run();
    emit recordingsChanged();
}

void AudioService::runBenchmark(qint64 rid)
{
    const QString path = audioPath(rid);
    if (path.isEmpty()) { emit error("pick a recording with audio to benchmark"); return; }
    setStatus(QStringLiteral("benchmarking CPU / GPU / NPU…"));
    call("benchmark", {{"path", path}}, [this](const QJsonObject &r, const QJsonObject &e) {
        setStatus({});
        if (!e.isEmpty()) { emit error(e.value("message").toString()); return; }
        m_backend = r.value("chosen").toString("cpu");
        m_lib.setSetting("audio.backend", m_backend);
        emit stateChanged();
        emit benchmarkDone(r.toVariantMap());
    });
}
