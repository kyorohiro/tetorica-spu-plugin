# Web SPU Synthとの機能対応・段階移植計画 — 2026-10-31

## 目標と現在地

Web版`docs/synth-spu`にある音作り機能を、Native SPU pluginへ段階的に移す。
同等の音作りができることを目標とし、画面の配置や操作方法はDAWに合わせて設計する。
すべての機能が現在のpluginへ実装済みという意味ではない。

開始TODOは`b8c95c8`。今回のPitch Bend / Sustainは実装し、coreと実AU/VST3で検証した。
[検証記録](midi_expression_20261031.md)と[作業TODO](issues/TODO.md)で、実装済みと未確認範囲を区別する。

pluginでは、24 physical voicesによる演奏、velocity、Note On/Off、Release、panic、
complete VAB v6/v7の読み込み、Program/Tone選択、bank bytesを含むDAW stateを実装済み。
VAB由来のADSR・tuning・loop・volume/panを使用する。ADSRはCustom overrideによる編集も実装した。
ほかの元Tone設定の編集UIは後続。[ADSRの検証記録](adsr_editing_20261031.md)を参照。
共有Reverbのpreset / Enable / Return / sendも実装した。初期Offで元の音を保つ。
[Reverbの検証記録](reverb_20261031.md)を参照。
112-sample PCMの4形状・単一canvasでのdraft描画・Applyも実装・検証済み。
Applyはworkerで準備した音源への交換であり、保持音・残響は停止する。通常PCM draftのlive適用は対象外。
[PCMの検証記録](pcm_waveform_20261031.md)を参照。
共有Noiseとlive Clockも実装し、元音源を保持してSampleへ戻せる。
[Noiseの検証記録](noise_20261031.md)を参照。
PMONの12音pair、4変調波形、live Ratio / Depth、Decayも実装・検証した。
[PMONの検証記録](pmon_20261031.md)を参照。
Software MorphもA/B/Resultの単一canvas、endpointのdraft / Apply、live Amountを実装した。
core・実AU/VST3・Standalone操作・installed AU更新・GarageBand旧state復元・両repo CIを確認した。
[Morphの検証記録](morph_20261101.md)と[保存・更新記録](morph_20261101.json)を参照。
Gainは現在0〜100%。WebのMaster 200%とは異なる。

実DAWでの確認済み範囲と残るProgram操作・dirty判定は、
[GarageBand検証記録](au_vab_garageband_20261031.md)を参照する。

## 移植する順序

| 段階 | Web版でできること | pluginで進めること |
| --- | --- | --- |
| 実装済み：演奏controller | ±2半音Pitch、Sustainによる離鍵保留 | チャンネル別MIDI Pitch Bend / CC64。発音中・Release中・新規音への反映、sample offset、交換cacheを検証済み。実UI交換中の追試は残る。 |
| 実装済み：ADSR | rate/modeの数値編集、A/D/S/Rのドラッグ、保持音への適用切替 | 元Toneを保つCustom編集、現在level/sample位置からの更新、次Noteからの更新、host parameter / state2を実装・検証済み。実DAW automation録音等は残る。 |
| 実装済み：SPU Reverb | preset、Enable、左右return。preset選択時に自動Enable | 共有preset/returnとAll notes / Tone send、state3を実装。Off・bypass・残響・保存互換を実AU/VST3で検証。実聴・DAW automation録音等は残る。 |
| 実装済み：PCM | 112-sample周期の4形状・自由描画 | 単一canvasのdraft / Apply / Revertとschema4の元PCM保存を追加。workerで循環ADPCMを準備し、交換時に保持音・残響を停止する。実AU/VST3・Standalone操作を検証、実DAW独自PCM操作は残る。 |
| 実装済み：Noise | Noise clock、Sample切替 | 共有Clock0〜63のlive変更、元音源・draft保持、19params/state5と旧形式互換。実AU/VST3とStandaloneを検証。実DAW独自操作等は残る。 |
| 実装済み：PMON | 変調波形・Ratio・Depth・Decay | 隣接voiceの12組、4変調波形、live Ratio / Depth、次NoteのDecay、24params / state6を追加した。元音源保持・Noise優先・旧state互換をcore / 実AU・VST3で検証。Standalone操作・installed AU更新・CIも完了。実聴・実DAW automation録音等は残る。 |
| 実装済み：Software Morph | A/B/Resultを同じcanvasへ表示し、編集対象を切替。保持音を再発音せずblend変更 | 元sourceとA/B原本を保つ26params / state7を実装。endpointはdraft / Apply、Amountだけlive。初期Off、A=Sine / B=Saw（振幅0.8）、Amount0。Offは元sourceへ戻す。PMON併用・Noise優先・実AU/VST3・Standalone・旧state復元・CIを検証。実聴・実DAW独自UIとautomation録音は残る。 |
| 音色・素材の入力 | `.spu.json`、audio、PSF1/MiniPSFからの静的VAB抽出、VH+VB | 分割VH/VBとpreset互換から始め、sample stateを設計してAudio・PSF抽出へ広げる。C++の明示VH/VB parserは既にあるが、file pickerはcomplete VABのみ。既知のNative loop flag制限も別途修正・検証する。 |
| monitor・演奏UI | Envelope/Waveform/Spectrum、Guitar/Bass/Ukulele、Octave、Pitch/Morph wheel、Master 200% | Morph AmountはSourceとkeyboard横へ実装済み。残るmonitor・Pitch wheel・Octave等はDAWの代替機能とStandaloneの利便性を考慮して追加する。 |

