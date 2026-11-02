# GarageBand Morph /Reverb automation確認（2026-11-02、作業中）

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
今回copyのDAW masterを一時0（−∞dB）へ下げている。元の値は`0.7119110226631165`（表示+0.0dB）。
終了時はmasterと再生 /録音停止状態を確認する。

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
- [ ] Morph EnabledとAmount曲線の作成、再生時の画面追従。
- [ ] Reverb Return左右の曲線作成、再生時の画面追従。
- [ ] 保存・正常終了・再起動後の音源 /parameter /曲線保持。
- [ ] 終了時masterと停止状態を確認し、結果JSONとTODOを更新する。

AIのclickではtrackのparameter popupが開かなかった。人の操作で開いた後はCUA画面取得が2回timeoutした。
人が`Software morph A to B`を選択し、CUAで表示を確認。AIのcanvas clickでは1〜3小節付近に低→高→低の3点曲線を作成でき、画面で曲線を確認して保存した。
AUのSource buttonへのAI clickは届かなかったため、Room preset /Source /Morph Onの準備を人へ依頼中。
この時点では曲線の作成・保存まで。再生・録音・聴感は未確認。version /tag /公開Releaseは変更していない。
