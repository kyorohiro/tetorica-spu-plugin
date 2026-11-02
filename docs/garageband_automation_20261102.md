# GarageBand Morph /Reverb曲線の再生・保存復元（2026-11-02）

開始`3425b36`、操作前TODO `875e9cc`。今回の対象は実DAWでの操作・曲線再生・保存復元。
既存の実AU hostによるsample-offset /ramp自動検証とは別の確認として記録する。
[確認済み範囲・hash・未確認項目のJSON](garageband_automation_20261102.json)も参照。

## 退避と作業先

- 作業先: `build/garageband-automation-20261102-zphye0it/spu-morph-reverb-automation.band`
- 開始前の全11filesコピー: 同directoryの`baseline-before-automation.band`
- 元の検証copy: `build/au-gain380-update-tqa8b91a/spu-gain380-restore-check.band`
- 退避hash /AU照合: 同directoryの`preflight.json`
- 操作前の埋め込みstate: 同directoryの`project-before-automation.json`

手元AUは`docs/ready_status_20261102.json`の全file manifestと一致した。
元の既知検証copyは退避後に保存し、今回のcopyは「手動保存バージョン」を選んで開いた。
CUAで今回copyの実window URLと再生 /録音停止を確認した。
画面追従の確認は今回copyのDAW masterを一時0（−∞dB）で行った。
WAV書出しはmaster `0.2992126047611237`（−15.0dB）で固定した。
最後は元の`0.7119110226631165`（+0.0dB）、tempo120、cycle Off、再生 /録音停止へ戻して保存した。

## 対象parameter

| ID | DAW表示名 | 値 |
|---|---|---|
| 24 | Enable software morph | Off /On |
| 25 | Software morph A to B | 0〜1 |
| 12 | Reverb preset | Off /Room /Studio Small /Studio Medium /Studio Large /Hall /Space Echo /Half Echo |
| 13 | Enable reverb | Off /On |
| 14 | Reverb return left | 0〜100% |
| 15 | Reverb return right | 0〜100% |

Morph AmountはSourceとkeyboard横の同じparameterへ接続される。
Reverb presetのUI選択だけがEnableもOnにし、host automationのpresetはEnableを変更しない。
Reverb検証はpreset /Enabled /全音sendを固定してから左右Returnへ別の曲線を描く。

## GarageBandの確認方法

