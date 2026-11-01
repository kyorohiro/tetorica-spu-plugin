# loopfix版AU更新・GarageBand旧project復元（2026-11-01）

**後続更新:** [Gain版AU更新・旧project復元](au_gain_update_20261101.md)を完了した。
現在のinstalled manifestは[Gain版記録](au_gain_update_20261101.json)。以下はloopfix版配置時の履歴。

ADPCMループ互換修正済みのAUを手元へ配置し、GarageBandの旧VAB音色を新しい検証copyで
保存・完全終了・再起動して、Program 3 / Tone 0 / Gain 99%の復元を確認した。
installed AUの`auval`と実AU hostの全回帰も成功した。

実装は`78e0b83`、Native SDKは`95cf71a`。作業前TODOは`67a547d`。
今回は既存buildの配置と検証・記録を行い、音源コードやparameter ID / state schemaは変更していない。
[実装修正の記録](loop_repeat_20261101.md)と、今回の[全file manifest・証跡JSON](au_loop_update_20261101.json)を参照。

## AUの配置と退避

GarageBandで開いていた既知の検証copyを保存前に別途退避し、保存・host終了を確認してから更新した。
更新前の全5filesは[Software audio manifest](software_audio_20261101.json)と一致。

```sh
python3 scripts/install_au.py --update \
  --expected docs/software_audio_20261101.json --backup-root build/au-backups
```

配置先は`~/Library/Audio/Plug-Ins/Components/TetoricaSPU.component`。
新sourceとinstalledの全5files / 2,056,401bytesが一致した。
旧bundleのコピー`previous.component`と退避原本`original.component`も、旧manifestの全5filesと一致した。

| 対象binary | SHA256 |
| --- | --- |
| 更新前 / 退避2組 | `b46c3150a8c1b5a0c22ce41fce78bc04d38b39a8442f99392bf03ed5d585ea7f` |
| 新build / installed | `60a6c1967f4fb56bb54f89a24f33769920b331f28ad8024969c9c861b805c368` |

journalは`build/au-backups/au-update-igtc8_ol/manifest.json`、状態は`installed`。
必要時はaudio hostを保存・終了してから、次の条件付きrollbackを実行する。
今回rollbackは実行していない。

```sh
python3 scripts/install_au.py --rollback build/au-backups/au-update-igtc8_ol/manifest.json
```

以後の更新で「現在の配置済みAU」を照合する場合は、
`--expected docs/au_loop_update_20261101.json`を使う。
rollbackに必要なjournalと退避bundleはローカルの`build/au-backups`にあるため、必要な間は保持する。

## 配置済みAUの自動検証

```sh
python3 scripts/validate_au.py
build/macos/tetorica_au_state_check \
  "$HOME/Library/Audio/Plug-Ins/Components/TetoricaSPU.component"
```

- `auval`: exit 0、26parameters、`AU VALIDATION SUCCEEDED.`。
  既知の非致命的warning `Preset name is not retained in retrieved class data`は残る。
  ローカルregistryの強制更新は行っていない。
- installed bundleを読む実host: exit 0、48kHzのMIDI / ADSR / Reverb / PCM / Noise /
  PMON / Morph / Web音色 / WAV / 20MiB Software audio / LoopRepeatを含む全13 PASS行。
- LoopRepeat: VAB/rawの4条件、flag4-only / markerless / 非ゼロrepeat先、元bytes維持、
  比較用flag6/2とのPCM完全一致、END後の継続、埋め込みstate再読込、Web元flagsを確認した。

これはaudio deviceやeditorを開かない実AUの検査。実聴や独自Load sound UIの確認は別項目。
production実装のoffline / macOS / sanitizer / VST3 / CI成功は[前回記録](loop_repeat_20261101.json)を参照。
今回はproduction sourceを変更しておらず、全build・全CIを再実行していない。

## GarageBandの保存・再起動

作業ディレクトリは`build/au-loop-update-nu3dlwkx`。
既知の`build/spu-vab-check.band`を`spu-loop-restore-check.band`へ複製し、そのcopyだけで検証した。
読み込み直後と保存・完全終了・再起動後のAU画面で、次を確認した。

- Embedded VAB / Program 3 / Tone 0 / Gain 99.0% / Project sound restored。
- Reverb Off、Morph Off。再生・録音は停止状態。
- 保存payloadはschema9 / 4,768bytesで完全一致。
  SHA256: `35a6b2cd76ff553bd121a87b5a46513cbb78b953da1f673cedd863447fb33e1c`。
- 埋め込みVABは3,744bytesで元fixtureと完全一致。
  SHA256: `71a7de205c0ebe1b8743288d90cd3b0204da9bd81aed9159af31f404d7cf2be6`。
- 原本`build/spu-au-check.band`の11filesと、複製元`build/spu-vab-check.band`の7filesは全件不変。

read-only inspectorは保存XML内のAU候補と埋め込みbytesを確認するもの。
active instrumentの復元は、別途GarageBand再起動後の実AU画面で確認した。
`restored-before-save.png` / `restored-after-reopen.png`とAX記録のhashをJSONへ保存している。
AU editorは新しい検証copy上で開いたままにしてある。

開始時に開いていた`build/software-garageband-r9kkkh93/spu-software-check.band`も既知の旧VAB検証copy。
保存前の全file退避は`pre-save-open-project.band`へ保持した。
このcopyは保存に伴いproject / UI / autosave filesが変わったが、埋め込みpayloadは一致した。
名前にsoftwareとあるが、今回そのprojectへ長いAudioを読み込んだわけではない。

## 未確認・次の候補

今回GarageBandで復元したのは旧demo VAB。
新flag4素材やWAV / Web v2を独自Load sound UIから読み、編集・保存・元fileなし再起動する操作は
[人のTODO](issues/TODO.md)に残す。新loop素材そのものは上のinstalled AU hostで検証した。
実聴、loop seam・クリック・latency、長時間再生、automation録音、Toneのみのdirty判定、
popup / file dialog / editor lifecycleも未確認。

次のコード候補はGain smoothing。分割VH/VB UIと合わせて未実装TODOとして維持した。
version / tag / release、npm / Web / Tauri配布は今回変更していない。
ログ・画面・project copy・rollback bundleはignored `build/`内のローカル証跡で、今回のdocs commitには含めない。
