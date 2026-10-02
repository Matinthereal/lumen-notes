#include "picturepixels.h"

#include <QCache>
#include <QImageReader>
#include <QMutex>
#include <QMutexLocker>
#include <QTransform>
#include <QVarLengthArray>
#include <QVector>
#include <algorithm>
#include <cmath>

namespace picturepixels {

namespace {

// Decoded pictures, by megabyte. A 12-megapixel photo is 48 MB, so this is a handful of photos or
// a great many screenshots; phones and tablets get half.
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
constexpr int kCacheMegabytes = 128;
#else
constexpr int kCacheMegabytes = 256;
#endif
QMutex g_cacheLock;
QCache<QString, QImage> g_cache(kCacheMegabytes);

bool everyPixelOpaque(const QImage &img)
{
    for (int y = 0; y < img.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x)
            if (qAlpha(line[x]) != 255) return false;
    }
    return true;
}

}

QImage load(const QString &path, int rotation, const QRectF &crop)
{
    const QString key = QStringLiteral("%1|%2|%3,%4,%5,%6").arg(path).arg(rotation)
                            .arg(crop.x()).arg(crop.y()).arg(crop.width()).arg(crop.height());
    {
        QMutexLocker lock(&g_cacheLock);
        if (const QImage *kept = g_cache.object(key)) return *kept;
    }
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QImage loaded = reader.read();
    if (loaded.isNull()) return {};
    // Turn first, then take the crop out of the turned picture — the order the crop rect was
    // measured in. The file itself is never written.
    if (rotation % 360 != 0) loaded = loaded.transformed(QTransform().rotate(rotation), Qt::SmoothTransformation);
    if (crop != QRectF(0, 0, 1, 1)) {
        const QRect box(qRound(crop.x() * loaded.width()), qRound(crop.y() * loaded.height()),
                        std::max(1, qRound(crop.width() * loaded.width())), std::max(1, qRound(crop.height() * loaded.height())));
        loaded = loaded.copy(box.intersected(loaded.rect()));
    }
    if (loaded.isNull()) return {};
    // Most PNGs carry an alpha channel with nothing in it. Knowing a picture is solid lets the
    // canvas lay the sharp texture over the base one without the two blending into each other.
    if (loaded.hasAlphaChannel()) {
        loaded = loaded.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        if (everyPixelOpaque(loaded)) loaded = loaded.convertToFormat(QImage::Format_RGB32);
    } else {
        loaded = loaded.convertToFormat(QImage::Format_RGB32);
    }
    const int megabytes = int(loaded.sizeInBytes() / (1024 * 1024)) + 1;
    if (megabytes < kCacheMegabytes) {
        QMutexLocker lock(&g_cacheLock);
        g_cache.insert(key, new QImage(loaded), megabytes);
    }
    return loaded;
}

