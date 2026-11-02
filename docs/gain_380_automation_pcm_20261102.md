# Gain 380%・連続automation・PCM自動反映（2026-11-02 JST）

開始`9a82e5b`、実装前TODO `c85e5e1`。ユーザーの続行指示により、Native SPU Pluginの音量上限、DAW automationの時刻処理、PCM編集の反映操作を実装した。
確定した実装sourceは`850a79f937b3cb6e36c502a434c93b2321aa1332`。最終bundleとStandalone UIの確認を含む。
親repoのFM Synth / Web SPU Synthの380%対応は並行作業とし、ここではNative Pluginの仕様と証跡を記録する。

## Gainと保存互換

- 画面のGainは合計0〜380%。100%を超える範囲はorangeで表示する。
- DAW parameter ID0の`Gain`は従来どおりplain 0〜100%、normalized 0〜1の対応を保つ。旧projectの50%を新しい最大値に合わせて190%へ変換しない。
- ID26へ`Gain Boost`を追加し、範囲1〜3.8、既定1とする。実効係数は`Gain / 100 * Gain Boost`。
- 画面の合計Gainを変更すると、100%以下はbase GainとBoost 1、100%超はbase Gain 100と対応するBoostを、両parameterのhost gesture付きで設定する。DAWのparameter一覧には既存Gainと追加Boostをそれぞれ公開する。
- 既存の5ms Gain smoothingを維持し、scheduled pointの時刻から新しいtargetへの補間を始める。restore / reset後の最初の出力は、保存したtargetから開始する。

state schema11は、従来のbase Gainをそのまま残し、source payloadに続く既存1024-byte固定部分の後ろへBoostのbinary64を8 bytes追加する。
旧schema1〜10とGain-only stateはBoost 1として復元する。新stateにはbaseとBoostの正確な組を保存し、Web / Audio importでも現在の両値を保持する。
新stateは旧schema10までのbuildで読み戻せないため、旧buildで開くprojectの元copyは保持する。
Web音色exportは従来どおりGainを音色へ含めない。

## 連続parameterの時刻処理

Gain / Boost / Morph Amount / Reverb return左右の5項目を、固定容量1024点のtimelineで処理する。
VST3では複数pointを取り込み、AUでは時刻付きSetとSchedule、線形rampを処理する。AUの連続parameterにはCanRamp flagを公開する。

指定sampleの前にparameterを更新し、同じoffsetは受け付け順の後勝ちとする。
block末尾と同じoffsetのpointは次blockのsample 0へ持ち越す。rampは元の開始値・終了値・経過sample数を保ち、block分割で波形を変えない。
負のramp開始位置はsample 0時点までの経過分を反映する。
未来point / active rampがあるslotでは、API上すでに最終targetが見えていても、その値を先にDSPへ同期しない。

audio consumerとhost ingressは別の固定配列を使い、try-onceのatomic guardで受け渡す。
容量超過・競合時は待たず、scheduled point / rampを破棄して最新のpublic targetへ戻す。MIDIのpanicと分離し、automation overflowだけで保持音を止めない。
AUのevent /public target更新もtry-once guardで組にして扱い、同じparameterへの並行setterで順序が逆転するのを防ぐ。重なった呼び出しではfallbackを使い、厳密なoffset順序は保証しない。
UI通知はatomic dirty maskへ集約し、UI threadで表示へ反映する。複数host threadからframeworkのSPSC通知queueへ書き込まない。
reset / restore / bypassは旧timelineを破棄する。importされた音源では、immutableなimport tupleを開始値とし、当該block用の新しいingress pointを保持する。
UIの手動parameter変更はoffset 0の新しいtargetとして、既存rampを上書きできる。

Morph / Reverbの連続値と、source topology / Reverb presetなどの離散値を分ける。
離散値はblock開始時に取得し、連続値を刻んで適用する途中でsource切替やpreset変更を再実行しない。

## PCM / Morphの反映操作

