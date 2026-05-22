/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#pragma once
#include "../common/Types.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace XArkMidi {

class PostMixEffects {
public:
    struct Output {
        f32 wetL = 0.0f;
        f32 wetR = 0.0f;
    };

    void Init(u32 sampleRate) {
        sampleRate_ = sampleRate;
        const f32 effectiveSampleRate = static_cast<f32>(std::max<u32>(1, sampleRate_));
        chorusBasePhaseStep_ = (kTwoPi * kChorusRateHz) / effectiveSampleRate;
        gsParameterSmoothing_ = 1.0f - std::pow(1.0f - kGsParameterSmoothingAt44100,
                                                44100.0f / effectiveSampleRate);
        const size_t reverbSize = DelaySamples(97.0f);
        reverbDelayL_.assign(reverbSize, 0.0f);
        reverbDelayR_.assign(reverbSize, 0.0f);
        reverbTap1_ = DelaySamples(29.7f) % reverbSize;
        reverbTap2_ = DelaySamples(37.1f) % reverbSize;
        reverbTap3_ = DelaySamples(41.1f) % reverbSize;
        reverbTap4_ = DelaySamples(43.7f) % reverbSize;
        reverbPreDelayL_.assign(DelaySamples(12.0f), 0.0f);
        reverbPreDelayR_.assign(DelaySamples(12.7f), 0.0f);
        reverbDiffusionL1_.assign(DelaySamples(5.1f), 0.0f);
        reverbDiffusionR1_.assign(DelaySamples(6.3f), 0.0f);
        reverbDiffusionL2_.assign(DelaySamples(11.7f), 0.0f);
        reverbDiffusionR2_.assign(DelaySamples(13.1f), 0.0f);
        earlyReflectionL_.assign(DelaySamples(24.0f), 0.0f);
        earlyReflectionR_.assign(DelaySamples(24.0f), 0.0f);
        earlyTap1_ = DelaySamples(7.3f) % earlyReflectionL_.size();
        earlyTap2_ = DelaySamples(13.7f) % earlyReflectionL_.size();
        earlyTap3_ = DelaySamples(19.1f) % earlyReflectionL_.size();

        const size_t chorusSize = DelaySamples(32.0f);
        chorusDelayL_.assign(chorusSize, 0.0f);
        chorusDelayR_.assign(chorusSize, 0.0f);
        chorusBaseTapL_ = DelaySamples(2.0f);
        chorusBaseTapR_ = DelaySamples(3.0f);
        chorusDepthTapL_ = DelaySamples(0.7f);
        chorusDepthTapR_ = DelaySamples(0.8f);
        chorusSecondaryBaseTapL_ = DelaySamples(6.8f);
        chorusSecondaryBaseTapR_ = DelaySamples(7.6f);
        chorusSecondaryDepthTapL_ = DelaySamples(1.1f);
        chorusSecondaryDepthTapR_ = DelaySamples(1.0f);
        ResetState();
    }

    void ResetState() {
        ResetGsState();
        ResetAudioState();
    }