QImage base(const QImage &source)
{
    if (source.isNull()) return {};
    QImage out = source;
    if (std::max(out.width(), out.height()) > kLongest)
        out = out.scaled(kLongest, kLongest, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return out.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

Sharp plan(QSize sourcePixels, const QRectF &device, const QRect &view)
{
    Sharp none;
    if (sourcePixels.isEmpty() || device.width() < 1 || device.height() < 1 || !device.intersects(QRectF(view))) return none;
    const double perSourceX = device.width() / sourcePixels.width();     // screen pixels per picture pixel
    const double perSourceY = device.height() / sourcePixels.height();
    const bool baseIsWhole = std::max(sourcePixels.width(), sourcePixels.height()) <= kLongest;

    Sharp sharp;
    if (std::min(perSourceX, perSourceY) >= 1.0) {
        // Enlarged. The base texture is the picture itself unless it was shrunk to kLongest; only
        // then is there anything more to show.
        if (baseIsWhole) return none;
        const QRectF seen = device.intersected(QRectF(view));
        const QRect part = QRectF((seen.left() - device.left()) / perSourceX, (seen.top() - device.top()) / perSourceY,
                                  seen.width() / perSourceX, seen.height() / perSourceY)
                               .toAlignedRect().intersected(QRect(QPoint(0, 0), sourcePixels));
        if (part.isEmpty()) return none;
        sharp.source = part;
        sharp.pixels = part.size();
        sharp.device = QRectF(device.left() + part.left() * perSourceX, device.top() + part.top() * perSourceY,
                              part.width() * perSourceX, part.height() * perSourceY);
        sharp.whole = part.size() == sourcePixels;
        return sharp;
    }

    // Shrunk. Put the picture's edges on whole pixels (it moves by half a pixel at most), then take
    // the part of that which is in view.
    const QRect snapped(QPoint(qRound(device.left()), qRound(device.top())),
                        QPoint(qRound(device.right()) - 1, qRound(device.bottom()) - 1));
    const QRect seen = snapped.intersected(view);
    if (seen.isEmpty()) return none;
    const double sx = double(sourcePixels.width()) / snapped.width(), sy = double(sourcePixels.height()) / snapped.height();
    sharp.source = QRectF((seen.left() - snapped.left()) * sx, (seen.top() - snapped.top()) * sy, seen.width() * sx, seen.height() * sy);
    sharp.pixels = seen.size();
    sharp.device = QRectF(seen);
    sharp.whole = seen == snapped;
    return sharp;
}

namespace {

// For each new pixel along one axis, the old pixels under it and how much of each: `from` may start
// and end part-way through a pixel, which is what lets a cut of the part in view line up exactly
// with a cut of the whole picture.
struct Span { int first; QVarLengthArray<float, 16> weights; };
QVector<Span> spans(double from, double length, int count, int limit)
{
    QVector<Span> out(count);
    const double step = length / count;
    for (int i = 0; i < count; ++i) {
        const double a = std::clamp(from + i * step, 0.0, double(limit)), b = std::clamp(from + (i + 1) * step, 0.0, double(limit));
        Span &s = out[i];
        s.first = std::min(int(std::floor(a)), limit - 1);
        const int last = std::max(s.first, std::min(int(std::ceil(b)) - 1, limit - 1));
        float total = 0;
        for (int p = s.first; p <= last; ++p) {
            const float w = float(std::max(0.0, std::min(b, p + 1.0) - std::max(a, double(p))));
            s.weights.append(w);
            total += w;
        }
        if (total <= 0) { s.weights.clear(); s.weights.append(1); total = 1; }
        for (float &w : s.weights) w /= total;
    }
    return out;
}

// Area averaging: every new pixel is the mean of the old pixels it covers, by how much of each it
// covers. This is what keeps a one-pixel line a line, where the graphics card's mipmaps make it a
// grey smear. `img` is RGB32 or premultiplied ARGB32, so the four bytes average independently.
//
// One old row at a time is squeezed sideways and added to the new row it falls in, so the working
// memory is two rows of the result however tall the picture is: a long scrolling screenshot must
// not cost a phone a couple of hundred megabytes each time the view settles.
QImage shrink(const QImage &img, const QRectF &window, QSize size)
{
    const QVector<Span> across = spans(window.x(), window.width(), size.width(), img.width());
    const QVector<Span> down = spans(window.y(), window.height(), size.height(), img.height());
    const int values = size.width() * 4;
    QVector<float> squeezed(values), sum(values);
    int squeezedRow = -1;       // neighbouring new rows share the old row their edge runs through
    QImage result(size, img.format());
    for (int y = 0; y < size.height(); ++y) {
        const Span &s = down[y];
        sum.fill(0);
        int row = s.first;
        for (float weight : s.weights) {
            if (row != squeezedRow) {
                const uchar *line = img.constScanLine(row);
                float *out = squeezed.data();
                for (int x = 0; x < size.width(); ++x, out += 4) {
                    const Span &a = across[x];
                    const uchar *px = line + a.first * 4;
                    float c0 = 0, c1 = 0, c2 = 0, c3 = 0;
                    for (float w : a.weights) { c0 += w * px[0]; c1 += w * px[1]; c2 += w * px[2]; c3 += w * px[3]; px += 4; }
                    out[0] = c0; out[1] = c1; out[2] = c2; out[3] = c3;
                }
                squeezedRow = row;
            }
            for (int i = 0; i < values; ++i) sum[i] += weight * squeezed[i];
            ++row;
        }
        uchar *out = result.scanLine(y);
        for (int i = 0; i < values; ++i) out[i] = uchar(std::clamp(sum[i] + 0.5f, 0.0f, 255.0f));
    }
    return result;
}

}

QImage render(const QImage &source, const Sharp &sharp)
{
    if (source.isNull() || !sharp.valid()) return {};
    const QRect whole = sharp.source.toAlignedRect().intersected(source.rect());
    if (whole.isEmpty()) return {};
    QImage out = QSizeF(sharp.pixels) == sharp.source.size() ? source.copy(whole) : shrink(source, sharp.source, sharp.pixels);
    return out.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

}
