# 作業記録

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