    void ResetAudioState() {
        std::fill(reverbDelayL_.begin(), reverbDelayL_.end(), 0.0f);
        std::fill(reverbDelayR_.begin(), reverbDelayR_.end(), 0.0f);
        std::fill(reverbPreDelayL_.begin(), reverbPreDelayL_.end(), 0.0f);
        std::fill(reverbPreDelayR_.begin(), reverbPreDelayR_.end(), 0.0f);
        std::fill(reverbDiffusionL1_.begin(), reverbDiffusionL1_.end(), 0.0f);
        std::fill(reverbDiffusionR1_.begin(), reverbDiffusionR1_.end(), 0.0f);
        std::fill(reverbDiffusionL2_.begin(), reverbDiffusionL2_.end(), 0.0f);
        std::fill(reverbDiffusionR2_.begin(), reverbDiffusionR2_.end(), 0.0f);
        std::fill(earlyReflectionL_.begin(), earlyReflectionL_.end(), 0.0f);
        std::fill(earlyReflectionR_.begin(), earlyReflectionR_.end(), 0.0f);
        std::fill(chorusDelayL_.begin(), chorusDelayL_.end(), 0.0f);
        std::fill(chorusDelayR_.begin(), chorusDelayR_.end(), 0.0f);
        reverbIndex_ = 0;
        earlyReflectionIndex_ = 0;
        reverbPreDelayIndexL_ = 0;
        reverbPreDelayIndexR_ = 0;
        reverbDiffusionIndexL1_ = 0;
        reverbDiffusionIndexR1_ = 0;
        reverbDiffusionIndexL2_ = 0;
        reverbDiffusionIndexR2_ = 0;
        reverbDampL_ = 0.0f;
        reverbDampR_ = 0.0f;
        reverbToneL_ = 0.0f;
        reverbToneR_ = 0.0f;
        reverbLowL_ = 0.0f;
        reverbLowR_ = 0.0f;
        gsReverbWetCurrent_ = gsReverbWetScale_;
        gsReverbFeedbackCurrent_ = gsReverbFeedbackScale_;
        gsMasterReverbSendCurrent_ = gsMasterReverbSendScale_;
        gsChorusWetCurrent_ = gsChorusWetScale_;
        gsChorusFeedbackCurrent_ = gsChorusFeedbackScale_;
        gsChorusToReverbCurrent_ = gsChorusToReverbScale_;
        gsChorusDelayCurrent_ = gsChorusDelayScale_;
        gsChorusDepthCurrent_ = gsChorusDepthScale_;
        gsChorusRateCurrent_ = gsChorusRateScale_;
        chorusIndex_ = 0;
        chorusSin_ = 0.0f;
        chorusCos_ = 1.0f;
        chorusDampL_ = 0.0f;
        chorusDampR_ = 0.0f;
        chorusToneL_ = 0.0f;
        chorusToneR_ = 0.0f;
    }

    void ResetGsState() {
        gsReverbWetScale_ = 1.0f;
        gsReverbFeedbackScale_ = 1.0f;
        gsMasterReverbSendScale_ = 1.0f;
        gsChorusWetScale_ = 1.0f;
        gsChorusFeedbackScale_ = 1.0f;
        gsChorusToReverbScale_ = 1.0f;
        gsChorusDelayScale_ = 1.0f;
        gsChorusDepthScale_ = 1.0f;
        gsChorusRateScale_ = 1.0f;
    }

    bool ApplyGsParameter(u8 param, u8 value) {
        const f32 t = NormalizeGs7Bit(value);
        switch (param) {
        case 0x05:
            gsReverbWetScale_ = Lerp(0.75f, 1.45f, t);
            gsReverbFeedbackScale_ = Lerp(0.80f, 1.35f, t);
            return true;
        case 0x08:
            gsReverbWetScale_ = Lerp(0.20f, 1.85f, t);
            return true;
        case 0x09:
            gsReverbFeedbackScale_ = Lerp(0.70f, 1.70f, t);
            return true;
        case 0x0A:
            gsMasterReverbSendScale_ = Lerp(0.70f, 1.35f, t);
            return true;
        case 0x0C:
            gsMasterReverbSendScale_ = Lerp(0.85f, 1.20f, t);
            return true;
        case 0x0D:
            gsChorusWetScale_ = Lerp(0.80f, 1.50f, t);
            gsChorusFeedbackScale_ = Lerp(0.80f, 1.30f, t);
            gsChorusDepthScale_ = Lerp(0.85f, 1.30f, t);
            return true;
        case 0x0F:
            gsChorusWetScale_ = Lerp(0.20f, 1.80f, t);
            return true;
        case 0x10:
            gsChorusFeedbackScale_ = Lerp(0.60f, 1.80f, t);
            return true;
        case 0x11:
            gsChorusDelayScale_ = Lerp(0.65f, 1.60f, t);
            return true;
        case 0x12:
            gsChorusRateScale_ = Lerp(0.60f, 1.80f, t);
            return true;
        case 0x13:
            gsChorusDepthScale_ = Lerp(0.55f, 1.85f, t);
            return true;
        case 0x14:
            gsChorusToReverbScale_ = Lerp(0.00f, 2.00f, t);
            return true;
        default:
            return false;
        }
    }

