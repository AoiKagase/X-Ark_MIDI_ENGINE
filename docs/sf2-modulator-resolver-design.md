# SF2 Modulator Resolver Design

目的: Pg81 Saw Wave 個別調整ではなく、SoundFont 2.04 specification の modulator / generator 階層規則に沿って SF2 再生エンジンを段階的に整理する。第一段階では設計だけを行い、Filter Q や NRPN の実装修正には入らない。

制約:

- FluidSynth / BASSMIDI / TiMidity++ のコードはコピー、翻訳、移植しない。
- SoundFont 2.04 specification と一般DSP式だけを基準にする。
- 旧互換挙動を壊さず、仕様準拠モードと旧互換モードを分ける。
- 実装は小さなコミットに分割する。

## Current State

現在の `Sf2File::ResolveZone()` は generator 階層を解決した後、instrument modulator を適用し、さらに implicit default modulator 相当の処理を手書き分岐で追加してから preset modulator を適用している。

主な問題:

- implicit default modulators が通常 modulator と別経路で扱われている。
- default suppression 判定が個別の `DefaultModulatorState` フラグに分散している。
- identical modulator の階層規則が「instrument は replace、preset は add」という spec の構造として表現されていない。
- modulator destination に `kModDestInitialPitch = 59` という generator 表外の便宜 destination が混在している。
- controller 変更時の再評価対象が全 SF2 controller 変更で広く走っており、destination ごとの分類が未整理。

## Target Model

新しい resolver は「modulator graph を作る段」と「評価して generator delta を出す段」を分ける。

```text
SF2 zone modulators + implicit default modulators
    -> validate/classify
    -> build effective instrument set
    -> add effective preset set
    -> evaluate source/amount source/transform/link graph
    -> accumulate deltas into Value Generator destinations
```

仕様準拠モードでは implicit default modulators も instrument-level modulator として扱う。旧互換モードでは既存の `applySf2*` compatibility flags と現行の音量/send 方針を保持し、resolver の仕様準拠テーブルを段階的に opt-in する。

## Data Structures

追加候補:

```cpp
enum class Sf2PlaybackMode {
    LegacyCompat,
    SpecCompliant,
};

enum class ModulatorLevel {
    ImplicitInstrumentDefault,
    InstrumentGlobal,
    InstrumentLocal,
    PresetGlobal,
    PresetLocal,
};

enum class ModulatorValidity {
    Valid,
    InvalidSource,
    InvalidAmountSource,
    InvalidTransform,
    InvalidDestination,
    InvalidLink,
    LinkCycle,
    Unsupported,
};

struct ModulatorIdentity {
    u16 source;
    u16 destination;
    u16 amountSource;
    u16 transform;
};

struct EvaluatedModulator {
    SFModList mod;
    ModulatorLevel level;
    ModulatorValidity validity;
    bool participatesInDefaultSuppression;
    bool isLinkDestination;
    u16 sourceControllerMaskClass;
};
```

`ModulatorIdentity` は spec 9.5 の identical modulator 判定に使う。現在の `BuildEffectiveZoneModEntries()` は同一 zone 内の duplicate を最後の定義だけ残す処理として残せるが、階層間の replace/add は別の resolver 段で扱う。

## Implicit Default Modulators

仕様準拠モードでは以下を instrument-level default set として明示的な `SFModList` に変換する。

