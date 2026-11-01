# Web SPU instrument import — 2026-11-01

開始は`883c3d7`、実装前TODOを`fd9ec5b`へcommit / pushした。
実装は`81eb78b947d9397f68ca0db70147b9bafebb4409`へ保存・push済み。
source / bundle / ログ / installed AU manifestは[検証JSON](web_instrument_import_20261101.json)。

## 操作と対応範囲

共通の **Load sound...** でcomplete `.vab`とWeb版の`.spu.json`を選ぶ。
Webのversion1（通常ADPCM、112-sample PCM、Software Morph、Noise、PMON）を読み込む。
version2の長いSoftware audioとWeb向け書き出しは今回追加しない。
`.json`という拡張子だけでは受け付けず、内部schemaと全内容も検証する。

元ADPCM・基準周波数・loop位置・符号付き左右volume・元ADSR全bits・sendを
immutable sourceへ保存する。nameとinspectorのraw pitchも保持する。
演奏の音程はWebのkeyboardと同じroot基準で、保存されたinspector pitchでは動かさない。
GainはWeb音色に含まれないため、読み込み採用時のplugin Gainを維持する。
左右volumeはplugin既存の音量校正を通す。Webとの出力PCM完全一致は約束しない。

Webで共有ReverbのpresetがOff以外ならEnable、sendは元voiceの設定を使う。
Noise / PMON / Morphは既存parameterに設定し、A/B原本も保存する。
Webの省略時Morph defaultsは振幅0.65のSine/Sawで、pluginの新規音色の0.8と区別する。

WebはMorph Onで現在blendのPCMとADPCMをsampleとして保存するため、import後の
Morph Offはその読み込み時sampleへ戻る。Web保存前のVABや前の音源は復元できない。
Morph Onはplugin既存の16bit private carrierを使い、A/B編集は明示Applyで反映する。
SPU実機回路とsoftware機能の区別、Noise優先、PMONの12音pairは維持する。

## 読み込みの境界

JSONは最大8MiB、深さ16、値数530000、ADPCM最大520192bytes。
duplicate / unknown keys、不正UTF8・surrogate・NUL名・非finite・値域・packet / END / loopを拒否する。
任意PCMがある場合は現行または旧Web encoderとのADPCM整合、Morph Onならblendとの整合も確認する。
nameはWeb同様80 UTF16 unitsまで。元nameを保持し、UIではASCII control文字を空白で表示する。

file読み込み・JSON解析・cache準備は専用workerで行う。
staging中はexchange generationを変えないため、不正fileは正常pendingも無効化しない。
準備完了をUI timerで受け、開始時のproject revision / generationが生きている場合だけ
engineと26parameterのtupleをpublicationする。採用は音声block境界でまとめて行う。
採用前にDAW保存しても元sampleと新設定を組にして保存する。
採用時に旧保持音・残響を止め、MIDI wheel / pedalの状態は引き継ぐ。
host restore / 新しいsource / editor closeで古いstagingを取り消す。

Native SDKのrepeat先packetにflag2がないloopは引き続き準備時に明示拒否する。
この制限を避けるためのADPCM書き換えやloop変更は行わない。

## DAW state

既存parameter ID0〜25とsource kind0〜2を維持し、schema8 / kind3 AdpcmSampleを追加する。
outer固定1024bytesと旧source payloadは不変。旧schema1〜7 / Gain-onlyも読む。
kind3 payloadは64-byte LE header、元ADPCM、UTF8 name、任意112Float32の順。

headerはversion1@0、flags(loop/reverb/waveform)@4、ADPCM長@8、name長@12、
root f64@16、左右volume f64@24/32、loopSample@40、packed ADSR@44、
inspector pitch@48、reserved0@52/56/60。
全metadataと元bytesを保存し、外部fileなしで復元する。

## 検証の進捗

