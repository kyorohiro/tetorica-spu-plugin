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
VAB file読込・Program/Tone UIを実装し、ローカル検証と[CI全job成功](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41191105787)。[今回の記録](../vab_ui_20261031.md)を参照。
AUを既知manifest照合付きで更新し、GarageBand標準AUプリセット経由でVAB保存・再起動・UI復元・PCM一致を確認した。
[今回の記録](../au_vab_garageband_20261031.md)。人のLoad VAB操作からVAB読込・再生時の採用・保存・再起動後のP3/T0復元も確認済み。
Toneの個別切替・保存・再起動後のP3/T1復元も確認済み。
Program操作後のP3/T0と保存・project再表示も確認した。P11中間画面とToneのみのdirty判定は未確認。
Pitch Bend / Sustainもcore・実AU/VST3・CIで検証し、installed AU更新とproject復元を完了。
ADSR編集も実装・ローカル検証・installed AU更新済み。[検証記録](../adsr_editing_20261031.md)。次はSPU Reverb。

### SPU Reverbの今回の区切り（2026-10-31、実装前）

- [ ] Native SDKへ確保・RAM/cache破棄を伴わない残響clear APIを追加・検証し、公開commitへ固定依存を更新する。通常resetをaudio callbackで呼ばない。
- [ ] Off＋7種の共有近似preset、Enable、Return L/R（初期25%）、All notes / Tone sendを公開する。元VABのsend flagは保持する。
- [ ] 手動の非Off preset選択で自動Enableする。automation / host restoreは各parameterの保存値をそのまま使う。
- [ ] 発音・ADSR・sample位置を保ってpreset / return / sendを変更する。Enable Offは即dryへ戻し残響を裏で減衰、preset OffとAll sound offは残響履歴も消す。
- [ ] 720×620のADSR / Reverb page切替で画面を整理し、bank・Gain・keyboardを共通に保つ。実機完全再現ではなくcoreの近似presetと表示する。
- [ ] 既存parameter ID0〜11を保ち、新設定をschema3へ保存する。schema1/2・Gain-onlyはReverb Offで互換復元し、不正stateは全設定と現在音を保つ。
- [ ] coreで全presetの残響、左右Return、held / release / pedal / send復元、panic再発音、分割一致・RT確保・ASanを検証する。初期dry音の一致も確認する。
- [ ] 実AU/VST3のparameter・保存復元・旧形式・PCMを検証し、Standalone / GarageBandの画面を確認する。
- [ ] 検証後に既知manifest付きでinstalled AUを安全に更新する。TODO・結果をcommit / pushし、CIを確認する。

外部入力Reverbは追加しない。Web・Tauri配布物とproject原本は変更しない。
Enable Offでwetを即muteし、preset Offで履歴も消す点は、sendのみを止めるWeb UIからの意図した改善とする。

### ADSR編集の今回の区切り（2026-10-31、実装前）

- [x] Tone本来のADSRを保持し、Custom ADSRの有効化・元Toneからのコピーを追加する。初期状態は元Toneを使用する。
- [x] Attack / Decay / Sustain level / Sustain / Releaseのrateとmodeをhost parameterとして公開する。Gainの既存indexは変えない。
- [x] Apply editsをOnにすると現在のlevel・sample位置からheld / pedal-held / Release音へ反映し、Offなら既存音は離鍵後まで旧設定を保持する。新しいNote Onは編集値を使う。
- [x] rate codeを使うADSRの概形と編集controlを追加し、実測msと誤解されないよう表示する。
- [x] 新設定をversion付きstateへ保存する。旧schema1・Gain-only stateは元Toneの音を保って復元し、不正stateは音源と全parameterを保持する。
- [x] coreのlive / deferred / Release / partition / RT確保と、実AU/VST3でのparameter・state・旧形式回帰を検証する。
- [x] Standalone / AU / VST3をbuildし、Standalone実操作とGarageBandでの旧state復元・新panel表示を確認する。
- [x] 検証結果・制限をcommit / pushし、CIを確認する（`550fbab`、[CI全job成功](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41201681624)）。
- [x] 検証後に既知manifest付きでinstalled AUを安全に更新し、検証projectの旧state復元を確認する（P3/T0/Gain99、Tone ADSR、Apply On）。

