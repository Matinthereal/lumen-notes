// cut <picture> <x> <y> <w> <h> <viewW> <viewH> <outdir> [rotation cropX cropY cropW cropH]: what Lumen's canvas would upload for this picture at this place
#include "media/picturepixels.h"
#include <QGuiApplication>
#include <QFile>
#include <cstdio>
static void raw(const QImage &img, const QString &path) {
    const QImage rgba = img.convertToFormat(QImage::Format_RGBA8888);
    QFile f(path); f.open(QIODevice::WriteOnly);
    for (int y = 0; y < rgba.height(); ++y) f.write(reinterpret_cast<const char *>(rgba.constScanLine(y)), rgba.width() * 4);
}
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    const QString dir = argv[8];
    const QImage source = argc > 13 ? picturepixels::load(argv[1], atoi(argv[9]), QRectF(atof(argv[10]), atof(argv[11]), atof(argv[12]), atof(argv[13]))) : picturepixels::load(argv[1], 0, QRectF(0, 0, 1, 1));
    const QImage base = picturepixels::base(source);
    raw(base, dir + "/base.rgba");
    const picturepixels::Sharp s = picturepixels::plan(source.size(), QRectF(atof(argv[2]), atof(argv[3]), atof(argv[4]), atof(argv[5])), QRect(0, 0, atoi(argv[6]), atoi(argv[7])));
    const QImage cut = picturepixels::render(source, s);
    if (!cut.isNull()) raw(cut, dir + "/sharp.rgba");
    printf("%d %d %d %d %d %g %g %g %g %d %d %d %d\n", base.width(), base.height(), s.valid(), s.pixels.width(), s.pixels.height(),
           s.device.x(), s.device.y(), s.device.width(), s.device.height(), int(s.source.x()), int(s.source.y()), int(s.source.width()), int(s.source.height()));
}
