#pragma once
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class Database;
class Library;

// Typed Markdown blocks on a page (SPEC §8). Each edit batch is one committed SQLite write, so
// there is nothing to journal; the FTS row is refreshed on every save.
class TextBlocks : public QObject {
    Q_OBJECT
public:
    TextBlocks(Database &db, Library &lib, QObject *parent = nullptr);
    Q_INVOKABLE QVariantList list(qint64 pageId) const;
    // h and colour belong to sticky notes (the text on a handwritten page); 0 and "" on a typed page.
    Q_INVOKABLE qint64 create(qint64 pageId, double x, double y, double w, qint64 recordingId = 0, qint64 tMs = 0,
                              double h = 0, const QString &colour = {});
    Q_INVOKABLE void setMarkdown(qint64 id, const QString &markdown, qint64 tMs = 0);
    Q_INVOKABLE void setGeometry(qint64 id, double x, double y, double w, double h = -1);   // h < 0: keep it
    Q_INVOKABLE void setColour(qint64 id, const QString &colour);
    Q_INVOKABLE void remove(qint64 id);
    Q_INVOKABLE QVariantMap block(qint64 id) const;
    Q_INVOKABLE QString pageText(qint64 pageId) const;   // all blocks joined, for Claude/OCR context
    // The page's headings, in reading order: [{blockId, level, text, x, y}] — the table of contents.
    Q_INVOKABLE QVariantList outline(qint64 pageId) const;
signals:
    void changed(qint64 pageId);
    void edited(qint64 pageId);    // the words changed; nothing about the block's place or look did
private:
    Database &m_db;
    Library &m_lib;
};
