# 作業記録

## 2026-10-31 JST — NoiseのCI成功・installed AU更新も完了

- 実装`e43d031`とrunner移行`3c10dfd`をpush、
  [CI 41210254431](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41210254431)のoffline / macos-15全job成功。
  両jobのinstaller11・CTest8/8、auval・公式validator47/0、実AU/VST3のNoise / PCM / ADSR / Reverb / MIDIを確認した。
- CIは23:28:07〜23:32:15 JST。初回macOS14 brownout cancellationと成功runを分け、
  metadata / artifact digest / ログを[検証JSON](noise_20261031.json)へ保存した。artifactのdownload/runtimeは未確認。
- AUは既知PCM版5filesを退避し`au-update-j01640dh`で更新、auval成功。新旧bundle各5files・原本11files一致。
  GarageBandで旧schema4 P3/T0/Gain99とSourceボタンを確認、saved payload3816bytesは更新前後で完全一致。
- **次の入口:** PMON。実装前に次の区切りをTODOへ追加する。
  [操作・更新・復帰手順](noise_20261031.md)を参照。実聴・長時間・DAW独自Noise操作は未確認。
  最後は録音/再生停止・AU editor表示。Release公開・version/tag変更はしていない。

## 2026-10-31 JST — Noise CIの旧macOSランナー停止を対処

- 実装`e43d031`の[CI 41209955649](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41209955649)は
  Offlineのinstaller11・CTest8/8成功、macOSはrunner割当前にcancelled（stepsなし）。
