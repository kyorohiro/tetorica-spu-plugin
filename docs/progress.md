# 作業記録

## 2026-10-31 JST — Load VAB dialogを観測、file選択だけ人の操作待ち

- 開始記録`610f5b2`をpush後、ユーザーが開いたAUのopen-panelをCUAで観測した。
- file選択の場所指定後、Return入力が背面documentへ移った。region名は変えずEscapeで解除した。
  人のclickでdialogが開く結果を、アプリ初期化不具合とは扱わず、code修正はしていない。
- 独自loader確認用に`build/ui-check/garageband-ui-source.vab`を用意し、原本とのhash一致を確認した。
- ユーザーへfile選択後のP3/T0/Loaded表示を質問した。選択完了の回答後にproject保存とstate照合へ進む。
- 保存済みcopyは引き続きP3/T1。元project・installed AUは変更していない。

## 2026-10-31 JST — 人のLoad VAB操作が成功、独自UIの追試を再開

- ユーザーが「ファイル選択が開く」と回答した。人のLoad VAB clickは動作すると確認できた。
- `1e641b0`から再開。アプリ不具合を理由とする修正は行わず、開いたdialogから独自UI経由のVAB読込を進める。
- 次: 現在の画面を読み、検証用VAB→P3/T0→project保存とstate照合。Program/ToneのCUA操作は別に切り分ける。

## 2026-10-31 JST — GarageBand VAB保存・再起動とCI成功、独自clickは人の確認待ち

- installer・state検査scriptを`ece046c`へ保存し、[CI 41192817552](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41192817552)は全job成功。
  両OSのinstaller11 tests、各5 CTest、APP/AU/VST3、auval・公式/実VST3・実AU stateが成功した。
- 既存AUはverified backup付きで更新済み。GarageBandで新UIとlegacy Gain99の復元を確認した。
- CUAからAU独自Load VAB / Builtinへのclickはhoverだけが見え、statusは変化しなかった。
  別processのAUHostingServiceへの入力経路の可能性があるが、原因は確定していない。
  人によるLoad VABの一度のclickをasync質問した。根拠のないplugin初期化修正はしていない。
- 独立して進められるGarageBand標準AU preset読込から、オリジナルVAB P3/T1/Gain99を読み、copy projectへ保存した。
  projectのAU XML断片で1候補・bank3744 bytes完全一致、state3784 bytesを確認した。
- loaded aupresetと検証用VAB copyを元pathから移動し、GarageBandを完全終了・再起動して同じ音色とUIを確認した。
  前後の44.1kHz stereo PCM16（4秒）書出しは最大差0、差RMS0。
  旧Builtinとは音程も変わり、選択Toneの予測値に一致した。実聴とは区別する。
- 原本project全11 fileの開始時hash一致を最終確認。copyを保存し、録音・再生停止、AU editorを開いた状態で残した。
- [詳細・復帰手順](au_vab_garageband_20261031.md)、[計測JSONと現installed manifest](au_vab_garageband_20261031.json)。
- 次は人のLoad VAB / Program / Tone操作とToneのみdirtyの確認。今回のhost preset経路を独自ボタン検証済みとは扱わない。

## 2026-10-31 JST — 既存AUの退避・更新完了、DAW操作の前

- installerの11テスト成功。既知manifest限定のupdate・verified backup・条件付きrollbackを追加した。
- GarageBandの未保存状態は「別名で保存」で`build/spu-vab-check.band`へ保持した。
  原本は全11 fileをbackupし、Save AsのAutosave cleanupも復元して開始時hashと一致する。
- host終了後に既存AUを退避して更新。reportは`build/au-backups/au-update-htc4qhrw/manifest.json`。
  old原本・old copy・installed newの全hashを照合した。sandbox外のauvalも成功した。
- [今回の記録と復帰コマンド](au_vab_garageband_20261031.md)。実環境のrollbackは未実行。
- 次: 新AUをGarageBandのcopyで開き、VAB/Toneのみの変更通知・保存・元fileなしの再起動を確認する。

