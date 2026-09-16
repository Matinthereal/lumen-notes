#include "strokecodec.h"
#include <QDataStream>
#include <cmath>
#include <cstring>

namespace {

void putVar(QByteArray &b, quint64 v) { while (v >= 0x80) { b.append(char((v & 0x7f) | 0x80)); v >>= 7; } b.append(char(v)); }
bool getVar(const char *&p, const char *end, quint64 &v)
{
    v = 0; int shift = 0;
    while (p < end && shift < 64) { const quint8 c = quint8(*p++); v |= quint64(c & 0x7f) << shift; if (!(c & 0x80)) return true; shift += 7; }
    return false;
}
quint64 zig(qint64 v) { return (quint64(v) << 1) ^ quint64(v >> 63); }
qint64 unzig(quint64 v) { return qint64(v >> 1) ^ -qint64(v & 1); }
template <class T> void putRaw(QByteArray &b, T v) { b.append(reinterpret_cast<const char *>(&v), sizeof v); }
template <class T> bool getRaw(const char *&p, const char *end, T &v) { if (end - p < qsizetype(sizeof v)) return false; std::memcpy(&v, p, sizeof v); p += sizeof v; return true; }

void encodeStroke(QByteArray &b, const Stroke &s)
{
    putRaw<quint64>(b, s.id);
    b.append(char(s.tool));
    putRaw<quint32>(b, quint32(s.color));
    putRaw<float>(b, s.width);
    b.append(char(0)); // flags
    putRaw<quint64>(b, s.recordingId);
    putRaw<quint32>(b, s.startMs);
    putVar(b, quint64(s.points.size()));
    qint64 px = 0, py = 0; quint32 pt = 0;
    for (const InkPoint &q : s.points) {
        const qint64 x = llround(double(q.x) * 64.0), y = llround(double(q.y) * 64.0);
        putVar(b, zig(x - px)); putVar(b, zig(y - py)); px = x; py = y;
        putRaw<quint16>(b, quint16(std::clamp(q.pressure, 0.f, 1.f) * 65535.f + 0.5f));
        b.append(char(qint8(std::clamp(q.tiltX, -127.f, 127.f))));
        b.append(char(qint8(std::clamp(q.tiltY, -127.f, 127.f))));
        putVar(b, quint64(q.tMs >= pt ? q.tMs - pt : 0)); pt = q.tMs;
    }
}

bool decodeStroke(const char *&p, const char *end, Stroke &s)
{
    quint8 tool = 0, flags = 0; quint32 rgba = 0; quint64 n = 0;
    if (!getRaw(p, end, s.id)) return false;
    if (p >= end) return false; tool = quint8(*p++);
    if (!getRaw(p, end, rgba) || !getRaw(p, end, s.width)) return false;
    if (p >= end) return false; flags = quint8(*p++); (void)flags;
    if (!getRaw(p, end, s.recordingId) || !getRaw(p, end, s.startMs)) return false;
    if (!getVar(p, end, n) || n > 10'000'000) return false;
    s.tool = tool == 1 ? InkTool::Highlighter : InkTool::Pen;
    s.color = rgba;
    s.points.clear(); s.points.reserve(int(n));
    qint64 px = 0, py = 0; quint32 pt = 0;
    for (quint64 i = 0; i < n; ++i) {
        quint64 dx, dy, dt; quint16 pr;
        if (!getVar(p, end, dx) || !getVar(p, end, dy) || !getRaw(p, end, pr)) return false;
        if (end - p < 2) return false;
        const qint8 tx = qint8(*p++), ty = qint8(*p++);
        if (!getVar(p, end, dt)) return false;
        px += unzig(dx); py += unzig(dy); pt += quint32(dt);
        s.points.append({float(px) / 64.f, float(py) / 64.f, pr / 65535.f, float(tx), float(ty), pt});
    }
    s.updateBounds();
    return true;
}

} // namespace

namespace strokecodec {

QByteArray encode(const QVector<Stroke> &strokes)
{
    QByteArray b;
    b.append("MYNK", 4);
    putRaw<quint16>(b, quint16(kVersion));
    putRaw<quint32>(b, quint32(strokes.size()));
    for (const Stroke &s : strokes) encodeStroke(b, s);
    return b;
}

bool decode(const QByteArray &blob, QVector<Stroke> &out)
{
    out.clear();
    if (blob.size() < 10 || std::memcmp(blob.constData(), "MYNK", 4) != 0) return blob.isEmpty();
    const char *p = blob.constData() + 4, *end = blob.constData() + blob.size();
    quint16 ver; quint32 n;
    if (!getRaw(p, end, ver) || ver != kVersion || !getRaw(p, end, n)) return false;
    if (n > quint32(blob.size() / 8)) return false;          // a corrupt count must not drive reserve()
    out.reserve(int(n));
    for (quint32 i = 0; i < n; ++i) { Stroke s; if (!decodeStroke(p, end, s)) return false; out.append(std::move(s)); }   // keep what decoded
    return true;
}

QByteArray encodeOne(const Stroke &s) { QByteArray b; encodeStroke(b, s); return b; }
bool decodeOne(const QByteArray &bytes, Stroke &s) { const char *p = bytes.constData(); return decodeStroke(p, p + bytes.size(), s); }

} // namespace strokecodec
