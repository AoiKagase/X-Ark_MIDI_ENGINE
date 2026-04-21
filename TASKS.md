# SF2 Spec Follow-up Tasks

2026-04-20 実装完了。

## 完了項目

- `§9.6 NRPN`
  - `ChannelState` に SF2 NRPN 状態と generator offset 保持を追加。
  - `ModulatorContext` に `nrpnOffsets` を追加。
  - `Synthesizer` から `Sf2File::ResolveZone()` まで NRPN offset を配線。
- `§8.2.4 concave / convex`
  - `DecodeModSourceValue()` の `sin/cos` 近似を、SF2 の amplitude-squared 形状に合わせた `sqrt` ベース実装へ置換。
- `CC67 Soft Pedal`
  - 新規 `NoteOn` にのみ attenuation/filter cutoff のオフセットを適用。
  - 発音中ボイスの controller refresh では再適用しない。
- `sm24`
  - 既存 24-bit 読み込み経路を維持。
  - `sm24 before smpl` を含む追加ケースをテストで固定。
- `SF2 ProgramLayer identity`
  - `ResolvedZone` / `Voice` に layer 識別子を保持し、controller refresh / retrigger 判定を `sample` 単位ではなく zone 単位へ修正。
  - 同一 sample を共有する複数 layer が refresh 後も別設定を保つ回帰テストを追加。
- `SF2 ROM sample override`
  - `Voice::NoteOn()` の境界計算が ROM override 時も `sampleDataOverrideCount` を使うよう修正。
  - ROM バンク長が本体 SF2 と異なるケースを `Sf2Compliance` に追加。
- `Special SF2 route clamp refresh`
  - `RefreshResolvedZoneControllers()` 側でも特殊 SF2 ルートの `clampAboveRoot` を再適用するよう修正。
  - 特殊ルート対象ノートで controller refresh 後もピッチが跳ねないことをテストで固定。
- `Exclusive class per-zone`
  - `Synthesizer` / `VoicePool` の先頭ゾーン一括配布をやめ、各 `ProgramLayer` / zone の resolved `ExclusiveClass` を使う実装へ修正。
  - 複数ゾーンで exclusive class が異なるプリセットのチョーク挙動テストを追加。

## テスト

- `Sf2Compliance` 全件パス。
- 追加した主な確認:
  - `TestProgramLayerRefreshMatchesZoneIdentity`
  - `TestExclusiveClassRespectsProgramLayerZone`
  - `TestRomOverrideUsesOverrideSampleLimit`
  - `TestSpecialSf2RouteClampSurvivesControllerRefresh`
  - `TestSourceCurvesQuarterPoints`
  - `TestSf2NrpnGeneratorOffsets`
  - `TestSoftPedalAffectsNewNoteOnOnly`
  - `TestSm24BeforeSmplAccepted`

## 補足

- `sm24` はこのコードベースではロード時に `i32` の 24-bit PCM プールへ前結合する設計のまま。
- `Soft Pedal` は MIDI 定義どおり `CC67`。`CC66` は Sostenuto。