## 2026-10-31 JST — AU更新とGarageBand VAB確認の開始

- ユーザーから継続の指示。cleanな `f312638` から開始し、今回のTODOを実装前に追加した。
- 既存AUのmanifest照合付き退避・更新・rollbackを準備し、GarageBandでは前回projectのcopyを使用する。
- VAB/Toneのみの変更通知、embedded bankの保存、元fileなしでの再起動とUI復元を確認する。
- 既存の未保存projectがあれば保持し、ユーザーによる処理が必要な場合だけ確認する。
- 次: installerの検証と、現在のGarageBand画面・既存AUのread-only確認。

## 2026-10-31 JST — VAB UIのCIも成功

- 実装 `80f895cb462ebd1179398eb5562f24b3d8a006b8` をorigin/mainへpushした。
- [CI 41191105787](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41191105787) は全job成功。
  Linux offline `124507902503`、macOS `124507902676`。
- 19:02:29〜19:06:14 JST。各5 CTest、APP/AU/VST3 build、auval、公式VST3、実AU/VST3 stateが成功。
  artifactのID / digestと未検証範囲は[VAB UI記録](vab_ui_20261031.md)へ保存。
- 既存AUが前回manifest全5件と一致すること、既存検証projectのAlternativeデータhash一致もread-onlyで確認。
  次回の安全なAU更新・別copyでのDAW VAB確認の入口を記録した。既存AU・projectは変更していない。
- 次はAU更新とGarageBandでのdirty判定・保存再起動・UI復元。Release公開はまだ行っていない。

## 2026-10-31 JST — VAB読み込み・選択UIのローカル検証完了

- 開始TODO `d3f7083` をcommit / pushしてから実装した。
- Load VAB / Builtin / sparse Program・Tone / statusを720×360内へ追加し、Gain・鍵盤を維持した。
- worker上のbounded file読込と、file初回・Program内限定の最初の再生可能Tone選択を接続した。
- editor寿命token、revision / generation fence、restore後のOnIdle表示更新、lock外のhost通知を追加。
- offline / macOS各5 suite、state / exchange ASan・TSan各2 suite、公式VST3 47検査と実AU/VST3のstate/PCM回帰が成功。
- Standaloneで日本語・空白file名の読込、Program 3/11、Tone 1、Builtin復帰、失敗とcancel後の選択保持を確認した。
  最終binaryの起動と描画も確認し、検証後終了。物理的な実聴・DAW VAB操作確認は別。
- AU hostはsandboxのCoreAudio制限により承認済みsandbox外実行で成功。既存AUとGarageBand projectは変更していない。
- [詳細・再現手順](vab_ui_20261031.md)。次は安全なAU更新、実DAWのdirty判定・保存再起動・UI復元確認。
- 現時点でCIは実装push後に確認する。Release公開・署名・公証は行っていない。

## 2026-10-31 JST — VAB読み込み・選択UIの開始

- ユーザーから継続の指示。cleanな `619f693` から開始し、実装前に今回のTODOを追加した。
- complete VABのbounded worker file読込、Program/Tone選択、準備・採用・失敗表示を接続する。
- 非同期dialogのeditor寿命、host restoreと古いrequestの世代関係、UI起点のhost変更通知を確認する。
- 既存720×360のGainと鍵盤を維持し、embedded stateからのUI再表示を実装する。
- 次: worker入力とUIを並行実装し、core / 実AU・VST3回帰と実際のStandalone操作を確認する。
- 分割VH/VBのdialog、実DAWのdirty判定・保存再起動の最終確認は別項目として記録する。

## 2026-10-31 JST — embedded DAW stateのCIも成功

- 実装 `57a710923af41c7e60971c1e73aa48e7101977b0` をorigin/mainへpushした。
- [CI 41189325333](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41189325333) 全job成功。
  Linux / macOS各5 suite、APP/AU/VST3 build、auval、公式VST3、実VST3、追加の実AU state試験が成功。
