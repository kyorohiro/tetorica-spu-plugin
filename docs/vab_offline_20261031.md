# VAB解析・offline音源準備 — 2026-10-31

## 実装した範囲

- `src/VabBank.*`: standard VAB v6/v7のcomplete / 明示VH+VB入力を解析する。
  入力上限、疎なProgram番号、packed Tone table、先頭VB予約領域、各Sample境界を検査する。
- 選択Toneを元ADPCMのまま準備する。最初のEND以降だけtrimし、tuning・ADSR・loop・
  volume/pan近似・Reverb send metadataを保持する。共有ReverbはまだOff。
- `SpuInstrument(preparedTone)`: fresh SPUへ選択Sampleを配置し、最初のENDとloop cacheまで
  audio thread外で事前再生する。既存engineへのRAM書換えや再生中の交換は行わない。
- `spu_vab_render`: complete VABのTone一覧を表示し、指定ToneをMIDI60でoffline WAV化する。
  UI・音声deviceは不要。既存の出力ファイルは上書きしない。

pluginのUIは引き続き内蔵Sine。VABのファイル選択・Program/Tone選択・DAWへのembedded bank保存は後続。
既存ユーザーAUの更新・GarageBandの再操作は今回行っていない。

## 検証

| 検査 | 結果 |
| --- | --- |
| 依存SHA / SDK link検査 | 成功、vendor変更なし |
| offline / macOS preset build・CTest | 各3 suite成功、APP / AU / VST3 build成功 |
| 最終parser regression | 8,516 assertions、7,572不正入力拒否、7 JS oracle cases一致 |
| ASan + UBSan付きparser regression | 成功 |
| 実VST3の公式validator | 47成功、0失敗 |
| 実VST3 PCM・MIDI境界・lifecycle・Gain state | 44.1 / 48 / 96kHzで成功、stateは従来通り12 bytes |
| 既存Sine | 変更前後の3秒WAV SHA-256一致（下記） |
| 長いVAB Sample | 65,536 bytes・非ゼロloop・block分割一致・24 voiceを検査 |
| 利用可能RAM上限 | 520,192 bytesを準備・再生、超過を拒否 |
| cache準備後のcallback | prepare / note / render / retrigger / panicでC++ new/delete 0 |

new/delete probeは、対象の不変Sample再生経路を検査するもの。全mallocやhard realtime保証とは別。
実聴・実DAWでのVAB操作・長時間realtimeは未検証。

既存Sine 3秒WAV: `7a61b56d356ac198c35808b503cf0b61b55a9547b21770eb2a67c97d0cc4aef3`。
追加の独立したASan/UBSan probeでも、原本と10,000件の決定的な変異・切り詰め入力で異常なし。
これは限定した入力集合での検査であり、全VAB対応の証明ではない。

fixtureとoracleの元commit・source SHA・license・再生成コマンドは
[`tests/fixtures/vab/README.md`](../tests/fixtures/vab/README.md)および`expected.json`に保存。
通常build / CTestは独立repoだけで完結し、親workspaceやNodeを必要としない。

## 確認したSDK制限

現在pinしているSDKでは、repeat先packetにrepeat bit `2`がないloopはEND後に無音になる。
同一の4packetデータで非ゼロloop startのflag `4`はtail peak 0、flag `6`は0.01超を確認した。
parserはJS版と同じく両方を受け付けるが、Native engine構築時は未対応パターンを理由付きで拒否する。
markerなしで先頭flag `0`に戻るケースも同様。元payload・flags・固定vendorは変更していない。

Sample 0、Noise sentinel 255、PSF展開、song再生、自動zone/layer再現は初期importerの対象外。
Reverb公開時には、Sample領域とReverb RAMの予約も合わせて設計する。

## 試すコマンド

```sh
cmake --preset offline
cmake --build --preset offline --parallel 4
ctest --preset offline
build/offline/spu_vab_render tests/fixtures/vab/spu-synth-demo.vab
build/offline/spu_vab_render tests/fixtures/vab/spu-synth-demo.vab 0 build/vab-tone-0.wav
```

今回の試作WAVはignoredな `build/vab-preview-20261031/tone-{0,1,2}.wav` に作成した。
各48kHz / stereo / PCM16 / 96,000 frames、すべて非ゼロ。
元fixtureのone-shot Tone 2はpeak 7 PCM16 unitsと小さい。音量を正規化せず元metadataを保持した。
WAV hashと計測値は同directoryの`report.json`。再実行時の上書き拒否と、不正Tone指定時に出力を作らないことも確認。

## 次の入口

1. workerで準備したengineの世代管理・block境界での交換・audio callback外での破棄。
2. version / size上限付きembedded bank state。復元直後のoffline renderでも準備完了していること。
3. VABファイル選択とProgram / Tone UI。失敗時は現在の楽器を保持する。

設計の詳細は[VAB import plan](vab_import_plan.md)、残項目は[TODO](issues/TODO.md)を参照。

## GitHub保存・CI

- 開始TODO: `68ab79c`、実装: `2f31cda1bca8a26e6591d61f3cd9a7267764252c`。origin/mainへpush済み。
- [Verify SPU Plugin / 41185732525](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41185732525)
  は全job成功。Linux offline `124492162533`、macOS `124492162353`。
- 2026-10-31 17:43:05〜17:46:30 JST。新規runnerで依存取得・build・CTest・AU配置/auval・
  公式VST3・実VST3のPCM/state/scheduler検査まで完了した。
- artifact `macos-development-bundles`: ID `13036918081`、2,034,794 bytes、SHA-256
  `76e0f154bbcda0b0fc25d67ded1db3a156e04d97024ef096ffac478a27658f80`。
  upload成功の記録であり、download後の実機試験・Release公開とは区別する。