| Source | Destination | Amount | Amount source | Transform | 備考 |
| --- | --- | ---: | --- | --- | --- |
| note-on velocity, concave, negative, unipolar | initialAttenuation | 960 cB | none | linear | velocity 0 は NoteOff なので評価対象外 |
| note-on velocity, linear, negative, unipolar | initialFilterFc | -2400 cents | none | linear | cutoff summing node |
| channel pressure, linear, positive, unipolar | vibLfoToPitch | 50 cents | none | linear | |
| CC1, linear, positive, unipolar | vibLfoToPitch | 50 cents | none | linear | |
| CC7, concave, negative, unipolar | initialAttenuation | 960 cB | none | linear | |
| CC10, linear, positive, bipolar | pan | 1000 | none | linear | |
| CC11, concave, negative, unipolar | initialAttenuation | 960 cB | none | linear | |
| CC91, linear, positive, unipolar | reverbEffectsSend | 200 | none | linear | |
| CC93, linear, positive, unipolar | chorusEffectsSend | 200 | none | linear | |
| pitch wheel, linear, positive, bipolar | Initial Pitch summing node | 12700 cents | pitch wheel sensitivity | linear | generator enum 59 ではない。destination 設計は後述 |

`Initial Pitch` は spec 8.4.10 の default modulator destination だが、generator enum 59 ではない。enum 59 は `unused5` であり、SF2 file 内の normal modulator destination として受け付けてはいけない。第一実装では以下を明示的に分ける。

- SF2 file 由来の modulator destination: spec 8.1.1 に従い Value Generator のみ許可する。generator 59 / enum 表外 destination は無視する。
- implicit default pitch-wheel modulator: file 由来 destination ではなく、resolver 内部の `InitialPitchAddCents` summing node として扱う。出力は `GEN_CoarseTune` / `GEN_FineTune` へ焼き込まず、voice の pitch path へ別値として渡す候補にする。
- 旧互換モード: 既存の channel pitch bend path と `kModDestInitialPitch` 相当の挙動を保持し、二重適用を避ける。

本段階の設計では、要件 5 を優先して spec-compliant mode の file modulator destination は Value Generator に限定する。同時に、spec default modulator 8.4.10 は漏らさず、内部 destination として channel pitch 更新責務に接続する設計にする。

## Hierarchy Rules

### Same Zone Duplicate

同一 zone 内では `(source, destination, amountSource, transform)` が同一の modulator は最後の定義だけを有効にする。無効、未対応、評価不能な modulator はこの duplicate 解決後に `validity != Valid` として保持するが、default suppression には参加させない。

### Instrument Level

処理順:

1. implicit default set を `ImplicitInstrumentDefault` として投入する。
2. instrument global zone の valid identical modulator は default を replace する。
3. instrument local zone の valid identical modulator は default または instrument global を replace する。
4. identical でない valid instrument modulator は同じ destination summing node に別 modulator として追加する。

重要: `Invalid*` / `Unsupported` / `LinkCycle` / `Unevaluated` は default suppression に参加しない。たとえば velocity -> initialAttenuation と同じ見た目でも amount source が未対応なら default は残る。

### Preset Level

処理順:

1. preset global zone を正規化する。
2. preset local zone の valid identical modulator は preset global を replace する。
3. preset effective set を instrument effective set へ add する。

Preset-level identical modulator は default/instrument 側を suppress/replace しない。同一 identity の場合は amount を加算した 1 つの modulator として評価してよい。identity が異なる場合は同じ destination summing node に別 modulator として追加する。

## Destination Policy

Spec-compliant mode:

- modulator destination は Value Generator のみ許可する。
- sample offset generators は modulator destination として無視する。
- `sampleModes` は modulator destination として無視する。
- Range Generator、Substitution Generator、Sample Generator は destination として無視する。
- `GEN_Instrument` と `GEN_SampleID` は terminal generator であり destination として無視する。
- `GEN_ExclusiveClass` と `GEN_OverridingRootKey` は sample generator 扱いとして modulator destination から外す。
- generator 59 は `unused5` なので無視する。`Initial Pitch` default modulator は file destination ではなく内部 summing node としてだけ扱う。

Value Generator destination 候補:

- pitch/filter/volume LFO and envelope amount destinations
- initialFilterFc / initialFilterQ
- chorusEffectsSend / reverbEffectsSend / pan
- envelope time / sustain destinations
- initialAttenuation
- coarseTune / fineTune / scaleTuning

