#pragma once
#include "ContinuousPeakEnvelope.h"
#include <cstdint>
#include <vector>

namespace tide::lab {

struct StereoSample {double left=0.,right=0.;};

// The half-band zeros are explicitly pruned below 1e-16. Constructor allocation
// is allowed; sample processing allocates nothing. Interpolation has causal FIR
// delay, which the surrounding limiter accounts for in both detector and dry.
class HalfBandStage {
public:
    explicit HalfBandStage(const std::vector<double>& taps):h_(taps),history_(2*taps.size()) {
        if (h_.size()<5||h_.size()%4!=1) throw std::invalid_argument("Half-band FIR length must be 4m+1");
        for (size_t k=0;k<h_.size();k+=2)
            if (k!=(h_.size()-1)/2&&std::abs(h_[k])>1e-16) throw std::invalid_argument("Expected half-band zeros");
        reset();
    }
    void reset() noexcept {std::fill(history_.begin(),history_.end(),StereoSample{});position_=0;phase_=false;}
    void up(StereoSample x,StereoSample& even,StereoSample& odd) noexcept {
        const auto* p=push(x);const auto center=p[-static_cast<std::ptrdiff_t>((h_.size()-1)/4)];
        const double centerGain=2*h_[(h_.size()-1)/2];even={centerGain*center.left,centerGain*center.right};odd={};
        for (size_t k=1;k<h_.size();k+=2) {
            const auto v=p[-static_cast<std::ptrdiff_t>(k/2)];const double gain=2*h_[k];
            odd.left+=gain*v.left;odd.right+=gain*v.right;
        }
    }
    bool down(StereoSample x,StereoSample& result) noexcept {
        const auto* p=push(x);const bool emit=!phase_;phase_=!phase_;if (!emit) return false;
        const auto center=p[-static_cast<std::ptrdiff_t>((h_.size()-1)/2)];const double gain=h_[(h_.size()-1)/2];
        result={gain*center.left,gain*center.right};
        for (size_t k=1;k<h_.size();k+=2) {
            const auto v=p[-static_cast<std::ptrdiff_t>(k)];result.left+=h_[k]*v.left;result.right+=h_[k]*v.right;
        }
        return true;
    }
    size_t length() const noexcept {return h_.size();}
private:
    const StereoSample* push(StereoSample x) noexcept {
        const auto n=h_.size();history_[position_]=history_[position_+n]=x;
        const auto* p=history_.data()+position_+n;if (++position_==n) position_=0;return p;
    }
    std::vector<double> h_;
    std::vector<StereoSample> history_;
    size_t position_=0;
    bool phase_=false;
};

class StereoFIR {
public:
    explicit StereoFIR(const std::vector<double>& h):h_(h),history_(2*h.size()) {
        if (h.empty()) throw std::invalid_argument("Empty FIR");reset();
    }
    void reset() noexcept {std::fill(history_.begin(),history_.end(),StereoSample{});position_=0;}
    StereoSample process(StereoSample x) noexcept {
        const auto n=h_.size();history_[position_]=history_[position_+n]=x;const auto* p=history_.data()+position_+n;
        StereoSample result;
        for (size_t k=0;k<n;++k) {const auto v=p[-static_cast<std::ptrdiff_t>(k)];result.left+=h_[k]*v.left;result.right+=h_[k]*v.right;}
        if (++position_==n) position_=0;return result;
    }
private:
    std::vector<double> h_;std::vector<StereoSample> history_;size_t position_=0;
};

// Native streaming implementation of p821_multistage_limiter.py. This is a
// quality experiment, not a recovered P821 algorithm and not zero aliasing.
// Factor 4/8 means 8/16x envelope evaluation followed by 4/8x integration output.
// Native latency is 340/346 samples, including the compensation FIR. The host
// must account for this latency. Fixed 48 kHz, fixed release, positive ceilings.
class ContinuousMultistageLimiter {
public:
    ContinuousMultistageLimiter(int factor,double release,const std::vector<double>& longHalfBand,
                               const std::vector<double>& shortHalfBand,const std::vector<double>& compensation)
        :factor_(factor),upFactor_(2*factor),compensation_(compensation) {
        if (factor!=4&&factor!=8) throw std::invalid_argument("Only factors 4 and 8 are supported");
        if (longHalfBand.size()!=513||shortHalfBand.size()!=65||compensation.size()!=129)
            throw std::invalid_argument("Expected span-256 and 64/128-order filters");
        for (int rate=2;rate<=upFactor_;rate*=2) {
            const auto& h=rate==2?longHalfBand:shortHalfBand;up_.emplace_back(h);upDelay_+=(h.size()-1)/(2*rate);
        }
        for (int rate=factor_;rate>=2;rate/=2) down_.emplace_back(rate==2?longHalfBand:shortHalfBand);
        size_t downDelay=0;for (int rate=2;rate<=factor_;rate*=2) downDelay+=((rate==2?longHalfBand:shortHalfBand).size()-1)/(2*rate);
        latency_=upDelay_+downDelay+64;dry_.resize(latency_+1);ceilings_.resize(upDelay_+2);
        for (auto& e:envelopes_) e.prepare(48000.*upFactor_,release);reset();
    }
    size_t latency() const noexcept {return latency_;}
    void reset() noexcept {
        for (auto& f:up_) f.reset();for (auto& f:down_) f.reset();for (auto& e:envelopes_) e.reset();compensation_.reset();
        std::fill(dry_.begin(),dry_.end(),StereoSample{});std::fill(ceilings_.begin(),ceilings_.end(),StereoSample{});
        dryPosition_=ceilingPosition_=0;received_=0;previousX_=previousZ_=previousOddX_=previousOddE_={};first_=true;
    }
    StereoSample process(StereoSample x,StereoSample cap) noexcept {
        cap.left=std::max(cap.left,1e-9);cap.right=std::max(cap.right,1e-9);
        if (first_) {std::fill(ceilings_.begin(),ceilings_.end(),cap);first_=false;}
        ceilings_[ceilingPosition_]=cap;
        const auto c0=ceilingAt(upDelay_),c1=ceilingAt(upDelay_-1);
        std::array<StereoSample,16> work {},next {};work[0]=x;size_t count=1;
        for (auto& f:up_) {for (size_t k=0;k<count;++k) f.up(work[k],next[2*k],next[2*k+1]);count*=2;work=next;}
        StereoSample downsampled;
        for (size_t j=0;j<count;++j) {
            const double fraction=double(j)/upFactor_;
            const StereoSample z {work[j].left/(c0.left+(c1.left-c0.left)*fraction),work[j].right/(c0.right+(c1.right-c0.right)*fraction)};
            if (received_>0) {
                const StereoSample e {envelopes_[0].process(previousZ_.left,z.left),envelopes_[1].process(previousZ_.right,z.right)};
                if (received_%2==0) {
                    StereoSample delta {interval(previousOddX_.left,previousX_.left,previousOddE_.left,e.left),
                                        interval(previousOddX_.right,previousX_.right,previousOddE_.right,e.right)};
                    previousOddX_=previousX_;previousOddE_=e;bool valid=true;
                    for (auto& f:down_) {StereoSample output;if (!f.down(delta,output)) {valid=false;break;}delta=output;}
                    if (valid) downsampled=delta;
                }
            }
            previousX_=work[j];previousZ_=z;++received_;
        }
        const auto correction=compensation_.process(downsampled);
        dry_[dryPosition_]=x;const auto delayed=dry_[(dryPosition_+1)%dry_.size()];
        dryPosition_=(dryPosition_+1)%dry_.size();ceilingPosition_=(ceilingPosition_+1)%ceilings_.size();
        return {delayed.left+correction.left,delayed.right+correction.right};
    }
private:
    StereoSample ceilingAt(size_t age) const noexcept {return ceilings_[(ceilingPosition_+ceilings_.size()-age)%ceilings_.size()];}
    static double rational(double x0,double x1,double e0,double e1) noexcept {
        const double u=(e1-e0)/e0;double a,b;
        if (std::abs(u)<1e-4) {const double u2=u*u,u3=u2*u,u4=u2*u2;a=1-u/2+u2/3-u3/4+u4/5;b=.5-u/3+u2/4-u3/5+u4/6;}
        else {const double l=std::log1p(u);a=l/u;b=(u-l)/(u*u);}
        return (x0*a+(x1-x0)*b)/e0;
    }
    static double interval(double x0,double x1,double e0,double e1) noexcept {
        if (std::max(e0,e1)<=1) return 0.;double area;
        if (std::min(e0,e1)>=1) area=rational(x0,x1,e0,e1);
        else {const double t=(1-e0)/(e1-e0),xm=x0*(1-t)+x1*t;
            area=e0>1?t*rational(x0,xm,e0,1)+(1-t)*(xm+x1)*.5:t*(x0+xm)*.5+(1-t)*rational(xm,x1,1,e1);}
        return area-(x0+x1)*.5;
    }
    int factor_,upFactor_;std::vector<HalfBandStage> up_,down_;StereoFIR compensation_;
    std::array<ContinuousPeakEnvelope,2> envelopes_;
    size_t upDelay_=0,latency_=0,dryPosition_=0,ceilingPosition_=0;uint64_t received_=0;
    std::vector<StereoSample> dry_,ceilings_;StereoSample previousX_,previousZ_,previousOddX_,previousOddE_;bool first_=true;
};
}
