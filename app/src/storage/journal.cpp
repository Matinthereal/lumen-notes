#include "journal.h"
#include "ink/inkdocument.h"
#include "strokecodec.h"
#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <cstring>
#include <unistd.h>

Q_LOGGING_CATEGORY(lcJournal, "lumen.journal")

static const char kMagic[4] = {'M', 'Y', 'N', 'J'};
static const quint16 kVersion = 1;
static const int kHeaderBytes = 6;

quint32 PageJournal::crc32(const QByteArray &data)
{
    static quint32 table[256];
    static bool init = false;
    if (!init) {
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        init = true;
    }
    quint32 c = 0xFFFFFFFFu;
    for (unsigned char ch : data) c = table[(c ^ ch) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

PageJournal::PageJournal(const QString &path) : m_file(path) {}
PageJournal::~PageJournal() { close(); }

bool PageJournal::open()
{
    if (m_file.isOpen()) return true;
    QDir().mkpath(QFileInfo(m_file).absolutePath());
    const bool fresh = !m_file.exists() || m_file.size() < kHeaderBytes;
    if (!m_file.open(QIODevice::ReadWrite)) { qCWarning(lcJournal) << "cannot open" << m_file.fileName(); return false; }
    if (fresh) {
        m_file.resize(0);
        m_file.write(kMagic, 4);
        m_file.write(reinterpret_cast<const char *>(&kVersion), 2);
        m_file.flush();
        ::fdatasync(int(m_file.handle()));
    }
    m_file.seek(m_file.size());
    return true;
}

void PageJournal::close() { if (m_file.isOpen()) m_file.close(); }

bool PageJournal::writeRecord(quint8 type, const QByteArray &payload)
{
    if (!open()) { ++m_failures; return false; }
    QByteArray rec;
    const quint32 len = quint32(payload.size()), crc = crc32(payload);
    rec.append(reinterpret_cast<const char *>(&len), 4);
    rec.append(char(type));
    rec.append(reinterpret_cast<const char *>(&crc), 4);
    rec.append(payload);
    m_file.seek(m_file.size());
    if (m_file.write(rec) != rec.size() || !m_file.flush() || ::fdatasync(int(m_file.handle())) != 0) {
        ++m_failures;
        qCWarning(lcJournal) << "append failed on" << m_file.fileName() << m_file.errorString();
        return false;
    }
    return true;
}

bool PageJournal::appendAdd(int index, const Stroke &s)
{
    QByteArray p; const qint32 i = index;
    p.append(reinterpret_cast<const char *>(&i), 4);
    p.append(strokecodec::encodeOne(s));
    return writeRecord(1, p);
}
bool PageJournal::appendRemove(quint64 id) { QByteArray p(reinterpret_cast<const char *>(&id), 8); return writeRecord(2, p); }
bool PageJournal::appendUpdate(const Stroke &s) { return writeRecord(3, strokecodec::encodeOne(s)); }

bool PageJournal::isEmpty() const { return !m_file.exists() || m_file.size() <= kHeaderBytes; }
qint64 PageJournal::bytes() const { return m_file.exists() ? m_file.size() : 0; }

int PageJournal::replay(InkDocument &doc)
{
    if (!m_file.exists()) return 0;
    QFile f(m_file.fileName());
    if (!f.open(QIODevice::ReadOnly)) return -1;
    const QByteArray all = f.readAll();
    if (all.size() < kHeaderBytes || std::memcmp(all.constData(), kMagic, 4) != 0) return all.isEmpty() ? 0 : -1;
    const char *p = all.constData() + kHeaderBytes, *end = all.constData() + all.size();
    int applied = 0;
    while (end - p >= 9) {
        quint32 len, crc; quint8 type;
        std::memcpy(&len, p, 4); type = quint8(p[4]); std::memcpy(&crc, p + 5, 4);
        if (end - (p + 9) < qint64(len)) break;               // torn tail: stop here, keep what we have
        const QByteArray payload = QByteArray::fromRawData(p + 9, int(len));
        if (crc32(payload) != crc) { qCWarning(lcJournal) << "bad crc in" << m_file.fileName() << "at record" << applied; break; }
        p += 9 + len;
        switch (type) {
        case 1: {
            if (payload.size() < 4) break;
            qint32 index; std::memcpy(&index, payload.constData(), 4);
            Stroke s;
            if (strokecodec::decodeOne(payload.mid(4), s)) { doc.insertStroke(index, s); ++applied; }
            break;
        }
        case 2: { if (payload.size() >= 8) { quint64 id; std::memcpy(&id, payload.constData(), 8); doc.removeStroke(id); ++applied; } break; }
        case 3: { Stroke s; if (strokecodec::decodeOne(payload, s)) { doc.updateStroke(s); ++applied; } break; }
        default: break;
        }
    }
    return applied;
}

bool PageJournal::truncate()
{
    if (!open()) return false;
    if (!m_file.resize(kHeaderBytes)) return false;
    m_file.flush();
    ::fdatasync(int(m_file.handle()));
    m_file.seek(kHeaderBytes);
    return true;
}
