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
