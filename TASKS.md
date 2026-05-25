# Right-Channel Noise Resolution Notes

2026-04-22 から追跡していた右チャンネルの瞬間的な跳ねは、疑似ループの retrigger が原因。
`XAME_COMPAT_SF2_ZERO_LENGTH_LOOP_RETRIGGER` を ON にすることで現状は解決している。

## 解決確認

- `bbs6-357.mid` + `FluidR3_GM2-2.SF2` の `CH1` 周辺で出ていた右側の一時的な増幅は、疑似ループ retrigger ON で解消。
- 現象は「安定した右寄り」ではなく、疑似ループ retrigger 不足による局所的な跳ねだった。
- `tests/main.cpp` と `tests/realtime_player.cpp` では `XAME_COMPAT_SF2_ZERO_LENGTH_LOOP_RETRIGGER` を有効化済み。

## 回帰確認ポイント

- 疑似ループ retrigger ON 時に、右側の一時的な音量跳ねが再発しないこと。
- `CH1 solo` と通常ミックスの両方で、不自然な周期感や局所増幅が戻らないこと。
- retrigger ON により、他の SF2 で不要なクリック、ピッチ跳ね、ループ境界の違和感が増えないこと。

## レビュー起点の修正候補

- `Voice::NoteOn()` で `ownedByParent` と `linkedVoiceIndex` を明示初期化する。
- `VoicePool::RenderBlock()` / `RenderSample()` の root `!active` 分岐で linked voice を回収する。
- `SynchronizeAggregatedLinkedVoice()` を、sample position は独立のまま envelope / NoteOff タイミングは同期する方向で検討する。
- `TryAppendExplicitSf2StereoPairPlan()` の `sampleLink` 条件を、片方向リンク許容または compat option 化するか検討する。
- リミッター簡略化が症状を増幅しているかを切り分けるため、旧挙動または低めの ceiling を比較する。
- `Sf2File::LoadFromMemory()` の `sm24` 復元で、負の `hi16 << 8` による未定義動作を除去する。
- `tests/sf2_compliance.cpp` の `sm24` 期待値計算も同じ未定義動作を踏まない形に直す。
- `VoicePool::RenderBlock()` の並列 worker 経路に対して、SF2 ボイス多重時の回帰テストを追加する。

## 直近の切り分けメモ

- 右側へ跳ねる主因は、リバーブ設定ではなく疑似ループの retrigger だった。
- `XARKMIDI_REVERB_DIRECT_FEEDBACK=1` は `CH1` の局所的な右増幅を下げるが、単独では問題音声との差分を十分には埋めない。
- `XARKMIDI_REVERB_LINKED_TAPS=1` 追加でも局所窓はやや改善するが、`test_ch1_2.wav` 全体との差分はむしろ増える。
- `XARKMIDI_DISABLE_MASTER_REVERB_SEND=1` は件数を減らしても最悪ピークが悪化したため、常用候補から外す。

## BASSMIDI 比較で残っている課題

- 各 Program ごとの音圧差を BASSMIDI と比較し、音色単位で過大/過小に出る傾向を記録する。
- リバーブ/コーラスなどのエフェクトの掛かり具合を BASSMIDI と比較し、send 量、wet/dry 比、ステレオ幅のどこで差が出ているか切り分ける。
- SF2 attenuation の dB 変換、velocity/key scaling、generator/modulator 合成後の比率計算が BASSMIDI 相当になっているか検証する。
- Program 別の比較では、同一 MIDI、同一 sound bank、同一 sample rate で X-Ark と BASSMIDI のレンダーを揃え、RMS/peak/LUFS と耳確認の両方で判定する。

## TODO: `multiply` / `apply default modulators` 廃止計画

- [x] 廃止後の固定挙動を最終決定する。
  - SF2 effects send は `SF2_SPEC_204` では resolver 解決値を使用。
  - legacy 経路では `multiply` を持たない前提で send 合成方針を確定（現状維持 or 仕様寄せのどちらかを明文化）。
  - `apply default modulators` なしで legacy 経路の implicit default をどう扱うかを確定。
- [x] C API から `XAME_COMPAT_MULTIPLY_SF2_MIDI_EFFECTS_SENDS` と `XAME_COMPAT_APPLY_SF2_CHANNEL_DEFAULT_MODULATORS` を削除する。
  - 対象: `include/XArkMidiEngine.h`
- [x] C# API から `CompatibilityFlags.MultiplySf2MidiEffectsSends` と `CompatibilityFlags.ApplySf2ChannelDefaultModulators` を削除する。
  - 対象: `XArkMidiEngine.cs`
- [x] 互換オプション解決ロジックから両フラグ経路を削除する。
  - 対象: `src/ApiCompatOptions.cpp`, `src/synth/Voice.h`
- [x] 音声処理側の到達不能分岐を削除して単純化する。
  - 対象: `src/synth/Voice.cpp` (`MixEffectsSend`, `RefreshEffectSends`)
  - 対象: `src/sf2/Sf2File.cpp` (`ctx->applySf2ChannelDefaults` 分岐)
  - 対象: `src/synth/Synthesizer.cpp` (`ctx.applySf2ChannelDefaults` 伝播)
- [x] GUI で廃止済み項目が再混入しないことを確認する（現状は除去済み）。
  - 対象: `tools/XArkMidiGuiPlayer/MainForm.cs`
- [x] テストを整理する。
  - 削除/置換: 両フラグの有効性を前提にしたテスト
  - 追加/維持: 廃止後の固定挙動（spec mode / legacy mode）検証
  - 対象: `tests/sf2_compliance.cpp`
- [x] ドキュメントを更新する。
  - `README.md` の列挙・サンプルコード・互換モード説明から両フラグ記述を削除
  - 変更理由と代替設定（`compatibilityMode`）を明記
- [x] 最終確認を実施する。
  - `Sf2Compliance` 全件パス
  - GUI ビルド成功
  - APIヘッダとラッパーの公開面に削除漏れがないことを確認