実装時は `IsValueGeneratorModDestination(u16 dest, Sf2PlaybackMode mode)` を新設し、既存の `IsSupportedModulatorDestination()` は旧互換判定または wrapper に縮小する。

## Source Curve Engine

`sfModSrcOper` と `sfModAmtSrcOper` は同じ decoder を通す。amount source だけを特別扱いしない。

Source decoder の責務:

- source index を controller domain に写像する。
- CC bit が立っている場合は `ctx.ccValues[index]` を SF2 controller domain に正規化する。7-bit source は unipolar max が `127/128`、bipolar range が `-1..127/128` であり、単純な `/127.0` ではない。
- CC palette では index `0`, `6`, `32`, `38`, `98..101`, `120..127` は MIDI function なので illegal として modulator 全体を無視する。index `33..63` は LSB contribution 予約なので同様に無視する。
- velocity は note-on velocity を SF2 controller domain に正規化する。現行 16-bit velocity 入力は 14-bit/7-bit 相当の境界を明示し、max を 1.0 に丸めない。
- key number は 0..127 を SF2 controller domain に正規化する。
- poly pressure、channel pressure、pitch wheel、pitch wheel sensitivity を `ModulatorContext` から読む。
- link source は通常 source ではなく graph edge として扱う。
- curve type は linear / concave / convex / switch のみ有効にする。
- direction は positive / negative を反映する。
- polarity は unipolar / bipolar を反映する。
- source type は 0..3 のみ有効にする。bits 11..15 が立つ source は reserved として modulator 全体を無視する。
- reserved bit または未知 source は `InvalidSource` とする。
- concave / convex は SF2 の source curve として実装する。単なる `sqrt()` 近似を仕様値として固定せず、少なくとも endpoints、negative/unipolar、bipolar の符号と最大値が spec の controller mapping に一致するテストを置く。

Transform engine:

- `sfModTransOper` は `amount * mappedSource * mappedAmountSource` の合成後に適用する。
- SF2 2.04 で定義された transform だけを有効化する。現行の linear / absolute 対応は維持し、未知 transform は `InvalidTransform` とする。

評価式:

```text
sourceValue = DecodeSource(sfModSrcOper)
amountValue = DecodeSource(sfModAmtSrcOper)  // 0 means constant 1.0
output = ApplyTransform(modAmount * sourceValue * amountValue, sfModTransOper)
```

## Linked Modulators

Linked modulator は zone 内の modulator list 相対 index で接続する。resolver は duplicate 解決後も、リンク先 index の意味を壊さないように raw zone index と effective entry index の対応表を持つ。

規則:

- `sfModDestOper & 0x8000` が link destination。
- link destination index は `Bag->wModNdx` からの raw modulator list 相対 index。duplicate/invalid filtering 後の packed index ではない。
- `sfModSrcOper == link` の modulator は incoming link を要求する。
- `sfModAmtSrcOper == link` は無効として無視する。
- 範囲外リンクは `InvalidLink` として無視する。
- cycle は DFS の visiting/visited で検出する。
- cycle に含まれる全 modulator を `LinkCycle` として無視する。cycle の外から cycle 内へ依存する modulator も評価不能として無視する。
- cycle 内 modulator は default suppression に参加しない。

## Controller Re-evaluation Classes

Controller 変更時に全 destination を無差別再評価しないため、modulator graph から destination ごとの dependency mask を作る。

分類:

- Note-on 固定: velocity, key number, forced key/velocity。NoteOn 時だけ resolver に反映し、発音中の controller refresh では原則再評価しない。
- Channel realtime: CC, channel pressure, pitch wheel, pitch wheel sensitivity。発音中 voice の再評価対象。
- Per-note realtime: poly pressure。該当 key の active voice のみ再評価対象。
- Link-derived: linked source の依存元 union。依存元に realtime source があれば再評価対象。
- Static: source 0 only。NoteOn 後の再評価不要。
- Zone-range dependency: modulator の有効範囲は同じ bag の generator keyRange / velRange に従う。発音中 controller refresh は既に選択済み zone の modulator graph を再評価し、CC 等で zone selection を変更しない。

