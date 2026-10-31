# PCM波形選択・描画 — 2026-10-31

実装前のTODOを`16c4bb5`へ保存・pushした。
Web/Tauriの配布物とproject原本は変更しない。Native SDKはReverb時の公開`da089d6`を維持する。

## 操作と音源処理

720×620のbank / Gain / keyboardを共通に保ち、PCM / ADSR / Reverbのpageを切り替える。
PCM pageはSine / Triangle / Saw / Squareと、112-sample周期を描く単一canvasを持つ。
4形状の振幅は既存Builtinに合わせて0.8。PCMは有限なFloat32、範囲は[-1,1]。
画面にSoftwareと表示し、softwareが作ったPCMをSPU ADPCMへ変換する機能として扱う。

shape選択・描画はdraftだけを変える。strokeはmouse-upで確定し、取消・page切替・editor close・
host restoreで途中のstrokeを破棄する。Revert draftは現在適用したPCMへ戻し、Builtin/VABでは
独立したSine draftへ戻す。未Applyのdraftはprojectへ保存しない。

Apply PCMは既存workerへ元PCMのcopyを渡し、循環ADPCM変換・SPU/cache準備を完了してから
engine exchangeへpublishする。audioはblock境界で交換するだけで、変換・RAM upload・確保・
旧engine破棄を行わない。交換で旧保持音と残響を停止するため、次のNote Onから新しい音を使う。
Gain / Custom ADSR / Reverb設定とlive wheel/pedalは維持する。VABの元sample bankは書き換えない。

Apply Aのaudio採用がUI timerより先行し、その間にApply Bを押す場合をレビューで検出した。
遅れて観測したAがdraft Bを消さないよう、`PcmDraftState`が要求世代とdraft revisionを管理する。
要求失敗時のdraft、さらに新しい未Apply編集も保持する。host restoreと別音源の選択は優先する。

## 周期ADPCMと音質

WebのBSD-3-Clause `encodePs1AdpcmCycle`をC++へ移植した。112 PCMを4×28 frames、64bytesへ
変換し、先頭repeat markerと終端END / REPEATを設定する。独立history候補と循環history候補を
実際の2周decodeで採点し、Webと同じRMSのstrict比較で選ぶ。符号付きpredictionのfloor、nibbleの
丸めtie、int16 overflow検証と、FP contraction無効化も合わせる。

Webの8組の元PCM / encoded参照と64bytesすべて一致した。参照は自作波形・silence・DC・
seeded noise・端値/丸めtieで、fixture generatorとWeb source hashを保存した。
explicit PCM Sineと既存Builtinは5ratesで出力が完全一致。初期demo WAVもbyte一致、SHA256は
`7a61b56d356ac198c35808b503cf0b61b55a9547b21770eb2a67c97d0cc4aef3`。

通常のSPU ADPCM量子化・Gaussian補間を使い、host PCMへ置き換えない。
112周期のPITCH上限は従来同じ約1,575Hz、MIDI92以上でclampする。

## 保存形式と互換性

既存17 host parameterとID0〜16を維持する。PCMの選択・描画は音源stateであり、今回automationにはしない。
schema4は既存32-byte header / 72-byte fixed領域を保ち、音源kind2をPCM cycleとする。
payload@32には112個のIEEE754 Float32 LE、448bytesを置く。Program / Toneは0、全体520bytes。
Gain / ADSR / Reverbの相対配置はVAB同様で、ADPCMは保存せず元PCMから準備し直す。

Builtin0とVAB1は従来どおり。schema1/2/3・Gain-onlyも復元する。
kind2はschema4だけに許可し、payload長、選択0/0、finite / 値域をengine準備・commit前に検証する。
不正stateは現在音源・全parameter・正常なpending replacementを保つ。
UIへPCM全112値を戻し、別のcustom周期も同一音源と誤判定しない。

## 検証と再開先

