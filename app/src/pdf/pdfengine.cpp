#include "pdfengine.h"
#include <fpdf_doc.h>
#include <fpdf_edit.h>
#include <fpdf_ppo.h>
#include <fpdf_save.h>
#include <fpdf_text.h>
#include <fpdf_transformpage.h>
#include <fpdfview.h>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QPageSize>
#include <QPainter>
#include <QPainterPath>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryDir>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <mutex>
#include <numbers>
#include <vector>

namespace {

constexpr double PT = 96.0 / 72.0;               // points → page units
constexpr int MaxOpen = 6;
constexpr double MaxRenderPixels = 40e6;         // 8x of an A4 page would be 230 MB of pixels
constexpr double PictureDpi = 300;               // pictures are embedded at no more than this
constexpr double TextPoints = 10.5;
constexpr auto StickyDefault = "#FFE9A8";

using Document = std::unique_ptr<fpdf_document_t__, decltype(&FPDF_CloseDocument)>;

FPDF_WIDESTRING wide(const QString &s) { return reinterpret_cast<FPDF_WIDESTRING>(s.utf16()); }

QString lastError()
{
    switch (FPDF_GetLastError()) {
    case FPDF_ERR_FILE: return QStringLiteral("the file is missing or cannot be read");
    case FPDF_ERR_FORMAT: return QStringLiteral("not a PDF, or a damaged one");
    case FPDF_ERR_PASSWORD: return QStringLiteral("the PDF is password protected");
    case FPDF_ERR_SECURITY: return QStringLiteral("the PDF uses unsupported security");
    case FPDF_ERR_PAGE: return QStringLiteral("that page is not in the PDF");
    default: return QStringLiteral("PDFium could not read the PDF");
    }
}

// One page of a document, closed with the scope.
struct Page {
    FPDF_PAGE page = nullptr;
    FPDF_TEXTPAGE text = nullptr;

    Page(FPDF_DOCUMENT doc, int index) : page(FPDF_LoadPage(doc, index)) {}
    ~Page()
    {
        if (text) FPDFText_ClosePage(text);
        if (page) FPDF_ClosePage(page);
    }
    Page(const Page &) = delete;
    Page &operator=(const Page &) = delete;

    FPDF_TEXTPAGE textPage()
    {
        if (!text && page) text = FPDFText_LoadPage(page);
        return text;
    }
    double width() const { return FPDF_GetPageWidthF(page); }      // as shown: /Rotate already applied
    double height() const { return FPDF_GetPageHeightF(page); }

