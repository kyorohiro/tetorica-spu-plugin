# AU / GarageBand検証 — 2026-10-31

## 結果

macOS ARM64、GarageBand 10.4.12で、実際のAUを選択し、MIDI録音、project保存、
閉じて再読込、設定復元、WAVE書出しまで確認した。
[計測値・SHAと残る確認](au_garageband_20261031.json)。実聴と長時間のリアルタイム試験は別途必要。

## AUの登録と規格検証

`scripts/install_au.py`を作り、`--install`でユーザー専用の
`~/Library/Audio/Plug-Ins/Components/TetoricaSPU.component`へ新規配置した。
既存bundleがないことを事前確認し、コピー後の全file hashがbuild元と一致した。
通常のbuildの自動deployは引き続きOFF。異なる既存bundleを置換する機能は持たない。

`/usr/bin/auval -v aumu Tspu Tetr`成功。登録AUを得られなかったsandbox内の初回実行は失敗として残し、
承認されたsandbox外の再実行で`AU VALIDATION SUCCEEDED.`を確認した。
AudioComponentRegistrarの強制終了や、セキュリティ設定の変更はしていない。

[auvalの出力](auval_20261031.txt)。API / render / format / MIDI検査が成功。
factory preset名をclass dataへ保持していないwarningが1件あるため、TODOに残す。

最初に追加したCI（`41182380646`）は、コピー直後のauvalがcomponentを見つけられず失敗した。
検証本体を通った結果へは読み替えていない。
固定済み[iPlug2公式CI](https://github.com/iPlug2/iPlug2/blob/b64192fe18afd9bc9a1fe324db5aceb48f4a0eee/Scripts/ci/test_project-mac.yml#L75)
にもregistrarの更新処理がある。そこで `scripts/validate_au.py` を用意し、使い捨てGitHub runnerの
実行ユーザーのregistrarだけをSIGTERMで更新し、最大30秒の発見待ちを追加した。
ローカルではこの更新オプションを拒否し、サービスを変更しない。
発見後のauval本検証は一度だけ実行し、失敗をそのまま返す。
ローカルの発見待ち付き検証も成功。登録反映・cacheが原因との見立ては、CI再実行で確認する。

## GarageBand実操作

起動時に既存の未保存projectがあったため、保存確認をキャンセルして保持した。
ユーザー自身が保存・終了した後、空の検証projectを作成。既存projectへSPUを挿入していない。
以降の画面操作はCUAで行った。

1. `build/spu-au-check.band`を新規保存。
2. ソフトウェア音源スロットで`AU Instruments → Tetorica → TetoricaSPU → Stereo`を選択。
3. AUの編集画面にNative SPU / Sine / Gain / 画面鍵盤が描画されることを確認。
4. Smart ControlでGainを変更。数値指定を試したが、実際に反映された値は**99%**だったため、
   保存・復元の確認値も99%とした。指定した37%になったとは扱わない。
5. カウントインとメトロノームをOFF。ミュージックタイピングでA / S / Dを各約700ms押し、MIDI録音。
6. 選択したMIDIリージョンを44.1kHz / stereo / PCM16 WAVEで書出し。
7. projectを保存して閉じ、同じ`.band`を再読込。AUがTetoricaSPU、Gainが99%であることをAXで確認。
8. 同じ選択リージョンを別名のWAVEへ再書出しし、前後を比較。
9. 最後に保存し、録音 / 再生とも停止、AUとGain99%の状態で残した。

検証project・WAVEは`build/`内に保存し、Gitには含めない。

## 出力比較

`build/spu-au-garageband-before.wav`と`build/spu-au-garageband-after.wav`は、
両方とも176,400 stereo frames（44.1kHz、4秒）、PCM16。
音声が入り、冒頭の区間では約130.83 / 146.81 / 164.77Hzの音程を観測した。
PCMの差は最大1 LSB、差のRMSは約2.62e-7（full scale比）。完全なbit一致とは扱わない。

```sh
python3 scripts/check_garageband_exports.py \
  build/spu-au-garageband-before.wav build/spu-au-garageband-after.wav \
  --report build/garageband-export-comparison.json
```

これはGarageBandのeffects / master / 書出し処理を含む比較で、Native SPUの絶対音量校正ではない。
最終スピーカー出力の録音・耳による確認・長時間の負荷試験はしていない。

## VST3の追加検証

実VST3で44.1 / 48 / 96kHzそれぞれ、以下も成功した。

- ブロック末尾のイベント保持、64 + 192 framesへ分割したPCMの一致。
- 同位置のOff→On / On→Offの順序、未整列と整列済みイベントのPCM一致。
- Velocity0とNoteOffのPCM一致。
- 2,049イベントによるoverflow時の停止と、その後の新規発音による復帰。
- processing再開、再activate、sample rate変更で保持音・保留イベントを消し、正常に復帰。

ブロック外のoffsetや未整列入力は、通常のhost契約を超える耐性検査として区別する。
プラグイン本体の変更は不要だった。

## 次

[VAB取り込み計画](vab_import_plan.md)に沿って、UI非依存のパーサーとTone準備関数を作る。
人に確認してもらう実聴項目は、ノイズ・音切れ・演奏latency。既存Web版との主観比較は未実施。
