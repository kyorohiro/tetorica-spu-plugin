# 作業記録

## 2026-10-31 JST — 準備済み音源の交換を開始

- cleanな `96ca5cc` から再開。実装前に今回のTODOを追加した。
- worker上のVAB解析・準備、request世代管理、block境界でのengine交換、callback外への破棄を進める。
- 音色変更ではheld notesと持ち越しMIDIをクリアする。古いrelease tailの保持・crossfadeは後続。
- UIとcustom stateはまだ接続しない。現状のGain state / 内蔵Sineの挙動を回帰検査する。

## 2026-10-31 JST — VAB実装のCIも成功

- 実装 `2f31cda1bca8a26e6591d61f3cd9a7267764252c` をorigin/mainへpushした。
- [CI 41185732525](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41185732525) 全job成功。
  Linux core / parser / imported Tone検査、macOSのAPP/AU/VST3 build・CTest・auval・公式VST3・実VST3検査を完了。
- CI 17:43:05〜17:46:30 JST。artifactとsource SHAは[VAB offline記録](vab_offline_20261031.md)へ追記。
- 次の実装はengineの安全な交換、embedded stateと復元直後render、ファイル選択・Program/Tone UI。
  現在plugin UIへVABはまだ公開していない。offline CLIとテストで基礎を確認した段階。

## 2026-10-31 JST — VAB解析・offline音源準備の完了

- 開始TODOを `68ab79c` にcommit / pushしてから実装した。
- C++ VAB parser・選択Tone準備・fresh Native engine・offline CLIを追加。
- 原本fixtureとJS oracleを独立repoへ保存。最終parser検査は8,516 assertions / 7,572拒否 / 7 oracle一致。
- offline / macOS各3 CTest suite、parser ASan/UBSan、公式VST3 47検査、実VST3の3rate PCM/state/schedulerが成功。
- 既存SineのWAV hashは変更前後で一致。ユーザーComponentsのAUは更新していない。
- SDKのrepeat bit制限を実PCMで確認。未対応loopを理由付きで拒否し、元データ・vendorは保持した。
- 詳細と再現コマンドは[VAB offline記録](vab_offline_20261031.md)。次はengine handoff→custom state→file/selection UI。
- CIは上の最終チェックポイントの通り成功。Release公開・署名・公証はしていない。

## 2026-10-31 JST — VAB解析・音源準備の開始

- cleanな `694c892` から再開。実装前に今回の区切りをTODOへ追加した。
- pure C++ parser / selected Tone preparationと、Native音源でのoffline検証を進める。
- parent workspaceのWeb版は照合元のみ。pluginのbuild・実行は親repoに依存させない。
- UI、再生中のengine handoff、DAW custom stateは後続。現時点でVAB UI対応済みとは扱わない。
- 次: bounded parserとfixture照合、準備済みToneのNative engine接続。

## 2026-10-31 JST — 初期チェックポイント

- ユーザーが作成した空のGit repositoryを確認。remoteは `https://github.com/kyorohiro/tetorica-spu-plugin.git`、mainは未commit。
- 実装前に `docs/issues/TODO.md` を作成。
- 対象はPS1 SPU。YM2612への置換はしない。
- 現在のMacにはXcode、CMake、auval、GarageBandがある。専用VST3 / CLAP validatorは未準備。
- 次: iPlug2OOSとNative SDKの固定依存・ビルド方法を調べて初期構成を作る。
- この時点では依存取得、コード実装、build、音声出力、DAW操作は未実行。

## 2026-10-31 JST — 固定依存の取得

- 最初のTODOを `0286aa6` にcommitし、origin/mainにも保存した。
- iPlug2OOS / iPlug2 / Native SDK / VST3 SDKをGit submoduleとして固定。
  正確なSHAは `dependencies.lock.json` に記録。
- Native SDKは公開commit `cf1747387eb2b66323124959d7b356b936197784`。
  公開済み0.1.6より後のSPU修正を含むが、0.1.7公開版とは扱わない。
