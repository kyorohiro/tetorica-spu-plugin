# SPU Plugin 作業・再開TODO

## 目的

iPlug2OOSを土台に、tetorica-soundchip-nativeのPS1 SPUをDAWから演奏する。
最初の対象はmacOS Standalone / AUv2 / VST3。音声生成はC++で行う。
既存Web版・Tauri版の配布物は変更しない。

## 再開手順

1. `git status --short --branch` とこのファイルを読む。既存の変更を消さない。
2. [作業記録](../progress.md)の最終チェックポイントを読む。
3. 依存ソースの取得が途中なら、既存の取得状態と固定revisionを確認してから再実行する。
4. 完了した検証と未実行の検証を区別する。build成功をDAW動作確認済みとは扱わない。
5. 変更の区切りごとに結果・コマンド・次の作業を記録し、ローカルcommitで保存する。

## Phase 0: 再開可能な土台

- [x] 実装前にTODOと作業記録を作成する。
- [ ] iPlug2OOSの構成・CI・依存取得方法を調査する。
- [ ] iPlug2 / テンプレートのrevisionとlicenseを固定・記録する。
- [ ] Native SDKの公開済みSPUソースと取り込み方を確認し、固定する。
- [ ] プラグインID・名前・ビルド出力先・ignore設定を用意する。
- [ ] macOSで未変更テンプレート相当のbuildを確認する。

## Phase 1: Native SPUの最小演奏

- [ ] UIから独立したSPU演奏層を作る（発音割当・Note On/Off・panic）。
- [ ] オリジナルの内蔵ADPCM波形を使って発音する。
- [ ] DAWのsample offsetに沿ってMIDIを適用する。
- [ ] 固定44.1kHz SPU出力をhost sample rateへ変換する。
- [ ] audio callbackでのファイル読込・動的確保・待機lockを避ける。
- [ ] 無音・音程・離鍵・和音・ブロック境界・複数instanceをオフラインで検査する。
- [ ] Standalone / AUv2 / VST3をbuildする。

## Phase 2: プラグインとしての検証

- [ ] AUをauvalで検証する（登録に必要な操作はその時点で確認）。
- [ ] VST3を専用validatorで検証する。
- [ ] 保存・復元、sample rate / block size変更、offline renderを検査する。
- [ ] GarageBandで読込・演奏・保存・再読込・書出しを確認する。
- [ ] CIにオフライン検査とbuildを追加する。未検証OSを明記する。

## Phase 3: VAB楽器と音作り

- [ ] VAB読み込み、Program / Tone選択を追加する。
- [ ] サンプルを含む音色と設定のproject保存・復元を実装する。
- [ ] ADSR / Reverb / Noise / PMONを順に公開する。
- [ ] 必要に応じてsoftware morph等のWeb版機能を移す（SPU実機機能とは表示を分ける）。

## 人による最終確認

- [ ] 実聴で音色・クリック・演奏latencyを確認する。
- [ ] 実DAW / 実機の長時間リアルタイム動作を確認する。オフライン試験と区別する。
- [ ] 署名・公証・配布・Release公開は具体的な成果物を確認してから進める。