`Auto apply`を既定Onとする。shapeの選択とcanvasのstroke完了時にworkerへ準備を要求し、毎pointer moveで音源を交換しない。
PCMとMorph endpointの両方を対象とし、準備した音源を採用すると保持音・残響が止まることを画面に表示する。
Auto applyはeditor側の設定で、音色stateやDAW automationへ追加しない。

Offでは編集結果をdraftとして保持し、未反映表示と`Apply PCM` / `Apply Morph`へ誘導する。
重複要求を抑止し、準備中のA→B→Aでは最後のAを新しい要求として扱う。
失敗した要求をtimerから無限retryしない。単にeditorを開いたときやworker完了を観測しただけでは、元VABを初期PCMへ置き換えない。

## 確認済みのローカル検証

| 対象 | この時点の結果 |
| --- | --- |
| Offline CTest | 32 / 32成功 |
| macOS CTest | 32 / 32成功 |
| Portable Python | 114件成功 |
| 実AU | 48kHz、22 PASS行。summary行を含む |
| 実VST3 | 44.1 /48 /96kHz、63 PASS行。summary行を含む |
| 公式VST3 validator | 47 passed /0 failed |
| timeline単体のASan /UBSan | 成功、diagnosticなし |
| timeline単体のThreadSanitizer | 2 host producer・render /reset consumer、計50,000点で成功 |
| 全core suiteのASan /UBSan | 32 /32成功、194.16秒 |
| 最終bundle再検証 | 実AU22 PASS /VST3 63 PASS /validator47成功・0失敗 |
| Standalone UI | Gain 380% orange、PCM自動・手動Applyを確認 |
| 通常CI | [全2 jobs成功](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41396824927) |
| 配布CI | [Windows /Mac Universal /Intel全3 jobs成功](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41396824926) |

timeline unitでは、sample offset /同時刻 /未来point /block境界 /render分割 /負ramp /整数限界 /有限double両端 /overflow /不正入力 /reset世代 /import baseline /instance独立と、callback中のC++ new /delete 0を確認した。
state unitではschema1〜10のBoost 1 migration、schema11のbase /Boost往復、不正Boostの拒否、32MiB Software PCM stateとpending import tupleを確認した。
PCM反映modelでは既定On /未編集source保持 /重複 /A→B→A /manual draft /明示retry /restore /最新編集を確認した。

実hostのPCM比較は、Gain 380%と5ms smoothing、複数point /未来carry /同時刻 /Morph・Reverbのblock分割、state保存 /旧schema10復元を含む。
1025点のautomation overflowでは最新targetへ復帰し、保持音を停止しないことを確認した。
AUではscheduled Set /Schedule /rampと、不正batchの拒否も確認している。
単体の同時ingress検証と、実DAW固有の並行API呼び出し・UI操作の網羅検証は区別する。

## Standaloneの画面確認

検証用StandaloneのGainを380.0%へ上げ、orangeのbar・数値を確認した。
以降の編集確認はGain 0%で行った。Sourceを開いた時点でAuto apply ON、元Builtinを保持していた。
ShapeからTriangleを選ぶだけで`Embedded PCM cycle` /`Loaded`となり、Applyボタンが無効化された。
Auto applyをOffにしてcanvasを描くと、`manual draft` /`Draft changed. Apply PCM to hear it and save it with the project.`を表示した。
手動Apply後、Auto applyをOnに戻して追加strokeを描き、描き終わりだけで反映済み表示へ戻ることを確認した。
最後はBuiltin Sine /All sound off /Gain 0%へ戻した。実聴とAU独自editor内の操作はこの確認に含めない。

## 再開・仕上げ

core ASan、実AU /VST3 /validator、StandaloneのGain /Auto apply /手描きPCMは確認した。
通常CI全2 jobsと配布CI全3 jobsが、同じ実装source `850a79f`で成功した。
[source・bundle・log hash・CI job metadata](gain_380_automation_pcm_20261102.json)に保存した。
実DAWでのautomation描画 /録音 /editor操作、実deviceの聴感と長時間動作は別途追試する。

公開済み`v0.0.0` tag /Release assets、手元installed AU、Native SDK pinは今回更新しない。