最終Offline / macOS CTestは各7/7成功、APP / AU / VST3 build成功。
ASanはRMS比較修正後の6testsが成功、25.00s、診断なし。draftの世代raceは別のtracked testで成功した。
初期音・Web byte parity・RT C++ new/delete・workerの所有権 / supersessionも成功。
実VST3 44.1/48/96kHz・実AU48kHzで4波形の音程/loop/相違、custom PCMの元112点、最初のNote On、
pending/activeへの不正state拒否、別instance分離、保持音/保留MIDIの破棄、17設定の保持を検証した。
既存ADSR / Reverb / MIDIも成功し、公式VST3 validatorは47成功 / 0失敗。

StandaloneでTriangleのApply、自由描画、Revert、手描きPCMのApplyとpage往復での保持を確認した。
初回GUIでReverbからPCMへ切り替えると旧control領域の背景が残る問題を検出し、切替完了時に
全controlを1回invalidateする修正を追加した。修正後、2回のReverb→PCM直後に表示跡がないことを確認した。
毎Idleの全描画やvendor変更は行わない。Standaloneは検証後に終了した。

source hashとログは[検証JSON](pcm_waveform_20261031.json)へ保存した。

## AU更新とGarageBand

検証copyを旧Reverb版で保存・終了し、schema3のP3/T0/Gain99へ保存されたことを確認した。
7filesを`build/pcm-update-project-7cq9x7vw/spu-vab-check.band`へ照合付きで退避。
旧AU全5filesがReverbの既知manifestと一致する場合だけ、新版へ更新した。
更新recordは`build/au-backups/au-update-kyijyyf8/manifest.json`。

新binary SHA256は`506642967e747e3bcc54aea8171cc919dcfb40d87d9a1b84ba377b49afda98c3`。
旧binaryは`6634a6c8ff1001769575b025acf1da765bb4499f8cd7e9f235e8d1ce3fb076c9`。
source / installed各5files、previous / original各5filesの一致を確認し、更新AUのauvalも成功した。
既存のfactory preset-name warningは残る。project原本`spu-au-check.band`も開始manifest全11filesと一致する。

GarageBand再起動後、Embedded VAB / Program3 / Tone0 / Gain99 / Project sound restoredと
Reverb Off / Enable Off / Return各25% / All notes、新しいPCMボタンを画面で確認した。
更新前後のschema3 payloadは3816bytes、SHA256
`6d8f5e2f1305475a54c21a0998d46e322042740c6cc07311987ade0a7fea3b03`で一致した。
最後は録音・再生停止、AU editor表示。独自PCMボタンへのremote clickはhoverのみで切り替わらず、
AUでのPCM描画操作成功として数えない。Standaloneの実操作とheadless実AUのstate/PCM検査とは区別する。

必要時はhostを保存・終了し、更新後manifestが変わっていない場合のみ戻せる。

```sh
python3 scripts/install_au.py --rollback build/au-backups/au-update-kyijyyf8/manifest.json
```

rollbackは実行していない。次回のAU更新expected recordは`docs/pcm_waveform_20261031.json`。
Release公開・version/tagの更新はしていない。

## Plugin CI

実装`e53d7da7de63445a19c95f527627911f7bf76321`の
[CI 41207294837](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41207294837)は
2026-10-31 22:49:22〜22:53:18 JST、offline / macosの全jobが成功した。
両jobのinstaller11 tests・CTest7/7、auval、公式VST3 validator47/0と、
実VST3 44.1/48/96kHz・実AU48kHzのPCM / ADSR / Reverb / MIDI suiteをログでも確認した。
production sourceとdependency lockはローカル検証時のhashと一致する。

artifact `macos-development-bundles`はID `13044727679`、2,244,493 bytes、digest
`sha256:6c337114e0d9eed983e080ea369ae79880952dc1ba74f35aa2c07bcbc1dc6461`。
metadataのみ確認し、artifactのdownload・別環境での実行はしていない。

## 残る確認

実聴、長時間realtime、実DAWの独自PCM操作・dirty通知は別の確認範囲。
live PCM書換え・Software morph・Noise / PMON・Audio入力は今回の範囲外。
次の機能候補はNoise、続いてPMON。Morph LFOと外部入力Reverbは追加しない。
