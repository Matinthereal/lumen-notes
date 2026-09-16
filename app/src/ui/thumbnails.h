#pragma once
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>

class Database;

// Page thumbnails for the sidebar: ink on paper at 160 px wide, rendered off the GUI thread from
// the stored stroke blob into cache/thumbs/<page>.png whenever a page is saved.
class Thumbnails : public QObject {
    Q_OBJECT
public:
    explicit Thumbnails(Database &db, QObject *parent = nullptr);
    Q_INVOKABLE QString pathFor(qint64 pageId) const;      // empty until rendered
    Q_INVOKABLE int version(qint64 pageId) const { return m_version.value(pageId, 0); }
    Q_INVOKABLE void ensure(qint64 pageId);                // render if missing
public slots:
    void refresh(qint64 pageId);
signals:
    void changed(qint64 pageId);
private:
    void render(qint64 pageId);
    Database &m_db;
    QHash<qint64, int> m_version;
    QSet<qint64> m_inFlight;
};
