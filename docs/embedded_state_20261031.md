# Embedded DAW state — 2026-10-31

## 保存・復元

`InstrumentDefinition`はprivateなbankコピーとsparse Program/Tone選択を持つ不変オブジェクト。
保存はこの定義とGainを読み、再生中のSPU handleやRAMを参照しない。
complete VABを丸ごと保存するため、元ファイルのpath・名前・外部bankは復元に不要。
VABを複数レイヤーやSEQ演奏へ展開する機能ではなく、選択Toneを全鍵盤で演奏する状態を保存する。

復元はcodec検証・選択Tone検証・fresh SPU構築・最初のENDまでのcache準備を同期完了し、
必要な確保が成功してから一度に新しい世代をpublishする。解析・準備・Node確保が失敗しても、
現在のGain・active engine・既に準備済みのpending engineを変更しない。
成功後は次のblockで採用し、前のheld noteと繰越MIDIを落として、新しいhost Note Onは保持する。
連続復元でもretire容量が採用を妨げないよう、commit時に制御側で回収する。

stateの呼び出しは非audioのhost lifecycle操作として扱う。復元中はhostのrender/MIDIを静止・直列化する契約で検査した。
復元中の並行renderに対して、Gainとengineのsample単位での同時切り替えを保証するものではない。
保存とaudio採用の並行処理は、producer側のaccepted definitionとatomicなactive観測で橋渡しし、
pending pointerを取り出してからactiveになるまでの隙間にも新しい定義を保存できる。
audio threadはshared_ptrのコピー・破棄をしない。

## 形式

inner stateはlittle-endian、schema 1、最大1 MiB + 40 bytes。

| offset | 値 |
| ---: | --- |
| 0 | 8 bytesの`TSPUST01` |
| 8 | u32 schema = 1 |
| 12 | u32 total bytes = 40 + bank bytes |
| 16 | u32 kind = 0 Builtin / 1 VAB |
| 20 | u32 sparse Program ID |
| 24 | u32 Program内Tone index |
| 28 | u32 complete VAB byte count |
| 32 | owned original VAB bytes |
| 32 + bank bytes | IEEE754 binary64 Gain percent (finite 0–100) |

Builtinはbankなし、Program/Toneとも0。VABは1 MiB以下で、選択ToneのADPCMまで検証する。
保持音・release tail・decoded cache・host sample rateは保存しない。
現在のSDKが再生できないrepeat bitのパターンは、Native構築で拒否する。

AUはこのinner stateをClassInfoのCFDataへ入れる。VST3は末尾に従来互換のu32 bypass (0/1)を付ける。
現在のBuiltinはAU40 bytes / VST3 44 bytes、原本3744-byte VABなら3784 / 3788 bytes。
旧AUの8-byte Gainと旧VST3の12-byte Gain+bypassも読める。旧形式はBuiltinとして復元し、次の保存で新形式になる。
新magicだけの8-byte切り詰めは、極小Gainとして誤受理しないよう明示的に拒否する。

## Host adapter

固定iPlug2のVST3 wrapperはinner復元の戻り値を正しく失敗にできず、AU wrapperは
inner検証前に名前付きpresetを復元する。AUは負の戻り値も成功扱いしてしまう。
そこでvendorは変更せず、plugin-local `StateAdapters.cpp`で次を行う。

- VST3 streamはcap内で読み、完全なinner長とbypassを検証してから復元する。
- AUはCFDictionary / CFNumber / CFString / CFDataの型と長さをコピー前に検証する。
  preset名は説明用で、未検証の`RestorePreset(name)`を実行しない。
- regular paramsは事前に完全検証し、trustedなGain chunkだけを`UnserializeParams`へ渡す。
- 失敗はhostへ返し、commit済みの通知エラーを「復元失敗」として返して状態を食い違わせない。

## 検証結果

| 検査 | 結果 |
| --- | --- |
| offline / macOS CTest | 各5 suite成功 |
| state / exchange ASan | 2 suite成功、検出報告なし |
| state / exchange TSan | 2 suite成功、競合検出報告なし |
| 公式VST3 validator | 47成功、0失敗 |
| 実VST3 restore | 44.1 / 48 / 96kHz、最初のblockから独立instanceとstereo PCM一致 |
| 実VST3 selection | P3T0 / P3T1 fine / P11T0 one-shot、左右Pan、Gain、別instance分離 |
| 実VST3 lifecycle | active/inactiveからの復元、古いheld/deferred除去、新Note維持、12回連続復元 |
| 実VST3 invalid state | 25ケース拒否、Gain・保存state・連続PCM・準備済みpendingを保持 |
| 実AU | 48kHz、ClassInfo round-trip、先頭Note offset37とPCM一致、別instance、CF型/長さ/破損拒否、legacy移行 |
| 原本asset | 元のVAB bytes・Program/Toneをbankごと保持。外部pathなし |