    Output ProcessSample(f32 dryL, f32 dryR, f32 reverbSendL, f32 reverbSendR,
                         f32 chorusSendL, f32 chorusSendR) {
        Output output{};
        const f32 masterReverbSendScale =
            SmoothScale(gsMasterReverbSendCurrent_, gsMasterReverbSendScale_);
        f32 reverbInL = ShapeEffectInput(reverbSendL + dryL * (kMasterReverbSend * masterReverbSendScale));
        f32 reverbInR = ShapeEffectInput(reverbSendR + dryR * (kMasterReverbSend * masterReverbSendScale));

        if (!chorusDelayL_.empty()) {
            const auto chorusWet = ProcessChorus(ShapeEffectInput(chorusSendL), ShapeEffectInput(chorusSendR));
            const f32 chorusWetScale = SmoothScale(gsChorusWetCurrent_, gsChorusWetScale_);
            output.wetL += chorusWet.wetL * (kChorusWetMix * chorusWetScale);
            output.wetR += chorusWet.wetR * (kChorusWetMix * chorusWetScale);
            const f32 chorusToReverbScale =
                SmoothScale(gsChorusToReverbCurrent_, gsChorusToReverbScale_);
            reverbInL += chorusWet.wetL * (kChorusToReverb * chorusToReverbScale);
            reverbInR += chorusWet.wetR * (kChorusToReverb * chorusToReverbScale);
        }

        if (!reverbDelayL_.empty()) {
            const auto reverbWet = ProcessReverb(ShapeEffectInput(reverbInL), ShapeEffectInput(reverbInR));
            const f32 reverbWetScale = SmoothScale(gsReverbWetCurrent_, gsReverbWetScale_);
            output.wetL += reverbWet.wetL * (kReverbWetMix * reverbWetScale);
            output.wetR += reverbWet.wetR * (kReverbWetMix * reverbWetScale);
        }
        const auto widenedWet = ApplyWetReturnWidth(output.wetL, output.wetR);
        output.wetL = ShapeWetReturn(widenedWet.wetL);
        output.wetR = ShapeWetReturn(widenedWet.wetR);
        return output;
    }

