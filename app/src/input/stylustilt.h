#pragma once
#include <QMutex>
#include <QPointF>
#include <array>

// Qt 6.10's Android plugin passes a stylus on with its position and pressure only: xTilt and yTilt
// arrive as 0, so the pencil never shades there. StylusTilt.java (packaging/android) reads the tilt
// from each stylus MotionEvent and records it here under the event's time; the tablet filter finds
// it again by the QTabletEvent's timestamp, which Qt takes from the same MotionEvent.
namespace stylustilt {

// Android gives a stylus's lean as AXIS_TILT (radians off upright, 0..π/2) and AXIS_ORIENTATION
// (radians, the way the tip points across the screen: 0 up, -π/2 left, π/2 right). It makes both
// from the digitiser's own x and y tilt (AOSP TouchInputMapper: orientation = atan2(-sin x, sin y),
// tilt = acos(cos x · cos y)). This undoes that, giving what Qt reports for the same pen on a
// desktop tablet: xTilt and yTilt in degrees, positive when the top of the pen leans right and
// towards the bottom of the screen.
QPointF fromAndroid(float tilt, float orientation);

// The last few stylus samples: written on Android's UI thread, read on Qt's.
class Recent {
public:
    void record(quint64 timeMs, float tilt, float orientation);
    // The Qt tilt of the newest sample with this event time; false if there is none.
    bool find(quint64 timeMs, QPointF *xyTilt) const;

private:
    struct Sample { quint64 timeMs = 0; float tilt = 0, orientation = 0; bool used = false; };
    std::array<Sample, 64> m_samples;
    int m_next = 0;
    mutable QMutex m_lock;
};

#ifdef Q_OS_ANDROID
Recent &recent();
void installAndroidHook();   // once, after the QGuiApplication exists
#endif

} // namespace stylustilt
