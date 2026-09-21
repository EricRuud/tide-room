#pragma once

// Fixed-setting research feature extractor: 48 kHz, 256 samples per frame.
// This is the measured-input side of our own behavioral model, not vendor code.
// Coefficients are supplied by the accompanying Python exporter. No allocation
// or file access occurs in process(). The dummy initial controller frame is a
// responsibility of the caller and is not returned here.
#include <algorithm>
#include <array>
#include <cmath>

namespace tide::p821lab {

class FeatureExtractor {
public:
    static constexpr int blockSize = 256;
    using Coefficients = std::array<double, 5>; // b0, b1, b2, a1, a2
    using FilterBank = std::array<Coefficients, 13>;
    struct Frame {
        std::array<std::array<float, 61>, 2> control {};
        std::array<float, 26> width {};
        double sharedLevelDb=-90.; // unrounded 47-frame raw stereo log-RMS
    };

    explicit FeatureExtractor(const FilterBank& coefficients,bool acEnabled=false,
                              const Coefficients& acCoefficients={1.,0.,0.,0.,0.})
        : coefficients_(coefficients),acEnabled_(acEnabled),acCoefficients_(acCoefficients) { reset(); }

    void reset() noexcept {
        delay_ = {}; rmsHistory_ = {}; levelRing_ = {}; acDelay_={}; widthDelay_={};
        ringPosition_ = 0; levelSum_ = 0.; initialized_ = false;
        quietFrames_=1; // caller supplies one virtual quiet controller frame
    }

    void enableRestingState(bool enabled) noexcept {restingState_=enabled;}

