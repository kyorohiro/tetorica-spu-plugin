# SPU Plugin 作業・再開TODO

## 目的

iPlug2OOSを土台に、tetorica-soundchip-nativeのPS1 SPUをDAWから演奏する。
最初の対象はmacOS Standalone / AUv2 / VST3。音声生成はC++で行う。
既存Web版・Tauri版の配布物は変更しない。

## 現在地

Native Sineの最小プラグインはローカルでbuild・自動検証済み。
固定依存とソースはGitHubにも保存した。
[ハッシュ・ローカル検証記録](../verification_20261031.json)、
[最初のCI](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41180834350)も成功。
AU / GarageBandでの録音・保存・復元・WAVE書出しを追試済み。
[AU検証を含む修正版CI](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41183069247)も全job成功。
[VAB解析・offline音源準備](../vab_offline_20261031.md)も完了。
[音源交換・worker準備](../engine_handoff_20261031.md)も実装・検証済み。
[embedded DAW state](../embedded_state_20261031.md)も実装済みで、[CI全job成功](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41189325333)。
次は[VAB取り込み計画](../vab_import_plan.md)のファイル読込・Program/Tone UIへ進む。

### DAW stateの今回の区切り（2026-10-31）

- [x] version / size上限付きの、bank bytes・Program/Tone選択・Gainの保存形式を実装する。
- [x] immutableな音色定義をengineと一緒に管理し、保存時に再生中のhandleへ触れない。
- [x] state復元を成功と返す前に解析・cache準備を完了する。失敗時は音源とGainを保持する。
- [x] pluginのcustom stateへ接続し、従来のGain-only状態も復元できるようにする。
- [x] 実VST3で埋め込みVABの別instance復元・直後の先頭block・不正state・持ち越しMIDIを検査する。
- [x] build / sanitizer / AU・VST3回帰とCIを確認し、結果・次のUI作業をcommit / pushする。

この区切りではファイル選択UIはまだ公開しない。VABを含むstateを実際のpluginへ復元する入口で検証する。

### 音源交換の今回の区切り（2026-10-31）

- [x] 準備済みengineの所有権を、audio block境界で非待機に交換する層を作る。
- [x] 古いrequestの結果・失敗した準備が現在の音源を上書きしない世代管理を実装する。
- [x] 旧engineの破棄をaudio callback外へ戻し、退避queue満杯時は交換を延期する。
- [x] workerでVAB解析・Tone準備を行う入口と、pluginへの交換層の接続を用意する。
- [x] 再生中の交換・sample rate変更・終了時の所有権・競合・callbackのnew/deleteを検証する。
- [x] 結果と次のstate/UI作業を記録し、commit / pushする。[CI全job成功](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41187048655)。

この区切りではVAB読み込みUIをまだ公開しない。DAW stateを復元した直後に正しい音が出る設計も別途検証する。

### VAB実装の今回の区切り（2026-10-31）

- [x] UI非依存のC++ VAB v6/v7パーサー（complete / 明示VH+VB）を実装する。
- [x] 選択ToneのADPCM・tuning・ADSR・loop・volume/panを検証・準備する。
- [x] オリジナルfixtureを独立repoへ取り込み、Web版との結果照合と不正入力の検査を追加する。
- [x] 準備したToneをNative音源でoffline演奏し、長いsample・loop・事前cache準備を検査する。
- [x] build / 既存pluginの回帰検査を行い、結果と次の入口をcommit / pushする。[CI全job成功](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41185732525)。

この区切りではファイル選択UI・再生中の音色交換・DAWへのVAB保存はまだ公開しない。
それぞれ音源準備と別に検証してから接続する。

### 今回進める範囲（2026-10-31）

- [x] AUをユーザー専用Componentsへ新規配置し、auvalで検査する。既存bundleは上書きしない。
- [x] GarageBandで新規の検証用projectを作り、AU選択・演奏・保存・再読込・書出しを試す。
- [x] VST3のMIDI境界・overflow復帰テストを追加する。
- [x] 上記の結果と手動確認が残る範囲を記録し、commit / pushする。

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

- [x] AUをauvalで検証する。ユーザーComponentsへ新規配置し、sandbox外で成功。factory preset名のwarningは後続TODO。
- [x] VST3をSteinberg公式validatorで検証する（47成功、0失敗）。
- [x] 初期Gain状態の保存・復元と44.1 / 48 / 96kHzの実VST3 offline renderを検査する。block sizeは公式validatorとcore分割一致で検査。
- [x] wrapperのイベント境界・順序・queue overflow復帰を実VST3で追加検査する（44.1 / 48 / 96kHz）。
- [x] GarageBandで読込・MIDI録音・保存・再読込・WAVE書出しを確認する。Gain99%復元、前後のPCM差最大1LSB。実聴は別。
- [x] CIにオフライン検査とbuildを追加する。最初のLinux core / macOS build・VST3検査が成功。Windows build・各OSの実聴は未確認。

## 初期版の既知の制約・後続改善

- [ ] 内蔵112標本Sineは約1,575HzでSPU PITCH上限。MIDI92以降も演奏できる波形構成を用意する。
- [ ] sample rate変換の品質・遅延報告を検討する。44.1kHz未満のanti-alias filterは未実装。
- [ ] Pitch Bend / Sustainを追加する。
- [ ] CC120をチャンネル別の即時停止にする（現状はglobal panic、CC123はchannel release）。
- [ ] Gain変更を滑らかにし、sample単位のautomationの扱いを検討する。
- [ ] AUでrender thread以外から届くMIDIのbounded ingressを検証・対応する。現状のhost MIDI / lifecycle操作は単一consumerとして直列化される前提。
- [ ] AU factory presetの名前を初期化し、auvalのpreset-name warningを解消する。
- [ ] 今後のAU更新時は既存bundleと前回配置したhashを照合し、退避・更新できる手順を用意する（現在のinstallerは新規配置・同一内容の確認だけ）。

## Phase 3: VAB楽器と音作り

- [x] C++ parser・選択Tone準備・新規Native instanceのoffline再生を実装・検証する。
- [x] オリジナルVABのTone一覧・WAV書出しCLIを用意する（`spu_vab_render`）。
- [x] workerで準備したengineを安全に交換する。世代管理・audio thread外の破棄・queue満杯時を検査する。
- [x] version付きembedded bank stateを実装する。復元直後の最初のrenderと不正stateのatomic failureを検査する。
- [ ] VAB読み込み、Program / Tone選択を追加する。
- [x] サンプルを含む音色と設定のproject保存・復元を実装する。
- [ ] 実DAWの画面からVABを読み、project保存・再起動・復元とUI表示を確認する（UI接続後）。
- [ ] Native SDKのrepeat先packetにflag 2がないloopを別途修正・検証する。現在のengineは明示的に拒否、parserは元bytesを保持する。
- [ ] ADSR / Reverb / Noise / PMONを順に公開する。
- [ ] 必要に応じてsoftware morph等のWeb版機能を移す（SPU実機機能とは表示を分ける）。

## 人による最終確認

- [ ] 実聴で音色・クリック・演奏latencyを確認する。
- [ ] 実DAW / 実機の長時間リアルタイム動作を確認する。オフライン試験と区別する。
- [ ] 署名・公証・配布・Release公開は具体的な成果物を確認してから進める。
- [ ] 公開配布時に実際にlinkした第三者コード・fontのnotice同梱を再確認する。初期版bundleには収集したTHIRD_PARTY_NOTICES.txtを含める。
