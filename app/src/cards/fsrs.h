#pragma once
#include <array>
#include <cstdint>

// FSRS scheduler (D-011), the published FSRS-5 formulas with the default parameters.
//   R(t, S) = (1 + F·t/S)^C  with C = -0.5, F = 19/81, so R(S, S) = 0.9 exactly.
// Pure, deterministic, tested. Times in days; ratings 1 Again, 2 Hard, 3 Good, 4 Easy.
namespace fsrs {

enum Rating : int { Again = 1, Hard = 2, Good = 3, Easy = 4 };
enum class State : int { New = 0, Learning = 1, Review = 2, Relearning = 3 };

struct Params {
    std::array<double, 19> w{0.40255, 1.18385, 3.173, 15.69105, 7.1949, 0.5345, 1.4604, 0.0046, 1.54575, 0.1192,
                             1.01925, 1.9395, 0.11, 0.29605, 2.2698, 0.2315, 2.9898, 0.51655, 0.6621};
    double desiredRetention = 0.9;
    double maxIntervalDays = 365 * 2;
};

struct CardState {
    double stability = 0;   // days
    double difficulty = 0;  // 1..10
    State state = State::New;
    int reps = 0;
    int lapses = 0;
};

struct Outcome {
    CardState next;
    double intervalDays = 0;   // 0 = again in ~10 minutes
    double scheduledDays = 0;
};

double retrievability(double elapsedDays, double stability);
double intervalFor(double stability, double retention);
double initialStability(const Params &p, int rating);
double initialDifficulty(const Params &p, int rating);
Outcome review(const Params &p, const CardState &cur, int rating, double elapsedDays);

} // namespace fsrs