Destination ごとの反映カテゴリ:

- Mix-only: `initialAttenuation`, `pan`, `reverbEffectsSend`, `chorusEffectsSend`。既存の `RefreshResolvedZoneControllers()` で比較的安全に更新可能。
- Filter realtime: `initialFilterFc`, `initialFilterQ`, `modLfoToFilterFc`, `modEnvToFilterFc`。係数更新とクリック/zipper noise 対策が必要。
- Pitch/sample-step: `coarseTune`, `fineTune`, `scaleTuning`, pitch amount destinations。`baseSampleStep` と channel/per-note pitch path の責務分離が必要。
- Envelope shape: attack/decay/hold/release/sustain, keynumTo*。発音中に再適用すると envelope phase が変わるため、spec-compliant mode での更新可否を destination ごとに明示する。
- LFO shape: delay/frequency/amount destinations。発音中再設定時の phase 維持方針を決める。
- Non-realtime/sample: sample offsets, `sampleModes`, sampleID, ranges。modulator destination として無視。

初期実装では dependency mask を作るだけに留め、既存の広めの `RefreshSf2ControllersForChannel()` 呼び出しは旧互換モードで維持する。spec-compliant mode では次段階で mask に基づく最小更新へ移行する。

## Impact Files

- `src/sf2/Sf2Types.h`: Value Generator 判定、modulator resolver 用の小さな型を置く候補。公開 API ではないため private header 新設も可。
- `src/sf2/Sf2File.h`: `DefaultModulatorState` を廃止し、resolver entry point と playback mode を追加する候補。
- `src/sf2/Sf2File.cpp`: 現在の `ResolveZone()`, `BuildEffectiveZoneModEntries()`, `DecodeModSourceValue()`, `ApplyModulatorEntries()`, `ScanUnsupportedModulators()` を段階的に分解する主対象。
- `src/soundbank/SoundBank.h`: `ModulatorContext` に playback mode と dependency/debug 出力を追加する候補。
- `src/synth/Synthesizer.h` / `src/synth/Synthesizer.cpp`: controller 変更時の再評価分類を使う呼び出し側。第一実装では最小変更に留める。
- `src/synth/Voice.h` / `src/synth/Voice.cpp`: destination 反映カテゴリごとの refresh。第一段階では設計のみ。
- `src/synth/VoicePool.h` / `src/synth/VoicePool.cpp`: active voice refresh の対象絞り込み。第一段階では設計のみ。
- `include/XArkMidiEngine.h`: 将来 `XAME_COMPAT_*` ではなく spec-compliant mode を明示する API flag を追加する候補。既存 ABI を壊さない。
- `tests/sf2_compliance.cpp`: resolver 単体テストと既存回帰の追加対象。
- `tests/dump_sf2_zone.cpp`: modulator resolver の debug dump を追加する候補。

## Implementation Steps

小さなコミット案:

1. ドキュメント追加のみ。本ファイル。
2. `Sf2ModulatorResolver` の private header/cpp を追加し、既存挙動から未接続の純粋関数テストだけを入れる。
3. source curve decoder を既存 `DecodeModSourceValue()` から移し、linear/concave/convex/switch、positive/negative、unipolar/bipolar のテストを追加する。
4. destination classifier を追加し、spec-compliant mode で sample offset generators / `sampleModes` / enum 表外 destination を無視するテストを追加する。
5. implicit default modulators を data table 化する。ただし `ResolveZone()` にはまだ接続しない。
6. instrument-level resolver を追加し、implicit default suppression/replace の unit test を通す。
7. preset-level resolver を追加し、identical preset modulator が add になる unit test を通す。
8. amount source と transform を通常 source と同じ経路で評価する実装へ置換する。
9. linked modulator の cycle 検出を「cycle 内全 modulator 無視」に厳密化する。
10. spec-compliant opt-in flag を内部に追加し、旧互換モードを既定として既存テストを通す。
11. `ResolveZone()` の default hand-written 分岐を spec-compliant path だけ resolver に差し替える。
12. controller dependency mask を生成し、まだ既存 refresh 経路には強く接続しない。

