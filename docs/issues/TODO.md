# SPU Plugin 作業・再開TODO

## 目的

iPlug2OOSを土台に、tetorica-soundchip-nativeのPS1 SPUをDAWから演奏する。
最初の対象はmacOS Standalone / AUv2 / VST3。音声生成はC++で行う。
既存Web版・Tauri版の配布物は変更しない。

## 現在地

Native Sineの最小プラグインはローカルでbuild・自動検証済み。
固定依存とソースはGitHubにも保存した。
[ハッシュ・ローカル検証記録](../verification_20261031.json)、
[最初のCI](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41180834350)を確認し、
次はAU / GarageBandでの確認、その後VAB読み込みへ進む。

### 今回進める範囲（2026-10-31）

- [ ] AUをユーザー専用Componentsへ新規配置し、auvalで検査する。既存bundleは上書きしない。
- [ ] GarageBandで新規の検証用projectを作り、AU選択・演奏・保存・再読込・書出しを試す。
- [ ] VST3のMIDI境界・overflow復帰テストを追加する。
- [ ] 上記の結果と手動確認が残る範囲を記録し、commit / pushする。

## 再開手順

1. `git status --short --branch` とこのファイルを読む。既存の変更を消さない。
2. [作業記録](../progress.md)の最終チェックポイントを読む。
3. 依存ソースの取得が途中なら、既存の取得状態と固定revisionを確認してから再実行する。
4. 完了した検証と未実行の検証を区別する。build成功をDAW動作確認済みとは扱わない。
5. 変更の区切りごとに結果・コマンド・次の作業を記録し、ローカルcommitで保存する。

## Phase 0: 再開可能な土台

- [x] 実装前にTODOと作業記録を作成する。
- [x] iPlug2OOSの構成・CI・依存取得方法を調査する。
- [x] iPlug2 / テンプレートのrevisionとlicenseを固定・記録する。
- [x] Native SDKのSPUソースを固定する。公開release後の修正が必要なため、公開commitを固定（dependencies.lock.json参照）。
- [x] プラグインID・名前・ビルド出力先・ignore設定を用意する。
- [x] macOS ARM64で未変更テンプレートのAPP / AU / VST3 buildを確認する。

## Phase 1: Native SPUの最小演奏

- [x] UIから独立したSPU演奏層を作る（発音割当・Note On/Off・panic）。
- [x] オリジナルの内蔵ADPCM波形を使って発音する。
- [x] DAWのsample offsetに沿ってMIDIを適用する。実VST3でoffset64より前の無音を検査。
- [x] 固定44.1kHz SPU出力をhost sample rateへ変換する（初期版は線形補間）。
- [x] built-in専用callbackでファイル読込・動的確保・待機lockを避ける。キャッシュ事前準備後のC++ new/deleteはテストで0。全mallocやhard RTの保証とは別。
- [x] 無音・音程・離鍵・和音・ブロック境界・複数instanceをオフラインで検査する。
- [x] macOS ARM64でStandalone / AUv2 / VST3をbuildする。

## Phase 2: プラグインとしての検証

- [ ] AUをauvalで検証する（登録に必要な操作はその時点で確認）。
- [x] VST3をSteinberg公式validatorで検証する（47成功、0失敗）。
- [x] 初期Gain状態の保存・復元と44.1 / 48 / 96kHzの実VST3 offline renderを検査する。block sizeは公式validatorとcore分割一致で検査。
- [ ] wrapperのイベント境界・順序・queue overflow復帰を実VST3で追加検査する。
- [ ] GarageBandで読込・演奏・保存・再読込・書出しを確認する。
- [x] CIにオフライン検査とbuildを追加する。最初のLinux core / macOS build・VST3検査が成功。Windows build・各OSの実聴は未確認。

## 初期版の既知の制約・後続改善

- [ ] 内蔵112標本Sineは約1,575HzでSPU PITCH上限。MIDI92以降も演奏できる波形構成を用意する。
- [ ] sample rate変換の品質・遅延報告を検討する。44.1kHz未満のanti-alias filterは未実装。
- [ ] Pitch Bend / Sustainを追加する。
- [ ] CC120をチャンネル別の即時停止にする（現状はglobal panic、CC123はchannel release）。
- [ ] Gain変更を滑らかにし、sample単位のautomationの扱いを検討する。

## Phase 3: VAB楽器と音作り

- [ ] VAB読み込み、Program / Tone選択を追加する。
- [ ] サンプルを含む音色と設定のproject保存・復元を実装する。
- [ ] ADSR / Reverb / Noise / PMONを順に公開する。
- [ ] 必要に応じてsoftware morph等のWeb版機能を移す（SPU実機機能とは表示を分ける）。

## 人による最終確認

- [ ] 実聴で音色・クリック・演奏latencyを確認する。
- [ ] 実DAW / 実機の長時間リアルタイム動作を確認する。オフライン試験と区別する。
- [ ] 署名・公証・配布・Release公開は具体的な成果物を確認してから進める。
- [ ] 公開配布時に実際にlinkした第三者コード・fontのnotice同梱を再確認する。初期版bundleには収集したTHIRD_PARTY_NOTICES.txtを含める。
