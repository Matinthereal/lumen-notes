#pragma once
#include <QImage>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QString>

// The pixels a picture on a page is drawn from. The canvas keeps one texture per picture that is
// good at any zoom (never longer than kLongest, with mipmaps), and once the view has stopped moving
// it asks for a second one cut for the screen as it is right now. That second one is what makes a
// screenshot's small text readable and a phone photo worth zooming into: the graphics card's own
// shrinking is a blur between two mipmap levels, and the first texture has already thrown away
// everything past kLongest.
namespace picturepixels {

constexpr int kLongest = 2560;

// The file as the page shows it: the camera's own turn applied, then the picture's turn, then its
// trim. Safe from any thread; the last few are kept, because every pan asks for the same ones.
QImage load(const QString &path, int rotation, const QRectF &crop);

// The texture for any zoom: the whole picture, shrunk to kLongest along its longer side.
QImage base(const QImage &source);

struct Sharp {
    QRectF source;      // the part of the picture's own pixels this is cut from (whole pixels when enlarged)
    QSize pixels;       // the texture's size
    QRectF device;      // where it goes, in physical screen pixels
    bool whole = false; // it covers the entire picture, not just the part in view
    bool valid() const { return !pixels.isEmpty(); }
    bool operator==(const Sharp &o) const { return source == o.source && pixels == o.pixels && device == o.device; }
};

// What to cut for a picture of `sourcePixels` drawn at `device` (physical pixels, as fractional as
// the zoom makes it) when `view` is the part of the screen the page has. Nothing when the picture
// is out of view, or when the base texture already holds every pixel the screen can show.
//
// Shrunk, the picture is resampled onto the screen's own pixel grid: one texel per pixel and its
// edges on whole pixels, so the card copies it instead of filtering it. Enlarged past what the base
// texture holds, the part in view is taken at the picture's own resolution.
Sharp plan(QSize sourcePixels, const QRectF &device, const QRect &view);

QImage render(const QImage &source, const Sharp &sharp);

}
