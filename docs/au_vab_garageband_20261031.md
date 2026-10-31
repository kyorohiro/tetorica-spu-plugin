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

installerと更新、GarageBand標準のAUプリセット経由でのVAB保存・再起動・PCM照合が完了。
[計測値・manifest・未確認範囲](au_vab_garageband_20261031.json)。

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

## GarageBandで確認できた範囲

1. 更新したAUの新UIを開き、旧stateからBuiltin Sine / Gain99%が復元されたことを確認した。
2. AU独自のLoad VABとBuiltin SineへCUAからclickを送ったが、どちらもstatusが変わらず、hoverだけが見えた。
   `AUHostingServiceXPC_arrow`が別processで動作している。CUAからそのprocessへは接続できなかった。
   input routingの可能性があるが、実際の人のクリックで未追試のためアプリ不具合とは断定しない。
   pluginの初期化コードは変更していない。ユーザーへ一度のLoad VAB操作を確認する質問を送った。
3. 独立して確認できる経路として、GarageBandのAU toolbar「詳細→読み込み」を使用した。
   オリジナルfixtureをschema 1に埋めた検証用`garageband-p3-t1.aupreset`を読み、
   画面でEmbedded VAB / Program 3 / Tone 1 / Gain99%を確認した。
   **plugin独自のLoad VABボタンを検証した結果ではない。**
4. copyのprojectを保存。AUのXML断片にfull bank3744 bytes、payload3784 bytes、P3/T1/Gain99が入り、原本bytesと完全一致した。
   matching candidateは1件、conflicting candidateは0件。
5. 選択したMIDIリージョン（bar1〜3）を44.1kHz / stereo PCM16 WAVEへ書き出した。
6. 保存してGarageBandを完全終了。読み込んだaupresetと検証用VAB copyを
   `build/garageband-source-away/`へ移動し、元pathに存在しないことを確認した。
   元のfixtureは変更していない。AU stateはbank bytesを埋め込む形式で、外部pathを保存しない。
7. `spu-vab-check.band`を再起動し、同じEmbedded VAB / P3 / T1 / Gain99%が表示された。
   同じリージョンを別WAVEへ書き出し、保存stateとPCMの一致を検査した。
8. 最後にcopyを保存し、録音・再生停止をAXで確認した。人によるclick確認用にAU画面を開いた状態で残した。

再起動前後の両WAVEは176,400 stereo frames（4秒）。PCMの最大差**0**、差のRMS **0**。
file SHAはheader等の違いにより異なるが、音声sample自体は一致した。

```sh
python3 scripts/check_garageband_exports.py build/spu-vab-before.wav build/spu-vab-after.wav \
  --report build/garageband-vab-export-comparison.json
```

前のBuiltin録音と比較した音程も変わった。安定区間の正方向zero crossingによる値は次の通り。

| 区間 | 以前のBuiltin | 今回のVAB P3/T1 |
| --- | ---: | ---: |
| 0.2〜0.6秒 | 130.837 Hz | 202.642 Hz |
| 0.9〜1.3秒 | 146.788 Hz | 227.443 Hz |
| 1.7〜2.1秒 | 164.767 Hz | 255.314 Hz |

同じMIDI48/50/52に対する選択Toneの予測値202.643 / 227.444 / 255.322 Hzと最大約0.009 Hz差。
FFTでも同じ帯域を確認した。これはデジタル書き出しの解析で、実聴・絶対Gain校正ではない。

## CI・再開

- 実装と更新前後の入口を`ece046ca100df90ed8dfa5e9d292282c17800937`へ保存し、origin/mainへpushした。
- [CI 41192817552](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41192817552) 全job成功。
  19:27:23〜19:31:22 JST。macOS `124512904335`、Linux `124512904442`。
  両OSのinstaller11テスト、各5 CTest、APP/AU/VST3 build、auval、公式・実VST3、実AU stateが成功した。
- artifact ID `13039677346`、2,152,962 bytes、SHA-256
  `c2a667e4253008c593e0748ff7088fc339bd27b558ccb45483b635b718c1c0dc`。
  upload確認であり、artifact download後の実機試験やRelease公開とは別。
- 現installed AUの全5 file manifestを今回のJSONの`au.file_sha256`へ記録した。
  次のupdateで`--expected docs/au_vab_garageband_20261031.json`として使用できる。

残る確認は、実際の人のクリックでのAU独自Load VAB / Program / Tone、Toneだけの変更時のproject dirty、
dialog / popup中のeditor close/reopen。現時点でこれらを成功扱いにはしていない。

物理的な実聴・長時間realtime試験、署名・公証・Release公開とは区別する。