- CI 18:35:33〜18:39:16 JST。artifactとsource SHAは[embedded state記録](embedded_state_20261031.md)に保存。
- 次はVABファイル選択・Program/Tone・状態表示のUIと、実DAWでの操作→保存→再起動→復元確認。
- stateは埋め込みbankを持ち、外部pathを必要としない。現時点で画面からのVAB読み込みはまだ公開していない。

## 2026-10-31 JST — embedded DAW stateのローカル検証完了

- 開始TODO `ba2b656` をcommit / pushしてから実装した。
- 不変のfull VAB/selection定義、schema 1 codec、準備済みengineのtransaction commit、custom stateを接続。
- vendorを変えずAU/VST3外側の型・長さ・失敗戻り値を検証するadapterを追加した。
- 原本VABを含むstateで実VST3の44.1/48/96kHz先頭block PCM、25不正ケースの保持、12回連続復元が成功。
- 実AUをworkspaceからprocess内登録し、48kHz ClassInfo/先頭MIDI/PCM/不正入力/legacy移行を確認。
  CoreAudioのsandbox制限で初回失敗、承認後のsandbox外で最終binaryも成功。既存AUの配置は変更なし。
- offline / macOS各5 suite、state+exchange ASan/TSan各2 suite、公式VST3 47検査が成功。
- pending→activeの保存時の隙間はproducer所有のaccepted definitionで補完し、audio側の共有データ破棄はない。
- 詳細・再現・契約は[embedded state記録](embedded_state_20261031.md)。次はfile/Program/Tone UIと実DAW保存復元。
- UIからのVAB読み込み、Release公開、署名・公証はまだ行っていない。

## 2026-10-31 JST — embedded DAW stateの開始

- cleanな `56c1c0e` から再開。実装前にstate用TODOを追加した。
- bank bytes・sparse Program / Tone選択・Gainをboundedなversion付きchunkへ保存する。
- audio側はmutable handleを保存処理へ共有しない。復元は同期準備を終えてからpublishし、最初のrenderを検査する。
- 不正state時に音源・Gain・pending requestを変えず、従来のGain-only stateも読み戻す方針。
- 次: immutable定義とcodec、iPlug custom state接続、実VST3の復元・MIDI試験。

## 2026-10-31 JST — 音源交換のCIも成功

- 実装 `cb8d3f6af992c6fd2b9b7c64fad240b3cf578c6c` をorigin/mainへpushした。
- [CI 41187048655](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41187048655) 全job成功。
  Linux / macOSの4 suite、APP/AU/VST3 build、auval、公式VST3と実VST3検査が成功した。
- CI 18:02:52〜18:06:41 JST。artifactとsource SHAは[音源交換記録](engine_handoff_20261031.md)に保存。
- 次はembedded bank / selection / Gainのcustom state。復元直後の最初のrenderと不正state時の保持を検査してから、VAB読込UIへ進む。
- 交換層をpluginへ接続したが、現UIはloaderを起動しない。実VST3での交換時MIDI検査もstate/UI接続時に追加する。

## 2026-10-31 JST — 音源交換・worker準備のローカル検証完了

- 開始TODOを `058fa89` にcommit / pushしてから実装した。
- `SpuEngineExchange` / `VabLoader`を追加し、pluginのblock境界とMIDI持ち越し処理へ接続した。
- 世代・pending pointer・4件retire ringで所有権を管理。audio側でengineの確保・破棄・独自mutex待機・通知をしない。
- offline / macOS各4 CTest suite、交換テストのASan / TSan、公式VST3 47検査と実VST3の3rate検査が成功。
- 9 group、104 RT probesでC++ new/delete 0、観測94音源確保/94回収、RT回収0。競合・終了・失敗後連続PCMを検査した。
- [詳細記録](engine_handoff_20261031.md)に契約・再現コマンド・未検証範囲を記載。
- AUの別thread MIDI ingressは既存wrapperの制約としてTODOへ追加。今回のhost操作は単一consumerとして直列化する前提。
- 次はimmutable bank/selection定義とembedded custom state、その直後render、file / Program / Tone UI。
- UIからVABはまだ公開していない。Release公開・署名・公証・既存AUの更新はしていない。

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
