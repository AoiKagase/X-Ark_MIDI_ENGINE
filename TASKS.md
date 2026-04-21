# SF2 Spec Follow-up Tasks

2026-04-20 実装完了。

## 未対応

- `SF2 ROM sample override`
  - `Voice::NoteOn()` が ROM 差し替え時でも `pcmDataSize` を境界計算に使っているため、`sampleDataOverrideCount` を使うよう修正。
  - ROM バンク長が本体 SF2 と異なるケースを `Sf2Compliance` に追加。
- `SF2 controller refresh zone matching`
  - `ProgramLayerPlan` は zone 単位で構築される一方、`Voice::MatchesResolvedZone()` が `sampleHeader` のみで一致判定しているため、同一 sample を共有する複数ゾーンを識別できる layer / zone キーへ見直し。
  - CC / pressure / pitch bend 更新で別ゾーン値に上書きされない回帰テストを追加。
- `Special SF2 route clamp refresh`
  - 特殊 SF2 ルートの `clampAboveRoot` が controller refresh 後も維持されるよう、`RefreshResolvedZoneControllers()` 側の再計算へ同等処理を反映。
  - 特殊ルート対象ノートで pitch/controller 更新後もピッチが跳ねないことをテストで固定。
- `Exclusive class per-zone`
  - `Synthesizer` / `VoicePool` で先頭ゾーンの `ExclusiveClass` を全ボイスへ配布しているため、`ProgramLayerPlan` の各 entry / zone ごとの resolved 値を保持する実装へ修正。
  - 複数ゾーンで exclusive class が異なるプリセットのチョーク挙動テストを追加。

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

## テスト

- `Sf2Compliance` 全件パス。
- 追加した主な確認:
  - `TestSourceCurvesQuarterPoints`
  - `TestSf2NrpnGeneratorOffsets`
  - `TestSoftPedalAffectsNewNoteOnOnly`
  - `TestSm24BeforeSmplAccepted`

## 補足

- `sm24` はこのコードベースではロード時に `i32` の 24-bit PCM プールへ前結合する設計のまま。
- `Soft Pedal` は MIDI 定義どおり `CC67`。`CC66` は Sostenuto。
