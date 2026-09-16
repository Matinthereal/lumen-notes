#pragma once
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

class Database;
class Library;
class WorkerSupervisor;

// Flashcards (Phase 6 creation, Phase 8 FSRS review, stats, Anki export/import via the cards worker).
class Cards : public QObject {
    Q_OBJECT
    Q_PROPERTY(int total READ total NOTIFY changed)
    Q_PROPERTY(int dueCount READ dueCount NOTIFY changed)
    Q_PROPERTY(int reviewedToday READ reviewedToday NOTIFY changed)
public:
    Cards(Database &db, Library &lib, WorkerSupervisor *worker = nullptr, QObject *parent = nullptr);
    int total() const;
    int dueCount() const;
    int reviewedToday() const;

    Q_INVOKABLE qint64 add(const QString &kind, const QString &front, const QString &back, qint64 sourcePage, const QString &tag = {}, bool reverse = false);
    Q_INVOKABLE int addMany(const QVariantList &cards, qint64 sourcePage);   // [{kind, front, back, tags:[..]}]
    Q_INVOKABLE void remove(qint64 id);          // soft: restore() puts it back
    Q_INVOKABLE void restore(qint64 id);
    Q_INVOKABLE QVariantMap card(qint64 id) const;
    Q_INVOKABLE QVariantList due(int limit = 50) const;            // cards to review now
    Q_INVOKABLE QVariantMap preview(qint64 cardId) const;           // predicted intervals per rating
    Q_INVOKABLE void review(qint64 cardId, int rating);             // 1 Again · 2 Hard · 3 Good · 4 Easy
    Q_INVOKABLE QVariantList all(const QString &filter = {}, int limit = 500) const;
    Q_INVOKABLE QVariantList stats(int days = 30) const;            // per tag: cards, reviews, accuracy, due
    Q_INVOKABLE void exportAnki(const QUrl &dest);
    Q_INVOKABLE void importAnki(const QUrl &file, qint64 sourcePage = 0);

signals:
    void changed();
    void exported(const QString &file, int count);
    void imported(int count);
    void failed(const QString &message);

protected:
    qint64 tagId(const QString &name);
    Database &m_db;
    Library &m_lib;
    WorkerSupervisor *m_worker;
    QHash<int, std::function<void(const QJsonObject &, const QJsonObject &)>> m_pending;
};
