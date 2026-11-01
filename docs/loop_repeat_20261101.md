# ADPCMループ互換性 — 2026-11-01

実装 `78e0b8358bd9855dbd061b421656213b2fb34041`、開始TODO `b306c56`。
Native SDKを `95cf71a857b15d0ea932ebf97d5fde80b5e80a95` に固定した。
[機械検証・hash・CI結果](loop_repeat_20261101.json)と、
[親Web Synth / Playgroundの全体TODO](https://github.com/kyorohiro/hello_ymfm_wasm/blob/main/docs/issues/todo_ps1_loop_compat_20261101.md)
（開始checkpoint `b164a303`）を参照。

## 修正した動作

LOOP STARTのflag4だけを持つpacketと、markerがなく明示repeat addressを使うpacketを、
元ADPCM bytesのままループ再生できるようにした。repeat先packetにflag2を要求しない。
END側のRepeat指定でループするかを決め、one-shotとの区別を維持する。
SDKはSTART flag4（bit2）を独立して検出し、cache更新時にもENDのRepeat状態を更新する。
pluginは `validateTone` の今回の拒否条件だけを除いた。

VAB / raw ADPCM / Web v1 / DAW埋込stateの元bytes、root、loop位置、ADSRとsendを保持する。
不正header、範囲外repeat、重複marker、ENDやmetadataの不整合は引き続き拒否する。
state schema9、旧schema互換、26parameter ID、容量上限とUIは変えていない。

旧SDKの公開APIで、repeat先flags0 /4はEND後に無音、flags2 /6は継続することを再現した。
同じ4packet素材を新SDKへ渡すと全flagsのtail peakが `0.0170888007`、ENDXは全て1になった。
これは再現測定であり、音色の比較は以下の別fixtureで行った。

## 検証したこと

| 対象 | 結果 |
| --- | --- |
| Offline CTest | 24/24、6.49秒 |
| macOS Standalone / AUv2 / VST3 buildとCTest | build成功、24/24、6.67秒 |
| ASan＋UBSan | 24/24、128.26秒。`detect_leaks=0`、両sanitizerはエラー時停止。LeakSanitizerは今回の検査対象外 |
| Python | installer11＋GarageBand inspector7、計18成功 |
| 実AU / VST3 | AU48k、VST3 44.1/48/96k。新ループ検査と既存全suite成功 |
| 公式VST3 validator | 47成功、0失敗 |

VAB / Rawそれぞれのmarker4とmarkerless0、非zero repeat位置、非自明なfilter2素材を使い、
同じbytesへtarget Repeat flag2（bit1）だけを加えたflags6 /2対照と比較した。
先頭blockとENDを何度も通過した後の左右PCMが完全一致し、元ADPCM bytesは不変だった。
全26設定、埋込stateからの新instance復元、Web export/import、Noise / PMON / Morphからの復帰も確認した。
長sample・全RAM・分割render・rate変更・callback内C++ new/delete0と、
実際に不正な重複markerがactive /正常pendingを壊さない回帰も成功した。

[Plugin CI 41276718988](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41276718988)は
exact source `78e0b8358bd9855dbd061b421656213b2fb34041` で全workflow1件・job2件成功。
offline /macosの全step、CI環境のAU登録・auval、VST3 validator、実AU/VST3検査、artifact uploadが成功した。
実行は05:22:43〜05:26:50 UTC。成功済みjobの追加ログは取得せず、run /job /step metadataで照合した。
artifact `macos-development-bundles` は2,662,921bytes、
SHA256 `9d9ef039a31de23b021f3b4ba28bc95f529a1d18ab3dd598ce383254933b75e1`。
download、別環境実行、公開Releaseは行っていない。
[Native CI 41275704074](https://github.com/kyorohiro/tetorica-soundchip-native/actions/runs/41275704074)も
exact SDK commitでNative5 platform /WASM7の12検証jobが成功し、Release jobはskippedだった。

## installed AUと未確認範囲

**後続作業で更新済み:** [AU更新・旧project保存再起動の記録](au_loop_update_20261101.md)。
現在のinstalled manifestは[こちら](au_loop_update_20261101.json)。以下はループ実装修正時点の履歴。

**今回、手元のinstalled AUは更新していない。**
[Software audio記録](software_audio_20261101.json)の5filesとread-onlyで一致を確認した。
installed binaryは `b46c3150a8c1b5a0c22ce41fce78bc04d38b39a8442f99392bf03ed5d585ea7f`。
新build AUの `60a6c1967f4fb56bb54f89a24f33769920b331f28ad8024969c9c861b805c368` を、
offline hostが直接読み込んだ検査とは区別する。今回GarageBandや既存projectは操作していない。

更新する場合はaudio hostを保存・終了し、READMEのknown-manifest退避・rollback手順で
`docs/software_audio_20261101.json` をexpectedとして使う。
人による実DAWの独自Load sound / Program / Tone操作、loop素材の保存・再起動、実聴、
長時間使用、automation録音は[TODO](issues/TODO.md)に残す。
version /tag /release公開、npm、Web/Tauri配布はこのplugin作業では変更していない。
親Web側のWASM・browser・配布整合性は親repoの記録へ分ける。

## 再実行の入口

```sh
python3 scripts/bootstrap.py --check
cmake --preset offline
cmake --build --preset offline --parallel 4
ctest --preset offline
cmake --preset macos
cmake --build --preset macos --parallel 4
ctest --preset macos
sh scripts/validate_vst3.sh build/macos/out/TetoricaSPU.vst3
sh scripts/check_vst3_audio.sh build/macos/out/TetoricaSPU.vst3
build/macos/tetorica_au_state_check build/macos/out/TetoricaSPU.component
python3 -m unittest tests/test_install_au.py tests/test_garageband_vab_state.py -v
```

実hostのLoopRepeat共通ケースは `tools/vst3-host/loop-repeat-checks.h`。
この入口はaudio deviceやeditorを開かず、音声出力の物理的な実聴を行わない。