各段階で、音の変化だけでなく、演奏中の編集・保存直後の復元・不正入力時の現音源保持を確認する。
coreのoffline検証と実AU/VST3の検証を行い、実DAWのUI確認や実聴とは区別して記録する。
今後の編集parameterにも、host automationとstate保存の扱いを設計する。

## 今回のPitch Bend / Sustain

Web版のPitchとSustainは画面・PC keyboard向けのglobal操作である。
現Web `synth-spu`にはWeb MIDI入力の接続はない。
pluginでは同じ演奏感を参考に、DAWから受けるMIDIの16チャンネルごとに独立させる。
今回の範囲はMIDI接続で、画面のwheelやcheckbox追加は別の段階とする。

- Pitch Bendは14-bit、中心8192、初期範囲±2半音。元のNote/tuningから計算し、
  発音中とRelease tailを再発音せず変更する。wheel位置は次のNote Onにも適用する。
- SustainはCC64の64以上でOn。Offで離鍵済みの音だけReleaseし、押下中の音は保持する。
  Webでは同じkeyboard sourceを再押下すると保留中の旧音をReleaseして再発音する。
  pluginではMIDIの同音再発音、voice stealing、チャンネル分離も検証する。
- CC123、CC121、panic/overflow、reset/activate、host restoreの後にstuck noteを残さない。
  UIでの音源選択ではcontroller状態を引き継ぎ、host restoreとlifecycle resetでは初期化する。
- Bend/Sustainのlive値はpresetへ保存しない。音色の保存と演奏controllerの状態を分ける。

WebのPitch wheelはpointer release等で中心へ戻る。
DAWから届くMIDI Bendは、host/controllerが送る値に従う。

## SPU機能とsoftware機能の区別

ADSR、Gaussian補間、Noise、隣接voiceのPMON、共有reverbはSPU側の機能を使う。
Noise clockは全voice共通の質感変更で、NoteやBendによる音階追従ではない。
PMONは1音に2 voicesを使うため、通常の24音から12音になる。
再現度・近似の有無は、移植時の実装と検証結果に従って記録する。

通常PCMの波形描画は、softwareが循環ADPCMを作りSPU sampleとして再生する機能。
Software Morphは物理RAM外のprivate bank1へ112-sampleの16bit PCMを用意し、
SPUのGaussian / ADSR / PMON / Reverbを通す。元4bit ADPCMとのbit一致は約束せず、Offで元音源へ戻す。
長いAudioのhost PCMも、PS1の物理512 KiB RAM外を使うsoftware拡張である。
この区別はpluginの画面と説明にも引き継ぐ。