    bool HasAudibleTail(f32 threshold) const {
        const auto hasAudibleSample = [threshold](const std::vector<f32>& buffer) {
            for (f32 sample : buffer) {
                if (std::fabs(sample) >= threshold) {
                    return true;
                }
            }
            return false;
        };

        return hasAudibleSample(reverbDelayL_) || hasAudibleSample(reverbDelayR_) ||
               hasAudibleSample(reverbPreDelayL_) || hasAudibleSample(reverbPreDelayR_) ||
               hasAudibleSample(reverbDiffusionL1_) || hasAudibleSample(reverbDiffusionR1_) ||
               hasAudibleSample(reverbDiffusionL2_) || hasAudibleSample(reverbDiffusionR2_) ||
               hasAudibleSample(earlyReflectionL_) || hasAudibleSample(earlyReflectionR_) ||
               hasAudibleSample(chorusDelayL_) || hasAudibleSample(chorusDelayR_) ||
               HasAudibleScalar(reverbDampL_, threshold) || HasAudibleScalar(reverbDampR_, threshold) ||
               HasAudibleScalar(reverbToneL_, threshold) || HasAudibleScalar(reverbToneR_, threshold) ||
               HasAudibleScalar(reverbLowL_, threshold) || HasAudibleScalar(reverbLowR_, threshold) ||
               HasAudibleScalar(chorusDampL_, threshold) || HasAudibleScalar(chorusDampR_, threshold) ||
               HasAudibleScalar(chorusToneL_, threshold) || HasAudibleScalar(chorusToneR_, threshold) ||
               HasPendingScale(gsReverbWetCurrent_, gsReverbWetScale_, threshold) ||
               HasPendingScale(gsReverbFeedbackCurrent_, gsReverbFeedbackScale_, threshold) ||
               HasPendingScale(gsMasterReverbSendCurrent_, gsMasterReverbSendScale_, threshold) ||
               HasPendingScale(gsChorusWetCurrent_, gsChorusWetScale_, threshold) ||
               HasPendingScale(gsChorusFeedbackCurrent_, gsChorusFeedbackScale_, threshold) ||
               HasPendingScale(gsChorusToReverbCurrent_, gsChorusToReverbScale_, threshold) ||
               HasPendingScale(gsChorusDelayCurrent_, gsChorusDelayScale_, threshold) ||
               HasPendingScale(gsChorusDepthCurrent_, gsChorusDepthScale_, threshold) ||
               HasPendingScale(gsChorusRateCurrent_, gsChorusRateScale_, threshold);
    }

private:
    static constexpr f32 kChorusFeedback = 0.22f;
    static constexpr f32 kChorusWetMix = 0.45f;
    static constexpr f32 kChorusToReverb = 0.30f;
    static constexpr f32 kChorusDamping = 0.52f;
    static constexpr f32 kChorusSecondaryMix = 0.34f;
    static constexpr f32 kChorusToneDamping = 0.76f;
    static constexpr f32 kMaxChorusFeedback = 0.36f;
    static constexpr f32 kReverbFeedback = 0.58f;
    static constexpr f32 kMaxReverbFeedback = 0.82f;
    static constexpr f32 kReverbWetMix = 0.95f;
    static constexpr f32 kReverbDamping = 0.38f;
    static constexpr f32 kReverbToneDamping = 0.70f;
    static constexpr f32 kReverbLowDamping = 0.035f;
    static constexpr f32 kReverbLowTrim = 0.18f;
    static constexpr f32 kEarlyReflectionMix = 0.16f;
    static constexpr f32 kWetReturnWidth = 1.14f;
    static constexpr f32 kWetReturnMaxSideRatio = 1.25f;
    static constexpr f32 kWetReturnShape = 0.18f;
    static constexpr f32 kGsParameterSmoothingAt44100 = 0.0025f;
    static constexpr f32 kEffectInputShape = 0.10f;
    static constexpr f32 kDenormalGuard = 1.0e-20f;
    static constexpr f32 kMasterReverbSend = 0.28f;
    static constexpr f32 kTwoPi = 6.28318530717958647692f;
    static constexpr f32 kChorusRateHz = 2.60f;

    struct WetPair {
        f32 wetL = 0.0f;
        f32 wetR = 0.0f;
    };

    static f32 NormalizeGs7Bit(u8 value) {
        return static_cast<f32>(value) / 127.0f;
    }

    static f32 Lerp(f32 a, f32 b, f32 t) {
        return a + (b - a) * t;
    }

    static f32 Clamp(f32 value, f32 low, f32 high) {
        return std::max(low, std::min(value, high));
    }

    static bool HasAudibleScalar(f32 value, f32 threshold) {
        return std::fabs(value) >= threshold;
    }

    static bool HasPendingScale(f32 current, f32 target, f32 threshold) {
        return std::fabs(target - current) >= threshold;
    }

    static f32 FlushTiny(f32 value) {
        return (std::fabs(value) < kDenormalGuard) ? 0.0f : value;
    }

    f32 SmoothScale(f32& current, f32 target) const {
        current += (target - current) * gsParameterSmoothing_;
        return current;
    }

    static f32 ShapeWetReturn(f32 sample) {
        return FlushTiny(sample / (1.0f + std::fabs(sample) * kWetReturnShape));
    }

    static f32 ShapeEffectInput(f32 sample) {
        return FlushTiny(sample / (1.0f + std::fabs(sample) * kEffectInputShape));
    }

