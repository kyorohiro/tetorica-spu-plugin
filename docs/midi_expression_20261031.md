# MIDI Pitch Bend・Sustain — 2026-10-31

開始TODOを`b8c95c8`へ保存してから実装した。
Web版との機能対応と次の移植順は[移植計画](spu_synth_parity_plan.md)を参照。

## 演奏の挙動

- MIDIの16チャンネルごとに14-bit Pitch BendとSustainを保持する。
- Bendの中心は8192、範囲は±2半音。0/8192/16383を-2/0/+2半音に対応させる。
  元NoteとsampleのtuningからSPU PITCHを再計算し、発音中・Release中の声を再発音せず変更する。
  次のNote Onにも適用する。RPNによるrange変更は今回の対象外。
- CC64の64以上でSustain On。離鍵時は鍵盤状態だけOffにし、実際のKey Offを保留する。
  63以下へ戻すと離鍵済みの声だけReleaseし、まだ押している鍵盤は維持する。
  sampleの自然終了やauthored ADSRは延長しない。
- 保留中の声は空きslotとして扱わない。同一channel/noteの再押下は同じslotで再発音し、
  全24 slotsがbusyなら従来のoldest voiceをstealする。古いpedal保留は新しい声へ持ち越さない。
- CC123はそのchannelの全鍵盤を離す操作で、Sustainに従う。
  CC121はそのchannelのBendを中立へ戻し、pedalを解除する。押下中の音は維持する。
- CC120と画面のAll sound offは既存のglobal即時mute。Bend/pedalの入力状態は維持する。
  channel別CC120は既存TODOとして残す。

`activeVoiceCount()`は従来の「物理的に押下中の鍵盤数」を返す。
pedal保留とRelease tailを含む発音数とは区別する。
CC番号・pedal閾値の参照は[MIDI Associationの一覧](https://midi.org/midi-1-0-control-change-messages)。

## 音源交換・lifecycle

coreとwrapperに固定長のcontroller状態を持つ。UIでVAB/Program/Tone/Builtinを切り替える時は、
新engineのprepare後にwrapperのlive値を再適用する。交換前の鍵盤は再発音しない。
block境界をまたぐpedal解除・BendもUI交換時に保持し、古いNoteイベントだけを破棄する。

成功したhost state restore、audio reset、deactivate、MIDI overflowからの復帰ではcontrollerを中立化する。
state restoreの解析・準備に失敗した場合、現在の音色・Gain・controller・準備済み交換を保持する。
live値をpresetへ追加していないため、既存schema 1とlegacy Gain-only stateの形式は変わらない。
host restoreでは前のblockから保留されたcontrollerも破棄し、restore後の新しいMIDIは保持する。

VST3 bypass中はframeworkがProcessBlockを呼ばないため、wrapperが毎blockの最後に
queue・発音・controllerを初期化する。解除後に古いpedalやNoteを再配送しない。

## VST3のMIDI adapter

固定iPlugのmapped CC処理は、1つのparameter queueにつき最後のpointだけを読む。
`plugin/MidiAdapters.cpp`でBend、CC64/120/121/123の全pointをsample offset付きで取り込み、
同一block内のpedal On→Offや連続Bendを落とさないようにした。16channelのmappingを公開する。
frameworkの別translation unitにも同じ設定が届くよう、VST3 targetのcompile definitionsで
CC/Input channel数16を指定する。config headerだけの設定で高channelのmappingが拒否された問題も修正した。

元frameworkの処理へはGain/bypassだけを渡して二重配送を防ぐ。
一時viewとqueueは固定長で、adapter内のnew/delete・mutexは不要。
不正なnormalized値・channel・負offsetは無視し、過多のpoint/queueは既存overflow rescueへ戻す。
block末尾とそれ以降のoffsetは、既存のNoteと同様に次blockへ繰り越す。
Gainのsample単位automationは今回の変更に含めない。

## 検証

- offline全5 CTest成功、1.94秒。macOS全5 CTest成功、2.31秒。
- APP/AUv2/VST3をbuild成功。既存installed bundleへの自動配置は行わない。
- Coreは独立した移調音とのPCM一致、Bend後の位相・Release継続、channel分離、
  pedal解除時の保持鍵盤、再発音、24voice stealing、CC123/121、invalid入力、partition一致を検査した。
- callback中のC++ new/deleteは0。全mallocやframework全体のhard realtime保証とは別。
- 初期Sineの3秒demoは変更前とbyte完全一致。SHA-256:
  `7a61b56d356ac198c35808b503cf0b61b55a9547b21770eb2a67c97d0cc4aef3`。
- 別`build/asan-midi`のAddressSanitizer Debugでcore/VAB 2 suite成功、13.85秒、診断なし。
- Steinberg公式VST3 validatorは47成功・0失敗。`build/midi-vst3-validator.log`。
- 実VST3は44.1/48/96 kHzのIMidiMapping→parameter changesから、実AUは48 kHzのMusicDeviceMIDIEventから検証成功。
  ±2半音・ch15・同block複数point・分割block一致・再発音なし・CC64 64/63・CC123/121・
  panic・reset/deactivate/restore・future controller破棄・live値の非保存と、既存state/PCMを検査した。
- VST3では不正normalized/channel/offset、1025point overflow、Gain保持、bypass中の跨block pedal順序と
  解除後のfresh note・中立Pitch・Releaseも成功。既存frameworkのreserved bypass propertyを実hostから使用する。
- AUのsandbox内初回はlocal factory登録不可で終了。承認済みsandbox外の最終実行では全検査成功。
- 最終ログは`build/midi-vst3-audio-final.log`、`build/midi-au-state-final.log`、`build/midi-vst3-validator-final.log`。
  [機械可読記録・bundle manifest](midi_expression_20261031.json)。CIとinstalled AU更新は後で追記する。

再現コマンド:

```sh
cmake --build --preset offline --parallel 4
ctest --preset offline
cmake --preset macos
cmake --build --preset macos --parallel 4
ctest --preset macos
sh scripts/validate_vst3.sh build/macos/out/TetoricaSPU.vst3
sh scripts/check_vst3_audio.sh build/macos/out/TetoricaSPU.vst3
sh scripts/check_au_state.sh build/macos/out/TetoricaSPU.component
```

今回のMIDI機能は実hostからの自動入力で検証する。
物理MIDI機器での演奏・実聴・長時間realtimeと区別する。画面のPitch wheel/Sustain checkboxは今回追加しない。
UIで実際に音色を切り替えながらwheel/pedalを操作する追試も未実施で、引継ぎ処理のsource reviewと分ける。
