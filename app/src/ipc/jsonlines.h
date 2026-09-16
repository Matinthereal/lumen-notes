#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QList>

// Newline-delimited JSON framing (D-006). Feed bytes in any chunking; pull complete objects out.
// Invalid lines are dropped and counted, never fatal.
class JsonLineBuffer {
public:
    void append(const QByteArray &bytes);
    bool next(QJsonObject &out);          // true if a complete object was available
    int droppedLines() const { return m_dropped; }
    static QByteArray encode(const QJsonObject &obj); // object + '\n'

private:
    QByteArray m_buf;
    int m_dropped = 0;
};