CustomをOffにするとbank内のADSRへ戻る。Custom値はProgram/Toneを切り替えても保持し、Copy toneで明示的に取り込めるようにする。
live MIDIのBend/Sustainは保存しない。Web版・Native SDKのvendor source・既存project原本は変更しない。
SPU Reverbはこの区切りの次に進める。

### Pitch Bend・Sustainの今回の区切り（2026-10-31、実装前）

- [x] Web spu-synthとの機能差を整理し、[段階移植計画](../spu_synth_parity_plan.md)を保存する。
- [x] チャンネル別14-bit Pitch Bendを追加する。中心8192、初期範囲±2半音、発音中とRelease中の音を再発音せず変更し、新規音にも適用する。
- [x] チャンネル別Sustain（CC64、64以上でOn）を追加する。離鍵を保留し、解除時は離鍵済みの音だけReleaseする。
- [x] 同音再発音・voice stealing・CC123・CC121・panic/overflow・reset/activate・host restore時の保持音とcontroller状態を整理し、stuck noteを防ぐ。
- [x] sample offsetを含むMIDI接続を行う。UI音源切替ではwheel/pedalの状態を引き継ぎ、host restoreとlifecycle resetでは初期化する（実装/source review、live UI交換は実機未追試）。
- [x] 固定iPlugのVST3 CC queueが最後のpointだけを処理する制約をplugin側adapterで補い、同block内のBend/pedal全pointを扱う（vendorは変更しない）。
- [x] VST3 bypass中にProcessBlockが止まってもMIDIを蓄積しないよう、queue・voices・controllersを初期化し、bypass解除後の古いpedal再適用を回帰検査する。
- [x] coreの音程・チャンネル分離・pedal・RT中new/deleteと、実AU/VST3のMIDI・offset・state回帰を検証する。
- [x] README・結果・未確認範囲を記録してcommit / pushし、CIを確認する（`6918642`、[CI全job成功](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41199344245)）。
- [x] 実plugin検証後、既知manifest付きの安全な手順でinstalled AUを更新・auval確認し、検証projectの復元を確認する（P3/T0/Gain99、embedded一致、旧bundleとproject backup保持）。

今回の範囲はDAW MIDIからの演奏機能。Bend/Sustainのlive値はpresetへ保存しない。
Webの±2半音と離鍵保留を参考にし、MIDIのチャンネルごとに独立させる。
既存のWeb版・installed AU・開いているGarageBandをbuildだけで書き換えない。
今回は検証後にGarageBandを保存・終了し、明示的なmanifest付きupdateでinstalled AUも更新済み。
次は[移植計画](../spu_synth_parity_plan.md)のADSR編集、続いてSPU Reverbを進める。

### Program/Tone・保存通知の今回の区切り（2026-10-31、開始時記録）

- [x] 保存済みP3/T0/Gain99のcopyを基準に、Tone 1だけへ変更する。停止中は必要に応じて再生し採用する（人の選択→Ready→再生→Loadedを確認）。
- [ ] Tone-only変更通知を、変更前の基準と区別して確認する。未変更でもGarageBand終了時の保存確認が出たため、保存promptだけでは判定できない（下記）。
- [x] 保存後のembedded P3/T1/Gain99と、再起動後の選択UIを照合する（full bank一致、再起動後もTone 1）。
- [x] Program操作後のP3/T0/Gain99を画面と保存stateで照合し、project close/reopen後の復元を確認する。
- [ ] Program 11の中間画面でTone 0になることを直接確認する。最終P3/T0とfixture期待値の確認とは区別する。
- [x] 通常のeditor close/reopenとproject close/reopenで選択を保持することを確認する。
- [ ] popup取消・popup中のeditor close/reopenを確認する。CUAで操作できない所は人の操作が必要。
- [x] 結果と未確認範囲を記録しcommit / pushする。再現した不具合があれば修正前に記録する。