- annotationと[公式告知](https://github.com/actions/runner-images/issues/13518)で、10月31日14:00UTC〜
  翌0:00UTCのmacOS14 brownoutと一致することを確認した。Noiseソース/テストの失敗ではない。
- 対応案をTODOへ追加後、workflowをmacos-15へ移して再実行する。bundleの最低OS target13.0は維持する。
  この時点ではauval・GarageBand再表示は未実施。上のチェックポイントで成功を確認した。

## 2026-10-31 JST — Noise実装・実AU/VST3・Source UI検証完了

- TODO `cd0c82d`の後、共有Noise / Clock0〜63を実装。Sample sourceとPCM draft保持、Clockは保持音・Releaseへlive反映。
  Source / ADSR / Reverb3ページ、19params・schema5、旧1〜4/legacy互換。
- private loopはzero host cacheを非audioで準備し、音はNative NOISE branchから出す。物理RAM容量を減らさない。
  初回VAB markerless loop回帰を検出し、物理repeat register復元を追加、RR31・24音・無音後の復帰も完全一致。
- Offline/macOS各8/8、ASan8/8（28.85s）、実AU48kHz / 実VST3 44.1/48/96kHzのNoise / 旧suite成功、validator47/0。
  音声比較側の768frame時間差を修正して厳密比較を維持した。初期Sine WAV hash不変、callback C++ new/delete0。
- StandaloneでNoise切替・Clock48→23入力・page往復・Sampleへ元音源/未Apply Triangle草稿の復帰を確認し終了。
  数値入力後のremote clickはEscapeで復帰、原因未切分け。Noise固有不具合として扱わず記録する。
- [詳細](noise_20261031.md) / [hash・ログ](noise_20261031.json)。次: pushしてCI、AU更新と旧schema4のGarageBand復元。
  現installed AUは旧PCM版、検証copy7filesを退避済み。実聴・長時間・DAW独自Noise操作は未確認。

## 2026-10-31 JST — Noiseモードの開始チェックポイント

- `35d0a0d`、main / origin同期・clean treeから開始。実装前に今回の範囲をTODOへ保存した。
- 初期Noise Off / clock48、共有clockは保持音へlive反映。Sample / Noise切替時は音・残響を停止する。
- 元Builtin / PCM / VABを保持し、one-shotでもNoiseが止まらないprivate loopを準備する。
  Source / ADSR / Reverbの3ページ、既存17parameter維持＋Noise2個、schema5と旧1〜4互換を予定する。
- 次: core・state・Source UI・実AU/VST3検証。この時点ではNoise実装・build・installed AU更新は未実施。

## 2026-10-31 JST — PCM編集のCI成功・installed AU更新も完了

- 実装`e53d7da7de63445a19c95f527627911f7bf76321`をpushし、
  [CI 41207294837](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41207294837)のoffline / macosが全job成功。
  installer11・CTest7/7は両job成功。auval・公式validator47/0、実PCM / ADSR / Reverb / MIDI suiteも全host/rateで成功。
- CIは22:49:22〜22:53:18 JST。artifact metadata / digestとログ根拠を[検証JSON](pcm_waveform_20261031.json)へ保存。
  artifactのdownload・別環境での実行は未確認。production sourceとlockはローカル検証時のhash不変。
- AUはReverbの既知manifestと一致した旧5filesを退避して`au-update-kyijyyf8`で更新、auval成功。
  新旧bundle各5files・project原本11filesを照合した。
- GarageBandで旧schema3 P3/T0/Gain99、Reverb Off / Return各25%、PCMボタンを確認した。
  payloadは更新前後3816bytes完全一致。独自PCMへのAI clickはhoverのみで、Standaloneの操作検証と分ける。
- **次の入口:** Noise、続いてPMON。実装前に次の区切りをTODOへ追加する。
  今回の[操作・更新・復帰手順](pcm_waveform_20261031.md)を参照。実聴・長時間・DAW独自PCM操作は未確認。
  最後は録音/再生停止・AU editor表示。Release公開・version/tag変更はしていない。

## 2026-10-31 JST — PCM編集の実装・実ホスト検証完了

- TODO `16c4bb5`の後、112-cycleの4形状・単一canvasのdraft / Apply / Revert、Software表示を実装した。
- 変換・sample/cache準備はworker、Applyでengine交換する。旧保持音・残響は停止、17parameterは維持。
  schema4は448-byte元PCMを内包し、schema1/2/3とGain-only互換。不正入力は正常pending/現音を保つ。
- Offline/macOS CTest各7/7、ASan6/6、実AU48kHz・実VST3 44.1/48/96kHz、公式validator47/0成功。
  Web8vectors完全一致、explicitPCM SineはBuiltinと5ratesで一致、初期WAV hash不変。
- Apply連打のdraft競合をtracked testで修正。GUIでページ切替時の表示跡を検出・修正し、2回の往復で確認した。
  Standaloneで描画・Apply/Revert・手描きPCM保持も確認し、終了した。
- [詳細](pcm_waveform_20261031.md) / [hash・ログ](pcm_waveform_20261031.json)。
  CIとinstalled AU更新は上のチェックポイントの通り完了。
  検証copyは旧Reverb版で保存・終了し7filesをbackupした。

## 2026-10-31 JST — PCM波形選択・描画の開始チェックポイント

- `adaba1b`、main / origin同期・clean treeから開始。実装前に今回の範囲をTODOへ保存した。
- Webは112 samples / 64-byte cyclic ADPCM。pluginも112周期で4形状と単一canvasのdraft編集を行う。
- Apply時だけworkerで変換・cacheを準備し、既存exchangeで交換する。保持音・残響は交換で停止する。
  今回はlive RAM書換えやMorphではなく、元PCMをprojectへ含める音色編集を実装する。
- 次: coreのcyclic encoder・音源定義/state・PCM page、実AU/VST3回帰を進める。
  この時点ではPCM実装・build・installed AU更新は未実施。

## 2026-10-31 JST — SPU ReverbのCIも成功

- 実装`552a45afdf88635d102dcba910b04be5f60be5cd`をpushし、
  [CI 41204737497](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41204737497)のoffline / macosが全job成功。
- 両jobのinstaller11 tests・CTest5/5、auval、公式VST3 validator47/0、
  実VST3 44.1/48/96kHz・実AU48kHzのReverb / ADSR / MIDI検査をログでも確認した。
- CIは22:15:26〜22:19:38 JST。artifactのmetadataを[検証JSON](reverb_20261031.json)へ保存した。
  artifactのdownload・別環境での実行は未確認。検証後にproduction sourceとlockのhash不変を照合した。
- **次の入口:** PCMの波形選択・描画。実装前に次の区切りをTODOへ追加する。
  実聴・長時間・実DAW automation録音等は未確認のまま保持する。公開Release・version/tag変更はしていない。

## 2026-10-31 JST — SPU Reverb実装・実AU/VST3・AU更新完了

- Native SDKへ残響だけを確保なしにclearするAPIを追加、公開`da089d6`へ固定した。
  SDK CI 41203348557はNative5 / WASM7の全12検証成功、Release job skipped。
- 8preset、Enable、左右Return、All notes / Tone send、ADSR/Reverb pageを追加。
  手動preset選択でEnableを更新、automation/restoreは独立値を保つ。17params・schema3は旧1/2/legacy互換。
- Offline/macOS CTest各5/5、ASan core/VAB2/2、実VST3 44.1/48/96kHzと実AU48kHzのFX/ADSR/MIDI suite、
  公式validator47/0が成功。初期demoはbyte一致。Standaloneの操作を確認した。
- 検証copyを旧AUで保存→schema2へ移行した後7filesをverified backup。
  旧AU全5filesを照合し`au-update-5sucrool`で更新、新旧bundle各5files・原本11files一致、auval成功。
- GarageBandで旧schema2 P3/T0/Gain99とReverb Off/Return各25%/All notes、新pageを確認した。
  payloadは更新前後3792bytes同一。最後は録音/再生停止・AU editor表示。
- [操作・検証・復帰手順](reverb_20261031.md) / [source hash・manifest・SDK CI](reverb_20261031.json)。
  plugin CIも上のチェックポイントの通り成功。次の機能候補はPCM波形選択・描画。
  実聴・長時間・実DAW automation録音等は未確認。公開Release・version/tag変更はしていない。

## 2026-10-31 JST — SPU Reverbの開始チェックポイント

- `34e3a92`、main/origin同期・clean treeから開始。実装前に今回の範囲をTODOへ保存した。
- 既存coreは32wordのexact-matchで7種の近似Reverbを選ぶ。preset / return / send変更は確保不要。
- 現panicはvoiceのみをmuteする。残響だけを消すNative APIを上流へ追加し、検証後のcommitに固定する。
  通常resetは確保・sample cache無効化を伴うためaudio callbackから呼ばない。
- 次: Native / core、後方互換state、ADSR/Reverb page、実AU/VST3検証を進める。
  この時点ではReverb実装・build・installed AU更新は未実施。

## 2026-10-31 JST — ADSRのCIも成功

- 実装`550fbab804305378f6e647d468ff5d5784cbe919`をpushし、
  [CI 41201681624](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41201681624)のoffline / macosが全job成功。
- 新しいADSR検査を含む実AU / VST3 PCM・state、公式validator、installer / CTestをCIログでも確認した。
  詳細は[検証JSON](adsr_editing_20261031.json)。installed AUも同じ実装で更新・GarageBand復元済み。
- **次の入口:** SPU Reverbのpreset / Enable / return / voice send。実装前にその区切りをTODOへ追加する。
  実聴・長時間・DAW automation録音は未確認項目のまま保持する。Release公開・version/tag変更はしていない。

## 2026-10-31 JST — ADSR編集・実AU/VST3検証とinstalled AU更新

- Tone原本を保つCustom ADSR、9つのrate/mode、Apply、編集graphとtyped入力を追加した。
  source raw bit13保持、Off中の旧Release、On edgeでの同値refreshを含む。
- state2は48+bank bytes。旧schema1/Gain-onlyを読み、全12parametersをtransactionで復元する。
- Offline/macOS CTest各5/5、ASan core/VAB2/2、実VST3 44.1/48/96kHzと実AU48kHz、公式validator47/0が成功。
  初期demoは変更前とbyte一致。StandaloneでCopy/graph/typed入力/mode/Applyを操作確認した。
- GarageBandの検証copyを保存して終了、project7 filesをverified backupした。
  旧AU全5files照合後`au-update-uw55khkl`で更新し、newsource/installed・oldbackup/archiveの各5files一致、auval成功。
- 旧projectを開きP3/T0/Gain99・Tone ADSR・Apply On・Project restoredと新panelを確認。
  embedded payloadは前後同一、元project11 filesは開始manifest同一。最後は録音/再生停止・AU editor表示。
- [記録・復帰手順](adsr_editing_20261031.md)、[hash/ログ/manifest](adsr_editing_20261031.json)。
  次: 実装をpushしてCI確認。次の音作り機能はSPU Reverb。実聴・長時間・実DAW automation録音等は未確認。

## 2026-10-31 JST — ADSR編集の開始チェックポイント

- `ed9bb31`、main/origin同期・clean treeから開始。実装前に今回の範囲をTODOへ保存した。
- Tone由来ADSRを保つCustom override、9つのrate/mode、Apply edits、概形graphを追加する。
  Gainのparameter index 0を保ち、旧stateはCustom Offで復元する。
- Native SDKにはlive envelope refresh APIがある。レジスタ更新と明示refreshを使い分け、KONやsample reloadを行わない。
- 次: core・host parameter/state・UIを実装し、実AU/VST3と旧projectの互換性を検証する。
  この時点ではADSRコード・build・検証・installed AU更新は未実施。

## 2026-10-31 JST — MIDI表現のCI成功、installed AU更新も完了

- 実装`69186420360e1b25888b5f17e9572ee957b882ca`をpushし、[CI 41199344245](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41199344245)全job成功。
- 検証copyを保存してGarageBandとAU hostを終了。project7 filesをverified copyへ保持した。
- 前回manifest全5 filesと一致するAUだけを`au-update-8jqmbsej`で退避・更新した。
  新installed/sourceと旧backup/archiveの各hash一致、sandbox外のauval成功を確認。
- GarageBandを再起動し、P3/T0/Gain99のVABと表示を復元、embedded payloadも更新前と一致した。
  原本project全11 filesの開始時hash一致も確認した。最後は録音・再生停止、AU画面を開いた状態。
- [今回の結果・更新/rollback手順](midi_expression_20261031.md)、[manifestとCI](midi_expression_20261031.json)。
- Web版相当の音作り機能を段階移植する計画を保存。次の実装はADSR編集→SPU Reverb。
  Pitch/Sustainは今回はDAW MIDIから操作する。残る実聴・long realtime・live UI交換・Tone-only dirty等は未確認として保持する。

## 2026-10-31 JST — Pitch Bend・Sustainの実AU/VST3検証が成功

- coreとwrapperの実装、VST3全point adapter、16channel設定、bypassとcontroller持越しの対処を完了した。
- 最終実VST3は44.1/48/96 kHz、実AUは48 kHzでMIDIからのPCM検査が成功。
  ±2半音、pedal閾値、channel分離、Release tail、同block複数point、restore/deactivate/overflow、
  将来offsetのcontroller破棄とfresh Note維持、VST3 bypass跨blockペダルを検査した。
- 公式VST3は最終buildで47成功。offline/macOS各5 suite、ASan2 suiteも成功。
  初期Sine PCMは変更前と完全一致。新しいproduction依存は追加していない。
- 初回VST3はconfig headerだけではframework全TUへ16channelが届かず失敗した。
  target compile definitionsへ接続して直した。AU sandbox初回のfactory登録拒否はoutside再実行で成功した。
- [今回の結果とmanifest](midi_expression_20261031.json)、[挙動とコマンド](midi_expression_20261031.md)、
  [Web版との段階移植計画](spu_synth_parity_plan.md)を保存した。
- 次: 実装をcommit / pushしてCIを確認し、保存・停止と既知manifest照合後にinstalled AUを更新する。

## 2026-10-31 JST — MIDI実装の統合検証、VST3 bypassの保留イベントを対処

- coreのBend/Sustain検査、offline/macOS各5 CTest、ASan core/VAB 2 suite、APP/AU/VST3 build、公式VST3 47検査が成功した。
- 固定iPlugのVST3 CCはqueue末尾だけを読むため、plugin側のbounded adapterで全pointを接続した。
  通常Gain/bypassは元のframeworkへ渡し、16channelを公開する。vendor自体は変更しない。
- reviewでbypass中にProcessBlockが呼ばれず、複数blockのpedalイベントが蓄積して古いOnが後から適用される経路を発見した。
  bypass中はqueue・発音・controllersをclearする方針で修正し、解除後のstuck note回帰を追加している。
- 実AU/VST3 host検査の完了後、検証projectを保存・host停止し、既知manifestを照合してAUを明示更新する。
  buildだけではinstalled AUを置換しない。更新前のbundleとprojectを引き続き保持する。

## 2026-10-31 JST — Web機能移植の整理、Pitch Bend・Sustain実装を開始

- ユーザーがPitch Bend→Sustainを承認。Web spu-synthと同じ音作り機能を段階的に移す方向も確認した。
- cleanな`d11edc1`から開始し、先にTODOへ今回の実装・検証範囲を記録した。
- WebのPitchは±2半音、保持音とRelease tailへ再発音せず反映。Sustainは離鍵だけを保留し、解除時に離鍵済みの音をReleaseする。
  Webは画面・PC鍵盤用で、pluginではMIDIチャンネル別Bend/CC64へ拡張する。
- 今回はMIDI offset・controller状態の音源切替/lifecycle・RT安全性も検証する。
  coreと実AU/VST3検証を並行し、rootがwrapper接続・移植計画・全体検証を担当する。
- 既存Web版とinstalled AUは変更しない。live controller値はDAW preset形式へ追加しない。

## 2026-10-31 JST — Program操作後のP3/T0保存・project再表示を確認

- Program 11→3の依頼に対する「Tone 0にならない」という回答は、ユーザーがチャット側の誤作動と訂正した。
  SPU不具合として扱わず、codeは変更していない。
- CUAの実画面ではEmbedded VAB/P3/T0/Gain99/Readyを確認した。P11中間画面は未観測。
  source reviewと既存offline CLIでfixtureのP3/T0・P11/T0の期待値を確認したが、実機P11成功とは分ける。
- 最初のQuit/Save操作は画面変化で保存終了を確認できなかったため、最新画面を読み直した。
  hostのFile→保存を実行し、`build/garageband-program-ui-state.json`でP3/T0/Gain99とbank3744 bytes完全一致を確認。
- 次にFile→閉じる→保存でcopy projectを閉じ、同じprojectを開き直した。
  AXで録音・再生停止、AU editorでP3/T0/Gain99/Project sound restored.を確認した。
  今回はprojectのclose/reopenであり、GarageBand main processの再起動確認ではない。
- 未確認のP11中間画面、Tone-only dirty、popup中の取消・editor closeはTODOへ残す。
  不具合修正なし。検証記録を更新してcommit / pushする。

## 2026-10-31 JST — Tone個別変更の保存・再起動後復元も確認

- ユーザーがTone 1を選択。CUAでもEmbedded VAB/P3/T1/Gain99とReady表示を確認した。
- GarageBandで既存MIDIを短時間再生し停止、editor再表示でLoadedを確認した。
  Gain・regionの内容は変更していない。終了時の保存確認でSaveを選び、main process終了を確認した。
- `build/garageband-tone-ui-state.json`でfull bank3744 bytes一致、P3/T1/Gain99、candidate1/conflict0を確認。
  payload3784 bytesのSHA-256は`272c96ddb52aaca232f2c95674ce6cf2c32a80d3144f36676a58ed0cfd652f2b`。
- GarageBandでcopyを再起動し、録音・再生停止、AU画面でP3/T1/Gain99/Project sound restored.を確認した。
- Toneだけの切替・保存・復元は成功。前述の基準問題があるため、Tone-only dirtyは依然未確認。
- 次はProgram 11→3の各切替でTone 0になるかをユーザーへ依頼した。code/installed AUは変更していない。

## 2026-10-31 JST — Tone選択は人の入力待ち、dirtyの基準を分離

- 開始TODOを`3057dd6`へ保存してpushした。AU画面はP3/T0/Gain99の復元状態。
- CUAでTone controlへclickするとhoverのみで、menuは開かなかった。
  製品のmenu不具合とは判定せず、ユーザーへTone 1だけの選択を依頼した。
- Tone未変更・Gain未変更・region未編集でも、editorを閉じてQuitすると保存確認が出た。
  CancelしてhostのFile→保存を明示後、再度Quitしても保存確認が出たため、再びCancelした。
  この基準では保存promptをTone-only dirty成功の証拠にできない。
- 保存済みbank/P3/T0/Gain99は一致し、payload SHAも前回と同じだった。
  AU通知のsource reviewではTone差分も検出しPresentPreset通知を送るが、DAWのdirty判定は未確認のまま。
- 次: 人のTone選択結果を確認し、保存・embedded state・再起動UIを追試する。
  コード・既存AUは変更していない。

## 2026-10-31 JST — Program/Tone・Toneのみ保存通知の確認を開始

- ユーザーから継続の指示。cleanな`bee1019`から開始し、今回の確認順をTODOへ記録した。
- 保存済みcopyのP3/T0/Gain99を基準にToneだけ変更し、DAWの保存確認を独立して検証する。
  Gain・MIDI regionは変更しない。Program切替とpopup取消は、その後で別に確認する。
- rootがGarageBand操作、並行してAUの既存通知経路をsource reviewする。
- 次: 開いているAU画面を確認しTone 1へ。CUAのAU入力制限が続く場合、必要なclickだけユーザーに依頼する。

## 2026-10-31 JST — AU独自Load VAB成功、保存・GarageBand再起動も確認

- ユーザーが検証用`build/ui-check/garageband-ui-source.vab`を選択した。
  当初のエラー報告は訂正され、実際にはP3/T0/Gain99と`Ready. Waiting for an audio block.`が表示された。
- GarageBandで検証用MIDIを再生すると`Loaded. Play MIDI or the keyboard.`へ遷移した。
  workerで準備済みの音源を次のaudio blockで採用する設計どおりで、code修正は不要だった。
- copyのprojectを保存し、独自Load経由のP3/T0/Gain99とfull bank3744 bytesの完全一致を検査した。
  payload3784 bytes、SHA-256 `f1fa0a3a18ec5e660d7266ee509b6c7f818c9e2c76a04d11fa6d70f1111bae10`。
  matching candidate1件、conflicting candidate0件。read-only報告は`build/garageband-vab-ui-state.json`。
- 再生・録音停止後に保存してGarageBandを完全終了し、同じcopyを再起動した。
  AU editorでEmbedded VAB / P3 / T0 / Gain99 / `Project sound restored.`を確認した。
  今回の読込元copyはその場に保持している。元pathなしの復元・PCM一致は前のP3/T1試験で確認済み。
- 残る確認はProgram/Tone個別操作、Toneのみdirty、dialog/popup中のeditor close/reopen。
  AU toolbarのpreset名が「手動」に変わったことは観測したが、Toneのみのdirty確認とは扱わない。
- 既存AUとplugin codeは変更していない。通過済みのbuild・CIは再実行しない。
  元の`spu-au-check.band`全11 fileも開始時manifestとhash一致した。
  今回の結果を詳細記録・TODOへ反映してcommit / pushする。

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


## 2026-11-01 JST — PMON完了

- 開始TODO `b0f89a3` を実装前にcommit / pushし、実装を `d4698d5` に保存した。
- 12音の隣接voice pair、4変調波形、live Ratio / Depth / waveform、次NoteのDecayと24params / schema6を追加した。旧schema1〜5 / legacy、Noise優先と元音源保持を維持する。
- Native SDKの確保なしhost PCM更新APIを公開し、CIをmacos-15へ移した公開 `d0c2750` へ依存を固定した。
- Offline9/9、macOS9/9、ASan全9suite、実AU48k / VST3 44.1・48・96k、公式validator47/0、auvalと両repositoryのCIが成功。初期Sine WAV hashは従来と完全一致。
- StandaloneのSource / PMON数値 / Hold / draft保持を確認し、AUをknown hash照合と退避付きで更新した。GarageBand旧schema5 P3/T0/Gain99を復元、原本11files不変。
- 詳細・コマンド・hash・未確認範囲は[PMON記録](pmon_20261031.md)と[JSON](pmon_20261031.json)。この時点のAU manifestも同JSON。
- 次の候補はsoftware morph。実装前にTODOへ範囲を記録する。実聴・長時間・実DAW automation録音は人による確認として残る。

## 2026-11-01 JST — Software Morph完了

- 開始TODO `589b263` を実装前にcommit / pushし、実装を `65b6daf` に保存した。Native SDKは2つのprivate host PCM bankを提供する公開 `d0e0324` へ固定した。
- A/B/Resultを1canvasへ表示し、A/B切替・形状選択・自由描画・Apply/Revertを追加。SourceとkeyboardのAmountは同一parameterで、採用済みA/Bを保持音・Release中も再発音せずblendする。これはsoftwareの機能。
- 元Builtin/VAB/PCMと通常PCM draftを保持し、Morph Offで元音源に戻る。Noise優先、PMONとの併用は12音。既存24parameterのIDを維持し26params、schema7と旧1〜6 / legacy互換を追加した。
- Offline / macOS全11tests、ASan全11testsと最終state / exchange / draft再検証、strict、実AU48k / VST3 44.1・48・96k、公式validator47/0、auval26paramsが成功。初期Sine WAV hashは従来と一致。
- SDK CI `41249290598` の全12検証job、plugin CI `41250360041` の両job成功。artifactはmetadataのみ照合し、download・別環境実行は未確認。
- Standaloneで単一canvas・A/B描画・Apply/Revert・Amount2か所の相互同期・draft保持を確認した。AUをmanifest照合・退避付きで更新し、GarageBand旧schema6のP3/T0/Gain99とMorph Offを復元。saved payload / VABは不変、原本11files不変。
- 操作・rollback・source hash・installed AU manifestは[Morph記録](morph_20261101.md)と[JSON](morph_20261101.json)。現在のAU manifestはこちらを使う。
- 人による実聴・長時間使用・実DAW独自Morph UIとautomation録音はTODOへ残す。Web/Tauriとversion/tag/releaseは今回変更していない。

## 2026-11-01 JST — Web音色ファイルv1 import完了

- 開始TODO `fd9ec5b` を実装前にcommit / pushし、実装を `81eb78b` に保存した。
- Load soundからcomplete VAB / Web `.spu.json` v1を選択できる。通常ADPCM・PCM・MorphとADSR/Reverb/Noise/PMONを読み、元sample / tuning / loop / signed volume / raw ADSR/send / nameをschema8へ保存する。旧1〜7 / legacyと26parameter IDを維持した。
- file / parse / engine準備はworkerのstagingで行う。失敗は正常pendingも保持し、元sourceと全parameterは同じblock境界で採用する。保存前後のepoch確認で、別sourceのpublicationとimportの26値適用が重なる際の混在tupleを避けた。
- Web writer/reader生成8fixtures、Offline14/14、最終macOS14/14、ASan全14と最終loader/exchange再検証2/2、strict、実AU48k / VST3 44.1・48・96k、公式validator47/0、auval26params成功。初期Sine WAV hashも不変。
- [CI41255456421](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41255456421)はproduction commitで両job・全step成功。新JSON headerは既存pinned iPlug2内のnlohmann3.12.0 MITを使い、offline CIにも必要なsubmodule initとbundle noticeを追加した。
- StandaloneでMorph/PMON/Reverb反映・単一canvas・v2エラー時の元音保持、最終buildでNoise clock8を確認。AUを退避更新してGarageBand旧schema7 P3/T0/Gain99とLoad soundを復元した。saved payload / VABは不変、原本11files不変。
- [操作・制限・rollback](web_instrument_import_20261101.md) / [source hash・現在のinstalled AU manifest・CI metadata](web_instrument_import_20261101.json)。実聴・長時間使用・実DAW独自Web file操作とautomation録音は未確認。
- 次の区切りはWeb向け書き出しと往復仕様の整理。長いAudio v2 / VH+VB UI / PSF抽出は後続。Native SDK・Web/Tauri配布とversion/tag/releaseは変更していない。

## 2026-11-01 JST — Web音色ファイルv1 export完了

- 実装前TODOを`c84dfe3`、実装を`fe24f25`へcommit / pushした。共通Save WebからBuiltin / 選択中VAB Tone / PCM cycle / import済みADPCMを`.spu.json` v1へ保存できる。
- click時点のcoherent saved snapshotをworkerでJSONへ変換し、native Save dialogで選んだ先へatomic replaceする。Cancel / 書込失敗は既存fileを保持し、未承認callbackはeditor close / source変更 / host restoreで破棄する。受理済みwriteはeditor close後も完了する。
- Gainは除外してvoice volumeへ焼き込まない。Custom ADSR / sendは実効値、Reverb bypassはOff、Noise優先のmasked flagsはOffへ変換する。MorphはA/B/Amountと現在blendを保存し、元VAB bankやDAW全stateのbackupとはしない。
- Offline / macOS / ASanは各17/17、strict検査成功。実Web Node reader/writer20件、Chromium20件、WebKit20件で公開file load→実engine→Save downloadを検証した。実AU48k / VST3 44.1・48・96k、公式validator47/0、auval26paramsも成功。初期Sine WAV hashは不変。
- [CI41260258393](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41260258393)は実装commitで両job・全step成功。Standaloneのnative保存・実Web reader読み戻し・Cancelを確認した。
- installed AUをknown manifest照合・退避付きで更新し、GarageBand旧schema8 P3/T0/Gain99と新Save Webを復元した。saved payload / VABは一致、原本11files不変。26parameter ID / state schema8 / SDK固定commitは維持した。
- [操作・制限・rollback](web_instrument_export_20261101.md) / [source hash・現在のinstalled AU manifest・CI metadata](web_instrument_export_20261101.json)。実聴・長時間使用・実DAW独自Save Web操作とautomation録音は未確認としてTODOへ残す。

## 2026-11-01 JST — WAV素材読み込み完了

- 実装前TODOを`39c7276`、実装を`af20bcc`へcommit / pushした。共通Load soundからPCM16 / Float32 mono/stereo WAVを読み込み、softwareでmono / 44.1kHz / SPU ADPCMへ変換する。自動normalizeや容量超過時の無断切捨ては行わない。
- 初期C4 / One shotで、ADPCM時はProgram/Toneの位置をRoot / Playbackへ切り替える。音声はSample / Morph Offで採用し、Gain / ADSR / Reverbと休止設定・要求時のMorph A/Bは保持する。Root / Loop編集では現在の全設定を保持したままworkerで音源を準備する。
- 変換後ADPCMとroot / loopを既存kind3 / schema8に埋め込み、元WAVなしで復元できる。26parameter ID・初期Sine WAV・SDK固定commitは不変。長いhost PCM / Web v2は別段階とした。
- Offline / macOS / ASanは各20/20、strict成功。実AU48k / VST3 44.1・48・96kで4format、元WAVなし復元、root / loop、One shot終了、1packet loop、不正active/pending保持と既存全suiteを検証した。公式validator47/0、auval26paramsも成功。
- StandaloneでPMON+Morph音色からWAVを読み、C3 / Loopへの編集・Save Web・不正Floatエラー時の元音保持とCancelを確認。保存した音色はChromium / WebKit公開UIでも読込・再保存が一致し、休止設定・Reverb33/27・A/Bを保持した。
- [初回CI41265269673](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41265269673)・表示境界補正`a6f1a2e`の[最終CI41266519833](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41266519833)は両job・全step成功。最小subnormal等のRoot表示は実source関数のUBSan probeでも確認した。音声 / stateのsourceはWAV実装以降不変。
- 初回WAV対応AUをknown manifest照合・退避付きで更新し、GarageBand旧schema8 P3/T0/Gain99を復元した。最終AUも再退避更新・auval成功、saved payload / VABと原本11filesは不変。最終editorはCUA clickで開けず、人の再表示確認へ残した。
- [操作・制限・rollback](wav_import_20261101.md) / [source hash・現在のinstalled AU manifest・CI metadata](wav_import_20261101.json)。実聴・長時間使用・最終AU editor再表示・実DAW独自WAV UIとautomation録音は未確認としてTODOへ残す。Web/Tauri配布とversion/tag/releaseは変更していない。

## 2026-11-01 JST — 長いAudio / Software PCM実装チェックポイント

- 開始TODO `cffffe0` の後、短WAVのADPCM bytesを維持し、容量超過を32MiB mono PCM16のSoftware audioへ自動切替する実装を追加。
- Software PCM kind4 / state9、shared immutable buffer、Web v2 base64 PCM16LE import/export（48MiB JSON）、26parameter ID・旧1〜8互換を接続。
- Nativeの長音源・PITCH1時のevent距離overflowをSDK `997256e`で修正・公開しpin。Native CI12検証成功。
- Offline / macOS各24件、ASan/UBSan24件、実AU48k・実VST3全3rate、実Web reader・Chromium/WebKitのsmall/20MiB往復成功。最終監査でSoftware rawMorph自動化時の持越しNoteOff誤破棄を修正し、追加host回帰を実行中。
- Standaloneで20MiB WAV読込、237.77秒表示、Root C4→A4、全体Loop、Save Web v2と元PCM byte完全一致を確認。実聴は未実施。
- 既知AUをhash退避付きで更新（journal `au-update-doadghp0`）。AUval / GarageBand旧saved state / 最終CIと記録は続行中。公開release・version/tag・Web/Tauri/npmは変更しない。

## 2026-11-01 JST — 長いAudio完了・CI成功・AU更新 /旧state復元

- 実装 `a02ee74` の[CI 41271743574](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41271743574)はoffline /macos全step成功。Native `997256e`もCI12検証成功。
- 最終Software rawMorph切替によるcarried NoteOff誤破棄を修正し、実AU48k/VST3全3rateで両方向のrelease一致・無音化を確認。最終macOS24/24、ASan全24＋最終core1成功、Python17、公式validator47/0、auval成功。
- installed AUは `au-update-doadghp0` でhash退避更新、5files一致。GarageBand旧VAB P3/T0/G99を画面で復元確認し、再保存では4768bytesのschema番号byte8だけが8→9に変更。他payload完全一致、原本11files不変。
- Standaloneで20MiB WAV・Root/Loop・Save Web v2の元PCM一致、実Web reader /Chromium/WebKitでsmall/20MiBの往復一致も確認。
- [今回の操作記録](software_audio_20261101.md) / [machine record・current AU manifest](software_audio_20261101.json)。
- **次:** 人のGarageBand Load sound直接操作・保存再起動・実聴/長時間/automation録音。AU内file panelへのAI入力は確認できず、最後の新しい検証copyはVABのまま、再生/録音停止・editor閉。Preview releaseはこれらの後に判断する。

## 2026-11-01 JST — ADPCM loop-start互換性の実装チェックポイント

- 開始TODO `b306c56` の後、SDK `95cf71a` へ固定。repeat先のflag2を必須にしていた制限を解除し、元ADPCM bytes・END /marker /metadata検証を保持した。
- VAB /Rawのmarker4・markerless0（nonzero repeat/filter2）を、従来flag6/2のPCMと先頭〜loop後まで比較。beforeはflag0/4が無音、afterは同一PCMで継続しENDXも保持する。
- offline/macOS/ASan+UBSan各24/24、実AU48k /VST3 44.1/48/96k full、公式validator47/0、Python18件成功。state9 /元bytes /Web保存 /Noise /PMON /Morph /長Software2banksを回帰検証。
- この時点ではSDK CI12検証成功、Plugin CIと親Web側検証は続行中だった。最終結果とinstalled AUを更新しない判断は下段を参照。version/tag /release公開は行わない。

## 2026-11-01 JST — ADPCM loop互換性の検証・Plugin CI完了

- 実装 `78e0b8358bd9855dbd061b421656213b2fb34041`、SDK `95cf71a`。
  [Plugin CI 41276718988](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41276718988)はexact SHAでworkflow1件 /job2件全成功。Native CIも12検証成功。
- 最終offline24/24（6.49s）、macOS APP/AU/VST3 buildと24/24（6.67s）、ASan/UBSan24/24（128.26s、LeakSanitizer対象外）、Python18件、公式validator47/0。
- 実AU48k /VST3 44.1/48/96k fullでVAB /Rawのflag4-onlyとmarkerless0を検査。元bytes /26params /state9保持、first blockとloop後PCMのflag6/2対照一致、Web保存復元、Noise/PMON/Morphからの2bank復帰が成功した。
- 手元のinstalled AUは更新していない。[Software audio manifest](software_audio_20261101.json)全5filesとread-only一致（binary `b46c3150…`）。新build AUを直接読む実host検査とは区別し、今回GarageBandやprojectも操作していない。
- [操作・検証・未確認範囲](loop_repeat_20261101.md) /[hash・CI metadata](loop_repeat_20261101.json)。親Web Synth /Playgroundの反映と配布整合性は[親TODO](https://github.com/kyorohiro/hello_ymfm_wasm/blob/main/docs/issues/todo_ps1_loop_compat_20261101.md)へ分ける。
- **次:** known manifest退避付きのinstalled AU更新を判断し、人が実DAW独自UIでloop素材を読み、保存再起動・実聴・長時間使用を確認する。既存のProgram/Tone dirty・automation録音・release判断も未確認として維持する。


## 2026-11-01 JST — loopfix版installed AU更新の開始

- ユーザーの続行指示で、loopfix実装78e0b83 /SDK95cf71aのAUをknown manifest退避付きで更新し、旧GarageBand保存データを確認する。
- 実行前TODOを追加。host状態・installed5files /buildhash /原本projectを確認後、更新と検証を進める。
- 未保存documentが見つかった場合は破棄せず保護する。公開releaseや別の機能実装はこの区切りへ混ぜない。

## 2026-11-01 JST — loopfix版installed AU更新・GarageBand保存再起動完了

- 既知Software audio manifest全5filesとの一致とhost終了を確認し、`au-update-igtc8_ol`でloopfix版AUへ退避更新した。新source /installedの全5files一致、旧コピー /退避原本の全5filesも旧manifestと一致。条件付きrollback用journalは保持している。
- 配置済みAUの`auval`成功（26params、既知preset-name warningは残る）。実installed bundleを読む48kHz host回帰も全13 PASS行、LoopRepeat4条件 /元bytes /embedded reopen /20MiB Software audioを含む。
- 既知VAB projectを新copyに複製し、GarageBandで保存・完全終了・再起動後のP3/T0/Gain99 /Embedded VAB /Project sound restoredを画面で確認した。schema9 payload4768bytesとVAB3744bytesは完全一致、原本11filesと複製元7filesは不変。
- 開始時の別の既知検証copyは全file退避後に保存した。project /UI /autosaveは変わったがpayloadは一致。新trialを再生・録音停止、AU editor表示状態で残している。
- [検証・戻し方](au_loop_update_20261101.md) /[current installed manifest・hash](au_loop_update_20261101.json)。実聴・長時間動作・新loop/WAV/Web v2の実DAW独自Load・automation録音等は未確認としてTODOに維持した。今回コード・version/tag /公開releaseは変更していない。次のコード候補はGain smoothing。

## 2026-11-01 JST — Gain smoothing開始

- ユーザーの続行指示で、作業前TODOを保存。開始`65ff2fe`、SDK `95cf71a`。
- SPU後段のlive Gain変更へ約5msのsample rampを加え、初回 /成功restoreは保存値へ同期する方針。既存parameter /state形式と通常automationのblock単位処理を維持する。
- コード・unit /実AU・VST3回帰・CIを検証する。開いているGarageBandやinstalled AUは今回のbuildでは置換しない。

## 2026-11-01 JST — Gain smoothingのローカル検証成功

- SPU出力後段へ5ms linear rampを接続し、source swapと成功restore/importを区別した。targetだけを既存state9へ保存し、26parameter IDs /Gain 0〜100%を維持。
- Offline /macOS /ASan+UBSanのCTest各25件が成功。RT区間のC++new/delete0、数式係数・分割完全一致・不正値・resetを確認。
- 実AU48k /実VST3全3rateで左右PCM oracle、down/up/途中変更、state保存target/firstrestore/失敗時保持、reset/reactivate/bypass後のsnapを検証。VST3 validator47/0も成功。
- この時点でCIはこれから確認。installed AU /GarageBandは維持し、実聴・実DAW automation /長時間動作は未確認として残す。

## 2026-11-01 JST — Gain smoothingのCI成功・区切り完了

- 実装`3ec6049` exact headの[CI 41291073473](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41291073473)でoffline /macOS全2job成功。全stepに失敗なし。CI側AU install/auval、VST3 validator /実host、AU実hostも成功した。
- [仕様・検証記録](gain_smoothing_20261101.md) /[source・binary・log hash /CI metadata](gain_smoothing_20261101.json)を保存。artifactはmetadataだけ確認し、download /実行 /release公開はしていない。
- 手元installed AUはloopfix版の全5filesが前回manifestと一致。Gain版への配置とGarageBand旧copyの確認は次の工程。実聴・長時間・automation録音と既存独自UI確認TODOも維持した。