    static WetPair ApplyWetReturnWidth(f32 wetL, f32 wetR) {
        const f32 mid = (wetL + wetR) * 0.5f;
        const f32 sideLimit = std::max(std::fabs(mid), 1.0e-6f) * kWetReturnMaxSideRatio;
        const f32 side = Clamp((wetL - wetR) * (0.5f * kWetReturnWidth), -sideLimit, sideLimit);
        return { mid + side, mid - side };
    }

    size_t DelaySamples(f32 ms) const {
        return std::max<size_t>(1, static_cast<size_t>(sampleRate_ * (ms / 1000.0f)));
    }

    static f32 ReadDelayInterpolated(const std::vector<f32>& delay, size_t index, f32 tap) {
        const size_t size = delay.size();
        const f32 fTap = std::max(1.0f, tap);
        const size_t iTap = static_cast<size_t>(fTap);
        const f32 frac = fTap - static_cast<f32>(iTap);
        const size_t w0 = iTap % size;
        const size_t w1 = (iTap + 1) % size;
        const size_t idx0 = (index >= w0) ? (index - w0) : (index + size - w0);
        const size_t idx1 = (index >= w1) ? (index - w1) : (index + size - w1);
        return delay[idx0] * (1.0f - frac) + delay[idx1] * frac;
    }

    WetPair ProcessChorus(f32 chorusInL, f32 chorusInR) {
        const size_t size = chorusDelayL_.size();
        const f32 delayScale = SmoothScale(gsChorusDelayCurrent_, gsChorusDelayScale_);
        const f32 depthScale = SmoothScale(gsChorusDepthCurrent_, gsChorusDepthScale_);
        const f32 rateScale = SmoothScale(gsChorusRateCurrent_, gsChorusRateScale_);
        const f32 baseTapL = static_cast<f32>(chorusBaseTapL_) * delayScale;
        const f32 baseTapR = static_cast<f32>(chorusBaseTapR_) * delayScale;
        const f32 depthTapL = static_cast<f32>(chorusDepthTapL_) * depthScale;
        const f32 depthTapR = static_cast<f32>(chorusDepthTapR_) * depthScale;
        const f32 fTapL = std::max(1.0f, baseTapL + (chorusSin_ + 1.0f) * 0.5f * depthTapL);
        const f32 fTapR = std::max(1.0f, baseTapR + (chorusCos_ + 1.0f) * 0.5f * depthTapR);
        const f32 secondaryTapL =
            static_cast<f32>(chorusSecondaryBaseTapL_) * delayScale +
            (chorusCos_ + 1.0f) * 0.5f * static_cast<f32>(chorusSecondaryDepthTapL_) * depthScale;
        const f32 secondaryTapR =
            static_cast<f32>(chorusSecondaryBaseTapR_) * delayScale +
            (1.0f - chorusSin_) * 0.5f * static_cast<f32>(chorusSecondaryDepthTapR_) * depthScale;
        const f32 chorusWetL = ReadDelayInterpolated(chorusDelayL_, chorusIndex_, fTapL);
        const f32 chorusWetR = ReadDelayInterpolated(chorusDelayR_, chorusIndex_, fTapR);
        const f32 chorusSecondaryWetL = ReadDelayInterpolated(chorusDelayL_, chorusIndex_, secondaryTapL);
        const f32 chorusSecondaryWetR = ReadDelayInterpolated(chorusDelayR_, chorusIndex_, secondaryTapR);
        chorusDampL_ = FlushTiny(chorusDampL_ + (chorusWetL - chorusDampL_) * kChorusDamping);
        chorusDampR_ = FlushTiny(chorusDampR_ + (chorusWetR - chorusDampR_) * kChorusDamping);
        const f32 feedbackScale = SmoothScale(gsChorusFeedbackCurrent_, gsChorusFeedbackScale_);
        const f32 feedback = Clamp(kChorusFeedback * feedbackScale, 0.0f, kMaxChorusFeedback);
        chorusDelayL_[chorusIndex_] = FlushTiny(chorusInL + chorusDampR_ * feedback);
        chorusDelayR_[chorusIndex_] = FlushTiny(chorusInR + chorusDampL_ * feedback);
        ++chorusIndex_;
        if (chorusIndex_ == size) {
            chorusIndex_ = 0;
        }

        const f32 phaseStepAngle = chorusBasePhaseStep_ * rateScale;
        const f32 phaseStepSin = std::sin(phaseStepAngle);
        const f32 phaseStepCos = std::cos(phaseStepAngle);
        const f32 nextSin = chorusSin_ * phaseStepCos + chorusCos_ * phaseStepSin;
        const f32 nextCos = chorusCos_ * phaseStepCos - chorusSin_ * phaseStepSin;
        chorusSin_ = nextSin;
        chorusCos_ = nextCos;
        const f32 mixedWetL = chorusWetL + chorusSecondaryWetL * kChorusSecondaryMix;
        const f32 mixedWetR = chorusWetR + chorusSecondaryWetR * kChorusSecondaryMix;
        chorusToneL_ = FlushTiny(chorusToneL_ + (mixedWetL - chorusToneL_) * kChorusToneDamping);
        chorusToneR_ = FlushTiny(chorusToneR_ + (mixedWetR - chorusToneR_) * kChorusToneDamping);
        return {
            chorusToneL_,
            chorusToneR_,
        };
    }

