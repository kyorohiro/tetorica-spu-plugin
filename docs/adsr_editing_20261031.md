# ADSR編集 — 2026-10-31

開始前にTODOを`896ac3e`へ保存・pushした。
Native SDKとiPlugの固定revisionは変更していない。

## 操作と音源処理

初期状態のTone ADSRはBuiltin / VABの元設定を使う。
Copy tone ADSRは元の9項目をhost parameterへコピーし、Custom ADSRを有効にする。
Custom値はbank / Program / Toneの切替で保持し、Tone ADSRへ戻すと新規音は元設定を使う。

Attack rate/mode、Decay rate、Sustain level/rate/direction/mode、Release rate/modeを編集できる。
グラフのA/D/S/Rを左右へ動かしてrateを、Dを上下へ動かしてSLを変更する。
数値入力も可能。グラフはrate codeの概形で、実測時間ではない。
SLは`(code+1)/16`であり、0でも無音ではない。Release 27–31はこのcoreでは無限保持になる。

Applyは初期On。両ADSR wordの更新後に既存Nativeの`ps1_spu_refresh_envelope`を使い、
現在level・sample位置からheld / pedal-held / Releaseの音へ反映する。再KONやsample再uploadはしない。
stageの自然な遷移はあるが、Attackからやり直さない。
Offでは既存音のlatched ADSRを離鍵後まで保持し、新規Note Onから編集値を使う。
Onに戻した時は、raw値が同じでも最新設定を現在音へ反映する。
Webの現checkboxはOnだけでrefreshしないため、この点は意図したUI改善である。

Gain ID 0を保持し、Custom ID1、9項目ID2–10、Apply ID11を追加した。
通常parameterのautomationは固定frameworkに従いblock境界で最後のpointを使う。
MIDIのNote / Bend / Sustainは従来どおりsample offsetで適用する。
元ToneのADSR2 bit13は元bank / sourceに保持し、named編集時にも合成して残す。

## 保存形式

family magicとbank offset32を保持し、schema 2は48+bank bytesとする。
Gain@32+bank、packed ADSR@40+bank、Enable/Apply flags@44+bank。
未知flags・Custom reserved bit13・不正lengthは拒否する。元VABのbit13は合法のまま保持する。
schema 1とlegacy Gain-onlyを読み、Custom Off / Apply On / custom初期値へ移行する。
全検証・engine準備・parameter chunk確保をcommit前に終え、失敗時は全設定と現在音を保つ。

## 完了した検証

- Offline / macOSのCTestは各5/5成功。APP / AU / VST3のbuild成功。
- Coreでlive Attack/Decay/Sustain/Release、Off時の旧Release、新規Note、同値Apply、
  phase継続、24voice（held / pedal / Release）、block分割一致、callback C++ new/delete=0を検査した。
- ASanのcore / VABは2/2成功、診断なし。初期3秒demoは変更前とbyte一致。
  SHA256 `7a61b56d356ac198c35808b503cf0b61b55a9547b21770eb2a67c97d0cc4aef3`。
- 実VST3は44.1/48/96kHz、実AUは48kHzで、全9項目保存、schema1 / legacy移行、
  不正stateの全parameter/PCM保持、復元直後の先頭block、enable/rate automation、
  Off時の保持と次Note、Onへの切替、再発音しない変更を検査した。既存MIDI suiteも成功。
- 公式VST3 validatorは47成功 / 0失敗。installed AUのauval成功。
  既存のfactory preset-name warningは残る。
- Standaloneの実画面でCopy tone→Custom、D drag→D8/SL6、Releaseへ27を入力→hold表示、
  Attack exponentialとApply Offの表示を確認し、終了した。
- グラフheaderの簡易stub harnessでもdrag/clamp・disabled・hold境界・取消を確認した。
  これは実hostでのgesture録音の検証とは区別する。

## installed AUとGarageBand

GarageBandの検証copyを保存・終了し、project7 filesを
`build/adsr-update-project-pfs0qfen/spu-vab-check.band`へ照合付きで保持した。
前回manifestの全5 filesに一致したAUだけを退避・更新した。
更新recordは`build/au-backups/au-update-uw55khkl/manifest.json`。
新binary SHA256は`3cf8e9519bb040553775765721572ef801fb30df575c7f4bdf4bed3802932ee0`。
旧binaryは`5123606ad73f7acd5b4896150f9f9d4f09bbac0c59a60be1ffa28f5d8962d869`。

GarageBandで旧schema1 projectを再起動し、Embedded VAB / Program3 / Tone0 / Gain99 / Tone ADSR /
Apply On / Project sound restoredを画面で確認した。新ADSR panelも表示される。
元の`spu-au-check.band`は編集しない。

必要時はhostを保存・終了してから、更新後manifestが変わっていない場合のみ戻せる。

```sh
python3 scripts/install_au.py --rollback build/au-backups/au-update-uw55khkl/manifest.json
```

rollbackそのものは実行していない。次回の安全なAU更新のexpected recordは
`docs/adsr_editing_20261031.json`を使う。

## 残る確認と次の作業

実聴、長時間realtime、実DAWでのADSR automation録音・drag中editor closeなどは未確認。
AU独自controlへCUAのclickが届きにくい制限は、Standalone操作とheadless AU検証で補った。
Tone-only dirty・Program11中間画面等の以前からのTODOも保持する。
次の実装はSPU Reverb。外部入力ReverbとMorph LFOは対象にしない。
実装`550fbab`の[CI 41201681624](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41201681624)は全job成功。
実AU/VST3のADSR suite、公式validator47/0、各jobのinstaller11件・CTest5件をログで照合した。
開始21:34:02 / 完了21:37:44 JST。artifactは`macos-development-bundles`、ID13043285892、
2,188,245 bytes。upload成功の記録であり、download後の実機検証はしていない。
CI・hash・実行ログの記録は[検証JSON](adsr_editing_20261031.json)を参照。