unit testにはtruncation・size/schema/kind/Gain破損・Sample 0/255・ADPCM・確保失敗・snapshot・
共有定義の寿命・callback C++ new/delete 0・80回の並行commit/保存/回収を含む。
sanitizer結果は検査した実行経路についてのもので、全mallocやhard realtimeの保証とは区別する。

AUの検証hostはbundleのfactoryをそのprocess内だけへ登録する。インストール、GUI、audio deviceは使わない。
このCodex環境ではCoreAudio登録がsandbox内で拒否され、承認後のsandbox外実行で成功した。
SandboxSafe metadata付きの最終binaryでも同じ制約を確認した。ユーザーの既存Componentsは更新していない。
GarageBandでVABを操作・保存して再起動する検査は、次のUI接続後に行う。

## 再実行

```sh
cmake --preset offline
cmake --build --preset offline --parallel 4
ctest --preset offline
cmake --preset macos
cmake --build --preset macos --parallel 4
ctest --preset macos
sh scripts/validate_vst3.sh build/macos/out/TetoricaSPU.vst3
sh scripts/check_vst3_audio.sh build/macos/out/TetoricaSPU.vst3
sh scripts/check_au_state.sh build/macos/out/TetoricaSPU.component
```

ASan/TSanは前回の`build/asan-handoff` / `build/tsan-handoff`を再configureし、
`instrument_state_test spu_engine_exchange_test`をbuildして、
`ctest --test-dir ... -R '^spu.(instrument.state|engine.exchange)$' --output-on-failure`で実行した。

## GitHub保存・CI

- 開始TODO `ba2b656`、実装 `57a710923af41c7e60971c1e73aa48e7101977b0`。origin/mainへpush済み。
- [Verify SPU Plugin / 41189325333](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41189325333)
  全job成功。Linux offline `124502646964`、macOS `124502647169`。
- 2026-10-31 18:35:33〜18:39:16 JST。fresh runnerの各5 CTest suite、APP/AU/VST3 build、auval、
  公式VST3、実VST3、今回追加の実AU state検証まで成功した。
- artifact `macos-development-bundles`: ID `13038361338`、2,083,993 bytes、SHA-256
  `0449225a9d8396961ef3cf131de8bf8a048fa4ec76474f0a916e3d6ba1c031e7`。
  upload完了の記録であり、download後の実機試験・Release公開とは別。
- ASan / TSanは上記ローカル実行の結果。今回のCI workflowには含めていない。

## 次の入口

VABファイル選択、Program/Tone選択、読込状況・エラー、復元された音色のUI表示。
その後、実DAWで操作→project保存→再起動→音色と選択UIの復元を確認する。

次の実装前にTODOを更新し、以下を入口にする。固定vendorの調査のみ完了しており、UIは未実装。

- `PromptForFile(..., EFileAction::Open, "vab", completion)`でcomplete VABを選ぶ。
  macOSでは非同期なので、callbackの文字列をコピーし、editor closeで無効になる弱い寿命tokenを使う。
  worker側で1 MiB capを確認してから読み、既存loaderへ接続する。pathは一時入力に留める。
- Program/Toneはparameterに結び付かないbuttonと所有する`IPopupMenu`で選び、tagをsparse Program ID / Toneへ対応付ける。
  `IVMenuButtonControl(kNoParameter)`は固定版でGetParamを参照するため使わない。
- `OnIdle`でloader statusとadopted generationを読む。Readyは公開済み、Loadedはaudio採用済みとして表示する。
  host restoreはUI threadとは限らないため、refresh flagから次のOnIdleでimmutable snapshotを読み直す。
  表示更新のためにengineを再requestしない。
- 既存720×360のGainとkeyboardを保ち、上部にLoad/Builtin・Program/Tone・statusを配置する。
- UI起点の成功した公開だけ、UI threadでhostへ変更通知する。失敗・cancel・host restoreでは通知しない。
  VST3は`GetComponentHandler()`からoptionalな`IComponentHandler2::setDirty(true)`を使う。
  固定版の`InformHostOfPresetChange()`はVST3で空実装。
  AUは`InformListeners(kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global)`でcustom state変更を通知する。
  AUのproject dirty判定とVST3 optional interfaceの対応はhost依存なので、DAWで確認する。
  変更していないGainのautomationを代用しない。