    WetPair ProcessReverb(f32 reverbInL, f32 reverbInR) {
        const size_t size = reverbDelayL_.size();
        reverbInL = ProcessDelay(reverbPreDelayL_, reverbPreDelayIndexL_, reverbInL);
        reverbInR = ProcessDelay(reverbPreDelayR_, reverbPreDelayIndexR_, reverbInR);
        reverbInL = ProcessAllpass(reverbDiffusionL1_, reverbDiffusionIndexL1_, reverbInL, 0.62f);
        reverbInR = ProcessAllpass(reverbDiffusionR1_, reverbDiffusionIndexR1_, reverbInR, 0.60f);
        reverbInL = ProcessAllpass(reverbDiffusionL2_, reverbDiffusionIndexL2_, reverbInL, 0.50f);
        reverbInR = ProcessAllpass(reverbDiffusionR2_, reverbDiffusionIndexR2_, reverbInR, 0.48f);
        const WetPair early = ProcessEarlyReflections(reverbInL, reverbInR);
        const f32 reverbWetL =
            reverbDelayL_[(reverbIndex_ >= reverbTap1_) ? (reverbIndex_ - reverbTap1_) : (reverbIndex_ + size - reverbTap1_)] * 0.30f +
            reverbDelayL_[(reverbIndex_ >= reverbTap2_) ? (reverbIndex_ - reverbTap2_) : (reverbIndex_ + size - reverbTap2_)] * 0.24f +
            reverbDelayR_[(reverbIndex_ >= reverbTap3_) ? (reverbIndex_ - reverbTap3_) : (reverbIndex_ + size - reverbTap3_)] * 0.18f +
            reverbDelayR_[(reverbIndex_ >= reverbTap4_) ? (reverbIndex_ - reverbTap4_) : (reverbIndex_ + size - reverbTap4_)] * 0.12f;
        const f32 reverbWetR =
            reverbDelayR_[(reverbIndex_ >= reverbTap1_) ? (reverbIndex_ - reverbTap1_) : (reverbIndex_ + size - reverbTap1_)] * 0.30f +
            reverbDelayR_[(reverbIndex_ >= reverbTap2_) ? (reverbIndex_ - reverbTap2_) : (reverbIndex_ + size - reverbTap2_)] * 0.24f +
            reverbDelayL_[(reverbIndex_ >= reverbTap3_) ? (reverbIndex_ - reverbTap3_) : (reverbIndex_ + size - reverbTap3_)] * 0.18f +
            reverbDelayL_[(reverbIndex_ >= reverbTap4_) ? (reverbIndex_ - reverbTap4_) : (reverbIndex_ + size - reverbTap4_)] * 0.12f;
        reverbDampL_ = FlushTiny(reverbDampL_ + (reverbWetL - reverbDampL_) * kReverbDamping);
        reverbDampR_ = FlushTiny(reverbDampR_ + (reverbWetR - reverbDampR_) * kReverbDamping);
        const f32 feedbackScale = SmoothScale(gsReverbFeedbackCurrent_, gsReverbFeedbackScale_);
        const f32 feedback = Clamp(kReverbFeedback * feedbackScale, 0.0f, kMaxReverbFeedback);
        reverbDelayL_[reverbIndex_] = FlushTiny(reverbInL + reverbDampR_ * feedback);
        reverbDelayR_[reverbIndex_] = FlushTiny(reverbInR + reverbDampL_ * feedback);
        ++reverbIndex_;
        if (reverbIndex_ == size) {
            reverbIndex_ = 0;
        }
        reverbLowL_ = FlushTiny(reverbLowL_ + (reverbWetL - reverbLowL_) * kReverbLowDamping);
        reverbLowR_ = FlushTiny(reverbLowR_ + (reverbWetR - reverbLowR_) * kReverbLowDamping);
        const f32 trimmedWetL = reverbWetL - reverbLowL_ * kReverbLowTrim;
        const f32 trimmedWetR = reverbWetR - reverbLowR_ * kReverbLowTrim;
        reverbToneL_ = FlushTiny(reverbToneL_ + (trimmedWetL - reverbToneL_) * kReverbToneDamping);
        reverbToneR_ = FlushTiny(reverbToneR_ + (trimmedWetR - reverbToneR_) * kReverbToneDamping);
        return {
            reverbToneL_ + early.wetL * kEarlyReflectionMix,
            reverbToneR_ + early.wetR * kEarlyReflectionMix,
        };
    }