    // PDF user space (points, y up, unrotated) → the page as shown, in page units from its top left.
    QPointF toUnits(double x, double y) const
    {
        constexpr int K = 16;       // device pixels per point: FPDF_PageToDevice rounds to whole pixels
        int dx = 0, dy = 0;
        FPDF_PageToDevice(page, 0, 0, qRound(width() * K), qRound(height() * K), 0, x, y, &dx, &dy);
        return QPointF(dx * PT / K, dy * PT / K);
    }
    QRectF charBox(int i) const
    {
        double l = 0, r = 0, b = 0, t = 0;
        if (!FPDFText_GetCharBox(text, i, &l, &r, &b, &t)) return {};
        return QRectF(toUnits(l, t), toUnits(r, b)).normalized();
    }
};

QString pageText(Page &pg)
{
    FPDF_TEXTPAGE tp = pg.textPage();
    const int n = tp ? FPDFText_CountChars(tp) : 0;
    if (n <= 0) return {};
    std::vector<unsigned short> buf(size_t(n) + 1);
    const int written = FPDFText_GetText(tp, 0, n, buf.data());      // counts the terminator
    QString s = QString::fromUtf16(reinterpret_cast<const char16_t *>(buf.data()), std::max(0, written - 1));
    s.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    s.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return s;
}

QColor colour(const QString &text)
{
    const QString hex = text.trimmed();
    if (hex.size() != 7 || !hex.startsWith(QLatin1Char('#'))) return {};
    return QColor::fromString(hex);
}

// pdf.py's _plain_markdown: PDF text is flat, so keep the words and line breaks, drop the syntax.
QString plainMarkdown(QString md)
{
    using RE = QRegularExpression;
    static const RE heading(QStringLiteral("^#{1,6}\\s*"), RE::MultilineOption);
    static const RE bold(QStringLiteral("\\*\\*(.+?)\\*\\*"), RE::DotMatchesEverythingOption);
    static const RE italic(QStringLiteral("(?<!\\*)\\*(?!\\*)(.+?)(?<!\\*)\\*(?!\\*)"), RE::DotMatchesEverythingOption);
    static const RE code(QStringLiteral("`([^`]*)`"));
    static const RE bullet(QStringLiteral("^\\s*[-*+]\\s+"), RE::MultilineOption);
    md.replace(heading, QString());
    md.replace(bold, QStringLiteral("\\1"));
    md.replace(italic, QStringLiteral("\\1"));
    md.replace(code, QStringLiteral("\\1"));
    md.replace(bullet, QStringLiteral("· "));
    return md.trimmed();
}

// pdf.py's _outline: the closed outline of a variable-width centreline [[x, y, w], ...].
QPolygonF outline(const QJsonArray &json, bool roundCaps)
{
    std::vector<std::array<double, 3>> pts;
    pts.reserve(size_t(json.size()));
    for (const QJsonValue &v : json) {
        const QJsonArray a = v.toArray();
        pts.push_back({a.at(0).toDouble(), a.at(1).toDouble(), a.at(2).toDouble()});
    }
    constexpr double pi = std::numbers::pi;
    QPolygonF poly;
    const size_t n = pts.size();
    if (n == 0) return poly;
    if (n == 1) {
        const auto [x, y, w] = pts[0];
        const double r = std::max(w, 0.3) / 2;
        for (int i = 0; i < 12; ++i) poly << QPointF(x + r * std::cos(i / 12.0 * 2 * pi), y + r * std::sin(i / 12.0 * 2 * pi));
        return poly;
    }
    auto cap = [&](size_t at, size_t from, double turn) {
        const auto [x, y, w] = pts[at];
        const double a0 = std::atan2(y - pts[from][1], x - pts[from][0]) + turn;
        for (int k = 1; k < 8; ++k) poly << QPointF(x + w / 2 * std::cos(a0 + k / 8.0 * pi), y + w / 2 * std::sin(a0 + k / 8.0 * pi));
    };
    QPolygonF right;
    for (size_t i = 0; i < n; ++i) {
        const auto [x, y, w] = pts[i];
        const auto &next = pts[std::min(i + 1, n - 1)], &prev = pts[i == 0 ? 0 : i - 1];
        const double dx = next[0] - prev[0], dy = next[1] - prev[1];
        const double len = std::hypot(dx, dy) > 0 ? std::hypot(dx, dy) : 1.0;
        const double nx = -dy / len * w / 2, ny = dx / len * w / 2;
        poly << QPointF(x + nx, y + ny);
        right << QPointF(x - nx, y - ny);
    }
    if (roundCaps) cap(n - 1, n - 2, -pi / 2);
    for (auto it = right.crbegin(); it != right.crend(); ++it) poly << *it;
    if (roundCaps) cap(0, 1, -pi / 2);
    return poly;
}

QFont textFont()
{
    QFont f(QStringLiteral("Helvetica"));
    f.setPointSizeF(TextPoints);
    return f;
}

void drawShape(QPainter &p, const QJsonObject &sh)
{
    const double x = sh.value("x").toDouble(), y = sh.value("y").toDouble();
    const double w = sh.value("w").toDouble(), h = sh.value("h").toDouble();
    const QColor stroke = colour(sh.value("stroke").toString(QStringLiteral("#000000")));
    const QColor fill = colour(sh.value("fill").toString());
    QPen pen(stroke, std::max(0.2 * PT, sh.value("width").toDouble(2)));
    pen.setCapStyle(Qt::FlatCap);
    pen.setJoinStyle(Qt::MiterJoin);
    p.setPen(stroke.isValid() ? pen : QPen(Qt::NoPen));
    const QString kind = sh.value("kind").toString(QStringLiteral("rect"));
    if (kind == QLatin1String("line") || kind == QLatin1String("arrow")) {
        const QPointF p0(x, y), p1(x + w, y + h);
        p.drawLine(p0, p1);
        if (kind == QLatin1String("arrow")) {
            const double ang = std::atan2(h, w), head = std::clamp(std::hypot(w, h) * 0.22, 10.0, 26.0);
            for (double spread : {-0.42, 0.42})
                p.drawLine(QPointF(p1.x() - std::cos(ang + spread) * head, p1.y() - std::sin(ang + spread) * head), p1);
        }
        return;
    }
    p.setBrush(fill.isValid() ? QBrush(fill) : QBrush(Qt::NoBrush));
    const QRectF r(x, y, std::abs(w), std::abs(h));
    if (kind == QLatin1String("ellipse")) p.drawEllipse(r);
    else if (kind == QLatin1String("triangle")) p.drawPolygon(QPolygonF{QPointF(r.center().x(), r.top()), r.bottomRight(), r.bottomLeft()});
    else p.drawRect(r);
}

void drawSticky(QPainter &p, const QJsonObject &block, const QString &text)
{
    constexpr double pad = 14, head = 30, minHeight = 120;
    const double x = block.value("x").toDouble(), y = block.value("y").toDouble(), w = block.value("w").toDouble();
    constexpr int flags = Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop;
    p.setFont(textFont());
    const QRectF need = p.boundingRect(QRectF(x + pad, y + head, w - 2 * pad, 1e6), flags, text);
    const double h = std::max({block.value("h").toDouble(), need.height() + head + pad, minHeight});
    QColor fill = colour(block.value("colour").toString());
    if (!fill.isValid()) fill = QColor::fromString(QLatin1String(StickyDefault));
    p.setPen(Qt::NoPen);
    p.setBrush(fill);
    p.drawRoundedRect(QRectF(x, y, w, h), 8, 8);
    p.setPen(QColor::fromRgbF(0.118f, 0.133f, 0.153f));
    p.drawText(QRectF(x + pad, y + head, w - 2 * pad, h - head), flags, text);
}

void drawPicture(QPainter &p, const QJsonObject &pic)
{
    const QRectF box(pic.value("x").toDouble(), pic.value("y").toDouble(), pic.value("w").toDouble(), pic.value("h").toDouble());
    QImageReader reader(pic.value("path").toString());
    const QSize most = (box.size() * PictureDpi / 96).toSize();
    if (reader.size().isValid() && (reader.size().width() > most.width() || reader.size().height() > most.height()) && !most.isEmpty())
        reader.setScaledSize(reader.size().scaled(most, Qt::KeepAspectRatio));
    const QImage img = reader.read();
    if (!img.isNull()) p.drawImage(box, img);       // a missing picture must not lose the whole export
}

// One page's own layer, in page units (the writer runs at 96 dpi): pictures under the ink as on
// screen, then ink, shapes, and typed text above it all.
void drawLayer(QPainter &p, const QJsonObject &pg)
{
    p.setRenderHint(QPainter::Antialiasing);
    for (const QJsonValue &pic : pg.value("images").toArray()) drawPicture(p, pic.toObject());
    p.setPen(Qt::NoPen);
    for (const QJsonValue &v : pg.value("polys").toArray()) {
        const QJsonObject poly = v.toObject();
        const QPolygonF shape = outline(poly.value("points").toArray(), poly.value("round").toBool(true));
        if (shape.size() < 3) continue;
        const QJsonArray c = poly.value("color").toArray();
        QColor fill = QColor::fromRgbF(c.at(0).toDouble(), c.at(1).toDouble(), c.at(2).toDouble());
        fill.setAlphaF(poly.value("opacity").toDouble(1.0));
        QPainterPath path;
        path.setFillRule(Qt::WindingFill);
        path.addPolygon(shape);
        path.closeSubpath();
        p.fillPath(path, fill);
    }
    for (const QJsonValue &sh : pg.value("shapes").toArray()) drawShape(p, sh.toObject());
    for (const QJsonValue &v : pg.value("blocks").toArray()) {
        const QJsonObject block = v.toObject();
        const QString text = plainMarkdown(block.value("markdown").toString());
        if (text.isEmpty()) continue;
        if (block.value("sticky").toBool()) { drawSticky(p, block, text); continue; }
        p.setFont(textFont());
        p.setPen(Qt::black);
        p.drawText(QRectF(block.value("x").toDouble(), block.value("y").toDouble(), block.value("w").toDouble(), 1e6),
                   Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, text);
    }
}

// The layer is drawn the way the page is shown; the page's own space may start away from the
// origin (a crop box) and be turned (/Rotate), so the layer is placed through both.
void placeLayer(FPDF_PAGE page, FPDF_PAGEOBJECT layer)
{
    float l = 0, b = 0, r = 0, t = 0;
    if (!FPDFPage_GetCropBox(page, &l, &b, &r, &t)) FPDFPage_GetMediaBox(page, &l, &b, &r, &t);
    const double uw = r - l, uh = t - b;
    switch (FPDFPage_GetRotation(page)) {             // quarter turns clockwise
    case 1: FPDFPageObj_Transform(layer, 0, 1, -1, 0, l + uw, b); break;
    case 2: FPDFPageObj_Transform(layer, -1, 0, 0, -1, l + uw, b + uh); break;
    case 3: FPDFPageObj_Transform(layer, 0, -1, 1, 0, l, b + uh); break;
    default: FPDFPageObj_Transform(layer, 1, 0, 0, 1, l, b); break;
    }
}

struct DeviceWriter : FPDF_FILEWRITE {
    QIODevice *device = nullptr;
};

int writeBlock(FPDF_FILEWRITE *self, const void *data, unsigned long size)
{
    QIODevice *device = static_cast<DeviceWriter *>(self)->device;
    return device->write(static_cast<const char *>(data), qint64(size)) == qint64(size) ? 1 : 0;
}

} // namespace

