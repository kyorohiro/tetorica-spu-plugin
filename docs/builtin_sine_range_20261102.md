# 内蔵Sineの高音域対応（2026-11-02）

新規Plugin instanceと **Builtin Sine** ボタンの音源を、MIDI0〜127・±2半音Bendに対応する
5-zone ADPCM Sineへ変更した。これはsoftwareで用意するmultisampleで、SPUのPITCH上限は変更しない。

開始`33bed02`、作業前TODO `df9be26`、実装`6229311`。Native SDK `95cf71a`は維持する。
Web /Tauri /npm /version /tag /公開releaseは変更しない。
[source・bundle・log hash /CI metadata](builtin_sine_range_20261102.json)も保存した。

## 再現と結果

旧版はMIDI92以上が約1,574.9Hzで頭打ちだった。旧static libraryと旧headerを退避してから測定し、
修正後のLegacyについて全128音のPCM hashと測定結果が44.1 /48 /96kHzで完全一致した。
ローカル証跡は`build/builtin-wide-20261102/`に保持する。

| MIDI | 旧版・Bend中央 Hz | 新版・Bend中央 Hz | 新版・Bend +2 Hz |
| --- | ---: | ---: | ---: |
| 92 | 1,574.9 | 1,661.1 | 1,864.7 |
| 104 | 1,574.9 | 3,322.3 | 3,729.5 |
| 116 | 1,574.9 | 6,644.5 | 7,458.9 |
| 127 | 1,574.9 | 12,543.1 | 14,079.6 |

この表は44.1kHzの実PCMから測定した値。SPUの14-bit PITCHへ丸めるため理想周波数と微差がある。
全128音×Bend両端/中央×44.1 /48 /96kHzも独立した周波数推定で検査した。
低sample-rate hostのNyquist制限や従来のlinear rate adapterは改善していない。

## 波形とリアルタイム処理

各sourceは112frames /64bytesの循環ADPCMで、含むSine周期数を1 /2 /4 /8 /16とする。
元factor1の固定bytesは変更せず、追加4組も既存cyclic encoderで一度生成した固定配列とした。
constructor /workerで全RAM uploadを終えてから、全5zoneを全24voiceでKON・loop・KOFFまでprimeする。
Native load_ramはcache全体を無効化するため、prime後のuploadは行わない。

| Note | 周期数 | root Hz |
| --- | ---: | ---: |
| 0〜89 | 1 | 393.75 |
| 90〜101 | 2 | 787.5 |
| 102〜113 | 4 | 1,575 |
| 114〜125 | 8 | 3,150 |
| 126〜127 | 16 | 6,300 |

Note Onで鍵盤番号からzoneを選び、voiceへlatched保存する。上方向+2半音の余裕を確保するため、
中央BendでもNote90/91は高域zoneを使う。BendはPITCHだけを変更し、cursor /ADSR /sourceを再開始しない。
ペダル保持中・Release中も同じzoneを使う。audio callbackでRAM upload・decode準備・C++ new/deleteを行わない。

異なるzoneへ物理voiceを再利用するときは、古いdecoderへ別rootのPITCHを適用したり、旧ENDがRepeatを
上書きしたりすることを避け、新sourceをfresh KONでlatchedし128-native-frame fade-inする。
この過渡PCMはLegacyのfade/restartと同一保証しない。同じ演奏履歴でzone0を使う0〜89は元PCMと一致する。
zone間のADPCM量子化音色・高域スペクトルの同一性や、帯域制限waveformの追加は今回の保証に含めない。

Noise /Software Morphが有効な間はその既存sourceを使い、Offへ戻した次Noteからwideを再開する。
PMONではcarrierだけがwideに対応し、modulatorの112-frame source /RatioによるPITCH上限は維持する。
VAB /任意ADPCM /通常PCM /Software audio /Morph自体のPITCH上限も今回変更しない。

## 旧project・保存・Web

kind0を従来Builtin Sineとして固定し、新しいwide sourceをkind5としてschema10へ追加する。
1024-byte fixed layout /容量上限 /parameter ID0〜25は不変。kind5はsource payloadとProgram/Toneを持たない。
古いschema1〜9のkind0、AU8-byte /VST3の12-byte Gain-only stateは常にLegacyへ復元する。
UIは **Builtin Sine (legacy)** と表示し、Builtin Sineボタンで明示的にwideへ切替できる。
旧projectは自動upgradeしない。元VABやPCMなどのsourceも保持する。

新保存はschema10を使うため、旧Plugin buildでは読めない。配置rollback用には更新前のproject copyを保持する。
今回はinstalled AUとGarageBand projectを変更していない。

Web `.spu.json`は単一sampleなので、wideのまま保存する形式はない。元112-frame /64-byte ADPCMへ投影し、
高音域mappingを失うことを既存export notesへ渡す。保存結果のdetails tooltipから確認できる。
Web再importは通常ADPCM sourceとなりwideへ戻らない。active Morphは従来のblend投影を使う。
notesのdata経路とpure exportを検査したが、この新しい説明の実GUI表示や保存前閲覧は未確認。

## 検証

- Offline /macOS /ASan+UBSanのCTest各29件成功。
- 新unitは全128音×3Bend×3rate、旧全128音と固定元ToneのPCM、新0〜89の元PCM、
  pedal /release中のBend、Noise /Morph masking、PMON carrier、block分割・steal /retriggerを検査した。
  別fresh instanceで全zone×全voice初使用とmode復帰を行い、ordinary /aligned C++ new/delete0を確認した。
- 実AU /VST3の新suiteは各44.1 /48 /96kHzで72周波数測定。zone境界・held Bend非KON・
  sample offset /block分割、state全26parameter /不正restore時activePCM保持、旧schema1〜9 /raw Gainを検査した。
- 実AUはpreset名を含め全19 PASS行、実VST3は全58 PASS行が成功。公式VST3 validatorは47 passed /0 failed。
- Pythonのbounded GarageBand inspectorはschema10 /kind5・旧schemaのkind制限を含め8件成功。
- 実装head `6229311`の[GitHub Actions](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41336715428)は
  offline /macos全2job・全step成功。CI内の新AU配置とauval、実AU /VST3も成功した。
  artifact metadata /digestを保存したが、artifact /log archiveのdownloadや配布artifact実行は行っていない。

実AUはローカルbuildをin-processの一時hostへ直接登録した検査で、audio device /editor /installを使わない。
最初のsandbox内実行はlocal AU factory登録不可で終了し、sandbox外で同じコマンドを実行して成功した。
AU全体のauvalはCIの一時配置で成功した。ローカルinstalled版のauval結果を新buildの結果として扱わない。

## 配置と次の確認

新APP /AU /VST3は`build/macos/out/`。手元installed AUは変更していない。
current installed manifestは引き続き[プリセット名・分割VAB記録](preset_split_vab_20261101.json)。
新旧bundleを取り違えず、配置更新はこのknown manifestと照合してhost保存終了・退避updateを行う。

実聴、高域の音色・zone切替時の過渡音、実DAWの新builtin選択・保存再起動、長時間使用は未確認。
既存の独自file panel /popup /automation録音等の人による確認もTODOに残す。
