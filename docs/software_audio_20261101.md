# 長いAudio / Software PCM — 2026-11-01

実装 `a02ee74`。開始TODOは `cffffe0`、Native SDKは `997256e` に固定。
[機械検証・hash・更新manifest](software_audio_20261101.json)に、実行済みと未確認範囲を分けた。

## 使い方と対応範囲

**Load sound...** でPCM16 / Float32 mono/stereoのWAVを選ぶ。
短い録音は従来と同じADPCM、物理sample RAMを超える録音は自動で **Software audio** になる。
入力32MiB、保持するmono 44.1kHz PCM16も32MiB（16,777,216frames、約6分20秒）まで。
変換後が上限を超える場合は途中で切らず、元音色を保持してエラーにする。
20MiB /44.1kHz /mono PCM16は全データを保持できることを検証した。

初期Root C4、One shot。上段の **Root** で録音の基準音を、**Playback** で全体Loopを選ぶ。
Software audioのPCMにはADPCM packet paddingを追加しない。Loopは最低4frames必要。
Root / Playbackの変更はworkerで新音源を準備し、採用時に保持音と旧残響を止める。
DAW projectにはPCMとmetadataを埋め込むため、再開時に元WAVは不要。
Stereo downmix / rate変換 / PCM16化後の値を保持するもので、元ファイルのformatを保存する機能ではない。

Software audioは**PS1物理SPU RAM外のソフト拡張**。Gaussian / ADSR / PMON / ReverbはSPU coreを通す。
bank0をNoise/PMON、bank1を長PCMに使う。Sample24音、PMON12組。
この音源ではcycle Morphを使用できず、Sourceページに説明し、keyboard横は **N/A** にする。
A/B・Amount・raw host EnabledはDAW stateに保持するが、再生ではEnabledを無視する。

**Save Web...** は元PCM16をbase64で埋め込んだ `.spu.json` v2を保存する。
Gainを焼き込まず、Custom ADSR/sendは実効voice設定へ反映する。
masked MorphはOffに投影し、A/B・Amountを保持する。Noise優先のPMONも従来通り投影する。
v2は最大48MiB JSON /32MiB decoded PCMまでの対応で、Web版の30分上限全体には対応しない。
v1は従来8MiB JSON /520192bytes ADPCMを維持する。

## 保存と修正

- kind4 / schema9へ64-byte metadata＋PCM16LE＋UTF-8 nameを追加。固定1024bytes、既存26parameter IDsとschema1〜8 / legacy互換を維持。
- private immutable PCMを共有する。Root/Loop変更、A/B保持、UI snapshot、import commitの検証で音声全量をserialize/decodeしない。
- canonical base64、完全PCM16、入力/出力サイズ、unknown/duplicate key、depth/node、root範囲、loop最低frames、Morph/waveform不可を検証。
- Native SDKの長いPCM / PITCH1でevent距離が32bit overflowし即ENDになる不具合を修正。Native側はRelease / sanitizer / CI12検証成功。
- 最終レビューで、Software音源のraw Morph自動化時に持越しNoteOffを捨てるwrapper経路を修正。実効Morph状態で切替を判断する。
- GarageBand saved-state inspectorもschema9と32MiB PCMへ対応。PCMはbyte数/hashで扱い、巨大な数値配列を作らない。旧schemaのkind/cap制限を維持。

## 検証したこと

| 対象 | 結果 |
| --- | --- |
| Offline / macOS CTest | 各24/24。最終macOSは11.24秒 |
| ASan＋UBSan | 全24/24、125.35秒。最終core変更も1/1再実行成功。macOSでLeakSanitizerは未対応 |
| Python | installer11＋inspector6、計17成功 |
| 実AU / VST3 | AU48k、VST3 44.1/48/96k。20MiBの実host save/restoreは各host48k |
| 最終NoteOff回帰 | raw Morph両方向、block末尾NoteOff持越し→次block切替、release PCM完全一致と無音化 |
| auval / 公式VST3 validator | 成功 /47成功・0失敗 |
| 実Web reader/writer | small /20MiBでmetadataとPCM16LE一致 |
| Chromium / WebKit | 各small /20MiBの公開load→実engine→Saveが一致。出力gain0 |
| 初期Sine | 既存WAV SHA256完全一致。core callbackのC++ new/delete0 |

[Plugin CI 41271743574](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41271743574)は
exact source `a02ee740034d55962c5acb201303467138361996` でoffline /macosとも全step成功。
[Native CI 41269354131](https://github.com/kyorohiro/tetorica-soundchip-native/actions/runs/41269354131)も12検証成功。
CI artifactはmetadata/digestを取得した。downloadや配布releaseはしていない。

Standaloneでは20MiB WAVの読込、237.77秒 /10,485,738frames表示、Root C4→A4、全体Loop、Save Web v2を確認。
保存したPCMは元WAV dataと完全一致し、SHA256は
`d9b7a79dfeae60d6ede8154ed4fa14415ca54f6e3370434d5f88581857b8ca90`。
この操作は最終wrapper NoteOff /受付文言修正前の同一feature UIで行った。修正後の実host回帰は上表で別途確認した。
大きなWAV/JSONはbuild内で生成し、Gitには入れていない。

## AU更新とGarageBand

既知WAV版AUの5filesを照合・退避して更新。journal:
`build/au-backups/au-update-doadghp0/manifest.json`。
installed /workspaceの5files一致、old backup /archiveの5files一致を確認。
新binary SHA256は `b46c3150a8c1b5a0c22ce41fce78bc04d38b39a8442f99392bf03ed5d585ea7f`。
以後の更新は [今回JSON](software_audio_20261101.json)をexpected manifestとして使う。

GarageBandは検証copy `build/spu-vab-check.band` を保存・終了し、更新後に開いて
**Program3 /Tone0 /Gain99 /Project sound restored** を画面で確認した。
保存後の4768bytes payloadは、schema番号のbyte offset8だけが8→9へ変わり、他は完全一致。
VAB3744bytesと設定を保持した。元 `build/spu-au-check.band` の11filesは不変。
更新前copyは `build/software-garageband-backup-jozarrje/spu-vab-check.band` に退避した。

さらに `build/software-garageband-r9kkkh93/spu-software-check.band` を作り、
AUのLoad soundをAIで押したが、操作可能なfile panelを確認できなかった。
このcopyは**長Audioを読み込んだprojectではなく、VAB P3/T0/G99のまま**。
最後はAU editorを閉じてcopyを保存、録音0/再生0。Standaloneは終了した。
新機能のGarageBand直接load/save成功とは記録しない。

## 再開時

- GarageBand AUで人がLoad soundからWAV /Web v2を選び、Root/Loop・キャンセル/不正入力時の保持・保存/再起動を確認する。
- 元WAVを別の場所へ移した後の復元、実聴、loop seam、PMON/Reverb、長時間動作とautomation録音を確認する。
- Release判断はこれらの結果を見て行う。version/tag、npm、Web/Tauri配布物は今回変更していない。

```sh
build/offline/software_audio_loader_test --write-fixtures build/software-audio-cases
build/offline/software_pcm_web_test --write-fixtures build/software-audio-cases
node scripts/check_web_software_audio.mjs --web-root ../.. \
  --cases build/software-audio-cases --engines chromium,webkit \
  --out build/software-audio-web-report.json
```