PdfEngine::PdfEngine()
{
    static std::once_flag once;
    std::call_once(once, [] {
        FPDF_LIBRARY_CONFIG config{};
        config.version = 2;
        FPDF_InitLibraryWithConfig(&config);
    });
}

PdfEngine::~PdfEngine()
{
    for (FPDF_DOCUMENT d : std::as_const(m_docs)) FPDF_CloseDocument(d);
}

QJsonObject PdfEngine::call(const QString &method, const QJsonObject &params, QString *error)
{
    if (method == QLatin1String("info")) return info(params, error);
    if (method == QLatin1String("render")) return render(params, error);
    if (method == QLatin1String("words")) return words(params, error);
    if (method == QLatin1String("text")) return text(params, error);
    if (method == QLatin1String("search")) return search(params, error);
    if (method == QLatin1String("export")) return exportPages(params, error);
    if (method == QLatin1String("make_test_pdf")) return makeTestPdf(params, error);
    *error = QStringLiteral("unknown method %1").arg(method);
    return {};
}

FPDF_DOCUMENT PdfEngine::document(const QString &path, QString *error)
{
    if (const auto it = m_docs.constFind(path); it != m_docs.cend()) {
        m_recent.removeOne(path);
        m_recent.append(path);
        return *it;
    }
    FPDF_DOCUMENT doc = FPDF_LoadDocument(QFile::encodeName(path).constData(), nullptr);
    if (!doc) {
        *error = QStringLiteral("%1: %2").arg(QFileInfo(path).fileName(), lastError());
        return nullptr;
    }
    m_docs.insert(path, doc);
    m_recent.append(path);
    while (m_recent.size() > MaxOpen) FPDF_CloseDocument(m_docs.take(m_recent.takeFirst()));
    return doc;
}

