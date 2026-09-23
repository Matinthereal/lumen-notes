#pragma once
#include <QString>

class Database;

// A whole notebook in one file: sections, pages, ink, typed text, pictures, PDF pages, shapes,
// tags and page links, plus the attachments they need. The file is itself a SQLite database, so
// exporting and importing is one ATTACH and a handful of INSERT … SELECTs, and a file from a newer
// Lumen can still be read by an older one (it just ignores what it does not know).
//
// Not carried: recordings and their transcripts, flashcards, past papers, and recognised
// handwriting (which is rebuilt on the machine that imports).
namespace notebookfile {

constexpr int kFormat = 1;

struct Result {
    bool ok = false;
    QString error;
    QString name;        // the notebook's name
    qint64 notebookId = 0;   // the imported notebook, on import
    int pages = 0;
    int attachments = 0;
};

Result exportNotebook(Database &db, qint64 notebookId, const QString &path);
Result importNotebook(Database &db, const QString &path);
QString describe(const QString &path);   // "Physics · 24 pages", or empty if it is not a Lumen file

} // namespace notebookfile
