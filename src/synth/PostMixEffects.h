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
        ResetState();
    }

    void ResetState() {
        ResetGsState();
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
        chorusIndex_ = 0;
        chorusSin_ = 0.0f;
        chorusCos_ = 1.0f;
        chorusDampL_ = 0.0f;
        chorusDampR_ = 0.0f;
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
        f32 reverbInL = reverbSendL + dryL * (kMasterReverbSend * gsMasterReverbSendScale_);
        f32 reverbInR = reverbSendR + dryR * (kMasterReverbSend * gsMasterReverbSendScale_);

        if (!chorusDelayL_.empty()) {
            const auto chorusWet = ProcessChorus(chorusSendL, chorusSendR);
            output.wetL += chorusWet.wetL * (kChorusWetMix * gsChorusWetScale_);
            output.wetR += chorusWet.wetR * (kChorusWetMix * gsChorusWetScale_);
            reverbInL += chorusWet.wetL * (kChorusToReverb * gsChorusToReverbScale_);
            reverbInR += chorusWet.wetR * (kChorusToReverb * gsChorusToReverbScale_);
        }

        if (!reverbDelayL_.empty()) {
            const auto reverbWet = ProcessReverb(reverbInL, reverbInR);
            output.wetL += reverbWet.wetL * (kReverbWetMix * gsReverbWetScale_);
            output.wetR += reverbWet.wetR * (kReverbWetMix * gsReverbWetScale_);
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
               hasAudibleSample(chorusDelayL_) || hasAudibleSample(chorusDelayR_);
    }

private:
    static constexpr f32 kChorusFeedback = 0.22f;
    static constexpr f32 kChorusWetMix = 0.45f;
    static constexpr f32 kChorusToReverb = 0.30f;
    static constexpr f32 kChorusDamping = 0.52f;
    static constexpr f32 kReverbFeedback = 0.58f;
    static constexpr f32 kReverbWetMix = 0.95f;
    static constexpr f32 kReverbDamping = 0.38f;
    static constexpr f32 kReverbToneDamping = 0.70f;
    static constexpr f32 kEarlyReflectionMix = 0.16f;
    static constexpr f32 kWetReturnWidth = 1.14f;
    static constexpr f32 kWetReturnShape = 0.18f;
    static constexpr f32 kMasterReverbSend = 0.28f;
    static constexpr f32 kChorusPhaseStepSin = 0.000369999991558f;
    static constexpr f32 kChorusPhaseStepCos = 0.999999940395f;

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

    static f32 ShapeWetReturn(f32 sample) {
        return sample / (1.0f + std::fabs(sample) * kWetReturnShape);
    }

    static WetPair ApplyWetReturnWidth(f32 wetL, f32 wetR) {
        const f32 mid = (wetL + wetR) * 0.5f;
        const f32 side = (wetL - wetR) * (0.5f * kWetReturnWidth);
        return { mid + side, mid - side };
    }

    size_t DelaySamples(f32 ms) const {
        return std::max<size_t>(1, static_cast<size_t>(sampleRate_ * (ms / 1000.0f)));
    }

    WetPair ProcessChorus(f32 chorusInL, f32 chorusInR) {
        const size_t size = chorusDelayL_.size();
        const f32 baseTapL = static_cast<f32>(chorusBaseTapL_) * gsChorusDelayScale_;
        const f32 baseTapR = static_cast<f32>(chorusBaseTapR_) * gsChorusDelayScale_;
        const f32 depthTapL = static_cast<f32>(chorusDepthTapL_) * gsChorusDepthScale_;
        const f32 depthTapR = static_cast<f32>(chorusDepthTapR_) * gsChorusDepthScale_;
        const f32 fTapL = std::max(1.0f, baseTapL + (chorusSin_ + 1.0f) * 0.5f * depthTapL);
        const f32 fTapR = std::max(1.0f, baseTapR + (chorusCos_ + 1.0f) * 0.5f * depthTapR);
        const size_t iTapL = static_cast<size_t>(fTapL);
        const size_t iTapR = static_cast<size_t>(fTapR);
        const f32 fracL = fTapL - static_cast<f32>(iTapL);
        const f32 fracR = fTapR - static_cast<f32>(iTapR);
        const size_t wL0 = iTapL % size;
        const size_t wL1 = (iTapL + 1) % size;
        const size_t wR0 = iTapR % size;
        const size_t wR1 = (iTapR + 1) % size;
        const size_t idxL0 = (chorusIndex_ >= wL0) ? (chorusIndex_ - wL0) : (chorusIndex_ + size - wL0);
        const size_t idxL1 = (chorusIndex_ >= wL1) ? (chorusIndex_ - wL1) : (chorusIndex_ + size - wL1);
        const size_t idxR0 = (chorusIndex_ >= wR0) ? (chorusIndex_ - wR0) : (chorusIndex_ + size - wR0);
        const size_t idxR1 = (chorusIndex_ >= wR1) ? (chorusIndex_ - wR1) : (chorusIndex_ + size - wR1);
        const f32 chorusWetL = chorusDelayL_[idxL0] * (1.0f - fracL) + chorusDelayL_[idxL1] * fracL;
        const f32 chorusWetR = chorusDelayR_[idxR0] * (1.0f - fracR) + chorusDelayR_[idxR1] * fracR;
        chorusDampL_ += (chorusWetL - chorusDampL_) * kChorusDamping;
        chorusDampR_ += (chorusWetR - chorusDampR_) * kChorusDamping;
        chorusDelayL_[chorusIndex_] = chorusInL + chorusDampR_ * (kChorusFeedback * gsChorusFeedbackScale_);
        chorusDelayR_[chorusIndex_] = chorusInR + chorusDampL_ * (kChorusFeedback * gsChorusFeedbackScale_);
        ++chorusIndex_;
        if (chorusIndex_ == size) {
            chorusIndex_ = 0;
        }

        const f32 phaseStepAngle =
            std::atan2(kChorusPhaseStepSin, kChorusPhaseStepCos) * gsChorusRateScale_;
        const f32 phaseStepSin = std::sin(phaseStepAngle);
        const f32 phaseStepCos = std::cos(phaseStepAngle);
        const f32 nextSin = chorusSin_ * phaseStepCos + chorusCos_ * phaseStepSin;
        const f32 nextCos = chorusCos_ * phaseStepCos - chorusSin_ * phaseStepSin;
        chorusSin_ = nextSin;
        chorusCos_ = nextCos;
        return { chorusWetL, chorusWetR };
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
        reverbDampL_ += (reverbWetL - reverbDampL_) * kReverbDamping;
        reverbDampR_ += (reverbWetR - reverbDampR_) * kReverbDamping;
        reverbDelayL_[reverbIndex_] = reverbInL + reverbDampR_ * (kReverbFeedback * gsReverbFeedbackScale_);
        reverbDelayR_[reverbIndex_] = reverbInR + reverbDampL_ * (kReverbFeedback * gsReverbFeedbackScale_);
        ++reverbIndex_;
        if (reverbIndex_ == size) {
            reverbIndex_ = 0;
        }
        reverbToneL_ += (reverbWetL - reverbToneL_) * kReverbToneDamping;
        reverbToneR_ += (reverbWetR - reverbToneR_) * kReverbToneDamping;
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
        const f32 wetL =
            readTap(earlyReflectionL_, earlyReflectionIndex_, earlyTap1_) * 0.42f +
            readTap(earlyReflectionR_, earlyReflectionIndex_, earlyTap2_) * 0.30f +
            readTap(earlyReflectionL_, earlyReflectionIndex_, earlyTap3_) * 0.18f;
        const f32 wetR =
            readTap(earlyReflectionR_, earlyReflectionIndex_, earlyTap1_) * 0.42f +
            readTap(earlyReflectionL_, earlyReflectionIndex_, earlyTap2_) * 0.30f +
            readTap(earlyReflectionR_, earlyReflectionIndex_, earlyTap3_) * 0.18f;
        earlyReflectionL_[earlyReflectionIndex_] = inputL;
        earlyReflectionR_[earlyReflectionIndex_] = inputR;
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
        const f32 output = delay[index];
        delay[index] = input;
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
        const f32 output = delayed - input;
        delay[index] = input + delayed * feedback;
        ++index;
        if (index == delay.size()) {
            index = 0;
        }
        return output;
    }

    u32 sampleRate_ = 44100;
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
    f32 chorusSin_ = 0.0f;
    f32 chorusCos_ = 1.0f;
    f32 chorusDampL_ = 0.0f;
    f32 chorusDampR_ = 0.0f;
    f32 gsReverbWetScale_ = 1.0f;
    f32 gsReverbFeedbackScale_ = 1.0f;
    f32 gsMasterReverbSendScale_ = 1.0f;
    f32 gsChorusWetScale_ = 1.0f;
    f32 gsChorusFeedbackScale_ = 1.0f;
    f32 gsChorusToReverbScale_ = 1.0f;
    f32 gsChorusDelayScale_ = 1.0f;
    f32 gsChorusDepthScale_ = 1.0f;
    f32 gsChorusRateScale_ = 1.0f;
};

} // namespace XArkMidi