- `python3 scripts/bootstrap.py` 成功。取得途中でも同じコマンドで再開できる。
  VST3のbase / pluginterfaces / public.sdkだけをiPlug2のignored SDK directoryへlink。
- iPlug2既定の自動deployはユーザーの既存bundleを置換し得るため、明示的にOFFとする。
- 次: stock template build、UI非依存SPU層の検証、Standalone / AUv2 / VST3接続。
- まだ実音・DAW・署名・公証は未確認。

## 2026-10-31 JST — 最小Native音源・プラグイン

- stock TemplateProjectのStandalone / AUv2 / VST3をmacOS ARM64でbuild成功。
  `cmake -S vendor/iPlug2OOS -B build/template -G 'Unix Makefiles' -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 -DIPLUG_DEPLOY_PLUGINS=OFF`
  `cmake --build build/template --target TemplateProject-app TemplateProject-au TemplateProject-vst3 --parallel 4`
- `src/SpuInstrument.*`: オリジナルSine、24 voice、MIDI channel、note on/off、panic、連続rate変換。
  デコードcacheをconstructorで事前準備し、callbackでSDK reset / RAM loadをしない。
- `plugin/`: iPlug2からNative SPUを直接呼ぶ。bounded MIDI queue、sample offset分割render、Gain、マウス鍵盤、全音停止。
  通常のDAW parameter stateでGainを保存する。独自VAB stateは未実装。
- `cmake --preset offline` / `cmake --build --preset offline --parallel 4` / `ctest --preset offline` 成功。
  1 suiteで音程・release・24voice・voice stealing・channel・partition一致・instance分離・8〜384kHz・C++ new/deleteを検査。
- `cmake --preset macos` / `cmake --build --preset macos --parallel 4` / `ctest --preset macos` 成功。
  出力は `build/macos/out/TetoricaSPU.{app,component,vst3}`。ユーザーのplugin directoryへは未配置。
- 公式VST3 validator自己test43成功。実VST3検証47成功、0失敗。
- 実VST3を独自headless hostで読み、44.1 / 48 / 96kHzのoffset64発音・C4音程・離鍵・Gain state roundtrip・別instanceへの復元と分離を確認。
  C4約261.667Hz、離鍵後peak0、保存Gain37%の振幅比0.37、state12 bytes。
- Standaloneの初回起動とGain／鍵盤の描画をCUAで確認。ウィンドウ名のTemplateProject残りを発見・修正。
  再build後の実起動でタイトルが `Tetorica SPU` になったことも確認し、終了した。
  鍵盤の実聴、DAW上の操作、AU登録・auvalはまだ未実施。
- 参考WAV生成: `build/offline/spu_instrument_test --wav build/spu-demo.wav`。
- 次: CI実行結果を記録し、AU / GarageBand確認、続いてVAB読み込みへ。
- framework / SDK / SPU / font等のlicense textを `plugin/resources/THIRD_PARTY_NOTICES.txt` へ収集し、各bundleのResourcesへ同梱する。
- 制限はREADMEとTODOを参照。署名・公証・Release公開・タグ作成はしていない。

## 2026-10-31 JST — CI完了チェックポイント

- 実装commit `1e1c2e7befb95edbbfccb703fb31720058b7d532` をmainへpushした。
- [Verify SPU Plugin](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41180834350) は全job成功。
  Linux offline job `124477744265`、macOS job `124477744521`。
  初期化・固定SHA検査・build・CTest・公式VST3 validator・実VST3 PCM / state検査が成功。
- CI開始16:30:04 JST、完了16:33:52 JST。
- CI artifact `macos-development-bundles`（ID `13034597642`、2,029,169 bytes）を確認。
  ZIP digest `b19bb95f6be3c4bf229d24aa78da05db46608e1a97f9a2d46715cb0aff3b72c2`。
  artifact upload成功の記録であり、ダウンロード後の実機・実聴検証とは区別する。
