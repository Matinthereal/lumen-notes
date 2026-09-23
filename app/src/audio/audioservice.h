#pragma once
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

class Database;
class Library;
class WorkerSupervisor;

// Recording, live transcription, re-pass, playback and the backend benchmark, all through the
// `audio` worker (Phase 5). Strokes and text written while recording are stamped by the canvas /
// text layer from `recordingId` + `nowMs()`. Audio can be deleted; the transcript stays.
class AudioService : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool recording READ recording NOTIFY stateChanged)
    Q_PROPERTY(qint64 recordingId READ recordingId NOTIFY stateChanged)
    Q_PROPERTY(qint64 recordingEpochMs READ recordingEpochMs NOTIFY stateChanged)
    Q_PROPERTY(qint64 elapsedMs READ elapsedMs NOTIFY tick)
    Q_PROPERTY(qreal level READ level NOTIFY levelChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(bool modelReady READ modelReady NOTIFY stateChanged)
    Q_PROPERTY(QVariantList sources READ sources NOTIFY sourcesChanged)
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourcesChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playbackChanged)
    Q_PROPERTY(qint64 playbackMs READ playbackMs NOTIFY playbackChanged)
    Q_PROPERTY(qint64 playbackRecordingId READ playbackRecordingId NOTIFY playbackChanged)
    Q_PROPERTY(qint64 playbackDurationMs READ playbackDurationMs NOTIFY playbackChanged)
    Q_PROPERTY(QString backend READ backend NOTIFY stateChanged)
    Q_PROPERTY(bool listening READ listening NOTIFY stateChanged)   // the live transcriber is up
    Q_PROPERTY(bool transcribing READ transcribing NOTIFY stateChanged)  // the re-pass is running
    Q_PROPERTY(qreal progress READ progress NOTIFY stateChanged)          // through the re-pass, 0..1 or -1
    Q_PROPERTY(bool preparing READ preparing NOTIFY stateChanged)        // the live model is loading
public:
    AudioService(Database &db, Library &lib, WorkerSupervisor *worker, QObject *parent = nullptr);

    bool recording() const { return m_recordingId != 0; }
    qint64 recordingId() const { return m_recordingId; }
    qint64 recordingEpochMs() const { return m_epochMs; }
    qint64 elapsedMs() const;
    qreal level() const { return m_level; }
    QString status() const { return m_status; }
    bool modelReady() const { return m_modelReady; }
    QVariantList sources() const { return m_sources; }
    QString source() const { return m_source; }
    void setSource(const QString &s);
    bool playing() const { return m_playing; }
    qint64 playbackMs() const { return m_playbackMs; }
    qint64 playbackRecordingId() const { return m_playbackRecordingId; }
    qint64 playbackDurationMs() const { return m_playbackDurationMs; }
    QString backend() const { return m_backend; }
    bool listening() const { return m_listening; }
    bool transcribing() const { return m_repassRequest != 0; }
    qreal progress() const { return m_progress; }
    bool preparing() const { return m_preparing; }
    Q_INVOKABLE void cancelRetranscribe();

    Q_INVOKABLE void refreshSources();
    Q_INVOKABLE void monitor(bool on);          // mic-check levels without recording
    Q_INVOKABLE void checkSource();             // one-second health check → micVerdict
    Q_PROPERTY(QString micVerdict READ micVerdict NOTIFY sourcesChanged)
    QString micVerdict() const { return m_micVerdict; }
    Q_INVOKABLE void prepareModels();
    Q_INVOKABLE bool startRecording(qint64 sectionId, qint64 pageId);
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE qint64 nowMs() const { return elapsedMs(); }
    Q_INVOKABLE QVariantList recordings(qint64 sectionId) const;
    Q_INVOKABLE QVariantMap recordingInfo(qint64 id) const;
    Q_INVOKABLE QVariantList segments(qint64 recordingId) const;
    Q_INVOKABLE void play(qint64 recordingId, qint64 ms);
    Q_INVOKABLE void pause();
    Q_INVOKABLE void seek(qint64 ms);
    Q_INVOKABLE void deleteAudio(qint64 recordingId);          // to the trash folder, not to nothing
    Q_INVOKABLE bool restoreAudio(qint64 recordingId);
    Q_INVOKABLE bool audioInTrash(qint64 recordingId) const;
    void purgeTrash(int olderThanDays = 30);
    Q_INVOKABLE void deleteRecording(qint64 recordingId);
    Q_INVOKABLE void deleteTranscript(qint64 recordingId);
    Q_INVOKABLE qint64 addMark(qint64 recordingId, qint64 tMs, const QString &label = {});   // "come back to this"
    Q_INVOKABLE QVariantList marks(qint64 recordingId) const;
    Q_INVOKABLE void removeMark(qint64 markId);
    Q_INVOKABLE void setPlaybackSpeed(double speed);
    Q_PROPERTY(double playbackSpeed READ playbackSpeed NOTIFY playbackChanged)
    double playbackSpeed() const { return m_playbackSpeed; }
    Q_INVOKABLE void renameRecording(qint64 id, const QString &title);
    Q_INVOKABLE void runBenchmark(qint64 recordingId);
    Q_INVOKABLE void retranscribe(qint64 recordingId);
    void finishForQuit();            // blocking (≤ 20 s): stop the worker's recording and store the file
    int recoverOrphans();            // recordings left with duration 0 + a temp file (app died mid-recording)

signals:
    void stateChanged();
    void tick();
    void levelChanged();
    void sourcesChanged();
    void playbackChanged();
    void segmentsChanged(qint64 recordingId);
    void marksChanged(qint64 recordingId);
    void benchmarkDone(const QVariantMap &result);
    void recordingsChanged();
    void error(const QString &message);

private:
    using Callback = std::function<void(const QJsonObject &, const QJsonObject &)>;
    int call(const QString &method, const QJsonObject &params, Callback cb = {});
    void onNotification(const QString &method, const QJsonObject &params);
    void storeSegments(qint64 recordingId, const QJsonArray &segs, const QString &pass, bool replace);
    void setStatus(const QString &s);
    QString audioPath(qint64 recordingId) const;
    QString attachmentSha(qint64 recordingId) const;

    Database &m_db;
    Library &m_lib;
    WorkerSupervisor *m_worker;
    QHash<int, Callback> m_pending;
    qint64 m_recordingId = 0, m_stoppingId = 0;
    bool m_listening = false;
    bool m_monitoring = false;
    bool m_recovered = false;
    QString m_micVerdict;
    qint64 m_epochMs = 0;
    QString m_tempPath;
    qreal m_level = 0;
    QString m_status;
    bool m_modelReady = false, m_preparing = false;
    int m_repassRequest = 0;
    qreal m_progress = -1;
    QVariantList m_sources;
    QString m_source = QStringLiteral("default");
    QString m_backend = QStringLiteral("cpu");
    bool m_playing = false;
    double m_playbackSpeed = 1.0;
    qint64 m_playbackMs = 0, m_playbackRecordingId = 0, m_playbackDurationMs = 0;
    QTimer m_tick, m_poll;
};