基準確認では、P3/T0/Gain99を再起動後、音色やregionを編集せずeditorを閉じてQuitしても保存確認が出た。
Cancel後にFile→保存を明示してQuitした場合も同じだった。終了はCancelしprojectを保持している。
保存promptをTone-only通知成功の証拠にしない。AUの通知コードはToneも比較してPresentPreset通知を送るが、DAWのdirty判定とは区別する。
CUAのTone clickはhoverだけでmenuが開かないため、人の操作でTone 1を選択し、保存・復元まで確認した。
Program 11→3の操作依頼後、P3/T0を実画面で確認した。
「Tone 0にならない」という回答はチャット側の誤作動としてユーザーが訂正したため、不具合報告として扱わない。
fixture期待値はP3/T0とP11/T0だが、P11中間画面の実機確認はこの結果に含めない。

### AU独自UIの追試（2026-10-31、開始時記録）

- [x] ユーザーが実際のclickでLoad VABのファイル選択が開くと確認した。
- [x] 独自load確認用の`build/ui-check/garageband-ui-source.vab`を作成し、元fixtureとのhash一致を確認した。
- [x] 開かれたdialogから検証用VABを読み込み、初回のP3/T0とstatusを確認する（Ready→GarageBand再生でLoaded）。
- [x] 独自UI経由の選択をprojectへ保存し、embedded stateとeditor再表示を確認する（bank完全一致、GarageBand終了・再起動後もP3/T0/Gain99）。
- [ ] Program/Tone操作・変更通知を人の操作で追試する。CUAでAU独自controlへのclickが届かない制限と区別して記録する。
- [x] 結果・TODOをcommit / pushする。コード修正がなければ通過済み検証を繰り返さない。

停止中の`Ready. Waiting for an audio block.`は準備済み・採用待ちの正常表示。
今回の再生でLoadedへ遷移したためロード失敗として扱わない。
後続のUI改善では、内部処理名より「音を弾くか再生を開始する」案内を検討する。

### AU更新・GarageBand VAB確認の今回の区切り（2026-10-31、実装前）

- [x] 既存AUの既知manifestを照合し、退避・staging・更新・条件付きrollbackの手順を追加・検証する。
- [x] GarageBandなどのhostを保存・停止し、既存bundleをComponents外へ保持したまま新版へ更新する。
- [x] 元の検証project全体と未保存状態を別copyへ保持する。
- [x] GarageBand標準のAUプリセット読込から、VAB P3/T1/Gain99を読み、projectへ保存する。
- [ ] copyのAU独自ボタンでVAB読込・Program/Tone変更・dirty表示を確認する（人によるclickの確認待ち、CUAではBuiltinも反応せず）。
- [x] projectを保存・終了し、読み込み元の検証用aupresetとVAB copyを移動してから再起動し、音色と選択UIを確認する。
- [x] 前後の音声書出し・project内embedded stateを照合する（PCM最大差0、bank bytes完全一致）。editor再表示も確認する。
- [ ] dialog/popup中の取消・editor close/reopen時の保持を実DAWで確認する。
- [x] 回帰検査を行い、結果・復帰先・未確認項目をcommit / pushしてCIを確認する。11 installer testsを含み全job成功。

ユーザー自身の未保存projectが見つかった場合は勝手に閉じない。既存の`spu-au-check.band`は原本として保持する。
更新は既知の旧manifestと一致する場合だけ行い、未知のAU・projectを上書きしない。

### VAB UIの今回の区切り（2026-10-31、実装前チェックポイント）

