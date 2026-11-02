# PSF版AU配置・GarageBand検証（2026-11-03 JST）

PSF実装`d379b60c4c695c6bb1cfbb26c16c52e91ab2b34d`のAUへ、既知manifest照合と退避付きで更新した。作業前TODOは`211e9ac`。
version0.0.1 /state schema11 /parameter27個は維持している。公開済みv0.0.1のtag /assetsは変更しない。

## 配置と回帰検証

旧AUの全5filesを`docs/ready_status_20261102.json`と照合し、GarageBandを正常終了してから更新した。
配置先は`~/Library/Audio/Plug-Ins/Components/TetoricaSPU.component`。
installerのjournalは`build/au-backups/au-update-23f4rvfb/manifest.json`で、旧AUの2copyを保持する。
新AU全5filesはbuild元 /installed /journalで一致。auval成功、実AU host22 PASSを確認した。
これは既存host機能の回帰検証で、PSF file panelの操作確認とは区別する。

次回更新のexpectedには[全file manifest](au_psf_update_20261103.json)を使う。
今回の配置を戻す場合は全hostを正常終了してから次を実行する。

```sh
python3 scripts/install_au.py --rollback build/au-backups/au-update-23f4rvfb/manifest.json
```

## 検証projectと保護

元は`build/au-gain380-update-tqa8b91a/spu-gain380-restore-check.band`。
開始時の全7filesを照合し、`build/psf-au-update-20261103/source-before-update.band`と
`build/psf-au-update-20261103/spu-psf-folder-restore-check.band`へ複製した。後者だけを今回の検証に使う。

更新後、GarageBandが以前の既知automation検証copyを自動で開いたため、切替前に全11filesを
`build/psf-au-update-20261103/automatic-reopen-before-switch.band`へ退避・照合した。
その既知copyは保存確認でSaveしてから今回専用projectへ切り替えた。元PCM検証projectは変更していない。

専用projectの実URLとAU画面で、Embedded PCM cycle /Gain380% orange /Project sound restoredを確認した。
Morph /Reverb /Noise /PMONはOff。操作時はDAW masterを一時的に0へ設定し、実聴の検証とは扱わない。
終了時はmaster+0.0dB /再生・録音・cycle Offへ戻す。

## 現在のcheckpoint

人のfolder選択後、実AU画面でPSF 1/2 /Embedded VAB /Program3 /Tone0 /Loadedを確認した。
専用projectへ保存し、schema11のVAB全3744bytesが期待するBank1と完全一致、Gain100×Boost3.8、各FX Offも確認した。
保存payloadは4776bytes /SHA256 `b33234398dab0d6260dc15882f5a0f37822da5c42e5972c0f3b150b65de162ab`。
AU独自UIへのAIクリックではmenuが開かず、Entry /Bank /Program /Toneの選択を人へ依頼中。
取消 /不正file時の保持、PSF元fileなしの保存再起動復元はまだ完了していない。

自作demoを`build/psf-au-update-20261103/psf-demo-source`へ複製し、原fixturesを保護して使う。
階層PSFLibを含む`Demo/track.minipsf`と`standalone.psf`から2banksを抽出する。
Bank1 /id23とBank2 /id24はそれぞれ3744bytes。音声データは同一で、VAB idだけが異なる。

- Bank1 SHA256: `71a7de205c0ebe1b8743288d90cd3b0204da9bd81aed9159af31f404d7cf2be6`
- Bank2 SHA256: `a85c06db6b608b6943040b86cff9a5af5902f489ef0e73abb9eba2d38497a42d`

保存後の検証では実URL /AU画面とread-only state inspectorを併用する。
inspectorだけでactive trackを証明しない。元PSF一式を退避しても選択VAB /Program /Toneが復元できるかを確認する。
catalog自体は保存しない仕様なので、再起動後のPSFボタンが`PSF...`になるのは正常。
実ゲームPSF・実聴・長時間動作・新version公開は別段階とする。
