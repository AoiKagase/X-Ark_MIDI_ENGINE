/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#pragma once
#include "../common/Types.h"
#include <algorithm>
#include <cmath>

namespace XArkMidi {

class OutputLimiter {
public:
    OutputLimiter() = default;

    void Process(f32& sampleL, f32& sampleR) const {
        Sanitize(sampleL, sampleR);

        constexpr f32 kCeiling = 0.98f;
        constexpr f32 kLimit = 0.999f;
        ApplySoftLimit(sampleL, sampleR, kCeiling, kLimit);
    }

private:
    static void Sanitize(f32& sampleL, f32& sampleR) {
        if (!std::isfinite(sampleL)) {
            sampleL = 0.0f;
        }
        if (!std::isfinite(sampleR)) {
            sampleR = 0.0f;
        }
    }

    static void ApplySoftLimit(f32& sampleL, f32& sampleR, f32 ceiling, f32 limit) {
        const f32 absPeak = std::max(std::abs(sampleL), std::abs(sampleR));
        if (absPeak <= ceiling) {
            return;
        }

        const f32 over = (absPeak - ceiling) / (limit - ceiling);
        const f32 shapedPeak = ceiling + (limit - ceiling) * std::tanh(over);
        const f32 gain = shapedPeak / absPeak;
        sampleL *= gain;
        sampleR *= gain;
    }

    friend class OutputStage;
};

class OutputStage {
public:
    enum class Mode {
        Standard,
        Enhanced,
    };

    void SetMode(Mode mode) { mode_ = mode; }
    Mode GetMode() const { return mode_; }
    void Reset() {
        smoothGain_ = 1.0f;
        densityEnergy_ = 0.0f;
        densityGain_ = 1.0f;
    }

    void Process(f32& sampleL, f32& sampleR, f32 inputGain) {
        if (mode_ == Mode::Enhanced) {
            ProcessEnhanced(sampleL, sampleR, inputGain);
            return;
        }

        sampleL *= inputGain;
        sampleR *= inputGain;
        limiter_.Process(sampleL, sampleR);
    }

private:
    static f32 ShapePeak(f32 peak, f32 kneeStart, f32 ceiling) {
        if (peak <= kneeStart) {
            return peak;
        }
        const f32 over = (peak - kneeStart) / (ceiling - kneeStart);
        return kneeStart + (ceiling - kneeStart) * std::tanh(over);
    }

    static void ApplyLinkedCurve(f32& sampleL, f32& sampleR, f32 kneeStart, f32 ceiling) {
        const f32 peak = std::max(std::abs(sampleL), std::abs(sampleR));
        if (peak <= kneeStart) {
            return;
        }
        const f32 shapedPeak = ShapePeak(peak, kneeStart, ceiling);
        const f32 gain = shapedPeak / peak;
        sampleL *= gain;
        sampleR *= gain;
    }

    void ProcessEnhanced(f32& sampleL, f32& sampleR, f32 inputGain) {
        constexpr f32 kInternalHeadroom = 0.84f;
        constexpr f32 kLoudnessDrive = 1.48f;
        constexpr f32 kDensityTarget = 0.58f;
        constexpr f32 kDensityGainFloor = 0.74f;
        constexpr f32 kDensityAttack = 0.015f;
        constexpr f32 kDensityRelease = 0.00045f;
        constexpr f32 kSaturationKneeStart = 0.72f;
        constexpr f32 kSaturationCeiling = 1.12f;
        constexpr f32 kPeakKneeStart = 0.82f;
        constexpr f32 kPeakCeiling = 0.985f;
        constexpr f32 kAttack = 0.45f;
        constexpr f32 kRelease = 0.0012f;

        sampleL *= inputGain * kInternalHeadroom * kLoudnessDrive;
        sampleR *= inputGain * kInternalHeadroom * kLoudnessDrive;
        OutputLimiter::Sanitize(sampleL, sampleR);

        const f32 energy = 0.5f * (sampleL * sampleL + sampleR * sampleR);
        const f32 densityCoeff = (energy > densityEnergy_) ? kDensityAttack : kDensityRelease;
        densityEnergy_ += (energy - densityEnergy_) * densityCoeff;
        const f32 density = std::sqrt(std::max(0.0f, densityEnergy_));
        f32 targetDensityGain = 1.0f;
        if (density > kDensityTarget) {
            targetDensityGain = std::clamp(kDensityTarget / density, kDensityGainFloor, 1.0f);
        }
        const f32 gainCoeff = (targetDensityGain < densityGain_) ? kDensityAttack : kDensityRelease;
        densityGain_ += (targetDensityGain - densityGain_) * gainCoeff;
        sampleL *= densityGain_;
        sampleR *= densityGain_;

        ApplyLinkedCurve(sampleL, sampleR, kSaturationKneeStart, kSaturationCeiling);

        const f32 peak = std::max(std::abs(sampleL), std::abs(sampleR));
        if (peak > kPeakKneeStart) {
            const f32 shapedPeak = ShapePeak(peak, kPeakKneeStart, kPeakCeiling);
            const f32 targetGain = shapedPeak / peak;
            const f32 coeff = (targetGain < smoothGain_) ? kAttack : kRelease;
            smoothGain_ += (targetGain - smoothGain_) * coeff;
        } else {
            smoothGain_ += (1.0f - smoothGain_) * kRelease;
        }

        sampleL *= smoothGain_;
        sampleR *= smoothGain_;
        OutputLimiter::ApplySoftLimit(sampleL, sampleR, 0.985f, 0.999f);
    }

    Mode mode_ = Mode::Standard;
    OutputLimiter limiter_;
    f32 smoothGain_ = 1.0f;
    f32 densityEnergy_ = 0.0f;
    f32 densityGain_ = 1.0f;
};

} // namespace XArkMidi