- [x] complete VABをファイル選択し、1 MiB上限付きでworkerから読む入口を追加する。
- [x] Load VAB / Builtin、sparse Program / Tone選択、読込・準備・採用・失敗表示を720×360へ配置する。
- [x] editorを閉じた後の非同期dialog callbackを無効化し、古い読み込みが新しい選択・host restoreを上書きしないようにする。
- [x] host restore後とeditor再表示時に、保存された音色をUIへ反映する。UI反映だけではengineを再要求しない（実装・source review、実DAW追試は下記）。
- [x] UI起点の成功した音色変更をhostへ通知する。取消・失敗・host restoreでは変更通知しない（DAW固有dirty判定は下記）。
- [x] worker file入力・state/音源交換・実AU/VST3の回帰と、可能な範囲の実UI操作を検証する。
- [x] 結果、未確認の実DAW操作、再開手順を記録してcommit / pushし、CIを確認する。

complete `.vab`を最初の対象とする。分割VH/VBの複数file dialogは後続。
元pathは読み込み入力にだけ使用し、DAW stateには既存のembedded bank形式を使用する。

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
- [x] Pitch Bend / Sustainを追加する（[検証記録](../midi_expression_20261031.md)）。
- [ ] CC120をチャンネル別の即時停止にする（現状はglobal panic、CC123はchannel release）。
- [ ] Gain変更を滑らかにし、sample単位のautomationの扱いを検討する。
- [ ] AUでrender thread以外から届くMIDIのbounded ingressを検証・対応する。現状のhost MIDI / lifecycle操作は単一consumerとして直列化される前提。
- [ ] AU factory presetの名前を初期化し、auvalのpreset-name warningを解消する。
- [x] AU更新時は既存bundleと前回配置したhashを照合し、退避・更新できる手順を用意する（explicit update / 条件付きrollback、11 tests、実更新済み）。

## Phase 3: VAB楽器と音作り

- [x] C++ parser・選択Tone準備・新規Native instanceのoffline再生を実装・検証する。
- [x] オリジナルVABのTone一覧・WAV書出しCLIを用意する（`spu_vab_render`）。
- [x] workerで準備したengineを安全に交換する。世代管理・audio thread外の破棄・queue満杯時を検査する。
- [x] version付きembedded bank stateを実装する。復元直後の最初のrenderと不正stateのatomic failureを検査する。
- [x] VAB読み込み、Program / Tone選択を追加する。
- [x] サンプルを含む音色と設定のproject保存・復元を実装する。
- [x] 実DAWの画面からVABを読み、project保存・再起動・復元とUI表示を確認する（GarageBandの独自Load/Tone、P3/T0とP3/T1）。
- [x] GarageBand標準AUプリセット経由で、VAB embedded stateのproject保存・元fileなし再起動・UI復元・音声PCM一致を確認する。
- [ ] VAB/Toneのみの変更でDAWがprojectを変更済みと扱うことを確認する。VST3はoptional setDirty、AUはpublic PresentPreset通知を使用中。
- [ ] editorを開いたままのhost restoreと、file dialog / Program・Tone popup中のeditor close/reopenを実DAWで追試する。
- [ ] 分割VH/VBのファイル選択を追加する（parserとoffline APIは対応済み）。
- [ ] Native SDKのrepeat先packetにflag 2がないloopを別途修正・検証する。現在のengineは明示的に拒否、parserは元bytesを保持する。
- [x] ADSRをhost parameter / state / graph編集へ公開する（[実AU/VST3・GUI検証](../adsr_editing_20261031.md)）。
- [ ] SPU Reverb、Noise、PMONを順に公開する。
- [ ] 実DAWでADSR automation録音・graph drag中のeditor close / host restoreを追試する。headless PCMとStandalone操作の検証とは分ける。
- [ ] 必要に応じてsoftware morph等のWeb版機能を移す（SPU実機機能とは表示を分ける）。

## 人による最終確認

- [ ] 実聴で音色・クリック・演奏latencyを確認する。
- [ ] 実DAW / 実機の長時間リアルタイム動作を確認する。オフライン試験と区別する。
- [ ] 署名・公証・配布・Release公開は具体的な成果物を確認してから進める。
- [ ] 公開配布時に実際にlinkした第三者コード・fontのnotice同梱を再確認する。初期版bundleには収集したTHIRD_PARTY_NOTICES.txtを含める。
