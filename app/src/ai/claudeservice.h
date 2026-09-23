#pragma once
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

class Database;
class Library;
class TextBlocks;
class AudioService;
class WorkerSupervisor;

// All Claude features (D-010): each one builds a prompt from a template in prompts/, hands it to
// QML for the maker to see, and only `send()` runs `claude -p` through the worker. Text only.
class ClaudeService : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY statusChanged)
    Q_PROPERTY(QString version READ version NOTIFY statusChanged)
    Q_PROPERTY(QString reason READ reason NOTIFY statusChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool online READ online WRITE setOnline NOTIFY statusChanged)
public:
    ClaudeService(Database &db, Library &lib, TextBlocks &blocks, AudioService &audio, WorkerSupervisor *worker, const QString &promptsDir, QObject *parent = nullptr);

    bool available() const { return m_available; }
    QString version() const { return m_version; }
    QString reason() const { return m_reason; }
    bool busy() const { return m_busy; }
    bool online() const { return m_online; }
    void setOnline(bool v);

    Q_INVOKABLE void refreshStatus();
    // Prompt builders: emit promptReady(feature, prompt, meta) for the preview panel.
    Q_INVOKABLE void prepareLectureNotes(qint64 recordingId, qint64 pageId);
    Q_INVOKABLE void prepareFlashcards(qint64 pageId, qint64 recordingId = 0);
    Q_INVOKABLE void prepareExplain(const QString &selection, qint64 pageId, const QString &imagePath = {});
    Q_INVOKABLE void prepareAsk(const QString &question);
    Q_INVOKABLE void improveLatex(const QString &imagePath, const QString &draft);   // sends straight away (the button is the consent)
    Q_INVOKABLE void send(const QString &feature, const QString &prompt, const QVariantMap &meta);
    Q_INVOKABLE void cancel();                         // stops the claude CLI mid-answer
    Q_INVOKABLE QString promptTemplate(const QString &name) const;
    Q_INVOKABLE QVariantList recentLog(int limit = 20) const;

signals:
    void statusChanged();
    void busyChanged();
    void promptReady(const QString &feature, const QString &prompt, const QVariantMap &meta);
    void responded(const QString &feature, const QString &text, const QVariantMap &meta);
    void notesCreated(qint64 pageId);
    void cardsProposed(const QVariantList &cards, const QVariantMap &meta);
    void failed(const QString &feature, const QString &message);

private:
    using Callback = std::function<void(const QJsonObject &, const QJsonObject &)>;
    int call(const QString &method, const QJsonObject &params, Callback cb);
    QString fill(const QString &tmpl, const QVariantMap &vars) const;
    QString subjectFor(qint64 pageId) const;
    void setBusy(bool b);

    Database &m_db;
    Library &m_lib;
    TextBlocks &m_blocks;
    AudioService &m_audio;
    WorkerSupervisor *m_worker;
    QString m_promptsDir;
    QHash<int, Callback> m_pending;
    bool m_available = false, m_busy = false, m_online = false;
    int m_runRequest = 0;
    QString m_version, m_reason;
};