QJsonObject PdfEngine::info(const QJsonObject &p, QString *error)
{
    FPDF_DOCUMENT doc = document(p.value("path").toString(), error);
    if (!doc) return {};
    const int pages = FPDF_GetPageCount(doc);
    QJsonArray sizes;
    for (int i = 0; i < pages; ++i) {
        FS_SIZEF size{};
        FPDF_GetPageSizeByIndexF(doc, i, &size);
        sizes.append(QJsonArray{size.width * PT, size.height * PT});
    }
    QString title;
    if (const unsigned long bytes = FPDF_GetMetaText(doc, "Title", nullptr, 0); bytes > 2) {
        std::vector<char16_t> buf(bytes / 2);
        FPDF_GetMetaText(doc, "Title", buf.data(), bytes);
        title = QString::fromUtf16(buf.data(), qsizetype(buf.size()) - 1);
    }
    return {{"pages", pages}, {"title", title}, {"sizes", sizes}};
}

QJsonObject PdfEngine::render(const QJsonObject &p, QString *error)
{
    const QString path = p.value("path").toString(), outDir = p.value("out_dir").toString();
    const int index = p.value("index").toInt();
    double scale = std::clamp(p.value("scale").toDouble(1.0), 0.1, 8.0);
    FPDF_DOCUMENT doc = document(path, error);
    if (!doc) return {};
    Page pg(doc, index);
    if (!pg.page) { *error = lastError(); return {}; }
    const double w = pg.width() * PT, h = pg.height() * PT;
    if (w * h * scale * scale > MaxRenderPixels) scale = std::sqrt(MaxRenderPixels / (w * h));
    const int pw = std::max(1, int(w * scale + 0.5)), ph = std::max(1, int(h * scale + 0.5));
    const QByteArray key = QCryptographicHash::hash(QStringLiteral("%1|%2|%3").arg(path).arg(index).arg(scale, 0, 'f', 3).toUtf8(),
                                                    QCryptographicHash::Sha1).toHex().left(20);
    QDir().mkpath(outDir);
    const QString out = QStringLiteral("%1/pdf-%2.png").arg(outDir, QString::fromLatin1(key));
    if (!QFileInfo::exists(out)) {
        QImage img(pw, ph, QImage::Format_ARGB32);        // B,G,R,A in memory: PDFium's BGRA
        if (img.isNull()) { *error = QStringLiteral("not enough memory to show this page"); return {}; }
        img.fill(Qt::white);
        FPDF_BITMAP bitmap = FPDFBitmap_CreateEx(pw, ph, FPDFBitmap_BGRA, img.bits(), int(img.bytesPerLine()));
        FPDF_RenderPageBitmap(bitmap, pg.page, 0, 0, pw, ph, 0, FPDF_ANNOT);
        FPDFBitmap_Destroy(bitmap);
        QSaveFile file(out);
        if (!file.open(QIODevice::WriteOnly) || !img.convertToFormat(QImage::Format_RGB32).save(&file, "PNG") || !file.commit()) {
            *error = QStringLiteral("cannot write %1").arg(out);
            return {};
        }
    }
    return {{"file", out}, {"width", pw}, {"height", ph}, {"pageWidth", w}, {"pageHeight", h}, {"scale", scale}};
}