WebのAudio入力は短い録音をSPU ADPCMへ変換し、長い録音をSoftware audioへ切り替える。
pluginのschema7はBuiltin/VAB/PCM cycleの3種類を保ち、bank上限1 MiB、state上限1 MiB+1024 bytes。
Noise / PMON / Morphは元音源と独立した共有parameterとして保存する。旧schema1〜6も読み込む。
各Enabledが同時Onのhost tupleは保持し、実効modeはNoise優先にする。SampleではMorphとPMONを併用できる。
PCM cycleは112 Float32、448bytesの元波形を保持し、prepared ADPCMは保存しない。
MorphはA/B各112 Float32の原本とEnabled / Amountを保存する。旧stateはMorph Off / Amount0 / default A/Bへ移行する。
長いPCMを入れる際は、音源kind、version互換、容量上限、所有権、workerでの準備、
DAW project内のsample保持を改めて設計する。現在のVAB上限だけを増やして対応済みとはしない。

## DAWに合わせて扱うUIと対象外

録音・MIDI編集・transpose・monitoringはDAWにもある。
Guitar/Bass/Ukulele配列やOctaveはStandaloneや画面鍵盤には役立つため、
DAW側の代替機能と用途を見て追加する。WebのEnable audio、出力device選択、Pin、
browser focus対策は、pluginへそのまま移す操作とは扱わない。

Webでユーザーが不要とした**Morph用LFO**と**外部入力Reverb**は、移植TODOへ復活させない。
PSF2、曲再生、任意voice間の自由PMON配線、key zone/layer、game driver固有の動作も、
現Webの対応済み機能として約束しない。

## 比較したソース

親repoの確認基準は`4c65bef7e56ae0c8c6f1646554cd3b81fe07d75d`。
以下の対象fileに未commit差分がない状態で確認した。

- [Web README](https://github.com/kyorohiro/hello_ymfm_wasm/blob/4c65bef7e56ae0c8c6f1646554cd3b81fe07d75d/docs/synth-spu/README.md)：画面機能、Audio/preset、VAB/PSF、制約の説明。
- [synth.js](https://github.com/kyorohiro/hello_ymfm_wasm/blob/4c65bef7e56ae0c8c6f1646554cd3b81fe07d75d/docs/synth-spu/synth.js)：`press`、`release`、`applyBend`、Sustain切替。file入力とPSF抽出の接続。
- [spu_synth_worklet.js](https://github.com/kyorohiro/hello_ymfm_wasm/blob/4c65bef7e56ae0c8c6f1646554cd3b81fe07d75d/docs/synth-spu/spu_synth_worklet.js)：`setInstrumentPitchBend`、`activeNotes`、`pitchForSample`。Release tailへの再発音なしのPitch更新。
- [spu_synth_panel.js](https://github.com/kyorohiro/hello_ymfm_wasm/blob/4c65bef7e56ae0c8c6f1646554cd3b81fe07d75d/docs/synth-spu/spu_synth_panel.js)：`createSpuInstrumentPanel`。PCM/ADSR/FX、Noise、PMON、単一canvasのmorph。
- [spu_synth_instrument.js](https://github.com/kyorohiro/hello_ymfm_wasm/blob/4c65bef7e56ae0c8c6f1646554cd3b81fe07d75d/docs/synth-spu/spu_synth_instrument.js)の`readSpuInstrument`/`writeSpuInstrument`と、[spu_synth_long_audio.js](https://github.com/kyorohiro/hello_ymfm_wasm/blob/4c65bef7e56ae0c8c6f1646554cd3b81fe07d75d/docs/synth-spu/spu_synth_long_audio.js)の`prepareHostAudio`：presetとSoftware audio。
- [音作りTODO](https://github.com/kyorohiro/hello_ymfm_wasm/blob/4c65bef7e56ae0c8c6f1646554cd3b81fe07d75d/docs/issues/todo_spu_synth_features_01.md)：PMON/Noise/morphの実装済み範囲と、Morph LFO・外部入力Reverbの除外。

plugin側の比較対象は[SpuInstrument](../src/SpuInstrument.h)、
[InstrumentState](../src/InstrumentState.h)、[InstrumentUI](../plugin/InstrumentUI.cpp)。
この計画は機能の対応表であり、Webとpluginの全出力がbit単位で一致するという保証ではない。
