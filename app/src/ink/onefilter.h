#pragma once
#include <cmath>
#include <numbers>

// The 1€ filter (Casiez, Roustan, Vogel 2012): jitter-free when slow, lag-free when fast. Used on
// pen x/y before the points are stored. Deterministic for a given sample sequence.
class OneEuroFilter {
public:
    OneEuroFilter(double minCutoffHz = 2.5, double beta = 0.03, double dCutoffHz = 1.0)
        : m_minCutoff(minCutoffHz), m_beta(beta), m_dCutoff(dCutoffHz) {}

    void reset() { m_hasPrev = false; }

    double filter(double value, double tSeconds)
    {
        if (!m_hasPrev) {
            m_hasPrev = true; m_x = value; m_dx = 0; m_t = tSeconds;
            return value;
        }
        double dt = tSeconds - m_t;
        if (dt <= 0) dt = 1.0 / 330.0;
        m_t = tSeconds;
        const double dx = (value - m_x) / dt;
        m_dx = lerp(m_dx, dx, alpha(m_dCutoff, dt));
        const double cutoff = m_minCutoff + m_beta * std::abs(m_dx);
        m_x = lerp(m_x, value, alpha(cutoff, dt));
        return m_x;
    }

private:
    static double alpha(double cutoff, double dt)
    {
        const double tau = 1.0 / (2.0 * std::numbers::pi * cutoff);
        return 1.0 / (1.0 + tau / dt);
    }
    static double lerp(double a, double b, double t) { return a + (b - a) * t; }
    double m_minCutoff, m_beta, m_dCutoff;
    bool m_hasPrev = false;
    double m_x = 0, m_dx = 0, m_t = 0;
};
