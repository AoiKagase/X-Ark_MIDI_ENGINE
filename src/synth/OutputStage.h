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
        EnhancedNatural,
        EnhancedWarm,
        EnhancedLoud,
    };

    struct Meter {
        Mode mode = Mode::Standard;
        f32 inputPeak = 0.0f;
        f32 outputPeak = 0.0f;
        f32 densityGain = 1.0f;
        f32 peakGain = 1.0f;
        u32 processedFrames = 0;
    };

    void SetMode(Mode mode) {
        mode_ = mode;
        RefreshActiveParams();
    }
    Mode GetMode() const { return mode_; }
    void SetSampleRate(u32 sampleRate) {
        const f32 effectiveSampleRate = static_cast<f32>(std::max<u32>(1, sampleRate));
        coefficientScaleExponent_ = 44100.0f / effectiveSampleRate;
        RefreshActiveParams();
    }
    void Reset() {
        smoothGain_ = 1.0f;
        densityEnergy_ = 0.0f;
        densityGain_ = 1.0f;
        BeginMeterBlock();
    }

    void BeginMeterBlock() {
        meter_ = {};
        meter_.mode = mode_;
        meter_.densityGain = densityGain_;
        meter_.peakGain = smoothGain_;
    }

    void Process(f32& sampleL, f32& sampleR, f32 inputGain) {
        const f32 inputPeak = std::max(std::abs(sampleL * inputGain), std::abs(sampleR * inputGain));
        if (mode_ != Mode::Standard) {
            ProcessEnhanced(sampleL, sampleR, inputGain, activeParams_);
            UpdateMeter(inputPeak, sampleL, sampleR);
            return;
        }

        sampleL *= inputGain;
        sampleR *= inputGain;
        limiter_.Process(sampleL, sampleR);
        UpdateMeter(inputPeak, sampleL, sampleR);
    }

    Meter GetMeter() const { return meter_; }

