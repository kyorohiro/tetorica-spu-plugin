# 準備済み音源の交換 — 2026-10-31

## 今回の接続

`SpuEngineExchange`をpluginの正の長さのaudio block先頭へ接続した。
`VabLoader`はcomplete VAB bytesとflat Tone indexを受け取り、workerで解析・Tone検証・
SPU構築・cache準備を完了させる。現時点のUIはloaderを起動せず、内蔵Sineを演奏する。
ファイル選択、Program/Tone UI、custom stateは後続。

## 所有権と世代

- 制御側のrequest発行・publish・回収は非audioのmutexで直列化する。
  新requestで世代を進め、古い完了・採用済み世代の再publishを拒否する。
- 準備済みengineは単一のatomic pending pointerへpublishする。
  publish側とaudio側のexchange操作で所有権を渡し、双方から破棄しない。
- audio側はretired queueの空きを確認してから候補を引き取る。古い候補や不正host rateは
  回収側へ戻し、現在の音源は保持する。成功時は現在のhost rateでallocation-free prepareして採用する。
- 旧engineのretired queueは4件。満杯なら候補を取り出さず、現在の音源で演奏を続ける。
  workerが20ms周期で回収する。audio側からworkerへの通知・待機は行わない。
- 採用判定の世代loadを境界とする。その後に始まったrequestは、先行する採用に続いて扱われ得る。
  競合するrequest発行と採用の全処理を単一mutexで囲む設計ではない。
- workerは実行中1件＋最新待機1件に制限。失敗したrequestのstatusはFailedとなり、現在のengineをresetしない。
  Readyはpublish完了であり、audio側の採用は`adoptedGeneration()`で区別する。

`instrument()`と`beginBlock()`は単一audio consumer専用。hostのReset/Deactivate等も、
audio処理と直列化または停止した状態で呼ぶ前提。loaderを先に停止・joinし、
producer/audio利用が終了してからexchangeを破棄する。
loader終了時点ですでにpublish済みの候補はexchangeの所有物として残る。

## MIDIと音色変更

切り替えた音源はheld noteなしで始まる。旧音源のtailは保持せず、crossfadeもまだ行わない。
前blockから持ち越したMIDIだけをstableに取り除き、hostが今回のblock用に新しく渡したイベントは残す。
これにより、将来のstate復元直後に届くsample 0のNote Onを捨てない構成とした。
zero-frame処理では交換せず、新しいイベントを持ち越し扱いにもしない。

## 検証の位置づけ

実engineでの交換・worker・競合・終了の検査を`tests/spu_engine_exchange_test.cpp`に追加した。
実VST3で交換時MIDIを操作する検査は、state/UIの入口を接続する次段階で追加する。
現在の実VST3検査は従来の内蔵Sine、offset境界、queue overflow、lifecycle、Gain stateの回帰検査。
ユーザーComponentsのAUの更新やGarageBandでの再操作は今回行わない。

アプリ独自のaudio経路には確保・engine破棄・mutex・worker通知を追加しない。
iPlug2 / hostのcallback全体がlock-freeという意味ではなく、C++ new/delete probeも
全mallocやhard realtime保証とは区別する。

既存のAU MIDI ingressは別TODO。Apple SDKのMusicDevice.hはrender thread外からの
MIDI呼び出しも扱うが、固定iPlug2の`IPlug/AUv2/IPlugAU.cpp`の`DoMIDIEvent`は直接
`ProcessMidiMsg`を呼ぶ。現在のwrapper queueはその並行呼び出しを保証していない。
今回の交換層と分け、bounded ingressと実hostでの検査を追加する。

## 次の入口

1. immutableなbank / selection定義と、version / size制限付きcustom state。
2. state復元を成功と返す前にengineの準備を完了し、直後の最初のrenderを検査する。
3. VABファイル選択・Program/Tone UIをつなぎ、失敗時の保持とstatus表示を確認する。

## ローカル実行結果

| 検査 | 結果 |
| --- | --- |
| offline / macOS preset | build成功、各4 CTest suite成功 |
| APP / AU / VST3 | build成功、ユーザーAUは未更新 |
| 交換・worker試験 | 9 group成功、offline実測1,744,573 checks / 104 RT probes |
| C++確保・破棄 | probe内new/delete 0、観測した音源サイズの94確保/94回収、RT回収0 |
| AddressSanitizer | 同じ交換試験成功、1.66秒、検出報告なし |
| ThreadSanitizer | 同じ交換試験成功、6.13秒、競合検出報告なし |
| 公式VST3 validator | 47成功、0失敗 |
| 実VST3 PCM / scheduler / lifecycle / Gain state | 44.1 / 48 / 96kHzの全検査成功、state12 bytes維持 |

check数と確保数はworkerのcoalescingや待機pollにより変わり得る。上記は今回の一実行。
失敗時の保持は、同じ演奏履歴を持つ参照音源との連続PCM一致で検査する。
新品とのpanic/retrigger比較で初期のSDK履歴差を交換処理の不具合と誤認しない。
テスト初期版の2失敗は、関数引数の一時所有権がfull-expression終端まで残る点と、
このDSP履歴を考慮してoracleを修正した。製品側の交換コードの修正は不要だった。

原本fixtureのworker準備・panが異なるToneの採用・現在のhost rate適用・古い完了の拒否・
失敗によるpending取消・retire満杯からの復帰・処理途中の要求置換・48段階のproducer/audio並行処理・
pending/retired/処理途中の終了を含む。sanitizer成功も、全スケジュールやhard realtimeの保証とは別。

```sh
cmake --preset offline
cmake --build --preset offline --parallel 4
ctest --preset offline
cmake --preset macos
cmake --build --preset macos --parallel 4
ctest --preset macos
```

sanitizerはpluginなしの別buildで実行した。例:

```sh
cmake -S . -B build/asan-handoff -DTETORICA_BUILD_PLUGIN=OFF -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS=-fsanitize=address -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address
cmake --build build/asan-handoff --target spu_engine_exchange_test --parallel 2
ctest --test-dir build/asan-handoff -R '^spu.engine.exchange$' --output-on-failure
```

TSanはbuild directoryを`build/tsan-handoff`、両flagを`-fsanitize=thread`として同様に実行。
ローカルAppleClang 17 / macOS ARM64で検査した。Windowsのbuild・実host検査は未実施。

## GitHub保存・CI

- 開始TODO: `058fa89`、実装: `cb8d3f6af992c6fd2b9b7c64fad240b3cf578c6c`。origin/mainへpush済み。
- [Verify SPU Plugin / 41187048655](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41187048655)
  全job成功。Linux offline `124495982069`、macOS `124495981895`。
- 2026-10-31 18:02:52〜18:06:41 JST。fresh runnerで依存取得・APP/AU/VST3 build・各4 CTest suite、
  AU配置/auval、公式VST3、実VST3のPCM/state/scheduler検査まで完了。
- artifact `macos-development-bundles`: ID `13037257134`、2,039,556 bytes、SHA-256
  `551763708a009a4b44349f31a3dbaff4bf84f54e4a6122e3e9df394ed9120612`。
  upload成功の記録であり、download後の実機試験・Release公開とは区別する。
- ASan / TSanは上記ローカル実行の結果で、今回のCI workflowには追加していない。