- actual Web writer / readerから8 fixtureを生成。source hashと基準値は`tests/fixtures/web/reference.json`。
- Offline全14tests成功（6.96s）、macOS全14tests成功（6.99s）。
- ASan全14tests成功（55.12s）。UTF8パスと公開import commitの全tuple検証を含むloader / exchangeを再buildし、2/2成功（3.49s）。
- strict C++検査、Raw signed volumeのvelocity127 / 64 / 1、旧state1〜7、原本保持を検証した。
- 初期Sine WAV `/private/tmp/tetorica-spu-adpcm-demo.wav` は従来SHA256 `7a61b56d356ac198c35808b503cf0b61b55a9547b21770eb2a67c97d0cc4aef3`と一致。
- StandaloneでMorph fixtureのload / Loaded、Reverb Studio Small On / Return33・27%、PMON Triangle / Ratio1.5 / Depth0.25 / Hold、Amount37.5%（表示38%）、A/B/Resultの単一canvasを確認した。
- 同画面でv2 long audioを選択し、明示エラーと元Morph / Amount / 波形保持を確認した。
- 実AU48k / 実VST3 44.1・48・96kでWeb6fixtures、元metadata / ADPCM、最初のblock、左右極性、root、ADSR/send、旧schema7移行、不正stateのpending / active / 全26parameter保持と既存suiteを検証した。
- 左右極性oracleの初回は厳しすぎる1e-6で失敗し、実測差7.62939e-6がheadroom4のPCM16 1LSBと一致した。根拠付き2LSB以下へ変更し成功。productionの符号処理は変更していない。
- VST3 malformed試験の未知versionに8の直書きが残り、新対応schema8を不正と誤判定した。current schema+1へ修正し成功。旧state互換の退行ではない。
- importの26parameter書込をepochで囲み、後続source publicationと保存が重なっても途中tupleを保存しない。immutable pending tupleはそのまま保存でき、通常parameter captureは16回まで安定epochで再試行する。audioは待機せず、再試行上限なら保存を失敗として返す。独立reviewでも報告した競合経路の解消を確認した。
- 公式VST3 validator47成功、0失敗。最終StandaloneでNoise fixtureのLoaded / Noise clock8 / Gain100%保持を確認し、検証後終了した。
- [CI](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41255456421)はproduction commitのmacOS / offline両job・全step成功。run / job / step / artifact metadataを保存し、artifact自体はdownload・別環境実行していない。
- installed AUをknown hash照合・退避付きで更新し、auval成功、26parameterを確認した。既知のpreset-name warningは残る。
- GarageBand旧schema7を開き、Embedded VAB / Program 3 / Tone 0 / Gain99% / Project sound restored / Load sound表示とReverb Off / Return各25% / Morph Offを確認した。再生・録音は停止、更新後再保存はしていない。
- saved payload4768bytesのSHA256 `22485a18acb3cded2ec6929fa97cb3c7acea42f31977f4ab0dbf2bdee8a68a5f`、VAB3744bytesは更新前と同一。原本11filesも不変。
- 実聴・長時間使用・実DAWでの独自Load sound操作とautomation録音は未確認。

## AU更新と再開

更新前installed AUのknown manifestは[Morph記録](morph_20261101.json)。旧AUで検証用copyを保存・終了し、
project全7filesを`build/web-import-update-project-7mhm7feh/spu-vab-check.band`へ退避してhash照合した。
現在のinstalled AU manifestは[検証JSONのau.file_sha256](web_instrument_import_20261101.json)。
binary SHA256は`9ce3c1efba6a7fafc2121f699d14dc5bbf2c2692bd933296842c6f5ffeaa9088`。
source / installed全5filesはnew manifest、previous / original退避はold manifestと一致する。
更新journalは`build/au-backups/au-update-bccipufg/manifest.json`。

rollbackが必要な場合は全audio hostを終了し、現在のAUがnew manifestと一致する状態で実行する。
今回rollbackとローカルregistrarの強制終了は行っていない。

```sh
python3 scripts/install_au.py --rollback build/au-backups/au-update-bccipufg/manifest.json
```

実聴・長時間・DAW独自file操作 / automationはTODOへ残す。Web向けexportは、
Morphの元sourceと現在blend、Noise+PMON raw flagsなどの形式差を設計してから進める。
Native SDK / Web / Tauri、version / tag / release公開は今回変更していない。