private:
    struct Params {
        f32 internalHeadroom;
        f32 loudnessDrive;
        f32 densityTarget;
        f32 densityGainFloor;
        f32 densityAttack;
        f32 densityRelease;
        f32 saturationKneeStart;
        f32 saturationCeiling;
        f32 peakKneeStart;
        f32 peakCeiling;
        f32 peakAttack;
        f32 peakRelease;
        f32 finalCeiling;
        f32 finalLimit;
    };

    static constexpr Params kNaturalParams{
        0.88f,   // internalHeadroom
        1.30f,   // loudnessDrive
        0.62f,   // densityTarget
        0.82f,   // densityGainFloor
        0.010f,  // densityAttack
        0.00055f,// densityRelease
        0.78f,   // saturationKneeStart
        1.08f,   // saturationCeiling
        0.88f,   // peakKneeStart
        0.990f,  // peakCeiling
        0.35f,   // peakAttack
        0.0014f, // peakRelease
        0.988f,  // finalCeiling
        0.999f,  // finalLimit
    };

    static constexpr Params kLoudParams{
        0.84f,   // internalHeadroom
        1.48f,   // loudnessDrive
        0.58f,   // densityTarget
        0.74f,   // densityGainFloor
        0.015f,  // densityAttack
        0.00045f,// densityRelease
        0.72f,   // saturationKneeStart
        1.12f,   // saturationCeiling
        0.82f,   // peakKneeStart
        0.985f,  // peakCeiling
        0.45f,   // peakAttack
        0.0012f, // peakRelease
        0.985f,  // finalCeiling
        0.999f,  // finalLimit
    };

    static constexpr Params kWarmParams{
        0.86f,   // internalHeadroom
        1.38f,   // loudnessDrive
        0.60f,   // densityTarget
        0.78f,   // densityGainFloor
        0.012f,  // densityAttack
        0.00050f,// densityRelease
        0.74f,   // saturationKneeStart
        1.10f,   // saturationCeiling
        0.85f,   // peakKneeStart
        0.988f,  // peakCeiling
        0.40f,   // peakAttack
        0.0013f, // peakRelease
        0.986f,  // finalCeiling
        0.999f,  // finalLimit
    };

    static constexpr const Params& ParamsForMode(Mode mode) {
        switch (mode) {
        case Mode::EnhancedNatural:
            return kNaturalParams;
        case Mode::EnhancedWarm:
            return kWarmParams;
        case Mode::EnhancedLoud:
        case Mode::Standard:
        default:
            return kLoudParams;
        }
    }

    Params ScaleParamsForSampleRate(const Params& params) const {
        Params scaled = params;
        scaled.densityAttack = ScaleCoefficientAt44100(params.densityAttack);
        scaled.densityRelease = ScaleCoefficientAt44100(params.densityRelease);
        scaled.peakAttack = ScaleCoefficientAt44100(params.peakAttack);
        scaled.peakRelease = ScaleCoefficientAt44100(params.peakRelease);
        return scaled;
    }

    f32 ScaleCoefficientAt44100(f32 coefficient) const {
        return 1.0f - std::pow(1.0f - coefficient, coefficientScaleExponent_);
    }

    void RefreshActiveParams() {
        activeParams_ = ScaleParamsForSampleRate(ParamsForMode(mode_));
    }

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

    void ProcessEnhanced(f32& sampleL, f32& sampleR, const f32 inputGain, const Params& params) {
        sampleL *= inputGain * params.internalHeadroom * params.loudnessDrive;
        sampleR *= inputGain * params.internalHeadroom * params.loudnessDrive;
        OutputLimiter::Sanitize(sampleL, sampleR);

        const f32 energy = 0.5f * (sampleL * sampleL + sampleR * sampleR);
        const f32 densityCoeff = (energy > densityEnergy_) ? params.densityAttack : params.densityRelease;
        densityEnergy_ += (energy - densityEnergy_) * densityCoeff;
        const f32 density = std::sqrt(std::max(0.0f, densityEnergy_));
        f32 targetDensityGain = 1.0f;
        if (density > params.densityTarget) {
            targetDensityGain = std::clamp(params.densityTarget / density, params.densityGainFloor, 1.0f);
        }
        const f32 gainCoeff = (targetDensityGain < densityGain_) ? params.densityAttack : params.densityRelease;
        densityGain_ += (targetDensityGain - densityGain_) * gainCoeff;
        sampleL *= densityGain_;
        sampleR *= densityGain_;

        ApplyLinkedCurve(sampleL, sampleR, params.saturationKneeStart, params.saturationCeiling);

        const f32 peak = std::max(std::abs(sampleL), std::abs(sampleR));
        if (peak > params.peakKneeStart) {
            const f32 shapedPeak = ShapePeak(peak, params.peakKneeStart, params.peakCeiling);
            const f32 targetGain = shapedPeak / peak;
            const f32 coeff = (targetGain < smoothGain_) ? params.peakAttack : params.peakRelease;
            smoothGain_ += (targetGain - smoothGain_) * coeff;
        } else {
            smoothGain_ += (1.0f - smoothGain_) * params.peakRelease;
        }

        sampleL *= smoothGain_;
        sampleR *= smoothGain_;
        OutputLimiter::ApplySoftLimit(sampleL, sampleR, params.finalCeiling, params.finalLimit);
    }

    void UpdateMeter(f32 inputPeak, f32 sampleL, f32 sampleR) {
        meter_.mode = mode_;
        meter_.inputPeak = std::max(meter_.inputPeak, inputPeak);
        meter_.outputPeak = std::max(meter_.outputPeak, std::max(std::abs(sampleL), std::abs(sampleR)));
        meter_.densityGain = densityGain_;
        meter_.peakGain = smoothGain_;
        ++meter_.processedFrames;
    }

    Mode mode_ = Mode::Standard;
    OutputLimiter limiter_;
    f32 coefficientScaleExponent_ = 1.0f;
    Params activeParams_ = kLoudParams;
    f32 smoothGain_ = 1.0f;
    f32 densityEnergy_ = 0.0f;
    f32 densityGain_ = 1.0f;
    Meter meter_;
};

} // namespace XArkMidi
