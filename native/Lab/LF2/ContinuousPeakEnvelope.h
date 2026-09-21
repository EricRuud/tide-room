#pragma once

// Research-only continuous release on a three-point quadratic reconstruction.
// Requires one future normalized input sample. No allocation or synchronization
// occurs in process(). This alone is not an antialiased audio renderer.
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace tide::lab {

class ContinuousPeakEnvelope {
public:
    void prepare(double sampleRate, double releaseSeconds) {
        if (!(std::isfinite(sampleRate) && std::isfinite(releaseSeconds) &&
              sampleRate > 0.0 && releaseSeconds >= 0.0001))
            throw std::invalid_argument("Invalid envelope sample rate or release");
        lambda = 1.0 / (sampleRate * releaseSeconds);
        if (lambda > 0.25)
            throw std::invalid_argument("Envelope integration step is too large");
        decay = std::exp(-lambda);
        fullMoments = moments(lambda);
        reset();
    }

    void reset() noexcept { previous = 0.0; state = 0.0; }

    double process(double current, double following) noexcept {
        const double a = previous;
        const double c = 0.5 * (following - 2.0 * current + previous);
        const double b = current - previous - c;
        state = std::max(state, std::abs(previous));
        if (b * (b + 2.0 * c) >= 0.0 && previous * current >= 0.0) {
            const double endpoint = std::abs(current);
            if (endpoint >= std::abs(previous) && endpoint >= state) {
                state = endpoint;
            } else {
                const double sign = previous + current >= 0.0 ? 1.0 : -1.0;
                state = std::max(endpoint, state * decay + sign *
                    (a * fullMoments[0] + b * fullMoments[1] + c * fullMoments[2]));
            }
        } else {
            std::array<double, 5> points {0.0, 1.0, 0.0, 0.0, 0.0};
            int count = 2;
            const auto add = [&](double point) {
                if (point > 0.0 && point < 1.0) points[count++] = point;
            };
            if (std::abs(c) > 1.0e-14) {
                add(-b / (2.0 * c));
                const double discriminant = b * b - 4.0 * c * a;
                if (discriminant > 0.0) {
                    const double q = -0.5 * (b + std::copysign(std::sqrt(discriminant), b));
                    add(q / c);
                    if (std::abs(q) > 1.0e-30) add(a / q);
                }
            } else if (std::abs(b) > 1.0e-14) {
                add(-a / b);
            }
            for (int i = 1; i < count; ++i) {
                const double value = points[i];
                int j = i - 1;
                while (j >= 0 && points[j] > value) {
                    points[j + 1] = points[j];
                    --j;
                }
                points[j + 1] = value;
            }
            for (int j = 0; j < count - 1; ++j) {
                const double left = points[j], right = points[j + 1];
                const double length = right - left;
                if (length <= 1.0e-14) continue;
                const double mid = 0.5 * (left + right);
                const double sign = a + b * mid + c * mid * mid >= 0.0 ? 1.0 : -1.0;
                const double A = sign * (a + b * left + c * left * left);
                const double B = sign * (b + 2.0 * c * left) * length;
                const double C = sign * c * length * length;
                const double local = lambda * length;
                const auto J = moments(local);
                state = state * std::exp(-local) + A * J[0] + B * J[1] + C * J[2];
                state = std::max(state, std::abs(a + b * right + c * right * right));
            }
        }
        previous = current;
        return state;
    }

private:
    static std::array<double, 3> moments(double value) noexcept {
        std::array<double, 3> result {};
        for (int m = 0; m < 3; ++m) {
            double term = value / (m + 1), sum = term;
            for (int k = 1; k < 12; ++k) {
                term *= -value / (m + k + 1);
                sum += term;
            }
            result[m] = sum;
        }
        return result;
    }

    double lambda = 1.0 / (48000.0 * 0.0046), decay = 0.0;
    double previous = 0.0, state = 0.0;
    std::array<double, 3> fullMoments {};
};

} // namespace tide::lab