QJsonObject PdfEngine::words(const QJsonObject &p, QString *error)
{
    FPDF_DOCUMENT doc = document(p.value("path").toString(), error);
    if (!doc) return {};
    Page pg(doc, p.value("index").toInt());
    if (!pg.page) { *error = lastError(); return {}; }
    QJsonArray out;
    FPDF_TEXTPAGE tp = pg.textPage();
    const int n = tp ? FPDFText_CountChars(tp) : 0;
    // PDFium has no blocks: every word is in block 0, and its line breaks number the lines.
    QString word;
    QRectF box;
    int line = 0, wordNo = 0;
    bool lineHasWords = false;
    auto flush = [&] {
        if (word.isEmpty()) return;
        out.append(QJsonArray{box.left(), box.top(), box.right(), box.bottom(), word, 0, line, wordNo++});
        word.clear();
        box = QRectF();
        lineHasWords = true;
    };
    for (int i = 0; i < n; ++i) {
        const char32_t u = FPDFText_GetUnicode(tp, i);
        if (u == U'\n' || u == U'\r') {
            flush();
            if (lineHasWords) { ++line; wordNo = 0; lineHasWords = false; }
            continue;
        }
        if (u == 0 || QChar::isSpace(u)) { flush(); continue; }
        box = box.united(pg.charBox(i));
        word += QString::fromUcs4(&u, 1);
    }
    flush();
    return {{"words", out}};
}