## Required Tests

Resolver unit tests:

- implicit default modulators が instrument-level default set として生成される。
- instrument global identical modulator が implicit default を replace する。
- instrument local identical modulator が implicit default / instrument global を replace する。
- preset global identical modulator が instrument/default 側へ add される。
- preset local identical modulator が preset global を replace してから instrument/default 側へ add される。
- invalid source modulator は default suppression に参加しない。
- unsupported amount source modulator は default suppression に参加しない。
- unsupported transform modulator は default suppression に参加しない。
- unevaluated linked modulator は default suppression に参加しない。
- same-zone duplicate は最後の定義だけ有効になる。
- amount source は source decoder と同じ curve/direction/polarity/transform 経路で評価される。
- `sfModAmtSrcOper == link` は無視される。
- linked modulator の範囲外 link は無視される。
- link destination index は raw zone modulator index 基準で解決され、duplicate 解決後の packed index と混同されない。
- linked modulator の cycle は cycle 内全 modulator が無視される。
- cycle に依存する modulator も評価不能として無視される。

Source curve tests:

- linear / concave / convex / switch。
- positive / negative。
- unipolar / bipolar。
- CC source。
- illegal CC source index `0`, `6`, `32`, `38`, `98..101`, `120..127` は無視される。
- reserved CC LSB source index `33..63` は無視される。
- velocity source。
- key number source。
- poly pressure source。
- channel pressure source。
- pitch wheel source。
- pitch wheel sensitivity source。
- link source。
- reserved source bit または未知 source は modulator 全体を無視する。

Destination tests:

- spec-compliant mode では Value Generator destination だけ適用される。
- sample offset generators は modulator destination として無視される。
- `sampleModes` は modulator destination として無視される。
- `GEN_SampleID`, `GEN_Instrument`, range generators, substitution/sample generators は無視される。
- enum 表外の `kModDestInitialPitch` 相当は spec-compliant mode で無視される。
- implicit default pitch-wheel -> Initial Pitch は spec-compliant mode でも内部 pitch summing node として評価され、file modulator destination の generator 59 とは混同されない。
- legacy mode では既存の pitch bend / send / attenuation 挙動が維持される。

Integration/regression tests:

- 既存 `TestForcedVelocityDefaultModulators` が旧互換モードで変わらない。
- 既存 `TestDefaultVelocityModulatorsAreNotSuppressedByAmountSourceMods` が維持される。
- 既存 `TestDuplicateModulatorsUseLastDefinition` が維持される。
- 既存 `TestLinkedModulatorsFeedTargetSource` が維持されるか、spec-compliant mode 専用期待値に分離される。
- controller refresh で CC7/10/11/91/93 の mix-only destination が発音中に更新される。
- pitch wheel / pitch wheel sensitivity 変更時、legacy channel pitch path と spec-compliant resolver path の責務が二重適用にならない。
- unsupported modulator count / transform count の既存 reporting が壊れない。

Verification:

- ドキュメントのみのコミットではビルド不要。
- resolver 実装を入れる各コミットで `Sf2Compliance` をビルドして実行する。
- public API flag を触るコミットでは MSVC project と CMake の両方を確認する。

## Non-goals For This Phase

- Filter Q のDSP式修正。
- NRPN の仕様準拠修正。
- Pg81 Saw Wave 専用の音色補正。
- FluidSynth / BASSMIDI / TiMidity++ とのコード比較による実装移植。
- 既存 default mode の音量、send、pitch bend 挙動の変更。
