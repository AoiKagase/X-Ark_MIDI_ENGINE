#include "DlsFile.h"
#include <cstring>
#include <algorithm>
#include <cmath>

namespace ArkMidi {

namespace {

constexpr u16 CONN_SRC_NONE_LOCAL             = 0x0000;
constexpr u16 CONN_SRC_KEYONVELOCITY_LOCAL    = 0x0002;
constexpr u16 CONN_SRC_KEYNUMBER_LOCAL        = 0x0003;
constexpr u16 CONN_DST_GAIN_LOCAL             = 0x0001;
constexpr u16 CONN_DST_ATTENUATION_LOCAL      = 0x0001;
constexpr u16 CONN_DST_PITCH_LOCAL            = 0x0003;
constexpr u16 CONN_DST_PAN_LOCAL              = 0x0004;
constexpr u16 CONN_DST_KEYNUMBER_LOCAL        = 0x0005;
constexpr u16 CONN_DST_EG1_ATTACKTIME_LOCAL   = 0x0206;
constexpr u16 CONN_DST_EG1_DECAYTIME_LOCAL    = 0x0207;
constexpr u16 CONN_DST_EG1_RELEASETIME_LOCAL  = 0x0209;
constexpr u16 CONN_DST_EG1_SUSTAINLEVEL_LOCAL = 0x020a;
constexpr u16 CONN_DST_EG1_DELAYTIME_LOCAL    = 0x020b;
constexpr u16 CONN_DST_EG1_HOLDTIME_LOCAL     = 0x020c;
constexpr u16 CONN_TRN_NONE_LOCAL             = 0x0000;

u32 MakeFourCC(const char* s) {
    return (static_cast<u32>(s[0]))
         | (static_cast<u32>(s[1]) << 8)
         | (static_cast<u32>(s[2]) << 16)
         | (static_cast<u32>(s[3]) << 24);
}

u32 ReadU32LEAt(const u8* p) {
    return static_cast<u32>(p[0])
         | (static_cast<u32>(p[1]) << 8)
         | (static_cast<u32>(p[2]) << 16)
         | (static_cast<u32>(p[3]) << 24);
}

u16 ReadU16LEAt(const u8* p) {
    return static_cast<u16>(p[0])
         | (static_cast<u16>(p[1]) << 8);
}

template <typename T>
T ClampValue(T value, T minValue, T maxValue) {
    return std::max(minValue, std::min(maxValue, value));
}

void AddGeneratorValue(std::vector<i32>& generators, int gen, i32 delta) {
    if (gen < 0 || gen >= GEN_COUNT) {
        return;
    }

    if (generators[gen] == 0x7fffffff) {
        generators[gen] = delta;
    } else {
        generators[gen] += delta;
    }
}

void AddPitchCents(std::vector<i32>& generators, i32 cents) {
    if (cents == 0) {
        return;
    }

    const i32 coarse = cents / 100;
    const i32 fine = cents - coarse * 100;
    AddGeneratorValue(generators, GEN_CoarseTune, coarse);
    AddGeneratorValue(generators, GEN_FineTune, fine);
}

void AddPitchCents(i32* generators, i32 cents) {
    if (cents == 0) {
        return;
    }

    generators[GEN_FineTune] += cents;
}

i32 DlsGainScaleToCentibels(i32 scale) {
    // DLS gain uses a signed fixed-point dB-like domain. Map conservatively to centibels.
    return static_cast<i32>(-(static_cast<f64>(scale) / 65536.0) * 10.0);
}

i32 DlsWsmpAttenuationToCentibels(u32 raw) {
    // wsmp attenuation is significantly coarser than SF2 initial attenuation in real banks.
    // Mapping it as full centibels tends to mute entire instruments, so keep it in 0.1dB units.
    return static_cast<i32>(raw / 655360u);
}

i32 DlsPanScaleToSf2Pan(i32 scale) {
    // Map fixed-point pan to the SF2 [-500, 500] domain.
    return ClampValue<i32>(static_cast<i32>(scale / 65536), -500, 500);
}

i32 DlsEnvelopeTimeToTimecents(i32 scale) {
    // DLS uses 16.16 timecents, with INT_MIN representing absolute zero time.
    if (scale == std::numeric_limits<i32>::min()) {
        return -32768;
    }
    return static_cast<i32>(scale / 65536);
}

void MergeArticulatorGenerator(i32* generators, int gen, i32 value) {
    if (gen < 0 || gen >= GEN_COUNT) {
        return;
    }

    switch (gen) {
    case GEN_OverridingRootKey:
    case GEN_Keynum:
    case GEN_Velocity:
        generators[gen] = value;
        break;
    default:
        generators[gen] += value;
        break;
    }
}

} // namespace

bool DlsFile::LoadFromMemory(const u8* data, size_t size) {
    sampleData_.clear();
    waves_.clear();
    instruments_.clear();
    poolTableOffsets_.clear();
    errorMsg_.clear();
    halveSampleRates_ = false;

    try {
        BinaryReader r(data, size);
        return ParseRiff(r);
    } catch (const std::exception& e) {
        errorMsg_ = e.what();
        return false;
    }
}

bool DlsFile::LoadFromFile(const std::wstring& path) {
    std::vector<u8> data;
    if (!ReadFileBytes(path, data, errorMsg_))
        return false;
    return LoadFromMemory(data.data(), data.size());
}

bool DlsFile::ParseRiff(BinaryReader& r) {
    u32 riffId = r.ReadU32LE();
    u32 riffSize = r.ReadU32LE();
    u32 riffType = r.ReadU32LE();

    if (riffId != MakeFourCC("RIFF")) {
        errorMsg_ = "Not a DLS file: missing RIFF header";
        return false;
    }
    if (riffType != MakeFourCC("DLS ")) {
        errorMsg_ = "Not a DLS file: RIFF type is not 'DLS '";
        return false;
    }

    size_t end = r.Tell() + (riffSize - 4);
    while (r.Tell() < end && !r.IsEof()) {
        u32 chunkId = r.ReadU32LE();
        u32 chunkSize = r.ReadU32LE();

        if (chunkId == MakeFourCC("ptbl")) {
            auto chunk = r.ReadSlice(chunkSize);
            if (!ParsePoolTable(chunk, chunkSize))
                return false;
        }
        else
        if (chunkId == MakeFourCC("LIST")) {
            u32 listType = r.ReadU32LE();
            auto listData = r.ReadSlice(chunkSize - 4);
            if (!ParseCollection(listData, listType))
                return false;
        } else {
            r.Skip(chunkSize);
        }
        if (chunkSize & 1) r.Skip(1);
    }

    if (instruments_.empty()) {
        errorMsg_ = "DLS contains no instruments";
        return false;
    }

    int rate44100 = 0;
    int rate22050 = 0;
    int totalWsmpWaves = 0;
    for (const auto& wave : waves_) {
        if (!wave.hasWsmp) continue;
        ++totalWsmpWaves;
        if (wave.sample.sampleRate == 44100) ++rate44100;
        if (wave.sample.sampleRate == 22050) ++rate22050;
    }
    // Some extracted DLS banks stamp 44.1kHz into every wsmp wave even though the
    // source content is authored at 22.05kHz, which shifts the entire bank up by one octave.
    // Only apply the correction when the bank is overwhelmingly in that malformed pattern.
    halveSampleRates_ = (totalWsmpWaves >= 16 && rate22050 == 0 && rate44100 * 10 >= totalWsmpWaves * 9);
    if (halveSampleRates_) {
        for (auto& wave : waves_) {
            if (wave.sample.sampleRate >= 22050) {
                wave.sample.sampleRate /= 2;
            }
        }
    }

    return true;
}

bool DlsFile::ParsePoolTable(BinaryReader& r, u32 /*chunkSize*/) {
    if (r.Remaining() < 8) {
        errorMsg_ = "Invalid DLS pool table";
        return false;
    }

    r.ReadU32LE(); // cbSize
    u32 cueCount = r.ReadU32LE();
    poolTableOffsets_.clear();
    poolTableOffsets_.reserve(cueCount);
    for (u32 i = 0; i < cueCount && r.Remaining() >= 4; ++i) {
        poolTableOffsets_.push_back(r.ReadU32LE());
    }
    return true;
}

bool DlsFile::ParseCollection(BinaryReader& r, u32 listType) {
    if (listType == MakeFourCC("wvpl")) {
        return ParseWavePool(r, static_cast<u32>(r.Size()));
    }
    if (listType == MakeFourCC("lins")) {
        return ParseInstruments(r, static_cast<u32>(r.Size()));
    }
    return true;
}

bool DlsFile::ParseWavePool(BinaryReader& r, u32 /*chunkSize*/) {
    while (!r.IsEof() && r.Remaining() >= 8) {
        u32 poolOffset = static_cast<u32>(r.Tell());
        u32 chunkId = r.ReadU32LE();
        u32 chunkSize = r.ReadU32LE();

        if (chunkId == MakeFourCC("LIST")) {
            u32 listType = r.ReadU32LE();
            auto listData = r.ReadSlice(chunkSize - 4);
            if (listType == MakeFourCC("wave")) {
                DlsWave wave;
                if (!ParseWaveList(listData, chunkSize - 4, wave))
                    return false;
                wave.poolOffset = poolOffset;
                waves_.push_back(wave);
            }
        } else {
            r.Skip(chunkSize);
        }
        if (chunkSize & 1) r.Skip(1);
    }
    return true;
}

bool DlsFile::ParseWaveList(BinaryReader& r, u32 /*chunkSize*/, DlsWave& outWave) {
    std::vector<i16> waveData;
    u16 formatTag = 0;
    u16 channels = 0;
    u16 bitsPerSample = 0;
    u32 sampleRate = 0;
    bool hasWsmp = false;
    u16 unityNote = 60;
    i16 fineTune = 0;
    bool looping = false;
    u32 loopStart = 0;
    u32 loopLength = 0;

    while (!r.IsEof() && r.Remaining() >= 8) {
        u32 chunkId = r.ReadU32LE();
        u32 chunkSize = r.ReadU32LE();

        if (chunkId == MakeFourCC("fmt ")) {
            auto fmt = r.ReadSlice(chunkSize);
            formatTag = fmt.ReadU16LE();
            channels = fmt.ReadU16LE();
            sampleRate = fmt.ReadU32LE();
            fmt.Skip(6);
            bitsPerSample = fmt.ReadU16LE();
        } else if (chunkId == MakeFourCC("data")) {
            auto data = r.ReadSlice(chunkSize);
            size_t count = data.Size() / sizeof(i16);
            waveData.resize(count);
            if (count > 0)
                std::memcpy(waveData.data(), data.CurrentPtr(), count * sizeof(i16));
        } else if (chunkId == MakeFourCC("wsmp")) {
            auto wsmp = r.ReadSlice(chunkSize);
            if (wsmp.Size() >= 20) {
                hasWsmp = true;
                u32 cbSize = wsmp.ReadU32LE();
                if (cbSize < 20 || cbSize > wsmp.Size()) cbSize = 20;
                unityNote = wsmp.ReadU16LE();
                fineTune = static_cast<i16>(wsmp.ReadU16LE());
                wsmp.Skip(4);
                u32 options = wsmp.ReadU32LE();
                u32 loopCount = wsmp.ReadU32LE();
                if (loopCount > 0 && wsmp.Remaining() >= 16) {
                    wsmp.ReadU32LE();
                    wsmp.ReadU32LE();
                    loopStart = wsmp.ReadU32LE();
                    loopLength = wsmp.ReadU32LE();
                    looping = (options & 0x1) != 0 || loopLength > 0;
                }
            } else {
                r.Skip(chunkSize);
            }
        } else {
            r.Skip(chunkSize);
        }

        if (chunkSize & 1) r.Skip(1);
    }

    if (formatTag != 1 || channels != 1 || bitsPerSample != 16) {
        errorMsg_ = "Only PCM mono 16-bit DLS waves are supported";
        return false;
    }
    if (waveData.empty()) {
        errorMsg_ = "DLS wave has no sample data";
        return false;
    }

    const u32 waveLength = static_cast<u32>(waveData.size());
    if (loopStart >= waveLength) {
        looping = false;
        loopStart = 0;
        loopLength = 0;
    } else if (loopLength > waveLength - loopStart) {
        loopLength = waveLength - loopStart;
    }
    if (loopLength <= 1) {
        looping = false;
        loopStart = 0;
        loopLength = 0;
    }

    u32 start = static_cast<u32>(sampleData_.size());
    sampleData_.insert(sampleData_.end(), waveData.begin(), waveData.end());
    outWave.sample.start = start;
    outWave.sample.end = start + static_cast<u32>(waveData.size());
    outWave.sample.loopStart = start + loopStart;
    outWave.sample.loopEnd = start + loopStart + loopLength;
    outWave.sample.sampleRate = sampleRate;
    outWave.sample.originalPitch = static_cast<u8>(ClampValue<int>(unityNote, 0, 127));
    outWave.sample.pitchCorrection = static_cast<i8>(ClampValue<int>(fineTune, -99, 99));
    outWave.sample.sampleType = 1;
    outWave.hasWsmp = hasWsmp;
    return true;
}

bool DlsFile::ParseInstruments(BinaryReader& r, u32 /*chunkSize*/) {
    while (!r.IsEof() && r.Remaining() >= 8) {
        u32 chunkId = r.ReadU32LE();
        u32 chunkSize = r.ReadU32LE();
        if (chunkId == MakeFourCC("LIST")) {
            u32 listType = r.ReadU32LE();
            auto listData = r.ReadSlice(chunkSize - 4);
            if (listType == MakeFourCC("ins ")) {
                DlsInstrument instrument;
                if (!ParseInstrumentList(listData, chunkSize - 4, instrument))
                    return false;
                instruments_.push_back(std::move(instrument));
            }
        } else {
            r.Skip(chunkSize);
        }
        if (chunkSize & 1) r.Skip(1);
    }
    return true;
}

bool DlsFile::ParseInstrumentList(BinaryReader& r, u32 /*chunkSize*/, DlsInstrument& outInstrument) {
    while (!r.IsEof() && r.Remaining() >= 8) {
        u32 chunkId = r.ReadU32LE();
        u32 chunkSize = r.ReadU32LE();
        if (chunkId == MakeFourCC("insh")) {
            auto insh = r.ReadSlice(chunkSize);
            insh.ReadU32LE();
            outInstrument.bank = insh.ReadU32LE();
            outInstrument.program = insh.ReadU32LE();
        } else if (chunkId == MakeFourCC("LIST")) {
            u32 listType = r.ReadU32LE();
            auto listData = r.ReadSlice(chunkSize - 4);
            if (listType == MakeFourCC("lrgn")) {
                while (!listData.IsEof() && listData.Remaining() >= 8) {
                    u32 subId = listData.ReadU32LE();
                    u32 subSize = listData.ReadU32LE();
                    if (subId == MakeFourCC("LIST")) {
                        u32 subType = listData.ReadU32LE();
                        auto regionData = listData.ReadSlice(subSize - 4);
                        if (subType == MakeFourCC("rgn ") || subType == MakeFourCC("rgn2")) {
                            DlsRegion region;
                            if (!ParseRegionList(regionData, subSize - 4, region))
                                return false;
                            outInstrument.regions.push_back(region);
                        }
                    } else {
                        listData.Skip(subSize);
                    }
                    if (subSize & 1) listData.Skip(1);
                }
            } else if (listType == MakeFourCC("lar2") || listType == MakeFourCC("lart")) {
                if (!ParseArticulatorList(listData, outInstrument.articulators, outInstrument.connections))
                    return false;
            }
        } else {
            r.Skip(chunkSize);
        }
        if (chunkSize & 1) r.Skip(1);
    }
    return true;
}

bool DlsFile::ParseRegionList(BinaryReader& r, u32 /*chunkSize*/, DlsRegion& outRegion) {
    while (!r.IsEof() && r.Remaining() >= 8) {
        u32 chunkId = r.ReadU32LE();
        u32 chunkSize = r.ReadU32LE();
        if (chunkId == MakeFourCC("rgnh")) {
            auto rgnh = r.ReadSlice(chunkSize);
            outRegion.keyLow = rgnh.ReadU16LE();
            outRegion.keyHigh = rgnh.ReadU16LE();
            outRegion.velLow = rgnh.ReadU16LE();
            outRegion.velHigh = rgnh.ReadU16LE();
            rgnh.ReadU16LE();
            outRegion.keyGroup = rgnh.ReadU16LE();
        } else if (chunkId == MakeFourCC("wlnk")) {
            auto wlnk = r.ReadSlice(chunkSize);
            wlnk.ReadU16LE();
            wlnk.ReadU16LE();
            wlnk.ReadU32LE();
            outRegion.waveIndex = wlnk.ReadU32LE();
        } else if (chunkId == MakeFourCC("wsmp")) {
            auto wsmp = r.ReadSlice(chunkSize);
            if (wsmp.Size() >= 20) {
                outRegion.hasWsmp = true;
                wsmp.ReadU32LE();
                outRegion.unityNote = wsmp.ReadU16LE();
                outRegion.fineTune = static_cast<i16>(wsmp.ReadU16LE());
                outRegion.attenuation = DlsWsmpAttenuationToCentibels(wsmp.ReadU32LE());
                u32 options = wsmp.ReadU32LE();
                u32 loopCount = wsmp.ReadU32LE();
                if (loopCount > 0 && wsmp.Remaining() >= 16) {
                    wsmp.ReadU32LE();
                    wsmp.ReadU32LE();
                    outRegion.loopStart = wsmp.ReadU32LE();
                    outRegion.loopLength = wsmp.ReadU32LE();
                    outRegion.looping = (options & 0x1) != 0 || outRegion.loopLength > 0;
                }
            } else {
                r.Skip(chunkSize);
            }
        } else if (chunkId == MakeFourCC("LIST")) {
            u32 listType = r.ReadU32LE();
            auto listData = r.ReadSlice(chunkSize - 4);
            if (listType == MakeFourCC("lar2") || listType == MakeFourCC("lart")) {
                if (!ParseArticulatorList(listData, outRegion.articulators, outRegion.connections))
                    return false;
            }
        } else {
            r.Skip(chunkSize);
        }
        if (chunkSize & 1) r.Skip(1);
    }
    return true;
}

bool DlsFile::ParseArticulatorList(BinaryReader& r, std::vector<i32>& outGenerators,
                                   std::vector<DlsConnection>& outConnections) {
    if (outGenerators.empty()) {
        outGenerators.assign(GEN_COUNT, 0x7fffffff);
    }

    while (!r.IsEof() && r.Remaining() >= 8) {
        u32 chunkId = r.ReadU32LE();
        u32 chunkSize = r.ReadU32LE();
        if (chunkId != MakeFourCC("art1") && chunkId != MakeFourCC("art2")) {
            r.Skip(chunkSize);
            if (chunkSize & 1) r.Skip(1);
            continue;
        }

        auto art = r.ReadSlice(chunkSize);
        if (art.Size() < 8) {
            if (chunkSize & 1) r.Skip(1);
            continue;
        }

        art.ReadU32LE(); // cbSize
        u32 connectionCount = art.ReadU32LE();
        for (u32 i = 0; i < connectionCount && art.Remaining() >= 12; ++i) {
            u16 source = art.ReadU16LE();
            u16 control = art.ReadU16LE();
            u16 destination = art.ReadU16LE();
            u16 transform = art.ReadU16LE();
            i32 scale = static_cast<i32>(art.ReadU32LE());

            outConnections.push_back({ source, control, destination, transform, scale });

            // First pass: static parameters only. Runtime modulators remain unsupported.
            if (source != CONN_SRC_NONE_LOCAL || control != CONN_SRC_NONE_LOCAL || transform != CONN_TRN_NONE_LOCAL)
                continue;

            auto setValue = [&](int gen, i32 value) {
                if (gen < 0 || gen >= GEN_COUNT) return;
                outGenerators[gen] = value;
            };

            switch (destination) {
            case CONN_DST_GAIN_LOCAL:
                AddGeneratorValue(outGenerators, GEN_InitialAttenuation, DlsGainScaleToCentibels(scale));
                break;
            case CONN_DST_PITCH_LOCAL:
                AddPitchCents(outGenerators, scale / 65536);
                break;
            case CONN_DST_PAN_LOCAL:
                AddGeneratorValue(outGenerators, GEN_Pan, DlsPanScaleToSf2Pan(scale));
                break;
            case CONN_DST_KEYNUMBER_LOCAL:
                setValue(GEN_OverridingRootKey, scale / 65536);
                break;
            case CONN_DST_EG1_ATTACKTIME_LOCAL:
                AddGeneratorValue(outGenerators, GEN_AttackVolEnv, DlsEnvelopeTimeToTimecents(scale));
                break;
            case CONN_DST_EG1_DECAYTIME_LOCAL:
                AddGeneratorValue(outGenerators, GEN_DecayVolEnv, DlsEnvelopeTimeToTimecents(scale));
                break;
            case CONN_DST_EG1_RELEASETIME_LOCAL:
                AddGeneratorValue(outGenerators, GEN_ReleaseVolEnv, DlsEnvelopeTimeToTimecents(scale));
                break;
            case CONN_DST_EG1_SUSTAINLEVEL_LOCAL:
            {
                f64 sustainPercent = std::clamp(static_cast<f64>(scale) / 65536.0, 0.0, 1000.0);
                if (sustainPercent >= 999.999) {
                    setValue(GEN_SustainVolEnv, 0);
                } else if (sustainPercent <= 0.0) {
                    setValue(GEN_SustainVolEnv, 1000);
                } else {
                    f64 linear = sustainPercent / 1000.0;
                    i32 cb = static_cast<i32>(std::clamp(-200.0 * std::log10(linear), 0.0, 1000.0));
                    setValue(GEN_SustainVolEnv, cb);
                }
                break;
            }
            case CONN_DST_EG1_DELAYTIME_LOCAL:
                AddGeneratorValue(outGenerators, GEN_DelayVolEnv, DlsEnvelopeTimeToTimecents(scale));
                break;
            case CONN_DST_EG1_HOLDTIME_LOCAL:
                AddGeneratorValue(outGenerators, GEN_HoldVolEnv, DlsEnvelopeTimeToTimecents(scale));
                break;
            default:
                break;
            }
        }

        if (chunkSize & 1) r.Skip(1);
    }

    return true;
}

void DlsFile::ApplyRegionToZone(const DlsRegion& region, const DlsWave& wave, ResolvedZone& outZone) const {
    outZone.sample = &wave.sample;
    const i32* defaults = GetSF2GeneratorDefaults();
    for (int i = 0; i < GEN_COUNT; ++i)
        outZone.generators[i] = defaults[i];

    outZone.generators[GEN_KeyRange] = static_cast<i32>((region.keyHigh << 8) | region.keyLow);
    outZone.generators[GEN_VelRange] = static_cast<i32>((region.velHigh << 8) | region.velLow);
    outZone.generators[GEN_OverridingRootKey] = region.hasWsmp
                                              ? ClampValue<int>(region.unityNote, 0, 127)
                                              : wave.sample.originalPitch;
    outZone.generators[GEN_FineTune] = region.hasWsmp ? region.fineTune : 0;
    outZone.generators[GEN_InitialAttenuation] = region.hasWsmp ? region.attenuation : 0;

    const bool waveLooping = wave.sample.loopEnd > wave.sample.loopStart + 1;
    const bool useRegionLoop = region.hasWsmp;
    const bool looping = useRegionLoop ? region.looping : waveLooping;
    outZone.generators[GEN_SampleModes] = looping ? 3 : 0;
    if (looping) {
        if (useRegionLoop) {
            const i32 waveLoopStartRel = static_cast<i32>(wave.sample.loopStart - wave.sample.start);
            const i32 waveLoopEndRel = static_cast<i32>(wave.sample.loopEnd - wave.sample.start);
            const u32 waveLength = wave.sample.end - wave.sample.start;
            const u32 regionLoopStartClamped = std::min(region.loopStart, waveLength);
            const u32 regionLoopEndClamped = std::min(region.loopStart + region.loopLength, waveLength);
            if (regionLoopEndClamped <= regionLoopStartClamped + 1) {
                outZone.generators[GEN_SampleModes] = 0;
                return;
            }

            const i32 regionLoopStartRel = static_cast<i32>(regionLoopStartClamped);
            const i32 regionLoopEndRel = static_cast<i32>(regionLoopEndClamped);
            outZone.generators[GEN_StartloopAddrsOffset] = regionLoopStartRel - waveLoopStartRel;
            outZone.generators[GEN_EndloopAddrsOffset] = regionLoopEndRel - waveLoopEndRel;
        } else {
            outZone.generators[GEN_StartloopAddrsOffset] = 0;
            outZone.generators[GEN_EndloopAddrsOffset] = 0;
        }
    }

}

bool DlsFile::FindZones(u16 bank, u8 program, u8 key, u8 velocity,
                        std::vector<ResolvedZone>& outZones,
                        const ModulatorContext* /*ctx*/) const {
    outZones.clear();

    const bool requestDrum = (bank == 128);
    const u16 requestBank = requestDrum ? 0 : bank;

    for (const auto& instrument : instruments_) {
        const bool instrumentDrum = (instrument.bank & 0x80000000u) != 0;
        const u16 instrumentBank = static_cast<u16>(((instrument.bank >> 8) & 0x7Fu) * 128u
                                                  + (instrument.bank & 0x7Fu));
        if (instrumentDrum != requestDrum || instrumentBank != requestBank || instrument.program != program)
            continue;

        for (const auto& region : instrument.regions) {
            if (key < region.keyLow || key > region.keyHigh) continue;
            if (velocity < region.velLow || velocity > region.velHigh) continue;

            const DlsWave* wave = nullptr;
            if (!poolTableOffsets_.empty()) {
                if (region.waveIndex >= poolTableOffsets_.size()) continue;
                u32 poolOffset = poolTableOffsets_[region.waveIndex];
                for (const auto& candidate : waves_) {
                    if (candidate.poolOffset == poolOffset) {
                        wave = &candidate;
                        break;
                    }
                }
            } else if (region.waveIndex < waves_.size()) {
                wave = &waves_[region.waveIndex];
            }
            if (!wave) continue;

            ResolvedZone zone;
            ApplyRegionToZone(region, *wave, zone);
            if (!instrument.articulators.empty()) {
                for (int i = 0; i < GEN_COUNT; ++i) {
                    if (instrument.articulators[i] != 0x7fffffff)
                        MergeArticulatorGenerator(zone.generators, i, instrument.articulators[i]);
                }
            }
            if (!region.articulators.empty()) {
                for (int i = 0; i < GEN_COUNT; ++i) {
                    if (region.articulators[i] != 0x7fffffff)
                        MergeArticulatorGenerator(zone.generators, i, region.articulators[i]);
                }
            }

            auto applyConnection = [&](const DlsConnection& conn) {
                if (conn.control != CONN_SRC_NONE_LOCAL || conn.transform != CONN_TRN_NONE_LOCAL)
                    return;

                i32 sourceValue = 0;
                if (conn.source == CONN_SRC_KEYNUMBER_LOCAL) {
                    sourceValue = static_cast<i32>(key) - 60;
                } else if (conn.source == CONN_SRC_KEYONVELOCITY_LOCAL) {
                    sourceValue = static_cast<i32>(velocity) - 64;
                } else {
                    return;
                }

                // Heuristic scaling: DLS lScale is fixed-point, but source normalization is implementation-defined.
                // Use a centered 7-bit source domain to approximate common hardware behavior.
                i32 delta = static_cast<i32>((static_cast<int64_t>(conn.scale) * sourceValue) / (64 * 65536));
                switch (conn.destination) {
                case CONN_DST_PITCH_LOCAL:
                    AddPitchCents(zone.generators, delta);
                    break;
                case CONN_DST_GAIN_LOCAL:
                    zone.generators[GEN_InitialAttenuation] += DlsGainScaleToCentibels(conn.scale) * sourceValue / 64;
                    break;
                case CONN_DST_PAN_LOCAL:
                    zone.generators[GEN_Pan] = ClampValue<i32>(
                        zone.generators[GEN_Pan] + (DlsPanScaleToSf2Pan(conn.scale) * sourceValue / 64),
                        -500, 500);
                    break;
                case CONN_DST_EG1_ATTACKTIME_LOCAL:
                    zone.generators[GEN_AttackVolEnv] += delta;
                    break;
                case CONN_DST_EG1_DECAYTIME_LOCAL:
                    zone.generators[GEN_DecayVolEnv] += delta;
                    break;
                case CONN_DST_EG1_DELAYTIME_LOCAL:
                    zone.generators[GEN_DelayVolEnv] += delta;
                    break;
                case CONN_DST_EG1_HOLDTIME_LOCAL:
                    zone.generators[GEN_HoldVolEnv] += delta;
                    break;
                case CONN_DST_EG1_RELEASETIME_LOCAL:
                    zone.generators[GEN_ReleaseVolEnv] += delta;
                    break;
                case CONN_DST_EG1_SUSTAINLEVEL_LOCAL:
                    zone.generators[GEN_SustainVolEnv] = std::clamp(zone.generators[GEN_SustainVolEnv] + delta, 0, 1000);
                    break;
                default:
                    break;
                }
            };

            for (const auto& conn : instrument.connections) applyConnection(conn);
            for (const auto& conn : region.connections) applyConnection(conn);
            outZones.push_back(zone);
        }
    }

    return !outZones.empty();
}

} // namespace ArkMidi
