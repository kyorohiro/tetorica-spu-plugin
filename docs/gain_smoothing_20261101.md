# Gain smoothing（2026-11-01）

再生中にGainを変更した際、SPU出力の音量を約5msの直線rampでつなぐようにした。
以前はblockの先頭で新しいGainへ即座に切り替わっていた。
左右へ同じ係数を1frameにつき1回適用し、MIDIの途中offset・256frameのscratch・
host blockの境界をまたいでもrampを継続する。

## 挙動と互換性

- ramp長はhost rateの`ceil(sampleRate * 0.005)`frames。
  44.1kHzでは221、48kHzでは240、96kHzでは480frames。
- 最初の出力sampleから1/Nずつ進み、N番目でtargetへ正確に到達する。
  同じtargetを再設定してもrampを再開始しない。途中でtargetが変わると、その時点の音量から再計算する。
- Gain 0への到達後は左右ともexact 0、定常時は従来の係数で出力する。
  非finite Gainは従来のsafe 0とし、範囲外値は0〜1へ制限する。
- 初回、audio reset / sample rate変更、deactivate後、VST3 bypass後、
  成功host restore、import tuple採用は最初の正の長さのblockで現在targetへ即時同期する。
  旧projectの初回NoteOnを、以前のprojectのGainからfadeさせない。
- 通常のUI音源 / Program / Tone交換ではGainの進行を保持する。
  失敗host restoreは進行中rampにも手を加えない。0framesではsample時間を進めない。
- Gain範囲0〜100%、parameter ID0〜25、state schema9 /旧互換を維持した。
  保存するGainはtarget値。rampの途中値・残framesは保存しない。
- SPU後段のplugin出力処理であり、SPU実機のvoice volume / ADSRとは別の処理。
  新しいUI操作や設定項目は追加していない。

通常のGain automationは、固定しているiPlug2の既存block単位target処理を維持する。
VST3は各queueの最後のpointを適用してからblockをrenderし、AUもこのwrapperでは
ordinary parameter offsetをDSPへscheduleしない。
**出力係数はsampleごとに補間するが、host automationの各pointをsample精度で再生する機能ではない。**
その拡張は別TODO。

host state restoreは従来どおり、audio processingを停止したquiescent lifecycle呼出が前提。
audio threadはstate mutexを取らず、Gain処理は固定サイズのscalar状態だけを使用する。

## 検証

作業前TODOは`2a6d9c5`、開始Pluginは`65ff2fe`、Native SDKは`95cf71a`のまま。
実装commitは`3ec6049`。
詳細なsource / binary / log hashとCIは[JSON記録](gain_smoothing_20261101.json)へ保存する。

- Offline / macOSのCTestは各25件成功。ASan / UBSanも25件成功。
- `spu.gain.smoother`は8 / 44.1 / 48 / 96 / 192kHz、数学的な係数、endpoint完全一致、
  途中反転、同targetの非再開始、分割完全一致、reset、不正・極端値、instance独立を検証する。
  callback相当の区間でC++ new/delete（alignedを含む）が0回であることも確認した。
- 実AUは48kHz、実VST3は44.1 / 48 / 96kHzで検査する。
  同じ演奏の100% Gain参照PCMへ独立した数学的ramp係数を掛け、左右全sampleを比較する。
  これは位相やKONを変更せず、hard stepと異なる出力になることも検証する。
- 上下変更 /途中retarget /同値更新 /可変block /MIDI分割、保存target、最初のrestore音量、
  不正state後のramp保持、instance分離を検証した。
- ramp途中からreset /reactivate /bypassして、新しいNoteOnの最初のsampleからtarget音量へ
  同期することも両hostで確認した。bypass中はexact 0。
- 既存Gain-only restore比較は、referenceも同じ初期stateへ復元してから比較するよう修正した。
  live変更のrampを、即時同期するrestoreと同じものとして比較しない。
- Steinberg VST3 validatorは47 passed / 0 failed。
- 実装`3ec6049`の[GitHub Actions 41291073473](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41291073473)は
  offline /macOSの全2job成功、step失敗なし。CIのAU install /auvalと両formatの実host検査も成功した。
  artifactはメタデータだけ確認し、download /実行はしていない。

自動検証はbuild内のbundleを直接ロードし、音声deviceやeditorを開かない。
AUのローカルfactory登録はsandbox内で拒否されたため、許可されたsandbox外の実行で検証した。
実聴でのクリック・操作感、実DAWのautomation録音と長時間再生は別の確認項目。
通常UIのbank交換 / live import採用時のGain境界はsource reviewで確認し、
今回の自動host検査をAU独自Load sound操作の確認とは扱わない。

## 配置済みAUと次の入口

**後続作業でGain版を配置済み:** [AU更新・GarageBand保存再起動の記録](au_gain_update_20261101.md)。
現在のinstalled manifestは[こちら](au_gain_update_20261101.json)。以下は実装検証時点の履歴。

実装検証時点では、installed AUと開いているGarageBandは変更していない。
[その時点のinstalled manifest](au_loop_update_20261101.json)はloopfix版。
新buildのAUとはbinary hashが異なる。
配置更新はhost保存・終了、known manifest照合、旧bundle退避を行う[手順](au_loop_update_20261101.md)で進める。

```sh
cmake --preset offline
cmake --build --preset offline --parallel 4
ctest --preset offline
cmake --preset macos
cmake --build --preset macos --parallel 4
ctest --preset macos
build/macos/tetorica_au_state_check build/macos/out/TetoricaSPU.component
sh scripts/check_vst3_audio.sh build/macos/out/TetoricaSPU.vst3
sh scripts/validate_vst3.sh build/macos/out/TetoricaSPU.vst3
```

version /tag /release、npm /Web /Tauri配布は変更していない。
分割VH/VB UI、sample精度ordinary automation、人による最終確認は[TODO](issues/TODO.md)に残す。
