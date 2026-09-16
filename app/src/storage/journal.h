#pragma once
#include <QFile>
#include <QString>
#include "ink/inktypes.h"

class InkDocument;

// Per-page append-only journal (D-004). Every document mutation is one record, CRC-checked and
// fdatasync'd before the call returns. Replay tolerates a torn last record (a crash mid-write).
//   file:   "MYNJ" u16 version
//   record: u32 length, u8 type, u32 crc32(payload), payload
//   types:  1 ADD(index i32, stroke)  2 REMOVE(id u64)  3 UPDATE(stroke)
class PageJournal {
public:
    explicit PageJournal(const QString &path);
    ~PageJournal();
    bool open();                 // creates the file with its header if missing
    void close();
    QString path() const { return m_file.fileName(); }
    bool appendAdd(int index, const Stroke &s);
    bool appendRemove(quint64 id);
    bool appendUpdate(const Stroke &s);
    bool isEmpty() const;        // header only (or missing)
    qint64 bytes() const;
    int replay(InkDocument &doc);   // returns records applied; -1 if the file is unreadable
    bool truncate();             // back to the header
    int appendFailures() const { return m_failures; }

    static quint32 crc32(const QByteArray &data);

private:
    bool writeRecord(quint8 type, const QByteArray &payload);
    QFile m_file;
    int m_failures = 0;
};