QJsonObject PdfEngine::text(const QJsonObject &p, QString *error)
{
    FPDF_DOCUMENT doc = document(p.value("path").toString(), error);
    if (!doc) return {};
    Page pg(doc, p.value("index").toInt());
    if (!pg.page) { *error = lastError(); return {}; }
    return {{"text", pageText(pg)}};
}

QJsonObject PdfEngine::search(const QJsonObject &p, QString *error)
{
    FPDF_DOCUMENT doc = document(p.value("path").toString(), error);
    if (!doc) return {};
    const QString query = p.value("query").toString();
    QJsonArray hits;
    const int pages = query.isEmpty() ? 0 : FPDF_GetPageCount(doc);
    for (int i = 0; i < pages; ++i) {
        Page pg(doc, i);
        FPDF_TEXTPAGE tp = pg.textPage();
        if (!tp) continue;
        FPDF_SCHHANDLE find = FPDFText_FindStart(tp, wide(query), 0, 0);     // 0: any case, like PyMuPDF
        if (!find) continue;
        int count = 0;
        QRectF first;
        while (FPDFText_FindNext(find)) {
            if (count++ > 0) continue;
            const int at = FPDFText_GetSchResultIndex(find), len = FPDFText_GetSchCount(find);
            for (int c = at; c < at + len; ++c) first = first.united(pg.charBox(c));
        }
        FPDFText_FindClose(find);
        if (count) hits.append(QJsonObject{{"index", i}, {"count", count}, {"first", QJsonArray{first.left(), first.top(), first.right(), first.bottom()}}});
    }
    return {{"hits", hits}};
}