    Frame process(const float* interleaved) noexcept {
        std::array<std::array<double, 2>, 14> energy {}, peak {};
        std::array<std::array<double, 2>, 6> widthEnergy {}, widthPeak {};
        std::array<double, 2> spatialEnergy {}, spatialPeak {};
        for (int n = 0; n < blockSize; ++n) {
            std::array<double,2> fast {interleaved[2*n],interleaved[2*n+1]};
            if (acEnabled_) for (int c=0;c<2;++c) {
                const double raw=fast[c];const auto& b=acCoefficients_;auto& s=acDelay_[c];
                fast[c]=b[0]*raw+s[0];s[0]=b[1]*raw-b[3]*fast[c]+s[1];s[1]=b[2]*raw-b[4]*fast[c];
            }
            const double left = fast[0], right = fast[1];
            const std::array<double, 2> spatial { .5*(left+right), .5*(left-right) };
            for (int c = 0; c < 2; ++c) {
                const double x = fast[c];
                energy[0][c] += x*x; peak[0][c] = std::max(peak[0][c], std::abs(x));
                spatialEnergy[c] += spatial[c]*spatial[c];
                spatialPeak[c] = std::max(spatialPeak[c], std::abs(spatial[c]));
                for (int k = 0; k < 13; ++k) {
                    const auto& b = coefficients_[k]; auto& s = delay_[k][c];
                    const double y = b[0]*x+s[0];
                    s[0] = b[1]*x-b[3]*y+s[1]; s[1] = b[2]*x-b[4]*y;
                    energy[k+1][c] += y*y; peak[k+1][c] = std::max(peak[k+1][c], std::abs(y));
                }
                if (acEnabled_) {
                    const double raw=interleaved[2*n+c];widthEnergy[0][c]+=raw*raw;
                    widthPeak[0][c]=std::max(widthPeak[0][c],std::abs(raw));
                    for (int k=0;k<5;++k) {
                        const auto& b=coefficients_[k];auto& s=widthDelay_[k][c];const double y=b[0]*raw+s[0];
                        s[0]=b[1]*raw-b[3]*y+s[1];s[1]=b[2]*raw-b[4]*y;
                        widthEnergy[k+1][c]+=y*y;widthPeak[k+1][c]=std::max(widthPeak[k+1][c],std::abs(y));
                    }
                }
            }
        }
        constexpr double rootHalf = .70710678118654752440084436210485;
        std::array<std::array<double, 2>, 14> rms {};
        for (int k = 0; k < 14; ++k)
            for (int c = 0; c < 2; ++c) { rms[k][c] = std::sqrt(energy[k][c]/blockSize); peak[k][c] *= rootHalf; }
        std::array<std::array<double,2>,6> widthRms {};
        for (int k=0;k<6;++k) for (int c=0;c<2;++c) {
            widthRms[k][c]=acEnabled_?std::sqrt(widthEnergy[k][c]/blockSize):rms[k][c];
            widthPeak[k][c]=acEnabled_?widthPeak[k][c]*rootHalf:peak[k][c];
        }
        const double level = .5*(std::max(-90.,20*std::log10(std::max(widthRms[0][0],1.e-10)))
                               +std::max(-90.,20*std::log10(std::max(widthRms[0][1],1.e-10))));
        if (!initialized_) { levelRing_.fill(level); levelSum_ = 47*level; initialized_ = true; }
        levelSum_ += level-levelRing_[ringPosition_]; levelRing_[ringPosition_] = level;
        ringPosition_ = (ringPosition_+1)%47; const double average = levelSum_/47;
        Frame result;result.sharedLevelDb=average;
        for (int c = 0; c < 2; ++c) {
            auto& f = result.control[c];
            for (int k = 0; k < 6; ++k) {
                f[k] = encode(rms[k][c]); f[k+6] = encode(rms[k][1-c]);
                f[k+15] = encode(peak[k][c]); f[k+21] = encode(peak[k][1-c]);
            }
            for (int k = 0; k < 2; ++k) {
                f[12+k] = encode(std::sqrt(spatialEnergy[k]/blockSize));
                f[27+k] = encode(spatialPeak[k]*rootHalf);
            }
            f[14] = static_cast<float>((average+30)/30);
            for (int k = 0; k < 8; ++k) {
                f[29+2*k] = encode(rms[6+k][c],2.,1.e-10);
                f[30+2*k] = encode(peak[6+k][c],2.,1.e-10);
                f[45+2*k] = encode(rms[6+k][1-c],2.,1.e-10);
                f[46+2*k] = encode(peak[6+k][1-c],2.,1.e-10);
            }
        }
        for (int k = 0; k < 6; ++k) {
            result.width[k] = encode(std::min(widthRms[k][0],widthRms[k][1]));
            result.width[k+6] = encode(std::max(widthRms[k][0],widthRms[k][1]));
            result.width[k+12] = encode(std::min(widthPeak[k][0],widthPeak[k][1]));
            result.width[k+18] = encode(std::max(widthPeak[k][0],widthPeak[k][1]));
        }
        result.width[24] = static_cast<float>((average+30)/30);
        quietFrames_=(result.width[0]<=-2.99999f&&result.width[6]<=-2.99999f)?std::min(188,quietFrames_+1):0;
        if (restingState_&&quietFrames_>=188) rmsHistory_={};
        for (int c = 0; c < 2; ++c) rmsHistory_[c] = std::max(widthRms[0][c], historyDecay_*rmsHistory_[c]);
        const double shared = std::min(rmsHistory_[0],rmsHistory_[1]);
        result.width[25] = shared > 1.e-5 ? static_cast<float>(shared/(shared+.003)) : 0.f;
        return result;
    }

private:
    static float encode(double amplitude, double maximum=1., double floor=1.e-8) noexcept {
        return static_cast<float>(std::clamp((20*std::log10(std::max(amplitude,floor))+30)/30,-3.,maximum));
    }
    FilterBank coefficients_;
    bool acEnabled_=false;
    Coefficients acCoefficients_ {};
    std::array<std::array<double,2>,2> acDelay_ {};
    std::array<std::array<std::array<double,2>,2>,5> widthDelay_ {};
    std::array<std::array<std::array<double, 2>, 2>, 13> delay_ {};
    std::array<double, 2> rmsHistory_ {};
    std::array<double, 47> levelRing_ {};
    int ringPosition_ = 0;
    double levelSum_ = 0.;
    bool initialized_ = false;
    bool restingState_=false;
    int quietFrames_=1;
    const double historyDecay_ = std::exp(-256./(48000*.25));
};

} // namespace tide::p821lab