    WetPair ProcessEarlyReflections(f32 inputL, f32 inputR) {
        if (earlyReflectionL_.empty()) {
            return {};
        }
        const size_t size = earlyReflectionL_.size();
        const auto readTap = [size](const std::vector<f32>& buffer, size_t index, size_t tap) {
            return buffer[(index >= tap) ? (index - tap) : (index + size - tap)];
        };
        const f32 wetL = FlushTiny(
            readTap(earlyReflectionL_, earlyReflectionIndex_, earlyTap1_) * 0.42f +
            readTap(earlyReflectionR_, earlyReflectionIndex_, earlyTap2_) * 0.30f +
            readTap(earlyReflectionL_, earlyReflectionIndex_, earlyTap3_) * 0.18f);
        const f32 wetR = FlushTiny(
            readTap(earlyReflectionR_, earlyReflectionIndex_, earlyTap1_) * 0.42f +
            readTap(earlyReflectionL_, earlyReflectionIndex_, earlyTap2_) * 0.30f +
            readTap(earlyReflectionR_, earlyReflectionIndex_, earlyTap3_) * 0.18f);
        earlyReflectionL_[earlyReflectionIndex_] = FlushTiny(inputL);
        earlyReflectionR_[earlyReflectionIndex_] = FlushTiny(inputR);
        ++earlyReflectionIndex_;
        if (earlyReflectionIndex_ == size) {
            earlyReflectionIndex_ = 0;
        }
        return { wetL, wetR };
    }

    static f32 ProcessDelay(std::vector<f32>& delay, size_t& index, f32 input) {
        if (delay.empty()) {
            return input;
        }
        const f32 output = FlushTiny(delay[index]);
        delay[index] = FlushTiny(input);
        ++index;
        if (index == delay.size()) {
            index = 0;
        }
        return output;
    }

    static f32 ProcessAllpass(std::vector<f32>& delay, size_t& index, f32 input, f32 feedback) {
        if (delay.empty()) {
            return input;
        }
        const f32 delayed = delay[index];
        const f32 output = FlushTiny(delayed - input);
        delay[index] = FlushTiny(input + delayed * feedback);
        ++index;
        if (index == delay.size()) {
            index = 0;
        }
        return output;
    }