// Flattened export. The source PDF pages are copied as they are (their text stays text), and each
// page's own layer — drawn by Qt into a scratch PDF — goes on top as one form object.
QJsonObject PdfEngine::exportPages(const QJsonObject &p, QString *error)
{
    const QJsonArray pages = p.value("pages").toArray();
    const QString out = p.value("out").toString();
    if (pages.isEmpty()) { *error = QStringLiteral("nothing to export"); return {}; }
    if (m_docs.contains(out)) { FPDF_CloseDocument(m_docs.take(out)); m_recent.removeOne(out); }

    QTemporaryDir scratch;
    if (!scratch.isValid()) { *error = QStringLiteral("no room for a scratch file"); return {}; }
    const QString layersPath = scratch.filePath(QStringLiteral("layers.pdf"));
    {
        QPdfWriter writer(layersPath);
        writer.setResolution(96);           // one painter unit = one page unit
        writer.setPageMargins(QMarginsF(0, 0, 0, 0));
        QPainter painter;
        for (qsizetype i = 0; i < pages.size(); ++i) {
            const QJsonObject pg = pages.at(i).toObject();
            const QSizeF size(pg.value("width").toDouble(794) / PT, pg.value("height").toDouble(1123) / PT);
            writer.setPageSize(QPageSize(size, QPageSize::Point, QString(), QPageSize::ExactMatch));
            if (i == 0) {
                if (!painter.begin(&writer)) { *error = QStringLiteral("cannot draw the export"); return {}; }
            } else if (!writer.newPage()) {
                *error = QStringLiteral("cannot draw the export");
                return {};
            }
            drawLayer(painter, pg);
        }
        painter.end();
    }
    const Document layers(FPDF_LoadDocument(QFile::encodeName(layersPath).constData(), nullptr), &FPDF_CloseDocument);
    if (!layers) { *error = QStringLiteral("export layers: %1").arg(lastError()); return {}; }

    const Document dest(FPDF_CreateNewDocument(), &FPDF_CloseDocument);
    for (int i = 0; i < int(pages.size()); ++i) {
        const QJsonObject pg = pages.at(i).toObject();
        FPDF_PAGE page = nullptr;
        if (const QString src = pg.value("src").toString(); !src.isEmpty()) {
            FPDF_DOCUMENT source = document(src, error);
            if (!source) return {};
            const int index = pg.value("index").toInt();
            if (!FPDF_ImportPagesByIndex(dest.get(), source, &index, 1, i)) {
                *error = QStringLiteral("cannot copy page %1 of %2").arg(index + 1).arg(QFileInfo(src).fileName());
                return {};
            }
            page = FPDF_LoadPage(dest.get(), i);
        } else {
            page = FPDFPage_New(dest.get(), i, pg.value("width").toDouble(794) / PT, pg.value("height").toDouble(1123) / PT);
        }
        if (!page) { *error = QStringLiteral("cannot make page %1 of the export").arg(i + 1); return {}; }
        FPDF_XOBJECT xobject = FPDF_NewXObjectFromPage(dest.get(), layers.get(), i);
        FPDF_PAGEOBJECT layer = xobject ? FPDF_NewFormObjectFromXObject(xobject) : nullptr;
        if (layer) {
            placeLayer(page, layer);
            FPDFPage_InsertObject(page, layer);
            FPDFPage_GenerateContent(page);
        }
        if (xobject) FPDF_CloseXObject(xobject);
        FPDF_ClosePage(page);
        if (!layer) { *error = QStringLiteral("cannot put the ink on page %1 of the export").arg(i + 1); return {}; }
    }

    // A document picked on Android is a content:// URI: written in place, there is nothing to rename.
    const bool inPlace = out.startsWith(QLatin1String("content:"));
    std::unique_ptr<QFileDevice> file;
    if (inPlace) file = std::make_unique<QFile>(out);
    else {
        QDir().mkpath(QFileInfo(out).absolutePath());
        file = std::make_unique<QSaveFile>(out);
    }
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) { *error = QStringLiteral("cannot write %1").arg(out); return {}; }
    DeviceWriter writer;
    writer.version = 1;
    writer.WriteBlock = writeBlock;
    writer.device = file.get();
    const bool saved = FPDF_SaveAsCopy(dest.get(), &writer, FPDF_NO_INCREMENTAL);
    const bool done = saved && (inPlace ? (file->close(), file->error() == QFileDevice::NoError) : static_cast<QSaveFile *>(file.get())->commit());
    if (!done) { *error = QStringLiteral("cannot write %1").arg(out); return {}; }
    return {{"file", out}, {"pages", pages.size()}};
}

QJsonObject PdfEngine::makeTestPdf(const QJsonObject &p, QString *error)
{
    const QString out = p.value("out").toString();
    if (m_docs.contains(out)) { FPDF_CloseDocument(m_docs.take(out)); m_recent.removeOne(out); }
    QPdfWriter writer(out);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageMargins(QMarginsF(0, 0, 0, 0));
    writer.setResolution(72);               // one painter unit = one point, as PyMuPDF counts
    QPainter painter;
    if (!painter.begin(&writer)) { *error = QStringLiteral("cannot write %1").arg(out); return {}; }
    QFont font(QStringLiteral("Helvetica"));
    font.setPixelSize(14);
    painter.setFont(font);
    double y = 72;
    for (const QJsonValue &line : p.value("lines").toArray()) {
        painter.drawText(QPointF(72, y), line.toString());
        y += 24;
    }
    painter.end();
    return {{"file", out}, {"pages", 1}};
}
