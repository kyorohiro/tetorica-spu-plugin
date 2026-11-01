# Gain smoothing版AU更新・GarageBand復元（2026-11-01）

Gain smoothing版AUを手元へ配置し、GarageBandの旧VAB音色を新しい検証copyで
保存・完全終了・再起動して、Program 3 / Tone 0 / Gain 99%の復元を確認した。
配置済みAUのauvalと、GainのPCM比較を含む実AU hostの全回帰も成功した。

実装は`3ec6049`、Native SDKは`95cf71a`、作業前TODOは`620da19`。
今回は既存buildの配置・検証・記録のみで、source /parameter ID /state形式は変更していない。
[Gainの仕様・実装検証](gain_smoothing_20261101.md)と[全file manifest・証跡JSON](au_gain_update_20261101.json)を参照。

## 配置・退避・戻し方

更新前のinstalled全5filesは[loopfix版manifest](au_loop_update_20261101.json)、
新build全5filesは[Gain版build記録](gain_smoothing_20261101.json)とbytes /SHAが一致した。
GarageBandの既知検証copyを保存前に退避し、保存・host終了を確認してから更新した。

```sh
python3 scripts/install_au.py --update \
  --expected docs/au_loop_update_20261101.json --backup-root build/au-backups
```

配置先は`~/Library/Audio/Plug-Ins/Components/TetoricaSPU.component`。
新source /installedの各5files / 2,056,401bytesが一致した。
旧コピー`previous.component`と退避原本`original.component`も旧manifestの全5filesと一致した。

| 対象binary | SHA256 |
| --- | --- |
| 更新前 / 退避2組 | `60a6c1967f4fb56bb54f89a24f33769920b331f28ad8024969c9c861b805c368` |
| Gain版build / installed | `cd59d41bd23a6f97467f8bfbbe1cdfcf9a95aaaf557a5970b134e2214a4a461e` |

journalは`build/au-backups/au-update-dpj7avip/manifest.json`、状態は`installed`。
必要時はaudio hostを保存・終了してから、次の条件付きrollbackを実行する。
今回rollbackは実行していない。

```sh
python3 scripts/install_au.py --rollback build/au-backups/au-update-dpj7avip/manifest.json
```

次のAU更新で現在の配置済みbundleを照合する場合は、
`--expected docs/au_gain_update_20261101.json`を使う。
戻し方に必要なjournalと旧bundleはローカル`build/au-backups`に保持している。

## 配置済みAUの検証

```sh
python3 scripts/validate_au.py
build/macos/tetorica_au_state_check \
  "$HOME/Library/Audio/Plug-Ins/Components/TetoricaSPU.component"
```

- auval: exit 0、26parameters、`AU VALIDATION SUCCEEDED.`。
  既知の非致命的warning `Preset name is not retained in retrieved class data`は残る。
  ローカルregistryの強制更新は行っていない。
- 実installed bundleを読む48kHz host: exit 0、全14 PASS行。
  Gainの5ms直線補間を、同位相の100%参照PCMへ数式係数を掛けて左右全sampleで確認した。
- 上下変更 /途中変更 /同targetの非再開始 /可変block /MIDI分割、保存target、
  最初のrestore、失敗state時のramp保持、reset /reactivate /bypass後の音量同期も成功。
- 既存MIDI /ADSR /Reverb /PCM /Noise /PMON /Morph /Web /WAV /20MiB Software audio /
  LoopRepeatの回帰も成功した。

このhost検証はaudio deviceやeditorを開かない。物理的な実聴やDAW独自Load操作の確認は別項目。
production実装のunit /sanitizer /VST3 /CI成功は[前回記録](gain_smoothing_20261101.json)を参照。
今回はsourceを変更しておらず、全build・全CIの再実行はしていない。

## GarageBandの保存・完全終了・再起動

作業dirは`build/au-gain-update-8o13a8ay`。
known source `build/spu-vab-check.band`を`spu-gain-restore-check.band`へ複製し、その新copyで検証した。
読み込み直後と保存・完全終了・再起動後の実AU画面で、
Embedded VAB / Program 3 / Tone 0 / Gain 99.0% / Project sound restored、Reverb Off /Morph Offを確認した。
main windowのproject URLと再生・録音停止も照合した。

- schema9の保存payloadは4,768bytesで完全一致。
  SHA256: `35a6b2cd76ff553bd121a87b5a46513cbb78b953da1f673cedd863447fb33e1c`。
- 埋め込みVABは3,744bytesで元fixtureと完全一致。
  SHA256: `71a7de205c0ebe1b8743288d90cd3b0204da9bd81aed9159af31f404d7cf2be6`。
- 保護原本`build/spu-au-check.band`の11files、複製元`build/spu-vab-check.band`の7filesは全件不変。

read-only inspectorは保存XML内のAU候補とbytesを検査するもので、active bindingの解析器ではない。
active instrumentの復元は、別途GarageBand再起動後の実AU画面で確認した。
`restored-before-save.png` /`restored-after-reopen.png`とAX記録のhashをJSONへ保存した。
新copyのAU editorは開いたまま、再生・録音停止状態で残している。

開始時に開いていた前回の`build/au-loop-update-nu3dlwkx/spu-loop-restore-check.band`も、
保存前に`pre-save-open-project.band`へ全file退避した。
この既知copyは意図的に保存したためproject /UI /autosave filesは変わり得るが、埋め込みpayloadは一致した。
original projectやknown sourceを保存・上書きしたわけではない。

## 残す確認項目

人によるGainの操作感・クリック・latency・長時間再生、実DAW automation録音、Toneのみのdirty判定、
editor /popup /file panelのlifecycleは[TODO](issues/TODO.md)に残す。
今回GarageBandで復元したのは旧demo VABであり、新loop /WAV /Web v2素材を独自Load soundから
読み込む検査や、実聴・録音は行っていない。

version /tag /release、npm /Web /Tauri配布は変更していない。
ログ・画面・project copy・rollback bundleはignored `build/`のローカル証跡で、docs commitには含めない。