Appleの[track automation手順](https://support.apple.com/en-euro/guide/garageband/gbnd939b92d8/mac)と[point編集手順](https://support.apple.com/en-ge/guide/garageband/gbnd65471e12/mac)に従い、Aで表示→track parameter選択→point作成→再生を確認する。
空laneの初回clickは先頭pointを作る。任意位置は既存curve上clickか空laneのdouble-clickで作成できる。

GarageBandが[操作録音として説明しているのはSmart Controls](https://support.apple.com/en-ge/guide/garageband/gbnd40fcef0f/mac)と[region automation](https://support.apple.com/en-ge/guide/garageband/gbnd0fa9da54/mac)。
任意AU parameterのTouch /Latch /Write録音が可能とは扱わず、手描き曲線の再生とリアルタイム録音を区別する。
`check_garageband_vab_state.py`はAU payloadの検査であり、automation curveやactive-track bindingを解析するものではない。

## 現在の確認状況

- [x] AU manifest照合、既知copyの退避、今回専用copyの実URL確認。
- [x] master0でtrack automationを表示した。
- [x] Morph EnabledとAmount曲線の作成、再生時の画面追従。
- [x] Reverb Return左右の曲線作成、再生時の画面追従（人の直接観察を含む）。
- [x] 保存・正常終了・再起動後の音源 /parameter /曲線保持。
- [x] 終了時masterと停止状態を確認し、結果JSONとTODOを更新する。

AIのclickではtrackのparameter popupが開かなかった。人の操作で開いた後はCUA画面取得が2回timeoutした。
人が`Software morph A to B`を選択し、CUAで表示を確認。AIのcanvas clickでは1〜3小節付近に低→高→低の3点曲線を作成でき、画面で曲線を確認して保存した。
AUのSource buttonへのAI clickは届かなかったため、人がRoom preset /Source /Morph Onを設定した。
保存後のAU payloadでもMorph On /Noise Off /Room On /Send all notesを確認した。

### Morph再生の確認

GarageBandの再生On・録音Off・master0を確認した上で、1〜5小節のcycle再生を開始した。
実AU画面でAmount **42%→86%→10%**、Sourceの横sliderとkeyboard横wheelの一致、Result波形の変化を観測した。
これをhost automation受信・表示追従の確認とし、聴感や音声exportの確認とはしない。
テンポを低速へ変更するAX操作は119までの変化にとどまったため、119で確認後に120へ戻した。
再生 /録音を停止して保存し、`after-morph-playback.band`へ全file照合付きcopyを保持した。
証跡は同directoryの`project-after-morph-playback.json`と`morph-playback-observations.json`。

### Reverb曲線の準備

人が`Reverb return left`、続けて`Reverb return right`を選択した。
AIで左は25%付近→高→低、右は25%付近→低→高の各3点曲線を作成し、選択名と曲線を実画面で確認して保存した。
座標clickはウインドウ位置と前面状態の影響を受け、`windowNotFoundAtPosition`とtimeoutが発生した。timeout後も実行済みの操作があるため、画面を取り直して曲線を確認してから続けた。
Smart ControlsのGain操作では絶対値指定が1刻みの変更となり、検証copyのGainが下がった。
人のAU Gain bar操作で380%へ戻し、実画面と保存payloadのGain100 × Boost3.8を照合した。元のstage copyも保持している。

### Reverb再生と保存・再起動

実AU画面でRoom /Enable On /Send all notesを確認して再生した。
AIの連続画像では末尾値9.1% /79.1%が続いたため、人へ8秒の直接観察を依頼し、**左右とも自動で動いている**ことを確認した。画像取得の間隔やfocus等の原因は特定していない。
コード監査でも左右固有の通知漏れは見つからず、UI停止不具合としての修正は行わなかった。

保存・正常終了後、process一覧でGarageBand不在を確認して再起動した。
同じprojectの実window URL、Rightの3点曲線と背景の他2曲線を確認した。
実AUはEmbedded PCM cycle /Morph、380% orange、Project sound restored、Room On /Send all notes、Return42.6% /46.0%、Morph47%を表示した。
保存前・終了後・再起動後のschema11 payload SHA256は完全一致した。
元PCMとMorph A/Bのhashも開始時から維持している。payload検査と実UIの確認は別々に行った。

### 音声export比較

`共有 → 曲をディスクに書き出す…`から、WAVE /非圧縮16ビット（CD品質）を選択した。
`サイクル範囲、または選択したリージョンの長さ（サイクルがオフの場合）を書き出す`を毎回Onにし、1〜5小節をexportした。
3本とも44.1kHz /stereo /PCM16 /352,800frames（8秒）。tempo120、master−15dB、Gain380と曲線・host FXを固定した。Normalizeの設定値は未観測・未変更。

| 比較 | 最大PCM16差 | 既存の1LSB以内の検査 |
|---|---:|---|
| 再起動前 → 再起動後1本目 | 3 | 失敗 |
| 再起動後1本目 → 同sessionの再書出し | 3 | 失敗 |
| 再起動前 → 同sessionの再書出し | 1 | 成功 |

最初の差は21,287 /705,600samples、差RMSは0.190294LSB（約−104.72dBFS）。冒頭だけ減衰し、3.25〜8秒は全PCMが一致した。
±8framesの比較ではずれ0が最小、発音開始frameも一致した。初期状態・残響・host FX等は候補だが、原因は未確定。
同sessionでも差が出るため、再起動後1本目だけを再現性の成功とは扱わない。比較scriptと閾値は変更していない。
各WAVのpeakは0.988494873、非無音を確認したが、これを絶対Gain校正や内部処理の非clip証明とはしない。

結果は`export-strict-results.json`、最初の差の詳細は`diagnostic-reopen-difference.json`。
1LSB条件を通った比較は`export-before-repeat-comparison.json`。音声の微小差の切り分けはTODOへ残した。

### 最後の状態と残り

再生 /録音 /cycle Off、tempo120、DAW master+0.0dBを確認して保存した。
全file照合した最終copyは`verified-automation.band`。Reverb画面を開き、380% /Room On /Morph47%の状態を残した。
今回pluginのsource /binary /SDK /version /tag /公開Releaseは変更していない。

残る確認はSmart Controls等によるリアルタイム操作録音、1〜3LSBのexport初期差の切り分け、実聴・長時間使用。
AUのAuto apply Offの描画・手動Applyも別TODO。v0.0.1 Previewの配布準備は次の区切りとする。