- [検証JSON](verification_20261031.json)にはソース・ローカルbundle・noticeのSHA、PCM計測値と未検証項目を保存。
- **次の入口:** Phase 2のAU登録・auval / GarageBand確認、その後Phase 3のVAB読み込み。
  既存Synthのリリースとは別repository。今回は開発版をmainへ保存しただけで、Release公開はしていない。

## 2026-10-31 JST — AU / GarageBand検証の開始

- ユーザーから継続の指示。まず今回の範囲をTODOへ追加した。
- 作業tree clean、mainはoriginと同期した `cf4a002` から開始。
- ユーザー専用 `~/Library/Audio/Plug-Ins/Components` にTetoricaSPU.componentは存在しないことを確認。
- 次: 既存bundleを置換しないAU配置スクリプトを準備して登録・auval、GarageBand検証。
- 別作業としてVST3イベント境界・overflow復帰テストを拡充する。まだ結果未確認。

## 2026-10-31 JST — AU / GarageBand実操作の完了

- AUをユーザーComponentsへ新規配置。sourceと同一SHAを確認、既存bundleの置換なし。
- auval成功（sandbox内の初回は登録AU取得不可、承認後のsandbox外実行で成功）。
- 既存GarageBand projectはユーザー自身に保存・終了してもらい、その後新規の検証projectを作成。
- `build/spu-au-check.band`でAU選択・UI描画・A/S/DのMIDI録音・保存・閉じて再読込を実施。
  AUとGain99%が復元された。選択regionを前後でWAVE出力し、差は最大1 PCM16単位。
- 追加VST3 scheduler / lifecycle検査も44.1 / 48 / 96kHzで成功。DSP本体を直す必要はなかった。
- 再現コマンド・制限・auval warningは[今回の記録](au_garageband_20261031.md)へ保存。
- VABの設計メモも保存。音色データの解析と、audio callback外のcache準備・状態復元が次の範囲。
- 実装・検証補助を`b47ec2220c2880254ba3cd977438cab61d4f7ff2`に保存し、mainへpush。
  [今回のCI](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41182380646)ではAU登録・auvalも追加した。

## 2026-10-31 JST — CIのAU発見手順を修正

- `41182380646` はLinux成功、macOSの新設AU検証stepで失敗。配置直後にAUが見つからず、render検証には入っていない。
- iPlug2自身のCIにもregistrar更新があることを確認。`1e869e52d247111ae4f598a4afd996147a95b9bc`で
  CIの実行ユーザーのregistrarに限ったSIGTERM更新と、最大30秒のdiscovery待ちを追加した。
- localでは `--refresh-ci-registry` を拒否することを確認し、通常のdiscovery + auvalはsandbox外で成功。
  ローカルのregistrarは終了させていない。検証本体の失敗を繰り返して隠す処理はない。
- [修正版CI](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41183069247) を確認するのが次の再開手順。
- GarageBand自体の確認は上記の通り完了しており、CIの登録問題とは分けて記録する。

## 2026-10-31 JST — 修正版CIも成功

- `41183069247` は全job成功。source `1e869e52d247111ae4f598a4afd996147a95b9bc`。
  macOS job `124484283601` / Linux offline job `124484283716`。
- CI開始17:04:05 JST、完了17:07:52 JST。CI registrar SIGTERM更新exit0、約7.4秒後にAU発見、
  `AU VALIDATION SUCCEEDED.`、VST3公式47成功、scheduler / lifecycleの3rate全成功をログで照合した。
- artifact `13036075459` / `macos-development-bundles` / 2,029,169 bytes。
  digest `96a0cc9f1b1efccc5edf3a03bebab54bcfae40baa1b88e48dd25fbc7751a83c7`。
  配布物を公開した記録ではない。最初のCI失敗も検証JSONに履歴として残した。
- 最後のGarageBand状態は検証用project保存済み、録音・再生停止、SPU / Gain99%。
- **次の作業:** `docs/vab_import_plan.md` の純粋なVABパーサーとTone準備関数。
  実聴・長時間動作の確認、AU preset-name warning、将来の安全なAU更新はTODOとして残す。
