# AU更新・GarageBand VAB確認 — 2026-10-31

開始TODO `b1836b2` をorigin/mainへ保存してから着手した。
VAB UI実装は`80f895c`、そのCIと記録は`f312638`。

## 原本と未保存状態の保持

- GarageBandは`build/spu-au-check.band`を開いており、録音・再生停止、Gain99%。終了時に未保存の確認が出たため取消した。
- disk上のproject全11 fileを`build/spu-au-original-20261031.band`へcopyし、
  `build/spu-au-original-manifest.json`へhashを保存した。
- GarageBandの「別名で保存」で、未保存状態を`build/spu-vab-check.band`へ保存した。
  copy側の終了時にも保存を選び、正常終了した。
- Save Asで原本のAutosave補助file4件が清掃されたため、内容が変わったfileがないことと
  missingがAutosave内だけであることを検査し、verified backupから不足分だけを戻した。
  原本全11 fileのhashが開始時のmanifestと一致することを確認した。
- 今後のGarageBand操作は`spu-vab-check.band`だけで行う。
  前回の録音・書出し原本や、ユーザーの別projectは操作しない。

## 更新と検証

installerと更新は完了。GarageBandのVAB操作はこれから確認する。

- `scripts/install_au.py`に明示的な`--update`と条件付き`--rollback`を追加した。
  通常のinspect / `--install`は異なる既存bundleを引き続き拒否する。
- 一時directoryで11テスト成功。未知の内容・symlink・staging中の変更・rename失敗と復旧・rollbackを検査した。
  Linux / macOS CIにも同じテストを追加した。
- 全host停止後、次のコマンドをsandbox外で実行し、installed stateで成功した。

  ```sh
  python3 scripts/install_au.py --update \
    --expected docs/au_garageband_20261031.json --backup-root build/au-backups
  ```

- update report: `build/au-backups/au-update-htc4qhrw/manifest.json`。
  `previous.component`は検証済み旧copy、`original.component`はrenameして保持した旧原本。
  どちらも前回manifest全5 fileと一致する。配置済み新版もsource全5 fileと一致する。
- 旧binary SHA-256 `63f5db3316aebaa08d66937fa4342c4bdf2f7440f20420f02ef1c1ef681da625`、
  新binary `af3da1d43f73a86d6008a3bb1c98a363d104dfea5d0db367d9467db2d9a04bb5`。
- `python3 scripts/validate_au.py`はsandbox内で発見できず、validation未実行で終了した。
  承認済みsandbox外の再実行では`AU VALIDATION SUCCEEDED.`。
  ローカルのregistrar・セキュリティ設定は変更していない。

復帰が必要な場合はhostを保存・終了し、配置先が今回の新版と一致することを確認して実行する。

```sh
python3 scripts/install_au.py --rollback build/au-backups/au-update-htc4qhrw/manifest.json
```

実環境のrollbackは実行していない（temporary directoryでテスト済み）。
更新後に第三者が配置先を変更した場合はrollbackを拒否する。

- 現installed AUは前回`au_garageband_20261031.json`の全5 file manifestと一致する場合だけ更新する。
- 旧bundleをComponents外へ保存し、新bundleをstage先で検証してから切り替える。
- rollbackは更新reportに記録した新bundleと配置先が一致する場合だけ行う。
- AU更新・登録検査の後、VAB読込・Toneだけの変更・dirty・保存・再起動を実画面で確認する。
- 読み込み元は`build/ui-check/garageband-source.vab`（オリジナルfixtureのcopy）。
  保存後はこのcopyを移動し、元pathなしでprojectが復元されることを確認する。
- project内のAU plistとembedded bank bytesの比較、前後のWAVE出力も可能な範囲で検査する。

読み取り専用の確認入口:

```sh
python3 scripts/check_garageband_vab_state.py build/spu-vab-check.band \
  --expected-vab tests/fixtures/vab/spu-synth-demo.vab --program 3 --tone 1 --gain 99
```

これは観測したAU XML断片を検査し、GarageBand全containerや候補のactive bindingを断定しない。
旧projectではlegacy Gain99の8-byte payloadが1候補見つかった。

物理的な実聴・長時間realtime試験、署名・公証・Release公開とは区別する。