    u32 sampleRate_ = 44100;
    f32 gsParameterSmoothing_ = kGsParameterSmoothingAt44100;
    std::vector<f32> reverbDelayL_;
    std::vector<f32> reverbDelayR_;
    std::vector<f32> reverbPreDelayL_;
    std::vector<f32> reverbPreDelayR_;
    std::vector<f32> reverbDiffusionL1_;
    std::vector<f32> reverbDiffusionR1_;
    std::vector<f32> reverbDiffusionL2_;
    std::vector<f32> reverbDiffusionR2_;
    std::vector<f32> earlyReflectionL_;
    std::vector<f32> earlyReflectionR_;
    size_t reverbIndex_ = 0;
    size_t earlyReflectionIndex_ = 0;
    size_t reverbPreDelayIndexL_ = 0;
    size_t reverbPreDelayIndexR_ = 0;
    size_t earlyTap1_ = 0;
    size_t earlyTap2_ = 0;
    size_t earlyTap3_ = 0;
    size_t reverbDiffusionIndexL1_ = 0;
    size_t reverbDiffusionIndexR1_ = 0;
    size_t reverbDiffusionIndexL2_ = 0;
    size_t reverbDiffusionIndexR2_ = 0;
    f32 reverbDampL_ = 0.0f;
    f32 reverbDampR_ = 0.0f;
    f32 reverbToneL_ = 0.0f;
    f32 reverbToneR_ = 0.0f;
    f32 reverbLowL_ = 0.0f;
    f32 reverbLowR_ = 0.0f;
    size_t reverbTap1_ = 0;
    size_t reverbTap2_ = 0;
    size_t reverbTap3_ = 0;
    size_t reverbTap4_ = 0;
    std::vector<f32> chorusDelayL_;
    std::vector<f32> chorusDelayR_;
    size_t chorusIndex_ = 0;
    size_t chorusBaseTapL_ = 0;
    size_t chorusBaseTapR_ = 0;
    size_t chorusDepthTapL_ = 0;
    size_t chorusDepthTapR_ = 0;
    size_t chorusSecondaryBaseTapL_ = 0;
    size_t chorusSecondaryBaseTapR_ = 0;
    size_t chorusSecondaryDepthTapL_ = 0;
    size_t chorusSecondaryDepthTapR_ = 0;
    f32 chorusSin_ = 0.0f;
    f32 chorusCos_ = 1.0f;
    f32 chorusBasePhaseStep_ = (kTwoPi * kChorusRateHz) / 44100.0f;
    f32 chorusDampL_ = 0.0f;
    f32 chorusDampR_ = 0.0f;
    f32 chorusToneL_ = 0.0f;
    f32 chorusToneR_ = 0.0f;
    f32 gsReverbWetScale_ = 1.0f;
    f32 gsReverbWetCurrent_ = 1.0f;
    f32 gsReverbFeedbackScale_ = 1.0f;
    f32 gsReverbFeedbackCurrent_ = 1.0f;
    f32 gsMasterReverbSendScale_ = 1.0f;
    f32 gsMasterReverbSendCurrent_ = 1.0f;
    f32 gsChorusWetScale_ = 1.0f;
    f32 gsChorusWetCurrent_ = 1.0f;
    f32 gsChorusFeedbackScale_ = 1.0f;
    f32 gsChorusFeedbackCurrent_ = 1.0f;
    f32 gsChorusToReverbScale_ = 1.0f;
    f32 gsChorusToReverbCurrent_ = 1.0f;
    f32 gsChorusDelayScale_ = 1.0f;
    f32 gsChorusDelayCurrent_ = 1.0f;
    f32 gsChorusDepthScale_ = 1.0f;
    f32 gsChorusDepthCurrent_ = 1.0f;
    f32 gsChorusRateScale_ = 1.0f;
    f32 gsChorusRateCurrent_ = 1.0f;
};

} // namespace XArkMidi
