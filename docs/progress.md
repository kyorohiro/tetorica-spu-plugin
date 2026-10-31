# 作業記録

## 2026-10-31 JST — 初期チェックポイント

- ユーザーが作成した空のGit repositoryを確認。remoteは `https://github.com/kyorohiro/tetorica-spu-plugin.git`、mainは未commit。
- 実装前に `docs/issues/TODO.md` を作成。
- 対象はPS1 SPU。YM2612への置換はしない。
- 現在のMacにはXcode、CMake、auval、GarageBandがある。専用VST3 / CLAP validatorは未準備。
- 次: iPlug2OOSとNative SDKの固定依存・ビルド方法を調べて初期構成を作る。
- この時点では依存取得、コード実装、build、音声出力、DAW操作は未実行。
