#include "stylustilt.h"
#include <QtMath>
#include <algorithm>
#include <cmath>
#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QJniEnvironment>
#include <QJniObject>
#include <QtCore/qcoreapplication_platform.h>
#endif

namespace stylustilt {

QPointF fromAndroid(float tilt, float orientation)
{
    // With sin x = -k·sin o and sin y = k·cos o, AOSP's cos(tilt) = cos x · cos y becomes
    // q·k⁴ - k² + s = 0 for s = sin²(tilt) and q = sin²o·cos²o. The smaller root, written so that
    // it also holds at q = 0 (a lean straight along an axis), is the digitiser's k².
    const double s = std::pow(std::sin(double(tilt)), 2);
    const double so = std::sin(double(orientation)), co = std::cos(double(orientation));
    const double q = so * so * co * co;
    const double k = std::sqrt(2 * s / (1 + std::sqrt(std::max(0.0, 1 - 4 * q * s))));
    return {qRadiansToDegrees(std::asin(std::clamp(-k * so, -1.0, 1.0))),
            qRadiansToDegrees(std::asin(std::clamp(k * co, -1.0, 1.0)))};
}

void Recent::record(quint64 timeMs, float tilt, float orientation)
{
    QMutexLocker lock(&m_lock);
    m_samples[m_next] = {timeMs, tilt, orientation, true};
    m_next = (m_next + 1) % int(m_samples.size());
}

bool Recent::find(quint64 timeMs, QPointF *xyTilt) const
{
    QMutexLocker lock(&m_lock);
    const int n = int(m_samples.size());
    for (int i = 1; i <= n; ++i) {
        const Sample &s = m_samples[(m_next - i + n) % n];
        if (s.used && s.timeMs == timeMs) {
            *xyTilt = fromAndroid(s.tilt, s.orientation);
            return true;
        }
    }
    return false;
}

#ifdef Q_OS_ANDROID
Recent &recent()
{
    static Recent samples;
    return samples;
}

static void JNICALL sampleFromJava(JNIEnv *, jclass, jlong timeMs, jfloat tilt, jfloat orientation)
{
    recent().record(quint64(timeMs), tilt, orientation);
}

void installAndroidHook()
{
    static const char className[] = "io/github/matinthereal/lumen/StylusTilt";
    QJniEnvironment env;
    if (!env.registerNativeMethods(className, {{"sample", "(JFF)V", reinterpret_cast<void *>(sampleFromJava)}})) {
        qWarning("stylus tilt: %s is missing from the package; the pencil will not shade", className);
        return;
    }
    QJniObject::callStaticMethod<void>(className, "install", "(Landroid/content/Context;)V",
                                       QNativeInterface::QAndroidApplication::context().object());
}
#endif

} // namespace stylustilt
