#include "fsrs.h"
#include <algorithm>
#include <cmath>

namespace fsrs {

static constexpr double DECAY = -0.5;
static constexpr double FACTOR = 19.0 / 81.0;

double retrievability(double t, double s)
{
    if (s <= 0) return 0;
    return std::pow(1.0 + FACTOR * std::max(0.0, t) / s, DECAY);
}

double intervalFor(double s, double r)
{
    return s / FACTOR * (std::pow(r, 1.0 / DECAY) - 1.0);
}

double initialStability(const Params &p, int g) { return std::max(0.1, p.w[std::clamp(g, 1, 4) - 1]); }

double initialDifficulty(const Params &p, int g)
{
    return std::clamp(p.w[4] - std::exp(p.w[5] * (g - 1)) + 1.0, 1.0, 10.0);
}

static double nextDifficulty(const Params &p, double d, int g)
{
    const double delta = -p.w[6] * (g - 3);
    const double dp = d + delta * (10.0 - d) / 9.0;
    const double d0 = initialDifficulty(p, 4);
    return std::clamp(p.w[7] * d0 + (1.0 - p.w[7]) * dp, 1.0, 10.0);
}

static double stabilityAfterRecall(const Params &p, double d, double s, double r, int g)
{
    const double hard = g == Hard ? p.w[15] : 1.0;
    const double easy = g == Easy ? p.w[16] : 1.0;
    return s * (std::exp(p.w[8]) * (11.0 - d) * std::pow(s, -p.w[9]) * (std::exp(p.w[10] * (1.0 - r)) - 1.0) * hard * easy + 1.0);
}

static double stabilityAfterForget(const Params &p, double d, double s, double r)
{
    const double sf = p.w[11] * std::pow(d, -p.w[12]) * (std::pow(s + 1.0, p.w[13]) - 1.0) * std::exp(p.w[14] * (1.0 - r));
    return std::min(sf, s);
}

static double shortTermStability(const Params &p, double s, int g)
{
    return s * std::exp(p.w[17] * (g - 3 + p.w[18]));
}

Outcome review(const Params &p, const CardState &cur, int rating, double elapsedDays)
{
    const int g = std::clamp(rating, 1, 4);
    Outcome o;
    CardState n = cur;
    n.reps = cur.reps + 1;
    if (cur.state == State::New || cur.stability <= 0) {
        n.stability = initialStability(p, g);
        n.difficulty = initialDifficulty(p, g);
    } else if (elapsedDays < 1.0) {
        n.stability = shortTermStability(p, cur.stability, g);
        n.difficulty = nextDifficulty(p, cur.difficulty, g);
    } else {
        const double r = retrievability(elapsedDays, cur.stability);
        n.difficulty = nextDifficulty(p, cur.difficulty, g);
        n.stability = g == Again ? stabilityAfterForget(p, cur.difficulty, cur.stability, r) : stabilityAfterRecall(p, cur.difficulty, cur.stability, r, g);
    }
    n.stability = std::max(0.1, n.stability);
    if (g == Again) {
        n.lapses = cur.lapses + (cur.state == State::Review ? 1 : 0);
        n.state = cur.state == State::New ? State::Learning : State::Relearning;
        o.intervalDays = 0;
    } else {
        n.state = State::Review;
        double days = std::round(intervalFor(n.stability, p.desiredRetention));
        days = std::clamp(days, 1.0, p.maxIntervalDays);
        if (g == Hard && cur.state == State::Review) days = std::max(1.0, std::min(days, std::round(elapsedDays * 1.2 + 1)));
        o.intervalDays = days;
    }
    o.scheduledDays = o.intervalDays;
    o.next = n;
    return o;
}

} // namespace fsrs
