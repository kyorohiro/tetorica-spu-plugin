# Gain 380%版AU更新・GarageBand復元（2026-11-02）

Gain 380% /時刻付きautomation /PCM Auto apply版のAUを手元へ退避更新した。
旧VABのP3/T0/Gain99をGarageBandで保存・完全終了・再起動し、schema11へ移行した状態と元bankの保持を確認した。
作業前TODOは`e4d8967`、配置した音源実装は`850a79f`。今回source /SDK pin /version /tag /公開Releaseは変更しない。
[current installed manifest・hash・検証JSON](au_gain380_update_20261102.json)を参照。

## 配置・退避・戻し方

旧AUの全5filesは`docs/preset_split_vab_20261101.json`と一致し、最新版buildはGain380の検証記録と一致した。
installerがhost終了、全file照合、旧bundleの退避2組、staging、更新journalを確認して配置した。

```sh
python3 scripts/install_au.py --update \
  --expected docs/preset_split_vab_20261101.json --backup-root build/au-backups
```

配置先は`~/Library/Audio/Plug-Ins/Components/TetoricaSPU.component`。
journalは`build/au-backups/au-update-zs33cpnr/manifest.json`、状態は`installed`。
sourceとinstalledの全5files、`previous.component` /`original.component`と旧manifestの全5filesは、それぞれ一致した。

| AU binary | SHA256 |
| --- | --- |
| 更新前・退避2組 | `3f82bcff46f182c2bd370f92516759796aa72aa8d4cf3474716376037d94aa81` |
| 更新後source /installed | `3137685f0a0b5b481ebf39a9afbf4f83a3f6823a42aa1212dd67dede0f26bc12` |

必要時はaudio hostを保存・終了し、次の条件付きrollbackを使う。今回rollbackは実行していない。

```sh
python3 scripts/install_au.py --rollback build/au-backups/au-update-zs33cpnr/manifest.json
```

**次の更新で現在の配置を照合するexpectedは`docs/au_gain380_update_20261102.json`。**
旧projectの元copyも保持する。schema11の新しい保存データは古いbuildで読み戻せない。

## 配置済みAUの自動検証

- `python3 scripts/validate_au.py`はexit0、27 parameters、`AU VALIDATION SUCCEEDED.`。preset-name warningは発生しなかった。
- 配置済みbundleを指定した実AU hostはexit0、summaryを含む22 PASS行。
- Gain380 /Boost保存・旧schema10移行、5ms smoothing、複数point /未来carry /同offset、Morph /Reverb分割一致、AU Schedule rampと不正batch拒否が成功した。
- MIDI /ADSR /Noise /PMON /Software audio /Web音色 /LoopRepeat /preset名の既存回帰も成功した。

これは実AUのoffline PCM検証であり、GarageBandでのautomation録音や実聴とは区別する。
実装source自体のunit /sanitizer /VST3 /全CI成功は[前回記録](gain_380_automation_pcm_20261102.md)を参照。

## GarageBandの保存・完全終了・再起動

`build/spu-vab-check.band`を新しい`build/au-gain380-update-tqa8b91a/spu-gain380-restore-check.band`へ複製し、そのcopyで検証した。
初回起動時にGarageBandが前回の`spu-preset-split-restore-check.band`を自動復元したため、保存前に全11filesを`pre-save-reopened-project.band`へ退避してから保存・切り替えた。
自動復元に伴う4つのAutosave項目は開始時点から増えているが、その退避内のschema9 VAB payloadは元と一致した。
保護原本`build/spu-au-check.band`の11filesと、複製元`build/spu-vab-check.band`の7filesは不変だった。

新copyの実AU画面で、Embedded VAB /Program 3 /Tone 0 /Gain99.0% /Project sound restoredを確認した。
保存後にGarageBand本体プロセスの終了を確認し、再起動した同じproject URLと実AU画面でも同じ値を確認した。再生・録音は停止している。

| 保存状態 | bytes | SHA256 |
| --- | ---: | --- |
| 変更前schema9 payload | 4768 | `35a6b2cd76ff553bd121a87b5a46513cbb78b953da1f673cedd863447fb33e1c` |
| 保存・終了・再起動後schema11 payload | 4776 | `565c252cbac9e64375f4b98308f8f423925f9ad120d6c7babdee2e4b09baa541` |
| 各段階の埋め込みVAB | 3744 | `71a7de205c0ebe1b8743288d90cd3b0204da9bd81aed9159af31f404d7cf2be6` |

schema11はbase Gain99 /Boost1 /実効99%を保持し、保存後・終了後・再起動後のpayloadは完全一致した。
feature操作前の復元済みcopyを`restored-before-feature-check.band`へ別途保存した。
XML inspectorは候補bytesの確認で、active trackの証明とは扱わない。実画面とproject URLの確認を併用している。

## 実AU editorの操作

AU独自画面へのAIクリックではSourceが切り替わらず、前面化と長押しclickでも同じだった。
人がSourceを押すと切り替わることをユーザーが報告し、続く実画面でもSourceページを確認した。
Auto applyは既定ONで、editorを開いてSourceへ移っただけでは元のEmbedded VABを保持していた。
人のShape操作でTriangleを選ぶとworker準備が完了し、GarageBand停止中は`Ready. Waiting for an audio block.`で待機した。
人のGain操作で380.0% orangeも確認した。Apply PCMは押していない。
DAW masterを一時0へ下げて既存MIDIを再生すると、実AU画面が`Loaded. Play MIDI or the keyboard.`へ変わり、Triangle /380% orange /Apply PCM無効を確認した。
再生と録音の停止を確認し、masterを元と同じ+0.0dB表示へ戻した。AXの正規化値は開始0.708661437から設定後0.711911023へ量子化されており、exactな元値へ戻ったとは扱わない。

Triangle /Gain380のprojectも保存・完全終了・再起動し、実AU画面でEmbedded PCM cycle /380% orange /Project sound restoredを確認した。
保存後・終了後・再起動後のschema11 payloadは各1480 bytes、SHA256 `bd6756f7001539754c1bb84f21edc93ea44b43a43057c5afbc14b0bf436e8a8b`で一致した。
112-frame PCMのSHA256も各`217f71d94347fc5a2408e063f46e65be74bf4fbe6052c09160ff1ffd270853c5`で一致し、base Gain100 /Boost3.8 /実効380%を保持した。
feature用copyは`spu-gain380-triangle380-check.band`、旧VAB復元済みcopyは`restored-before-feature-check.band`へ保持している。

音声処理待ちの間、上段はReadyでもApplyボタンと下段がPreparingのままで区別しにくい。
worker準備中とhost音声処理待ちを分け、停止中のGarageBandではDAW再生で反映することを案内する改善をTODOへ追加した。今回は表示や音源コードを変更していない。

最後はTriangle /Gain380 /Reverb Off /Morph Offの検証copyを表示し、再生・録音停止で残した。
実AUでのAuto apply Offの描画・手動Apply、Morph /Reverbの独自UIとautomation描画・録音、実deviceの聴感・長時間は残る確認項目とする。
ログ・project copy・rollback bundleはignored `build/`へ保持し、公開配布やWeb側の更新は行わない。
