# 音源準備状態の表示改善（2026-11-02）

停止中のGarageBandで音源準備が終わっても、上段がReady、下段がPreparingのままになる表示を改善した。
作業前TODOは`05a3772`、実装は`6cac7e8791acf1f4837a3f7e660f73fd034e90e7`。

- `Preparing...`: workerで音源を準備中。
- `Ready`: 準備完了、hostの次の音声処理を待機。AU/VST3の上段は`Ready - start DAW playback to apply.`。説明にはApplyを再度押す必要がないことを示す。
- `Loaded`: 新しい音源と必要な設定を反映済み。

PCM /MorphのApplyボタン・説明文を同じ状態へ揃え、VAB /Web /WAVの上段にもReady案内を使う。
StandaloneはDAW再生ではなくaudio settingsを案内する。
Readyは既存の世代・採用markerから表示するだけで、host停止の検出やaudio開始は行わない。
Web /WAVはengine採用だけでLoadedにせず、import設定tupleの適用完了も待つ。
音源処理・queue・parameter27個・schema11・SDK pinは維持した。

## 検証

- 実`VabLoader` /`SpuEngineExchange`を使い、audio callbackなしでPCM準備→Ready維持→最初の`beginBlock`でLoadedを検証した。
- Morph、要求の置き換え、失敗、restore、import設定tupleの未反映でも誤ったReady /Loadedを残さないことを検証した。
- macOS APP /AU /VST3をbuild。最終macOS関連CTest3/3、offline関連3/3、変更対象のASan/UBSan1/1が成功した。
- Standalone実画面でTriangleの自動反映と上段 /ApplyボタンのLoadedを確認した。音は鳴らさずGain0で操作した。
- 更新済みAUのauval成功（27params、preset-name warningなし）、実AU host22 PASSが成功した。
- [Verify SPU Plugin全2 jobs](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41412026089)と[Build Preview Candidates全3 jobs](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41412026049)は同じ実装SHAで成功。Windows、Mac ARM /Intelの配布物に対する検証を含む。source・hash・CIは[JSON記録](ready_status_20261102.json)を参照。

実AUの独自UIへのAIクリックは届かないため、新Ready文言を停止中の実画面で保持する操作は未確認。
前回は人のTriangle操作で停止中Ready→DAW再生後Loadedを確認済みで、今回その状態を実workerテストでも再現した。
今回のGarageBandでは既存PCM /380%の復元を確認し、再生・録音は停止したまま。実聴やautomation録音は実施していない。

## 手元AUの配置と復元

`docs/au_gain380_update_20261102.json`の全5filesと既存AUを照合し、GarageBandの検証copyを11files退避して保存・正常終了した。
installerは旧AUを2コピー保持して更新した。配置先はuser-onlyの`~/Library/Audio/Plug-Ins/Components/TetoricaSPU.component`。
[新しい全file manifest](ready_status_20261102.json)が次回更新のexpectedになる。

```sh
python3 scripts/install_au.py --update \
  --expected docs/ready_status_20261102.json --backup-root build/au-backups
# 今回の更新を戻す場合（全hostを終了して実行）:
python3 scripts/install_au.py --rollback build/au-backups/au-update-cdgu7f00/manifest.json
```

今回のjournalは`build/au-backups/au-update-cdgu7f00/manifest.json`。
GarageBandは`build/au-gain380-update-tqa8b91a/spu-gain380-restore-check.band`を再起動し、実AU画面でEmbedded PCM cycle /380% orange /Project sound restoredを確認した。
保存済みschema11 payloadと112-frame Triangle PCMのSHA256は更新前後で一致した。プロジェクト全体のfile hashは保存により変わり得る。
更新前コピーは`build/ready-status-20261102/before-status-update.band`に保持した。元VAB検証projectへの書き込みは行っていない。

次はAuto apply Offの描画・手動Apply、Morph /Reverbの実DAW操作とautomation録音を確認する。
実聴・長時間使用、v0.0.1 Preview公開も別TODO。今回公開済みv0.0.0 /tag /assetsは変更していない。
