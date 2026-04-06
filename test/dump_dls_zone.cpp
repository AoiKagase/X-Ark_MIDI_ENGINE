#include <cstdio>
#include <vector>
#include <cmath>

#include "../ArkMidiEngine/src/dls/DlsFile.h"

using namespace ArkMidi;

int wmain(int argc, wchar_t* argv[]) {
    if (argc < 6) {
        std::fwprintf(stderr, L"Usage: %ls <input.dls> <bank> <program> <key> <velocity>\n", argv[0]);
        return 1;
    }

    const std::wstring path = argv[1];
    const u16 bank = static_cast<u16>(_wtoi(argv[2]));
    const u8 program = static_cast<u8>(_wtoi(argv[3]));
    const u8 key = static_cast<u8>(_wtoi(argv[4]));
    const u8 velocity = static_cast<u8>(_wtoi(argv[5]));

    DlsFile dls;
    if (!dls.LoadFromFile(path)) {
        std::fprintf(stderr, "DLS parse failed: %s\n", dls.ErrorMessage().c_str());
        return 1;
    }

    std::vector<ResolvedZone> zones;
    if (!dls.FindZones(bank, program, key, velocity, zones, nullptr)) {
        std::printf("zones=0\n");
        return 0;
    }

    std::printf("zones=%zu\n", zones.size());
    for (size_t i = 0; i < zones.size(); ++i) {
        const auto& z = zones[i];
        const i16* pcm = dls.SampleData();
        const size_t sampleCount = dls.SampleDataCount();
        const u32 start = z.sample->start;
        const u32 end = z.sample->end;
        int peak = 0;
        double sumSq = 0.0;
        u32 frames = 0;
        for (u32 pos = start; pos < end && pos < sampleCount; ++pos) {
            const int v = pcm[pos];
            const int a = std::abs(v);
            if (a > peak) peak = a;
            sumSq += static_cast<double>(v) * static_cast<double>(v);
            ++frames;
        }
        const double rms = frames > 0 ? std::sqrt(sumSq / frames) : 0.0;
        std::printf(
            "zone=%zu root=%d fine=%d att=%d pan=%d sampleModes=%d atk=%d hold=%d dec=%d sus=%d rel=%d start=%u end=%u loopStart=%u loopEnd=%u lsOff=%d leOff=%d sampleRate=%u peak=%d rms=%.0f\n",
            i,
            z.generators[GEN_OverridingRootKey],
            z.generators[GEN_FineTune],
            z.generators[GEN_InitialAttenuation],
            z.generators[GEN_Pan],
            z.generators[GEN_SampleModes],
            z.generators[GEN_AttackVolEnv],
            z.generators[GEN_HoldVolEnv],
            z.generators[GEN_DecayVolEnv],
            z.generators[GEN_SustainVolEnv],
            z.generators[GEN_ReleaseVolEnv],
            z.sample->start,
            z.sample->end,
            z.sample->loopStart,
            z.sample->loopEnd,
            z.generators[GEN_StartloopAddrsOffset] + z.generators[GEN_StartloopAddrsCoarse] * 32768,
            z.generators[GEN_EndloopAddrsOffset] + z.generators[GEN_EndloopAddrsCoarse] * 32768,
            z.sample->sampleRate,
            peak,
            rms);
    }

    return 0;
}
