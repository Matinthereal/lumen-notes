#pragma once
#include <QQuickImageProvider>

// image://theme/<icon-name> → the Plasma icon theme (Breeze by default), so the toolbar looks
// native and follows the user's theme. Falls back to Breeze when the platform theme gives nothing.
class ThemeIconProvider : public QQuickImageProvider {
public:
    ThemeIconProvider();
    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override;
};
