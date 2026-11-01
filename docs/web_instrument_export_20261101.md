# Web SPU instrument export — 2026-11-01

開始は`b8c2e3b`、実装前TODOを`c84dfe3`へcommit / pushした。
この記録は検証中。完了した範囲と未確認範囲を追記する。

## Save Web

共通ヘッダーの **Save Web...** で、現在保存対象の音色を`.spu.json` version1へ書き出す。
Builtin、選択中VAB Tone、112PCM、import済みADPCMが対象。
選択中のToneだけを持ち出す形式で、元VAB bankやDAW projectのbackupではない。
未Apply draft、現在の保持音・残響、MIDI controller状態は保存しない。

buttonを押した時点のcoherent saved snapshotを捕捉する。
JSON生成とfile I/Oは専用workerで行い、音源・26parameter・正常pendingには触れない。
Native Save dialogで保存先を選び、同じfilesystemの一時fileを完全に書いてからatomic replaceする。
cancel / 不正path / 書込失敗では既存destinationを変更しない。
editor close / host restore / 別sourceによって、未承認の保存dialog・準備を取り消す。
既にSaveを受け付けたfile書込はeditor close後も完了でき、instance破棄時はworkerをjoinする。
書き出しの説明はstatusのtooltipに表示する。

## Web形式へ渡す値

元ADPCM、root、loop、signed左右volumeを保持する。
Custom ADSRは元ADSR2のreserved bit13を保持して実効2wordsへ変換する。
Reverb sendはAll notes / Tone設定を解決し、Webの単一voice sendへ格納する。
Reverbがbypassならpreset Offへ変換する。return値は保持し、休止中の選択presetは復元しない。

NoiseとPMON両flagがOnの場合はNoiseを優先し、Webが拒否するPMON EnabledだけをOffにする。
Noise中のMorph EnabledもOffにし、AmountとA/Bは保持する。
Morphが実効Onの場合は現在blendの112Float32 PCMを循環ADPCMへ変換し、
A/B/Amount、root44100/112、loop0とともに保存する。元のVAB等はfileに含まれない。
WebでMorphをOffにしても、保存前の元sourceへ戻るとは約束しない。

plugin GainはWebのinstrumentに対応するfieldがないため保存せず、volumeへ焼き込まない。
Gain0でも書き出した音色はWebで発音し得る。Web Masterと音量校正が異なるため、再生音量は調整する。
pluginの16bit Morph / PMONとWebの4bit ADPCMでは音が完全一致する保証はない。
元PCM付きlegacy ADPCMはfileでは保持するが、Web UIのloadではcanonical再encodeされ得る。

Webのfresh起動時にはeditor slotsがRAMを予約するため、通常ADPCMの書き出し上限は
518656bytes、実効PMON時518592bytes。超過は保存準備時に明示エラーにする。
元PCMはWebが予約済みslotを使う。以前のsampleでWeb RAMが埋まっている場合はreloadが必要。
JSONは既存strict importerでself-readし、8MiB以内・Web v1の値域と整合性を確認する。

## 維持する範囲

DAW stateはschema8、26parameterのIDと初期値も維持する。
音声処理の変更はBuiltinの固定ADPCM定数をpure helperと共有する整理だけ。
Native SDK / Web / Tauri配布物、長いAudio v2、VH/VB picker、PSF抽出、
Morph LFO、外部入力Reverb、version/tag/release公開は今回変更しない。

## 検証の進捗

- pure source helperの全4Kinds・元bytes/metadata独立所有とstrict compile成功。
- 初期Sine WAV `/private/tmp/tetorica-spu-export-demo.wav` は従来SHA256 `7a61b56d356ac198c35808b503cf0b61b55a9547b21770eb2a67c97d0cc4aef3`と一致。
- pure writerの20-case matrixとstrict compile成功。Builtin/VAB/PCM/Raw、Morph、Noise優先、ADSR、Reverb、Gain非bake、RAM境界と不正入力を検証した。
- Offline全17成功（7.63s）、macOS全17成功（7.96s）、ASan全17成功（59.88s）。pure module / 新3testsのstrict検査も成功した。
- 実Web Node reader/writer20件、Chromium20件、WebKit20件の公開file load→実engine→Save download round tripを検証した。physical outputはgain0でmute、page exceptionなし。legacy PCMだけWeb仕様どおりcanonical bytesへ変わり、PCMと設定は保持した。
- StandaloneのSave Webから`build/web-export-ui-yv0v6i7n/standalone.spu.json`を生成し、実Web readerでBuiltin Sine / v1 / 64bytes / volume各1を確認した。2回目のSave dialogをCancelし、BuiltinとGain100%保持・Web save cancelled表示を確認して終了した。
- 実AU48k、実VST3 44.1 / 48 / 96kの既存全suite、公式VST3 validator47/0成功。CIとinstalled AU更新は継続中。
- 実聴・長時間使用・DAW独自Save Web UIは未確認。

更新前installed AU manifestは[import記録](web_instrument_import_20261101.json)。
この時点ではinstalled AUの更新は行っていない。
