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
