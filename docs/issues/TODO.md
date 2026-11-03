# SPU Plugin 作業・再開TODO

## 目的

iPlug2OOSを土台に、tetorica-soundchip-nativeのPS1 SPUをDAWから演奏する。
最初の対象はmacOS Standalone / AUv2 / VST3。音声生成はC++で行う。
既存Web版・Tauri版の配布物は変更しない。

## 現在地

Native Sineの最小プラグインはローカルでbuild・自動検証済み。
固定依存とソースはGitHubにも保存した。
[ハッシュ・ローカル検証記録](../verification_20261031.json)、
[最初のCI](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41180834350)も成功。
AU / GarageBandでの録音・保存・復元・WAVE書出しを追試済み。
[AU検証を含む修正版CI](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41183069247)も全job成功。
[VAB解析・offline音源準備](../vab_offline_20261031.md)も完了。
[音源交換・worker準備](../engine_handoff_20261031.md)も実装・検証済み。
[embedded DAW state](../embedded_state_20261031.md)も実装済みで、[CI全job成功](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41189325333)。
VAB file読込・Program/Tone UIを実装し、ローカル検証と[CI全job成功](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41191105787)。[今回の記録](../vab_ui_20261031.md)を参照。
AUを既知manifest照合付きで更新し、GarageBand標準AUプリセット経由でVAB保存・再起動・UI復元・PCM一致を確認した。
[今回の記録](../au_vab_garageband_20261031.md)。人のLoad VAB操作からVAB読込・再生時の採用・保存・再起動後のP3/T0復元も確認済み。
Toneの個別切替・保存・再起動後のP3/T1復元も確認済み。
Program操作後のP3/T0と保存・project再表示も確認した。P11中間画面とToneのみのdirty判定は未確認。
Pitch Bend / Sustainもcore・実AU/VST3・CIで検証し、installed AU更新とproject復元を完了。
ADSR編集とSPU Reverbも実装・ローカル検証・installed AU更新済み。Reverbの[CI全job成功](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41204737497)も確認した。
[ADSR](../adsr_editing_20261031.md) / [Reverb](../reverb_20261031.md)の記録を参照。
PCMの4波形・自由描画・Apply / Revertも実装し、実AU/VST3とStandaloneで検証した。
[PCMの記録](../pcm_waveform_20261031.md)を参照。Noiseも実装・ローカル検証済み。
[Noiseの記録](../noise_20261031.md)を参照。PMONも実装・検証・AU更新・CI成功済み。
[PMONの記録](../pmon_20261031.md)を参照。Software Morphも実装・検証・AU更新・旧state復元・両repo CI成功済み。
[Morphの記録](../morph_20261101.md)と[保存・更新記録](../morph_20261101.json)を参照。
Web音色v1の[import](../web_instrument_import_20261101.md)と[export](../web_instrument_export_20261101.md)も完了した。
WAV素材の読み込み・Root / Playback編集も完了した。[操作・検証記録](../wav_import_20261101.md)を参照。
長いAudio /Software PCMとWeb v2を実装し、実AU/VST3・Web実interop・両repo CI成功済み。
[操作・検証記録](../software_audio_20261101.md)を参照。
flag4-only /markerless repeatの互換性も修正し、元bytesのまま実AU/VST3・CIで検証した。
[実装の記録](../loop_repeat_20261101.md)を参照。loopfix版installed AUへの更新・旧VAB projectの保存再起動も完了した。
[loopfix AU更新・復元記録](../au_loop_update_20261101.md)を参照。
Gain smoothingも実装し、unit /実AU・VST3 /sanitizer /CIを確認した。[検証記録](../gain_smoothing_20261101.md)。
Gain版installed AUへの退避更新・旧VABの保存再起動も完了した。
[Gain版AU更新・復元記録](../au_gain_update_20261101.md)を参照。
CC120のチャンネル別停止も実装・検証・[CI全2job成功](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41299598667)まで完了した。
[仕様](../cc120_20261101.md)を参照。CC120版installed AUへの退避更新とGarageBand旧VABの保存再起動も完了した。
[CC120版の配置・復元記録](../au_cc120_update_20261101.md)を参照。
AUプリセット名warning解消と分割VH/VBのLoad sound対応も完了した。
[最新の実装・配置・復元記録](../preset_split_vab_20261101.md) /[current installed AU manifest](../preset_split_vab_20261101.json)を参照。
内蔵Sineの広域化も実装・CI成功した。[新build検証記録](../builtin_sine_range_20261102.md)を参照。手元installed AUはPSF対応版へ退避更新し、[最新manifest](../au_psf_update_20261103.json)とGarageBandのPCM /Gain380復元、PSFの実DAW操作・保存再起動復元も確認した。下の結果記録を参照。

### v0.0.2 PSF Preview公開（2026-11-03、公開・再download照合完了）

開始`9015b62`。ユーザーがPSF対応版のtag /GitHub Release公開を指示したため、version変更・build前に今回の範囲を記録する。
Previewはannotated `v0.0.2` tagに基づくGitHub Prereleaseとして公開する。実ゲームPSF・実聴・長時間テストはユーザーが後日行う。

- [x] v0.0.1との差分・配布範囲・既知制限を確認し、CMake /config /APP・AU・VST3のversionを0.0.2へ揃えた。state schema11 /parameter27個 /ID /固定依存を維持し、文字・数値version整合を確認した。
- [x] [公開文面の草稿](../release_notes_v0_0_2.md)へPSF folder /Entry /Bank /Program /Tone、抽出VAB保存復元、未対応PSF2 /CPU実行 /曲再生、実ゲーム・実聴・長時間未確認を明記した。CI /公証結果と証跡linkは成功後に追記する。
- [x] version /metadata・ローカルPython114 /Universal build /CTest35/35が成功し、候補source `626dcecfe50ab80c6e97cce803478d0774a3241a`の通常2 /配布3 /署名3 CI jobsすべて成功。Windows x64 VST3とmacOS Universal APP /AU /VST3を検証した。
- [x] 既存の隔離署名workflowでDeveloper ID署名・公証Accepted・stapleとcredential cleanup成功を確認し、同じ最終DMGをApple Silicon /Intelの実hostで検証した。Secrets値を取得・表示していない。
- [x] 最終Mac DMG /Windows ZIPとSHA256 sidecarの4filesを取得し、manifest /source /version /hash /署名・公証を照合した。[候補検証記録](../preview_v0_0_2_20261103.md)に初回手元xcrun失敗と同じDMGでの再検証成功も分けて保存。installed AUは不変、既存v0.0.0 /v0.0.1 tag /assetsを保持する。
- [x] 検証済みsource `626dcec`へannotated v0.0.2 tagを作成・pushし、確認済み4filesと公開本文をGitHub Prereleaseへ公開した。全4assetsを再downloadして候補とのbyte一致・sidecar /GitHub digest・remote tagを照合した。
- [x] README /TODO /公開記録と再開手順を更新する。tag後の[追加通常CI全2jobs](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41549097435)も同じsourceで成功。今回GarageBand projectやinstalled AUは変更していない。

[v0.0.2公開URL](https://github.com/kyorohiro/tetorica-spu-plugin/releases/tag/v0.0.2) /[公開・再取得記録](../release_v0_0_2_20261103.md)。実ゲームPSF・実聴・長時間テストはユーザーが後日行う。

### PSF対応AU更新・GarageBand検証（2026-11-03、完了）

開始`8423bac`、PSF実装`d379b60`。ユーザーがAU更新とGarageBand確認の続行を指示したため、更新前に今回の範囲を記録する。

- [x] 現在のGarageBand project /transportとinstalled AU全filesを確認し、検証projectの別copyと元filesのhashを保持した。既知PCM検証projectを2copy保持し、今回専用copyで作業している。
- [x] 既知manifest `ready_status_20261102.json`と旧AUを照合して全hostを正常終了し、退避・rollback journal付きでPSF版AUへ更新した。journalは`build/au-backups/au-update-23f4rvfb/manifest.json`。
- [x] installed AUの全5files一致・auval・実AU host22 PASSを確認し、GarageBand専用copyのEmbedded PCM cycle /Gain380% /Project sound restoredを実画面で確認した。
- [x] 今回専用projectでPSF folder＋階層PSFLibを読み込み、Entry /Bank /Program /Toneを確認した。人の操作後、Bank1 /Bank2の実AU画面と保存VAB全bytesが一致した。Program11でTone0になることも人が確認した。
- [x] 取消 /不正CRC error時のP3/T1 /Gain380%保持を実AU画面で確認した。人のfolder操作後、保存payload全4776bytesも前後完全一致。
- [x] PSFから選んだBank2を保存・正常終了・process不在確認・元PSF一式退避・再起動し、実URL /AU画面 /再保存payloadでBank2 /P3/T1 /Gain380%の復元を確認した。5段階のpayload SHAが完全一致した。
- [x] 更新manifest・rollback・project退避・確認範囲・未確認事項を記録した。checkpointと最終結果をpushし、公開v0.0.1のtag /assetsは変更していない。

[配置・結果・再開手順](../au_psf_update_20261103.md)。最後はmaster+0.0dB /tempo120 /再生・録音・cycle Off、専用projectのAU editorを表示。実ゲームPSF・実聴・長時間・PSF版公開は別段階。

### PSF1 /MiniPSF 静的VAB抽出のPlugin移植（2026-11-02着手、11-03実装・CI結果記録）

開始`c46c378`。ユーザーがWeb版のPSF抽出をPluginへ追加するよう指示したため、実装前に範囲を記録する。

- [x] Web版のPSF1 header /CRC /zlib /PS-X EXE /PSFLib合成順と、RAM coverage /VAB body誤検出guardをNativeへ移植する。PSF2・曲再生・CPU /driver実行は今回の対象外。
- [x] 依存fileを含む読込・展開・探索をaudio callback外で処理し、file数・圧縮 /展開bytes・再帰・path範囲を制限する。取消・不正入力・古いworker結果・restore時は以前の音源を保つ。
- [x] Load soundからPSF /MiniPSFと必要なPSFLibを読み込み、抽出bank /Program /Toneを選べるUIを用意する。対応範囲と読めない理由を表示する。
- [x] 抽出VABを既存embedded state経路へ渡し、元PSF /PSFLibなしで保存・再起動復元できるようにする。既存parameter ID /state schema /SDK pinは維持する。
- [x] 自作demoと不正入力・依存順・欠損 /cycle /path /展開上限・coverage /曖昧bodyの自動検証、workerの失敗 /取消 /世代検証、実AU /VST3 /build /ローカル検証を行った。CTest35/35・関連ASan/UBSan3/3・実AU /VST3とStandalone画面確認が成功。CIは次の項目。
- [x] 結果・制約・操作方法・再開手順を記録した。実装`d379b60`をpush済み。公開済みv0.0.1 tag /assetsと手元installed AU /GarageBand projectは今回変更していない。

- [x] Windows /Mac ARM /IntelのCIを通し、同じsourceの通常2 /配布3 jobsすべて成功した。[記録](../psf_import_20261102.md) /[証跡JSON](../psf_import_20261102.json)。
- [x] その後の続行指示により、既知manifest照合・退避付きAU更新とGarageBandのPSF folder /Entry /Bank /保存復元も完了した。[更新・実DAW検証記録](../au_psf_update_20261103.md)。元ゲームPSF・実聴・新version公開は別段階。

### v0.0.1 Preview 公開（2026-11-02、公開・再download照合完了）

開始`772bf41`。ユーザーがtag作成・GitHub Preview公開を指示したため、公開前に今回の範囲を記録する。

- [x] 候補source `b80a8a01fd278bda4a1463bdebb6cd8b1fc49422`、全8CI jobs、最終DMG /Windows ZIPと各sidecarの4filesを再照合した。
- [x] 互換性・既知制限と導入方法を含む公開文面を確定し、証跡へのlinkを検証記録commitへ固定した。既存v0.0.1 tag /Releaseがないことも確認した。
- [x] 検証済みsourceへannotated `v0.0.1` tagを作成・pushし、4filesをPrereleaseへ公開した。build /署名はやり直していない。
- [x] 公開後に全4assetsを新しい保存先へ再downloadし、候補とのbyte一致・sidecar・GitHub digest・remote tagのsourceを照合した。
- [x] README /公開記録 /TODOを更新する。tag push後の追加[通常CI](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41436861716)も同じsourceで全2jobs成功。手元AU・GarageBand project、既存v0.0.0 tag /Releaseは変更していない。

[公開URL](https://github.com/kyorohiro/tetorica-spu-plugin/releases/tag/v0.0.1) /[公開・再取得記録](../release_v0_0_1_20261102.md)。実聴 /長時間 /リアルタイム操作録音・冒頭1〜3LSB差の切分けは引き続き下のTODOに残す。

### v0.0.1 Preview 配布準備（2026-11-02、候補検証完了、公開は上記）

開始`9084858`。ユーザーの続行指示により、v0.0.0以降の改善をまとめた次の配布候補を準備する。実装・build前に今回の範囲を記録する。

- [x] v0.0.0との差分とversion定義を確認し、CMake /config /3種のplistをv0.0.1へ揃えた。parameter ID /state schema11 /固定SDKは維持する。
- [x] [公開文面の下書き](../release_notes_v0_0_1.md)へ変更点・導入物・互換性・既知の制限を記録した。GarageBand exportの冒頭1〜3LSB差、リアルタイム操作録音・実聴・長時間動作の未確認を残す。
- [x] version /metadata整合、Python114、Universal build・CTest32/32を確認し、macOS Universal /Windows x64のCI候補を作成した。通常2 /配布3 /署名3の全8jobsが同じsourceで成功。
- [x] 既存の隔離署名workflowでDeveloper ID署名・公証Accepted・staple、同じDMGのARM /Intel実AU /VST3検査が成功。secret値は取得・表示せず、cleanupも成功した。
- [x] [検証記録](../preview_v0_0_1_20261102.md)と[JSON](../preview_v0_0_1_20261102.json)へsource /artifact /hash /CIと再開手順を保存した。installed AUの全5filesは開始時と同一。GarageBand projectへの操作は行っていない。
- [x] tag /公開Releaseは別段階とし、今回は候補と公開文面まで用意した。公開済みv0.0.0のtag /4assetsの一致も照合済み。
- [x] その後のユーザー公開指示により、候補source `b80a8a01fd278bda4a1463bdebb6cd8b1fc49422`をtagにし、4filesをPrereleaseへ公開・再download照合した。上の公開記録を参照。

### GarageBand Morph /Reverb automation（2026-11-02、曲線再生・保存復元完了）

開始`3425b36`。ユーザーの続行指示により、既存自動検証に加えて実DAWの操作・保存復元を確認する。操作前に今回の範囲を記録する。

- [x] 現在のGarageBand projectとinstalled AUを確認し、既知検証projectの別copy・hashを保持して今回専用のcopyで作業する。未確認のユーザーprojectや原本を上書きしない。
- [x] Morph Enabled /Amountを実DAWで操作し、手描きAmount automationの作成・再生時のparameterと画面追従を確認した。実AUで42%→86%→10%、横slider /keyboard wheel /Result波形が連動した。master0での受信・表示確認とする。
- [ ] Smart Controlsのregion automation等を使うリアルタイム操作録音は別途確認する。手描きcurveの再生を操作録音の成功とは扱わない。
- [x] Room /Enabled On /Send all notesを固定し、左右別のReturn曲線を作成・保存した。人の直接観察で左右とも動くことを確認。共有presetのUI auto-enableとhost automationの独立Enableを区別した。
- [x] 記録したprojectを保存・正常終了・再起動し、実URL /音源 /curve表示 /AUの380%・Room・Morphを確認した。保存payloadも完全一致。state inspectorだけでactive track確認を代用しない。
- [x] AI入力が届かない箇所は人の最小操作を依頼し、その間に進められる検証・記録を行った。実聴未実施とautomation録音未確認を成功扱いしない。
- [x] 結果・残作業・再開手順を記録する。今回version /tag /公開Releaseは変更せず、v0.0.1 Previewは別作業とする。
- [ ] 8秒のWAVE exportに生じる冒頭1〜3LSB差の原因を、host FX /初期状態等を分けて調べる。最初の再起動前後比較と同session比較は最大3で既存1LSB条件に失敗し、再起動前と3本目は最大1で成功。全export比較の成功とはせず、閾値を緩めない。

GarageBand10.4.12で手描き3曲線の再生と保存・復元を確認した。[操作・export全結果と残作業](../garageband_automation_20261102.md) /[hash・証跡JSON](../garageband_automation_20261102.json)。最後はmaster+0.0dB /tempo120 /再生・録音・cycle Offへ戻して保存し、AU Reverb画面を残した。手描きcurveとリアルタイム操作録音は区別する。

### 音源準備・音声処理待ちの表示改善（2026-11-02、実装・AU更新完了）

開始`4139641`。ユーザーが表示改善の続行を指示したため、実装前に今回の範囲を記録する。

- [x] worker準備中、準備完了・host音声処理待ち、適用済みを区別し、上段・PCM/Morph Applyボタン・説明文を揃える。
- [x] ReadyではDAW再生を促し、準備済みの音源が次の音声処理で反映されることを説明する。host停止を検出したとは表示せず、plugin内鍵盤だけでhostを起動できるとも案内しない。Standaloneはaudio settingsへの案内に分けた。
- [x] PCM /MorphとVAB /Web /WAVの既存準備・採用markerを使い、失敗・新しい要求・restoreで古いReady表示を残さない。音源交換・queue・state/schema /parameter /SDKは変更しない。
- [x] 実workerを音声callbackなしで準備しReady維持→最初のblockでLoaded、失敗 /supersession /restore /import tupleを検証した。macOS APP /AU /VST3 build、関連CTest3件とASan/UBSan、Standalone Loaded画面、更新済みAUのauval /実host22 PASS /GarageBand復元を確認した。CIは[記録](../ready_status_20261102.md)を参照。
- [x] 結果と配置済みAUのmanifestを記録する。実装`6cac7e8`をpush済み。公開Release・tag /versionは変更せず、既知manifest照合・退避付きで手元AUを更新した。

[仕様・検証と残る実画面確認](../ready_status_20261102.md) /[current installed AU manifest](../ready_status_20261102.json)。

Morph /Reverbの実DAW automation録音とv0.0.1 Preview公開は別の区切りとする。

### Gain 380%版AUの退避更新・GarageBand検証（2026-11-02、更新・保存復元完了）

開始Plugin `d985587`、音源実装 `850a79f`。ユーザーの「進めて」により、既存AUの退避更新とGarageBandでの保存・復元、今回の操作確認を進める。実装前に作業範囲を記録する。

- [x] 現在のinstalled AUを`docs/preset_split_vab_20261101.json`の全file manifestと照合し、最新版buildも検証記録のhashと照合する。
- [x] 開いているGarageBand projectを特定する。既知検証copyは保存前に退避し、原本や未確認のユーザーprojectを上書きせず、hostを正常終了する。
- [x] 既存installerのhash照合・退避・journal・rollback手順でAUを更新する。unknown bundleを置き換えない。
- [x] 配置済みAUでauvalと実AU hostの全回帰を確認する（27params /schema11 /Gain380 /timed automation /旧state migration）。
- [x] 新しい検証project copyで旧VABのP3/T0/Gain99とBoost1復元を確認し、保存・完全終了・再起動後のstate11 /埋め込みbank /parameterを照合する。
- [x] 実AU editorの380% orangeとSource /Auto apply On /Triangle自動反映を人の入力と実画面で確認した。停止中は音声処理待ちとなり、DAW再生でLoadedへ進んだ。Triangle /380%も保存・完全終了・再起動でpayload一致を確認した。Offの描画・Morph /Reverb操作とDAW automation録音は後続の未確認項目へ分ける。
- [x] 配置manifest /退避・戻し方 /検証範囲 /残る実聴・長時間確認を記録し、commit /pushする。version /tag /公開Release・Web配布は変更しない。

配置済みAUのauval（27params）と実AU22 PASS、旧schema9 VAB→11のP3/T0/Gain99 /Boost1、Triangle /Gain100 /Boost3.8の保存復元が成功した。[配置・退避・戻し方と確認範囲](../au_gain380_update_20261102.md) /[current installed manifest](../au_gain380_update_20261102.json)。

- [x] worker準備中とReady（host音声処理待ち）をUIで区別する。GarageBand停止中はDAW再生が必要な場合があることを案内し、ReadyでもPreparingが続く表示を改善した。[今回の記録](../ready_status_20261102.md)。
- [ ] 実AU editorのAuto apply Offで描画・manual Apply、Morph /Reverbの操作とautomation描画・録音を追試する。人の直接操作とAI入力の制約を区別する。

### Gain 380%・連続parameter automation・PCM即反映（2026-11-02、実装・CI完了）

開始`9a82e5b`。ユーザーがDAW automation改善の続行に加え、FM Synth / Web SPU Synth / Native SPU Pluginの380%と、PCM編集の分かりやすい反映操作を指定したため、実装前に記録する。

- [x] 既存Gain ID0のrange0〜100% /normalized mappingと旧音量を維持し、ID26へGain Boost（1〜3.8、既定1）を追加する。画面は合計0〜380%のGainとし、100%超をorangeで区別する。
- [x] schema11へbase GainとBoostを保存し、旧schema1〜10 /Gain-onlyをBoost1で読む。source /parameter ID0〜25を保持し、Web /Audio import時にも両Gain値を保つ。
- [x] Gain /Boostのautomation pointを指定sample offsetで処理し、既存5ms smoothingの開始時刻を守る。Morph Amount /Reverb return左右にも同じ連続parameter schedulerを使う。
- [x] VST3の複数point、AUの時刻付きSet /Schedule、同offset /未来offset /block分割 /MIDI順序 /容量上限 /reset /bypassを検証する。固定容量・audio callback内の確保と待機lockを避け、他の離散parameterのblock処理と区別する。
- [x] PCM /Morph endpointへAuto applyを既定Onで追加する。shape選択 /stroke完了時だけworker要求し、音源交換で保持音・残響が止まることを明示する。Offでは未反映表示とApplyボタンへ誘導する。
- [x] 速い編集のsupersession、重複、A→B→A、失敗 /取消 /restore /closeを検証する。開くだけで現在のVABを初期PCMへ置き換えない。
- [x] unit /sanitizer /実AU /VST3 /Standalone /CIと旧state回帰を確認し、README /parity plan /progressと再開記録をcommit /pushする。

単体・最終bundle /Standalone UI /CIの検証を完了した。

- [x] 固定容量timelineのstep /ramp /同時刻 /未来carry /block分割 /不正値 /overflow /reset世代 /import baseline /callback new・delete 0を検証する。2 producerとrender /resetのThreadSanitizerも成功した。
- [x] base /Boostの保存往復、旧schema1〜10のunity migration、不正Boost拒否、32MiB Software PCMとpending import tupleを検証する。
- [x] PCM apply modelの既定On /未編集source保持 /重複 /A→B→A /manual draft /明示retry /restore /最新編集を検証する。
- [x] Offline /macOS /ASan+UBSanの全core CTest各32件とPython114件を確認する（全ASan194.16秒）。

親repoのFM /Web SPU380%も並行する。公開済み`v0.0.0` tag /assets、installed AU、Native SDK pinはこの作業では変更しない。

ローカル検証はOffline /macOS CTest各32 /32、Python114、実AU22 PASS行・実VST3 63 PASS行（各summary込み）、validator47 /0まで成功した。
全core ASan /UBSan32 /32と2 producerのThreadSanitizerも成功。実装`850a79f`の最終bundle、Standalone Gain /Auto applyと[通常CI全2 jobs](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41396824927)・[配布CI全3 jobs](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41396824926)も成功した。
[仕様・検証と残る実DAW確認](../gain_380_automation_pcm_20261102.md)を参照。

### 画面鍵盤のOctaveとPitch操作（2026-11-02、実装・CI完了）

開始Plugin `dc7d57b`、Native SDK `95cf71a`。ユーザーのPlugin再開指示により、実装前に今回のTODOを記録する。
SPU Synthのitch.io devlogはユーザーが掲載済みと報告した。

- [x] 画面鍵盤へOctave − / ＋と範囲表示を追加する。初期MIDI48〜84の37鍵を保持し、開始位置0 / 12 / … / 84へ切り替える。DAW入力のMIDI noteは変更しない。
- [x] range変更前に元のmouse noteをNoteOffし、pointer cancel / mouse exit / editor closeでもnoteを残さない。Octaveはinstance別editor設定とし、host state / automationへ追加しない。
- [x] 固定±2半音のPitch wheelをMorph横へ追加する。既存のUI MIDI FIFOへchannel 1の14-bit Bendを送り、保持音へ反映する。
- [x] Pitchはrelease / cancel / mouse exit / editor closeで中心へ戻す。UI wheel操作がないeditor開閉で、DAW由来のBendを上書きしない。host restore後の表示と操作も整合させる。reset直後の新gestureをtimerが消すraceも独立レビューで修正した。
- [x] 範囲上下限 /Pitch全16,384値 /cancel /instance独立をunitで検証した。NoteOff-before-range /epochはsource review、core30件 /実AU19 PASS行 /VST3 57 PASS行 /validator47/0 /Python112件とStandalone表示を確認した。実DAWのpointer競合は別途追試する。
- [x] 操作仕様・検証結果・残る実DAW / 実聴確認をREADME / parity plan / progressへ記録し、commit / pushとCIを確認する（実装`94f9f46`、通常全2 /配布全3 jobs成功）。

26個のhost parameter ID / rangeとstate schema10、音源・SDK pinを維持する。
Gain 200%は既存parameter 0のnormalized automation互換を考慮して別途設計する。
公開済み`v0.0.0` tag / assetsと手元installed AUは、この実装では更新しない。

[操作・検証・残る確認](../keyboard_performance_20261102.md) /[source・bundle・log記録](../keyboard_performance_20261102.json)。
[通常CI](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41382566323)と[配布CI](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41382566348)は、同じsource `94f9f46bc128b09387f2ebd29757b719e5298b0a`で全jobs成功した。Mac ARM /Intel /Windowsの実配布host検証を含む。

- [ ] 実DAW内の画面鍵盤 /Pitch操作とMIDI併用・録音、gesture途中のclose /restore、実聴・長時間を追試する。

### macOS Developer ID署名・公証の今回の区切り（2026-11-02、v0.0.0 Preview公開完了）

開始Plugin `d83ed67`、Native SDK `95cf71a`。ユーザーが署名 /公証用Secretsを登録したため、実装前に今回のTODOを保存した（`9d0fbe1`）。続く指示で`v0.0.0` tagとGitHub Releaseへの公開を許可された。

- [x] 必要な5つのRepository secretsの名前だけを確認し、値や秘密鍵をログ /artifact /gitへ出さない。
- [x] Developer ID /hardened runtime /timestamp署名、公証、ticket付与に対応した配布形式をAppleの仕様で確認し、既存のad-hoc Previewを維持して追加する。
- [x] mainからの手動実行だけで動く署名用Actionsを追加する。証明書 /秘密鍵 /公証資格情報は一時keychainへ入れ、失敗時も削除する。
- [x] Universal APP /AU /VST3のstaging copy専用署名・検査を実装する。音源 /schema10 /parameter ID0〜25 /依存pin /installed AUを変更しない。実署名の成功は次項で確認する。
- [x] Actionsで署名 /公証済みartifactを生成し、signature /Team ID /hardened runtime /timestamp /notarization /manifest /SHA256、同じ成果物のARM /Intel実AU /VST3 /auvalを検証する。
- [x] 失敗時の再開点・Apple submission ID・公開可能な検査証跡・残る実機確認を記録し、TODO /READMEを更新してcommit /pushする。
- [x] project /bundle versionを`0.0.0`へ揃え、Mac署名・公証済みDMGとWindows VST3 ZIPの同じsourceの検証が成功してから`v0.0.0` tagを作り、GitHubへPreview Releaseとして公開する。既存tag /releaseがある場合は無断で置き換えない。

秘密鍵のローカル書き出し・Secretsの値の取得、手元AU /GarageBandの操作は行わない。

最初のsource `da7131e`では[通常CI全2job](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41361594937)とWindows候補が成功した。
Mac Previewは固定plistの旧version、[署名CI](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41361750027)は最初のcodesignとkeychain companionのcleanupで停止した（公証への提出前）。5つのSecretsによる証明書取込・notary credentials検証は成功した。
plistとAU metadataを`0.0.0`へ揃え、codesignのchain構築に必要な一時keychainを署名中だけ検索対象へ保持するよう修正した。
cleanupはAppleの公開AtomicFile実装に沿う既知companionだけを検証して削除し、エラー診断は固定語彙で記録する。
修正版`1e46aa6`の通常全2 /配布全3 /署名全3 jobsが成功し、再CIで`.flD3051040`とcleanup `cleaned`を確認した。
Apple公証`2a012f72-2fcf-40e8-af6d-f516e9b53e8a`はAccepted。
同じsourceの`v0.0.0` tagと[Preview Release](https://github.com/kyorohiro/tetorica-spu-plugin/releases/tag/v0.0.0)を公開し、4 assetsの再download・byte /SHA256一致とremote tagのsource一致を確認した。
[公開・検証記録と残る確認](../release_v0_0_0_20261102.md)を参照。手元AUは更新していない。

### macOS /Windows配布候補の今回の区切り（2026-11-02、配布候補・CI完了）

開始Plugin `85c8f4e`、Native SDK `95cf71a`。ユーザーの指示で配布物作成と各OSのActions検証を進める。

- [x] macOSのUniversal（arm64 /x86_64）APP /AU /VST3を、全static dependencyを含めて構築・検証する。
- [x] Windows x64のAPP /VST3に必要なresource・font・MSVC runtime・SDK接続を整え、Windows runnerでbuild /CTest /実VST3 host /validatorを確認する。
- [x] Linuxの固定iPlug2描画 /APP /VST3 /Timer基盤が未対応と確認した。ユーザーの回答により、macOS /Windowsを先に仕上げ、Linux Pluginは別TODOとする。Linux core成功をPlugin配布と混同しない。
- [x] Preview候補ZIPとSHA256 /source・binary manifest /導入手順 /license noticesを作る。macOSのstaging copyだけにad-hoc署名し、Developer ID署名・公証済みとは表示しない。
- [x] GitHub Actionsで候補artifactを生成・取得し、ZIP内容・architecture・同梱物・hashを照合する。既存の音源 /保存互換の回帰も維持する（source `74903c5`、[Preview全3job](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41351695066)・[通常検証全2job](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41351695098)成功）。
- [x] 結果・OS別制限・再開TODOを記録してcommit /pushする。

version /tag /公開Releaseはこの段階では作成せず、具体的な配布候補を先に準備する。
手元installed AU・GarageBand project、Web /Tauri /npm /SDK pinは変更しない。

関連記録: [Preview配布候補](../preview_builds_20261102.md)。Windows ZIPはVST3限定。
APPは内部buildのみとし、旧ASIO SDKの配布条件を確認してから別途判断する。

- [ ] 各OSの実DAWでGUI・native file panel・実deviceの音 /latency・長時間動作を確認する。Mac13.0実機も別確認。
- [x] 配布のversion /tag /署名 /公証 /公開Releaseを準備する。`v0.0.0` Previewを公開済み。正式版判断は残る実DAW /device /長時間確認後とし、手元AUは更新しない。
- [ ] Linux Plugin用platform /Timer /UIを移植するか、別のnative wrapperを採用するか設計する。固定iPlug2には実装がなく、NO_IGRAPHICSだけで対応済みとしない。
- [ ] Windows Standalone配布前に旧ASIO SDKの条件を確認するか、別driver構成へ分ける。

### 内蔵Sine高音域対応の今回の区切り（2026-11-02、実装・CI完了）

開始Plugin `33bed02`、Native SDK `95cf71a`。ユーザーの続行指示で、実装前にTODOを保存する。

- [x] MIDI92以上とPitch Bend両端で、現在の約1,575Hzの頭打ちを再現・数値記録する。
- [x] 内蔵Sine専用の高域用波形と事前準備を追加し、MIDI0〜127・±2半音を演奏できるようにする。Note On /Bend /renderで確保・解放・RAM upload・cache準備を行わない。
- [x] 同じ演奏履歴のMIDI0〜89のPCMと旧project全音域の再生を保つ。旧builtinのclampも含む保存互換を設計し、新規音色との区別を永続化する。既存parameter ID0〜25を維持する。
- [x] 高音域の周波数・境界・Bend・polyphony /sustain /release /PMON・Noise・Morph切替・block分割・state往復・Web exportの対応範囲を検証する。任意VAB /PCM /Morphの実機PITCH上限は別の制約として扱う。
- [x] Offline /macOS /ASan+UBSan各29件、実AU 19 /VST3 58 PASS行、validator47/0、Python inspector8件、[実装head CI全2job成功](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41336715428)を確認し、記録してcommit /pushする。

この区切りではWeb /Tauri /npm /SDK pin /version /tag /公開releaseを変更しない。
手元installed AUは既知manifestのまま保持し、新buildの自動host検証と配置更新を区別する。

実装`6229311`、[仕様・検証](../builtin_sine_range_20261102.md) /[source・bundle・証跡hash](../builtin_sine_range_20261102.json)。
新規Sineはwide、旧schema1〜9のkind0とGain-onlyはLegacyを維持し、新保存はschema10とする。
Web exportは元単一sampleへの投影となり、range損失を結果tooltipへ記録する。

- [ ] known manifest（`docs/preset_split_vab_20261101.json`）でinstalled AUを退避更新し、新wide Sineと旧GarageBand projectの保存再起動を確認する。新buildの自動host検証とは区別する。
- [ ] 実聴で高域、zone境界、cross-zone voice再利用時の過渡音を確認する。新0〜89の同じzone0履歴のPCM一致と、旧project全音域のLegacy保持は検証済み。

### AUプリセット名・分割VH/VB読込の今回の区切り（2026-11-01、完了）

開始Plugin `56f6403`、Native SDK `95cf71a`。ユーザーの続行指示で、作業前TODOを保存する。

- [x] AUのfactory /present preset名とClassInfo保存復元の整合を調べ、既知のpreset-name warningの原因を修正する。独自embedded stateの検証前変更や不正restore時の副作用を加えない。
- [x] 実AU hostにpreset名のproperty round-trip・不正state保持の回帰を加え、既存parameter /音源 /state互換を保つ（実AU48kHz全16 PASS行）。
- [x] workerへ分割VH/VB file読込を追加し、既存parserのbytes・容量検証を再利用する。元VH+VBをbyte-exactに連結したembedded bankとし、元fileなしで保存復元・Web exportできるようにする。
- [x] Load soundでVHまたはVBを選び、対応するもう一方を選択できるようにする。取消 /error /古いdialog callback /editor close時の保全を実装し、file読込と準備をaudio thread外で行う（lifetime /serial /revision /generationを静的レビュー）。
- [x] split fixtureと不正 /世代競合 /容量上限 /embedded保存をunit、既存実AU/VST3回帰を検証する（CTest各28件×3構成、validator47/0成功）。Standaloneで両選択順・2枚目取消・不正pair保持・再試行を確認した。実DAW独自split panelと途中restore /closeの直接操作は未確認。
- [x] 最終head `9e64eac`の[GitHub Actions全2job成功](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41308675010)とartifact metadataを確認する。artifact /log archiveのdownloadや実行は行っていない。
- [x] 検証後にknown manifest付きAU退避更新とauval warning消失・GarageBand旧project保存再起動を確認する（最終`au-update-260st6dc`、26params /16 PASS行 /P3/T0/G99 /payload4768bytes一致）。実DAW独自file panelは未確認として区別する。
- [x] 仕様 /制限 /source・binary・log /配置manifestと再開TODOを記録してcommit /pushする。

初回候補は負番号PresentPresetの名前更新を無視する経路が残り、`au-update-hh1zg247`でrollbackした。
公開API red→greenとApple ABI static_assertを追加し、最終headでwarning消失・CI成功を確認した。
[記録](../preset_split_vab_20261101.md)。次回updateのexpectedは`docs/preset_split_vab_20261101.json`。
GarageBandは`build/preset-split-20261101/spu-preset-split-restore-check.band`のAU editorを表示、再生・録音停止。

- [ ] 実DAW独自Load soundでVH/VB両順序・取消・不正pairを追試する。選択途中のhost restore /editor closeも確認する。Standalone両順序 /取消 /error /retry、workerとstaleガードの検証は完了済み。

既存parameter ID0〜25 /state schema9 /Gain範囲 /SDK pinを維持する。
Web /Tauri /npm /version /tag /公開releaseは今回変更しない。

### CC120版AU更新・GarageBand復元の今回の区切り（2026-11-01、完了）

開始Plugin `476bfa9`、実装`0bb1bb7`、Native SDK `95cf71a`。
ユーザーの続行指示で、作業前TODOを保存してから配置と検証を行う。

- [x] installed全5filesを`docs/au_gain_update_20261101.json`へ、新build全5filesを`docs/cc120_20261101.json`のbuild bundleへ照合する。
- [x] GarageBandの現在のdocument /再生・録音 /保存ダイアログを確認し、既知検証copyを保存前に全11files退避する。原本を保持し、hostを保存・終了する。
- [x] known manifest一致時だけCC120版AUへ退避更新し、journal /旧新hash /rollback手順を保存する（`au-update-anheafyl`）。
- [x] installed版のauvalと実AU host検証を行い、チャンネル別CC120・他channel PCM /controller /shared Reverb保持・PMON停止再利用を確認する（26params、48kHz全15 PASS行）。
- [x] 新しいGarageBand検証copyで旧VAB P3/T0/Gain99 /embedded payloadの復元、保存・完全終了・再起動・editor再表示を確認する（payload4768bytes /VAB3744bytes一致、原本11files・known7files不変）。
- [x] current installed manifest /検証記録 /再開TODOを更新してcommit /pushする。実GUIボタン・実聴・長時間・実DAW automationの未確認範囲を区別する。

[検証記録](../au_cc120_update_20261101.md)。次回updateのexpectedは`docs/au_cc120_update_20261101.json`。
GarageBandは`build/au-cc120-update-v1d1boac/spu-cc120-restore-check.band`上のAU editorを表示、再生・録音停止。

今回は配置と検証のみ。実装 /parameter /state /SDK pin、Web /Tauri /npm /version /tag /公開releaseは変更しない。

### CC120チャンネル別停止の今回の区切り（2026-11-01、完了）

開始Plugin `faa8922`、Native SDK `95cf71a`。ユーザーの続行指示で、作業前TODOを保存する。

- [x] 対象MIDI channelのheld /pedal-held /release voiceを即時muteするcore APIを追加し、PMONのcarrier/modulator両方を停止する。不正channelは無変更とする。
- [x] CC120をそのAPIへ接続し、他channelのvoice /bend /pedal /allocator /resampler phaseを保持する。共有Reverb historyは消さず減衰を継続する。通常のrate変換に保持済みの短いmixed sample historyは維持する。
- [x] UIのAll sound offは独立したthread-safe global panic要求へ接続し、全channel・共有残響停止を維持する。reset /host restore /deactivate /bypass /overflowの既存global cleanupを維持する。
  UIのNoteOnと全停止を同じ固定容量SPSC FIFOに載せる。別atomic flagではframework queueのdrain直後に古いNoteOnが遅れて届くraceがあるため、順序付きcommandとして渡す。
- [x] unitと実AU /VST3でchannel0 /15、held /sustain /release、PMON /Noise /Morph /Software PCM、他channelのPCM連続性、同offset順序 /future offset /分割、controller保持 /state不変を検証する。新処理のRT確保・lockを避ける（全27件×3構成、UI queueのTSan、実AU48k /VST3全3rate成功）。
- [x] build /CTest /sanitizer /validator /CIを確認し、仕様・制限・source /binary /log記録と再開TODOをcommit /pushする（実装`0bb1bb7`、[CI全2job成功](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41299598667)）。

[検証記録](../cc120_20261101.md) /[source・binary・log hash /CI metadata](../cc120_20261101.json)。

- [x] 後続の配置更新でGain版installed AUのknown manifestを照合し、host保存終了・退避update後にGarageBand旧projectの復元を確認した（[配置記録](../au_cc120_update_20261101.md)）。
- [ ] 実DAWのAll sound off buttonを操作し、保持音・共有残響の全停止と停止後の新しいUI NoteOnを確認する。unit /source reviewと実host MIDI CC120検査は成功済みで、実GUI操作は別に確認する。

既存parameter ID0〜25 /state schema9 /Gain範囲を維持する。
実装build時点ではinstalled AUとGarageBandを変更せず、後続の上段作業でknown manifest照合・host保存終了・退避更新を行った。
AU preset-name warning・分割VH/VB UI・version /tag /release・Web /Tauri /npm配布は今回追加しない。

### Gain smoothing版AU更新・GarageBand復元の今回の区切り（2026-11-01、完了）

開始Plugin `fcb2848`、実装`3ec6049`、Native SDK `95cf71a`。
ユーザーの続行指示で、作業前にこのTODOを保存する。

- [x] installed全5filesを`docs/au_loop_update_20261101.json`へ、新build全5filesを`docs/gain_smoothing_20261101.json`へ照合する。
- [x] GarageBandの現在のdocument /再生・録音 /未保存状態を確認し、既知検証copyを保存前に退避する。原本は保持し、hostを保存・終了する。
- [x] known manifest一致時だけGain版AUへ退避更新し、journal /旧新hash /rollback手順を保存する（`au-update-dpj7avip`）。
- [x] installed版のauvalと実AU host検証（Gainの5ms /reset /restore /bypassを含む）を確認する（26params、48kHz全14 PASS行）。
- [x] 新しいGarageBand検証copyでP3/T0/Gain99 /embedded VABの復元、保存・完全終了・再起動・editor再表示を確認し、payload4768bytes /VAB3744bytes /原本11files・known7filesの一致を確認する。
- [x] current installed manifest /検証記録 /再開TODOを更新してcommit /pushする。実聴・長時間・実DAW automation録音・独自Load操作は成功していないものを未確認として残す。

[検証記録](../au_gain_update_20261101.md)。次回updateのexpectedは`docs/au_gain_update_20261101.json`。
GarageBandは`build/au-gain-update-8o13a8ay/spu-gain-restore-check.band`上のAU editorを表示、再生・録音停止。

Gainのsourceやparameter /state形式、version /tag /公開release、Web /Tauri /npm配布は変更しない。

### Gain smoothingの今回の区切り（2026-11-01、完了）

開始Plugin `65ff2fe`、Native SDK `95cf71a`。ユーザーの続行指示で音量変更時の段差を抑える。
作業前にこのTODOを保存する。

- [x] Gainの適用位置とhost automation / reset / restore / importの境界を確認する。
- [x] SPU後段のGainに約5msの直線rampを加える。左右共通のsample係数とし、MIDI分割・scratch / block境界でrampを再開始しない。同値targetでの再開始、RT確保・lockを避ける。
- [x] 初回 / reset・rate変更 / deactivate後 / 成功host restore / imported tuple採用は保存Gainへ即時同期する。通常の音源交換ではrampを維持し、失敗restoreでは進行中rampも保持する。
- [x] 既存Gain 0〜100% / parameter ID0〜25 / state schema9を維持し、保存するのはtarget値だけとする。通常Gain automationのhost block単位の扱いを説明し、sample offset対応の拡張は今回追加しない。
- [x] 44.1 / 48 / 96kHz、上下変更・途中反転・0への到達・定常PCM一致・分割一致・旧state初回音量・不正入力保持をunit / 実AU・VST3で検証する（reset / reactivate / bypass直後のPCMも成功）。
- [x] build / CTest / validator / sanitizer / CIを確認し、結果・制限・次の入口を記録してcommit / pushする（実装`3ec6049`、CTest各25件、VST3 validator47/0、[CI全2job成功](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41291073473)）。

[検証・次の入口](../gain_smoothing_20261101.md) /[source・binary・log・CI metadata](../gain_smoothing_20261101.json)。

実装時点ではinstalled AUとGarageBandを変更せず、後続の上段作業で照合・退避更新した。

- [x] 後続作業でknown manifest照合・host保存終了・退避updateを行い、Gain版AUで旧GarageBand copyの保存再起動を確認した（[記録](../au_gain_update_20261101.md)）。

分割VH/VB UI、version / tag /公開release、Web / Tauri / npm配布は今回変更しない。

### loopfix版AU更新・旧GarageBand復元の今回の区切り（2026-11-01、完了）

ユーザー「では、続きをおねがいします」で、最新版AUへの更新→GarageBand旧project復元確認を開始する。
開始Plugin `1e81db9`、Native SDK `95cf71a`。この節を先に保存し、既存ファイルを保護して進める。

- [x] installed AUを `docs/software_audio_20261101.json` の全file hashと照合し、新buildがloopfix版であることを確認する。
- [x] GarageBandの現在のdocument /再生 /録音 /未保存状態を確認する。保存対象を保護し、hostを終了してから更新する。
- [x] known hash一致時だけ旧AUを退避し、新AUへ更新する。journal /新旧hash /rollback手順を保存する（`au-update-igtc8_ol`）。
- [x] auval /登録と、新installed AUを読む実AU hostのPCM・state回帰を確認する（auval成功、48kHz全13 PASS行、LoopRepeat4条件を含む）。
- [x] 旧GarageBand projectの原本は保持し、新しい検証copyでVAB P3/T0/Gain99・保存設定の復元 /再表示 /保存再起動を確認する。保存payload4768bytes /元VAB3744bytes /原本11filesと複製元7filesは一致。
- [x] 結果 /current installed manifest /人の実聴・独自UI /長時間 /automationの未確認範囲を記録し、commit /pushする。

[記録](../au_loop_update_20261101.md)。次回updateのexpectedは`docs/au_loop_update_20261101.json`。
GarageBandは`build/au-loop-update-nu3dlwkx/spu-loop-restore-check.band`を開き、AU editorを表示中。再生・録音は停止。

分割VH/VB UI・Gain smoothing・version/tag /公開release /Web /Tauri /npm配布は今回追加しない。
新しい音源の実DAW独自Load操作は既存のAI入力制約を見て判断し、成功していない操作を確認済みと扱わない。

### ADPCMループ互換性の今回の区切り（2026-11-01、完了）

- [x] 元ADPCMのflag4-only LOOP START /markerless repeat registerの再現fixtureで、Nativeの開始packet flag2必須制限を確認し、SDKで修正する。END側REPEAT・one-shotの区別を維持する。
- [x] validateToneの今回の制限だけを解除し、VAB /raw ADPCM /Web v1 /state9・旧schemaの元bytes /root /loop /FXを保持する。
- [x] worker /program初期Tone探索 /Web loader /Python inspectorのunsupported期待と説明を更新し、元bytes /PCM持続 /loop位置 /不正入力atomicの回帰を加える。
- [x] 既存flag6 /markerless /Gaussian /long Software2banks /RT new-delete0と、実AU /VST3 /sanitizer /CIを検証し、公開SDK commitへsubmodule /lockを固定する。
- [x] README /検証記録 /再開TODOを更新してcommit /pushする。今回はinstalled AUを更新せず、Software audio版5filesの一致をread-onlyで確認した。
- [x] 後続作業で既知Software audio manifestの照合・退避・rollback手順を使い、installed AU更新と旧VABの保存再起動を確認した（[記録](../au_loop_update_20261101.md)）。
- [ ] 実DAW独自UIで新loop素材を読み込み、保存再起動を確認する。人の実聴と長時間使用も別に確認する。

開始Plugin `6ddd13b`、SDK `997256e`。親Web Synth /Playgroundへの反映もユーザー了承済み。
実装 `78e0b83`、SDK `95cf71a`。offline/macOS/ASan各24件、実AU48k /VST3全3rate、validator47/0、Python18成功。
[Plugin CI全job成功](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41276718988)。[記録](../loop_repeat_20261101.md) /[hash・CI metadata](../loop_repeat_20261101.json)。
全体TODO: [親repo](https://github.com/kyorohiro/hello_ymfm_wasm/blob/main/docs/issues/todo_ps1_loop_compat_20261101.md)（開始`b164a303`）。分割VH/VB UI・Gain smoothing・version/tag /release公開は今回の範囲外。

### 長いAudio / Software PCMの今回の区切り（2026-11-01、完了）

- [x] 既存WAVの入力32MiBを維持し、通常SPU ADPCM容量以内は従来bytesを保つ。超過する素材はmono / 44.1kHz / signed PCM16のSoftware audioへ自動切替し、20MiB級WAVを途中切捨てなく演奏できるようにする。
- [x] 保持PCMは32MiB（16,777,216frames、約6分20秒）、Web v2 JSONは48MiBを独立上限にする。Web側の30分 / 224MiBより小さい対応範囲として明示し、入力サイズ・再標本化後frames・state/base64長を確保前に検証する。
- [x] Software audioはPS1物理RAM外のソフト拡張とUIに表示する。Root / One shot / Loop entire sample、元PCM16・signed volume・ADSR / send / nameを保持し、Gaussian / envelope / PMON / ReverbはSPU coreで処理する。
- [x] bank0はNoise / PMON、bank1は長PCMへ使う。長PCM中はcycle Morphを実効OffとしてUIから使用不可にし、A/B / Amountとraw authored設定は保持する。Noise優先とPMON12組、Sample24音を維持し、callback内で大PCMを書き換えない。
- [x] Native SDKの長音源・低PITCHでEND距離がuint32 overflowする問題を修正し、境界1,048,576frames / PITCH1・one-shot / loopを回帰検証する。SDKをcommit / pushして依存lockとsubmoduleを公開commitへ固定する。新bank API・version/tag/releaseは追加しない。
- [x] private immutable PCM storageを共有し、Root / Loop / A/B変更やauthoring snapshotで音声全量をコピーしない。worker以外でdecode / resample / Native大cache準備を行わず、検証目的だけのstate全量encodeとUIのSerialize→decode snapshotを除く。
- [x] source kind4 / state schema9へPCM16LEとmetadataを埋め込み、元fileなしでDAW復元する。旧schema1〜8 / legacy、26parameter IDと旧小source上限を維持し、不正入力でactive / pending / scalar値を壊さない。
- [x] Web `.spu.json` v2のbase64 PCM16LEを読み書きする。v1は従来8MiB / ADPCM制約を維持する。v2のroot範囲・loop最低4frames・waveform / enabled Morph禁止、名前・JSON深さ / node上限 / duplicate / unknown key・canonical base64を確保前に検証する。
- [x] Software sourceのv2 exportは元のPCM16を保持しGainを焼き込まない。Noise / PMON / Morphのmask表現を説明し、長い録音と112cycle Morphを混同しない。file dialog / Cancel / editor close / host restore / latest ticketの既存保証を保つ。
- [x] build内に決定的な20MiB fixtureを生成して検証し、大きな録音をgitへ入れない。RT new/delete0・全音源切替・MIDI / FX・state9 / 旧互換・不正入力保持、実AU/VST3の元fileなし復元、実Web reader / Chromium / WebKit往復・ASan・両repo CIを検証する。
- [x] StandaloneのSoftware表示 / Root / Loop / 保存、worker不正入力時保持、installed AUのknown hash退避更新と既存GarageBand保存データを確認し、検証記録と再開TODOをcommit / pushする。AI入力が届かない実DAW独自UI・実聴 / 長時間確認は人のTODOへ区別して残す。

開始は`a08f670`、main / origin同期・clean tree。更新前installed AU manifestは[WAV記録](../wav_import_20261101.json)。
AIFF / 圧縮音声 / streaming再生・Morph LFO・外部入力Reverb・Web/Tauri配布とnpm公開は今回追加しない。

実装 `a02ee74`。Offline/macOS24件、ASan/UBSan24件、実AU/VST3・Web2engineの20MiB往復、CI全成功。
最終監査の持越しNoteOff修正も実host全rateで回帰成功。

**人が再開したとき:**

- [ ] GarageBand AUでWAV /Web v2をLoad soundから直接読み込む。Root/Loop・取消/エラー時保持・保存/再起動・元file移動後の復元を確認する。AIからAU内のfile panelを操作できず、この直接操作は未確認。
- [ ] 20MiB級素材の実聴、loop seam、PMON/Reverb、長時間再生とautomation録音を確認する。
- [ ] これらを見てPreview releaseを判断する。実装・CI成功を実聴確認済みとは扱わない。

### WAV素材読み込みの今回の区切り（2026-11-01、完了）

- [x] RIFF/WAVEのPCM16 / IEEE Float32、mono / stereoをboundedなpure decoderで読む。未知chunkとpaddingを処理し、不正サイズ・format・NaN/Inf・空音声を変更前に拒否する。入力上限32MiB、出力は既存SPU sample RAM内に限定し、黙って切り捨てない。
- [x] stereoは左右平均でmono化し、44.1kHzへ変換して通常SPU ADPCMを作る。自動normalizeしない。sample rate変換の品質と終端28-frame paddingを明記する。これはsoftwareによる素材変換であり、長いAudioのhost PCM再生とは区別する。
- [x] 初期rootはMIDI60 / C4、初期One shotとする。読み込み後に基準音程とOne shot / Loop entire sampleを変更でき、元ADPCM・loop metadataとともに保存する。loop切替で元PCM付きcycleとの不整合を作らない。
- [x] file I/O・decode・resample・encode・engine準備はworkerで行い、成功時だけ既存transactionで採用する。失敗・取消・新しいsource・host restore・editor closeで現音源・全parameter・正常pendingを壊さない。
- [x] WAV読込成功時はSample / Morph Offで素材を演奏できるようにし、Gain / ADSR / ReverbとPMON等の休止設定・Morph A/Bは保持する。root / loopだけの変更では現在の音作り設定を保持する。採用時の保持音・残響停止は既存仕様とする。
- [x] 既存AdpcmSample kind3 / state8 / 26parameter IDを使い、変換済みsampleをDAWへ埋め込む。元WAVなし復元、Web v1書き出し、root音程・one-shot / loop・MIDI / FX回帰・不正入力のatomic failureを検証する。
- [x] 共通Load soundへWAVを追加し、既存720×620・Source / ADSR / Reverb・鍵盤を保つ。VAB用Program/Toneの位置をADPCM時はRoot / Playbackへ使い、音声変換と容量超過を明示する。
- [x] Offline / macOS / ASan・実AU/VST3・validator・CI、Standaloneのfile読込 / root / loop操作、既知AU退避更新と旧GarageBand復元を確認し、検証記録をcommit / pushする。
- [ ] 人の実聴・長時間使用、最終AUのeditor再表示、実DAW独自WAV読込 / Root / Playback操作とautomation録音を確認する。Standalone UI・実AU/VST3の自動検査・初回WAV対応AUの旧GarageBand復元とは別に記録する。最終AUのeditorはCUA clickでは開けなかった。
- [x] 次の区切りで長いAudioのhost PCM / Web v2を設計・実装した。shared所有権・独立容量上限・state9 /旧互換・Noise/PMON共存・長AudioのMorph使用不可は、上段の[記録](../software_audio_20261101.md)を参照。

開始は`ecd4681`、main / origin同期・clean tree。更新前installed AU manifestは[export記録](../web_instrument_export_20261101.json)。
WAV実装`af20bcc`、Root表示の境界補正を含む最終`a6f1a2e`、[最終CI両job成功](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41266519833)。Offline / macOS / ASanは各20/20、実AU48k / VST3全3rate・公式validator47/0・auval26params成功。表示補正は実source関数のUBSan極値probeも成功した。
StandaloneのWAV読込・C3 Root・全体Loop・Save Web・不正Float / Cancel時の元音保持、実Chromium / WebKitの読み込み・再保存を確認した。初回WAV対応AUでGarageBand旧schema8 P3/T0/Gain99を復元し、最終AUも退避更新・auval検証済み。最終editor再表示は人確認へ残す。saved payload / VABと原本11filesは不変。
操作・制限・rollbackは[WAV記録](../wav_import_20261101.md)、現在のinstalled AU manifest・source hash・CI metadataは[JSON](../wav_import_20261101.json)。
今回の最小対応はPCM16 / Float32 WAV。AIFF / 圧縮音声 / WAVE extensible / 長いhost PCMとWeb v2は別段階とし、未対応formatを明示する。
Native SDK・Web/Tauri配布・version/tag/release、Morph LFOと外部入力Reverbは変更しない。

### Web音色ファイル書き出しの今回の区切り（2026-11-01、完了）

- [x] 現在の保存対象音色をWeb `.spu.json` v1へ書き出す。Builtin / 選択中VAB Tone / PCM cycle / import済みADPCMを対象にし、未Apply draft・保持音・decoder状態・Bend / Sustainは出力しない。
- [x] 元ADPCM・root・loop・signed左右volumeを保持し、Custom ADSRはsource reserved bit13をmergeした実効word、Reverb sendはAll notes / Tone指定を解決した値を出す。VAB全bankではなく現在選択Toneのsnapshotと説明する。
- [x] Morphが実効Onなら現在blendの112PCM・循環ADPCM・A/B・Amountを出力する。Web形式に入らない元sourceはDAW projectへ保持し、Web fileから元VAB等へ戻せるとは約束しない。Noise中はMorphをeffective OffとしてA/B等を保持する。
- [x] Noise / PMON両raw OnはNoise優先でPMON EnabledだけOffへ、Reverb bypassはpreset Offへ投影する。変更した表現はexport結果の説明へ出し、pluginの値・現音・pendingは変更しない。
- [x] Webにないplugin Gainはvolumeへ焼き込まず、元の音色volumeを保存する。WebのMaster / voice校正との差とGain除外を説明し、波形・出音レベルのbit一致は約束しない。Web presetはDAW state全体のlossless backupとは区別する。
- [x] pure writerは全tupleを検証し、元bytesを保った新JSONだけを作る。Web実readerによるreadbackとC++ read/write/read、全kind・Morph / Noise優先 / Reverb / ADSRのmatrixで検証する。
- [x] 共通Save Web操作を追加し、既存720×620・3pages・鍵盤を維持する。現在のcoherent saved snapshotを使い、JSON生成とfile I/Oはworkerで実行する。file dialog中のeditor close / host restoreで古いcallbackを使わない。
- [x] native Save dialogで選ばれたfileへ、同一filesystemの一時fileからatomic replaceする。キャンセル・不正path・書込失敗で既存fileを壊さず、UTF8 path / 既存file更新 / latest / worker joinを検証する。
- [x] Offline / macOS / ASan / 実AU・VST3 / validator / CI、Standalone save dialogと実Web readerでの読み戻しを確認し、既知AU退避更新・旧GarageBand復元・記録をcommit / pushする。
- [ ] 人の実聴・長時間使用、実DAW独自Save Web dialog操作とautomation録音を確認する。Standaloneの保存・Cancel、Web両browserの公開load/save、実AU/VST3の自動state検査とは別に記録する。

開始は`b8c2e3b`、main / origin同期・clean tree。更新前installed AU manifestは[import記録](../web_instrument_import_20261101.json)。
実装`fe24f25`、[CI両job成功](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41260258393)。通常 / macOS / ASanは各17/17、実Web Node / Chromium / WebKitは各20件成功。
installed AUを退避更新し、auval26paramsとGarageBand旧schema8 P3/T0/Gain99・Save Web表示を確認した。saved payload4768bytes / VABと原本11filesは不変。
操作・制限・rollbackは[export記録](../web_instrument_export_20261101.md)、この区切り終了時のinstalled AU manifest・source hash・CI metadataは[JSON](../web_instrument_export_20261101.json)。
長いAudio v2・VH+VB UI・PSF抽出・Morph LFO・外部入力Reverb・Web/Tauri配布変更・version/tag/release公開は今回追加しない。

### Web音色ファイル読み込みの今回の区切り（2026-11-01、完了）

- [x] Web `.spu.json` version1の通常ADPCM / 112 PCM / Morph音色を読み込む。JSONの構文・schema・全値・sampleとwaveform/Morphの整合性を、現在音源やparameterを変更する前に検証する。この区切り時点のversion2は明示エラーとした。現在は上段の長いAudio対応でbounded v2 import/exportを実装済み。
- [x] 任意ADPCMを保持するimmutable source kindを追加し、元bytes・baseFrequencyHz・loopStart・signed左右volume・source ADSR・reverb sendと任意の元PCMを保存する。Webのpreset.pitchはinspector値として保持し、keyboard音程はWebと同じroot基準にする。
- [x] parserはサイズ・深さ・配列長を制限し、duplicate / unknown keys・非finite・不正Unicode・不正ADPCMを拒否する。この時点のNative repeat flag制限は明示エラーとしたが、現在は[loop互換性修正](../loop_repeat_20261101.md)で元bytesのまま対応済み。
- [x] Reverb / Noise / PMON / Morphを既存26parameterとA/B定義へ対応させる。Webにないplugin Gainは読み込み時の値を保持する。WebとpluginのMorph Off / ADPCM・16bit carrierの違いを説明し、bit完全一致は約束しない。
- [x] workerでfile読込・parse・engine準備を行い、全設定と元sampleをtransactionで採用する。失敗や古い完了・host restore後の完了は現音源・全parameter・正常pendingを壊さない。採用時は保持音と残響を停止し、次のNoteから再生する。
- [x] 既存ID0〜25とsource kind0〜2を維持し、state schema8で新sourceを保存する。旧1〜7 / Gain-onlyを読み、不正state・当時の未対応loop時のatomic failureと元fileなし復元を検証した。現在のloop修正では正常loop成功と不正marker時保持へ回帰を更新した。
- [x] 共通file操作から`.vab`と`.spu.json`を読み込めるようにし、import名・状態・エラーを表示する。Source / ADSR / Reverbと720×620を保ち、file dialog/editor closeやpending中の世代管理を検証する。
- [x] Webの実writerで作るreference fixtureとC++ parser/coreの対応、実AU/VST3のstate・first block・MIDI / FX回帰、sanitizer / validator / CI、Standalone読込UIを検証し、記録をcommit / pushする。
- [x] Web向け書き出し・往復は[次の区切り](../web_instrument_export_20261101.md)で完了した。Morph Onの現在blend保存・元source保持、Noise+PMONのraw flag差を整理し、実Web readerと両browserで検証した。長いAudio / PSF抽出 / VH+VB UIは後続。

開始は`883c3d7`、main / origin同期・clean tree。更新前installed AU manifestは[Morph記録](../morph_20261101.json)。
まずv1読み込みを完成させる。Web / Tauri配布物、Morph LFO、外部入力Reverb、version / tag / release公開は変更しない。
実装`81eb78b`、[CI全job成功](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41255456421)。
installed AUをknown hash照合・退避付きで更新し、auval26paramsとGarageBand旧schema7のP3/T0/Gain99・Load sound表示を確認した。saved payload4768bytesとVABは不変、原本11files不変。
操作・制限・rollbackは[import記録](../web_instrument_import_20261101.md)、import時点のAU manifest・source hash・CI metadataは[JSON](../web_instrument_import_20261101.json)。現在のinstalled AU manifestは上のWAV記録。

### Software Morphの今回の区切り（2026-11-01、完了）

- [x] 元Builtin / VAB / PCMを保持したまま、112-sample Wave A / Wave Bを線形blendするSoftware Morphを追加する。初期 / 旧stateはOff、Amount0、A=Sine / B=Saw（既存PCM形状の振幅0.8）。Offは保持した元sampleへ戻る。
- [x] A / B / Resultを1つのcanvasへ重ね、編集対象A / Bを切り替えて4形状・自由描画を行う。Resultは編集不可のblend表示。A/B編集はdraft→Apply、Revertはdraftだけを戻す。通常PCM draftも独立して保持する。
- [x] A/B Applyは既存worker / engine交換を使い、保持音・残響を停止する。Amount0〜1だけは発音中・Sustain・Releaseにも再発音せず反映する。Webのendpoint編集即反映 / Offで最終blend保持とは異なることを明記する。
- [x] SPU実機の機能と区別してSoftware Morphと表示する。720×620とSource / ADSR / Reverbの3tabs・Reverb初期表示を維持し、AmountはSource内と共通keyboard横の2か所を同じhost parameterへbindする。
- [x] PMONとの併用に対応する。Native SDKのprivate host PCMを2banksへ広げ、bank0はNoise / PMON、bank1はMorph carrier。既存ABIはbank0として維持し、同一長のlive更新・mask選択を確保なしで行う。物理SPU RAM容量とGaussian / ADSR / cursorを保持する。
- [x] Morph On/Offの実効source変更は保持音・残響を停止し、Noise中は設定だけ保持する。Noiseは優先し、PMONの12音pair・carrier ADSR / send、Bend / Sustainを維持する。元Tone tuningへ戻せるようMorphは112周期用tuningを使う。
- [x] 既存ID0〜23を維持し、Morph Enabled24 / Amount25を追加する。A/Bはimmutable音源定義へ保存し、元source選択・PCM Applyで保持する。schema7固定1024bytesでflags / reserved0 / amount / A112f32 / B112f32を追加、旧1〜6 / legacyを読み込む。
- [x] 不正amount / flags / reserved / A/B非finite・範囲・長さをcommit前に拒否し、active / pending / 全parameterを保持する。遅延Apply観測・新しいdraft・host restore後の旧worker完了で古い内容へ戻さない。
- [x] coreでAmount両端 / 中間、held / Release更新、PMON併用、Noise優先、元sample復帰 / 全RAM / repeat、分割一致・C++確保破棄0、初期Sine byte一致を検証する。実AU/VST3で26params / state7 / 旧互換 / automation / MIDI / atomic failureを検証する。
- [x] Standaloneで1canvasのA/B切替・描画・Apply / Revert・2か所Amountの50% / 26%同期を確認する。build / CTest / sanitizer / validator / 両repo CI、既知AU退避更新とGarageBand旧schema6復元を確認し、結果をcommit / pushする。

開始は`091fd42`、main / origin同期・clean tree。更新前installed AU manifestは[PMON記録](../pmon_20261031.json)。
実装は`65b6daf`、Native SDKは`d0e0324`へ固定した。[plugin CI](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41250360041)・[SDK CI](https://github.com/kyorohiro/tetorica-soundchip-native/actions/runs/41249290598)は成功。
installed AUを退避更新し、`auval`成功、GarageBandのP3/T0/Gain99/Morph Offと旧schema6 payload（SHA256 `7feadeb5…`）一致を確認した。
この区切り終了時のinstalled AU manifestは[Morph記録](../morph_20261101.json)。実聴・長時間リアルタイム・実DAW独自Morph UIとautomation録音は未確認。
Morph LFO、外部入力Reverb、長いAudioへのMorph、Web / Tauri配布変更は追加しない。

### PMONの今回の区切り（2026-10-31、実装前）

- [x] 隣接even modulator → odd carrierの12組でSPU PMONを公開する。初期Offで従来Sine・元VAB・PCMを保つ。mode切替は保持音・残響を停止する。
- [x] Modulator Sine / Triangle / Saw / Square、Ratio 0.01〜16（初期1）、Depth 0〜1（初期0.5）、Decay 0〜5000ms（初期500、0はHold）を追加する。波形・Ratio・Depthは発音中も反映、Decayは次Note Onへ適用する。Decay時間はSPU rateの近似。
- [x] 最大12音、同音再発音・voice stealing・ch別Bend / Sustain・Release・panicをpair単位で処理する。modulatorはdry / reverbへ混ぜず、carrierだけに元Tone / Custom ADSRとReverb sendを適用する。
- [x] Noise Enabled17 / Clock18を含む既存19parameterを維持し、PMON Enabled19 / Waveform20 / Ratio21 / Depth22 / Decay23を追加する。両EnabledがOnのraw状態も保存可能とし、実効modeはNoise優先に固定する。UIの3択は両flagを適切に更新する。
- [x] state6（固定112bytes）へ保存し、schema1〜5 / Gain-onlyからPMON Off・初期値で復元する。不正tuple / stateを変更前に拒否し、音源・全parameter・正常pendingを保つ。
- [x] Native SDKへ同一長host PCMの確保なし更新APIを追加する。112framesのprivate cacheを非audioで常設し、波形 / Depth変更はcursor・Gaussian履歴・ADSRをresetせず更新する。元VABの最大RAM容量とrepeat復元を保つ。SDK公開commitとlockを固定する。
- [x] Source / ADSR / Reverbの3タブと720×620を維持する。SourceにSample / SPU Noise / Sample + PMON、Modulator / Ratio / Depth / Decay、carrier編集へ戻る操作を置く。元sample・PCM draftを保持し、Reverbの初期表示を維持する。
- [x] coreで12音・live変更・Decay latch・pair controller・全RAM / repeat回帰・分割一致・C++確保/破棄0・初期Sine byte一致を検証する。実AU/VST3で24params / state / automation / 旧互換 / atomic failureを検証する。
- [x] StandaloneのSource / PMON操作、build / CTest / sanitizer / validator / CIを確認する。検証済み範囲・実聴等の未確認範囲を記録し、commit / pushする。
- [x] host保存・終了、既知manifest照合・退避後にinstalled AUを更新し、GarageBand検証用projectの旧state復元を確認する。

開始時点は`d73a05e`、main / origin同期・clean tree。更新前AU manifestは[Noise記録](../noise_20261031.json)。
実装`d4698d5`、[CI全job成功](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41212257829)。
2026-11-01 JSTに検証を完了し、この区切り終了時のinstalled AU manifestは[PMON記録](../pmon_20261031.json)。
任意voice間の配線、Morph LFO、外部入力Reverb、Web / Tauri配布変更は今回の範囲に含めない。

### Noiseモードの今回の区切り（2026-10-31、実装前）

- [x] SPUの全24voice共通Noiseモードと、共有Noise clock 0〜63（初期48）を公開する。初期はSample、Noise Offで従来音を保つ。
- [x] Noise clockは発音中・Release中にも再発音せず反映する。Note / Pitch BendはNoiseの音階を変えず、鍵盤・velocity・ADSR・Sustain・Reverbを使って演奏する。
- [x] Sample / Noise切替時は保持音・残響を止める。元Builtin / PCM / VABを保持し、Sampleへ戻ると元音源を使う。Noiseはprivate loopを準備し、元VABがone-shotでもsample ENDで止めない。
- [x] PCMタブをSourceへ広げ、Sample時の単一canvasとNoise時のClock説明を切り替える。720×620と3タブ・共通bank / Gain / keyboardを維持し、NoiseがSPU機能であることとClockが共有の質感変更であることを表示する。
- [x] 既存parameter ID0〜16を維持し、Noise Enabled17 / Clock18を追加する。schema5に保存し、旧schema1〜4・Gain-onlyをNoise Off / clock48で復元する。不正stateは全設定・現音源・正常pendingを保つ。
- [x] 元sampleとprivate loopを非audioで読み込み・cache準備し、mode / clock切替・発音・renderでC++確保/破棄を行わない。ADSR / Reverbのregister bitsを壊さず、clock変更では保持音・sample位置をresetしない。
- [x] coreでlive clock / pitch非追従 / private loop / 24voices / reset・panic / 分割一致 / RT確保と初期Sine byte一致を検証する。
- [x] 実AU/VST3の19parameter保存・復元・旧形式・automation・不正state保持を検証し、Sourceページの実操作を確認する。build / CTest / validator / CIと結果をcommit / pushする（実装`e43d031`、runner移行`3c10dfd`、[CI全job成功](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41210254431)）。
- [x] 初回Noise CIのmacOS14 jobが公式brownoutで実行前cancelledとなったため、workflowをmacos-15へ移し、新runで全検証する。初回Offline8/8成功と未実行macOSを記録に残す。
- [x] host保存・終了と既知manifest照合後にinstalled AUを更新し、GarageBandの旧schema4互換と設定復元を確認する（`au-update-j01640dh`、P3/T0/Gain99、schema4payload3816bytes一致、原本11files不変）。

今回のNoiseは既存sourceを保持する共有モード。VAB sample255の自動Noise Tone再構築、PMON、NoiseのNoteへの調律は追加しない。
開始時点は`35d0a0d`、main / origin同期・clean tree。今回の最終AU既知manifestは[Noise記録](../noise_20261031.json)。

### PCM波形選択・描画の今回の区切り（2026-10-31、実装前）

- [x] Webと同じ112-sample周期、Sine / Triangle / Saw / Squareと単一canvasでの自由描画を追加する。softwareで作るPCMをSPU ADPCMへ変換する機能と表示する。
- [x] editorはPCM / ADSR / Reverbのpage切替で720×620を維持し、共通bank・Gain・keyboardを残す。描画はdraftとし、Apply PCMで反映、Revertで現在音源へ戻せるようにする。
- [x] Apply PCMはworkerでcyclic ADPCM変換・engine/cache準備を完了し、既存exchangeでblock境界に交換する。交換で保持音と残響が止まることをUIへ明記し、次のNote Onから新波形を使う。drag中にRAMを書き換えない。
- [x] PCM原本112個をimmutableな音源定義へ保存し、元ファイルなしでproject復元できるようにする。新stateは旧schema1/2/3・Gain-onlyを読み、既存parameter ID0〜16を保つ。
- [x] 値域・finite・長さ・kindの不正入力をcommit前に拒否し、現音源・設定・準備中の正常stateを保つ。VABのProgram/Tone選択は引き続きVABに限定する。
- [x] 初期Builtin SineのPCM byte一致、cyclic encoderのWeb基準、4形状の音程・音の差・loop、RT確保・交換・保存互換をcoreで検証する。
- [x] 実AU/VST3でPCM入りstate・最初のNote On・不正stateのatomic性・ADSR/Reverb/MIDI回帰を検証し、Standaloneの描画・Apply/Revert・page切替を確認する。
- [x] build / CTest / validator / CIを確認し、結果・制限・次の入口をcommit / pushする（`e53d7da`、[CI全job成功](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41207294837)）。installed AUはhost保存・終了と既知manifest照合を経て`au-update-kyijyyf8`で更新し、GarageBand旧schema3 P3/T0/Gain99復元を確認した。

初期Builtin SineとVABの既存音は変更しない。PCMの選択・描画は音色stateとして保存し、今回host automation parameterには追加しない。
波形のlive書き換え・Morph・Noise・PMON・Audio file入力は後続。Morph LFOと外部入力Reverbは追加しない。
開始時点は`adaba1b`、main / origin同期・clean tree。今回の最終AU既知manifestは[PCM記録](../pcm_waveform_20261031.json)。

### SPU Reverbの今回の区切り（2026-10-31、実装前）

- [x] Native SDKへ確保・RAM/cache破棄を伴わない残響clear APIを追加・検証し、公開commitへ固定依存を更新する（`da089d6`、SDK CI全検証成功）。通常resetをaudio callbackで呼ばない。
- [x] Off＋7種の共有近似preset、Enable、Return L/R（初期25%）、All notes / Tone sendを公開する。元VABのsend flagは保持する。
- [x] 手動の非Off preset選択で自動Enableする。automation / host restoreは各parameterの保存値をそのまま使う。
- [x] 発音・ADSR・sample位置を保ってpreset / return / sendを変更する。Enable Offは即dryへ戻し残響を裏で減衰、preset OffとAll sound offは残響履歴も消す。
- [x] 720×620のADSR / Reverb page切替で画面を整理し、bank・Gain・keyboardを共通に保つ。実機完全再現ではなくcoreの近似presetと表示する。
- [x] 既存parameter ID0〜11を保ち、新設定をschema3へ保存する。schema1/2・Gain-onlyはReverb Offで互換復元し、不正stateは全設定と現在音を保つ。
- [x] coreで全presetの残響、左右Return、send復元、panic、分割一致・RT確保・ASanを検証する。初期dry音もbyte一致。
- [x] 実AU/VST3のparameter・保存復元・旧形式・PCMを検証し、Standaloneの操作とGarageBandの旧state復元・画面を確認する。
- [x] 検証後に既知manifest付きでinstalled AUを安全に更新する（`au-update-5sucrool`、旧schema2 P3/T0/Gain99復元、原本11files一致）。
- [x] TODO・結果をcommit / pushし、plugin CIを確認する（`552a45a`、[offline / macos全job成功](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41204737497)）。

外部入力Reverbは追加しない。Web・Tauri配布物とproject原本は変更しない。
Enable Offでwetを即muteし、preset Offで履歴も消す点は、sendのみを止めるWeb UIからの意図した改善とする。

### ADSR編集の今回の区切り（2026-10-31、実装前）

- [x] Tone本来のADSRを保持し、Custom ADSRの有効化・元Toneからのコピーを追加する。初期状態は元Toneを使用する。
- [x] Attack / Decay / Sustain level / Sustain / Releaseのrateとmodeをhost parameterとして公開する。Gainの既存indexは変えない。
- [x] Apply editsをOnにすると現在のlevel・sample位置からheld / pedal-held / Release音へ反映し、Offなら既存音は離鍵後まで旧設定を保持する。新しいNote Onは編集値を使う。
- [x] rate codeを使うADSRの概形と編集controlを追加し、実測msと誤解されないよう表示する。
- [x] 新設定をversion付きstateへ保存する。旧schema1・Gain-only stateは元Toneの音を保って復元し、不正stateは音源と全parameterを保持する。
- [x] coreのlive / deferred / Release / partition / RT確保と、実AU/VST3でのparameter・state・旧形式回帰を検証する。
- [x] Standalone / AU / VST3をbuildし、Standalone実操作とGarageBandでの旧state復元・新panel表示を確認する。
- [x] 検証結果・制限をcommit / pushし、CIを確認する（`550fbab`、[CI全job成功](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41201681624)）。
- [x] 検証後に既知manifest付きでinstalled AUを安全に更新し、検証projectの旧state復元を確認する（P3/T0/Gain99、Tone ADSR、Apply On）。

CustomをOffにするとbank内のADSRへ戻る。Custom値はProgram/Toneを切り替えても保持し、Copy toneで明示的に取り込めるようにする。
live MIDIのBend/Sustainは保存しない。Web版・Native SDKのvendor source・既存project原本は変更しない。
SPU Reverbはこの区切りの次に進める。

### Pitch Bend・Sustainの今回の区切り（2026-10-31、実装前）

- [x] Web spu-synthとの機能差を整理し、[段階移植計画](../spu_synth_parity_plan.md)を保存する。
- [x] チャンネル別14-bit Pitch Bendを追加する。中心8192、初期範囲±2半音、発音中とRelease中の音を再発音せず変更し、新規音にも適用する。
- [x] チャンネル別Sustain（CC64、64以上でOn）を追加する。離鍵を保留し、解除時は離鍵済みの音だけReleaseする。
- [x] 同音再発音・voice stealing・CC123・CC121・panic/overflow・reset/activate・host restore時の保持音とcontroller状態を整理し、stuck noteを防ぐ。
- [x] sample offsetを含むMIDI接続を行う。UI音源切替ではwheel/pedalの状態を引き継ぎ、host restoreとlifecycle resetでは初期化する（実装/source review、live UI交換は実機未追試）。
- [x] 固定iPlugのVST3 CC queueが最後のpointだけを処理する制約をplugin側adapterで補い、同block内のBend/pedal全pointを扱う（vendorは変更しない）。
- [x] VST3 bypass中にProcessBlockが止まってもMIDIを蓄積しないよう、queue・voices・controllersを初期化し、bypass解除後の古いpedal再適用を回帰検査する。
- [x] coreの音程・チャンネル分離・pedal・RT中new/deleteと、実AU/VST3のMIDI・offset・state回帰を検証する。
- [x] README・結果・未確認範囲を記録してcommit / pushし、CIを確認する（`6918642`、[CI全job成功](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41199344245)）。
- [x] 実plugin検証後、既知manifest付きの安全な手順でinstalled AUを更新・auval確認し、検証projectの復元を確認する（P3/T0/Gain99、embedded一致、旧bundleとproject backup保持）。

今回の範囲はDAW MIDIからの演奏機能。Bend/Sustainのlive値はpresetへ保存しない。
Webの±2半音と離鍵保留を参考にし、MIDIのチャンネルごとに独立させる。
既存のWeb版・installed AU・開いているGarageBandをbuildだけで書き換えない。
今回は検証後にGarageBandを保存・終了し、明示的なmanifest付きupdateでinstalled AUも更新済み。
次は[移植計画](../spu_synth_parity_plan.md)のADSR編集、続いてSPU Reverbを進める。

### Program/Tone・保存通知の今回の区切り（2026-10-31、開始時記録）

- [x] 保存済みP3/T0/Gain99のcopyを基準に、Tone 1だけへ変更する。停止中は必要に応じて再生し採用する（人の選択→Ready→再生→Loadedを確認）。
- [ ] Tone-only変更通知を、変更前の基準と区別して確認する。未変更でもGarageBand終了時の保存確認が出たため、保存promptだけでは判定できない（下記）。
- [x] 保存後のembedded P3/T1/Gain99と、再起動後の選択UIを照合する（full bank一致、再起動後もTone 1）。
- [x] Program操作後のP3/T0/Gain99を画面と保存stateで照合し、project close/reopen後の復元を確認する。
- [ ] Program 11の中間画面でTone 0になることを直接確認する。最終P3/T0とfixture期待値の確認とは区別する。
- [x] 通常のeditor close/reopenとproject close/reopenで選択を保持することを確認する。
- [ ] popup取消・popup中のeditor close/reopenを確認する。CUAで操作できない所は人の操作が必要。
- [x] 結果と未確認範囲を記録しcommit / pushする。再現した不具合があれば修正前に記録する。

基準確認では、P3/T0/Gain99を再起動後、音色やregionを編集せずeditorを閉じてQuitしても保存確認が出た。
Cancel後にFile→保存を明示してQuitした場合も同じだった。終了はCancelしprojectを保持している。
保存promptをTone-only通知成功の証拠にしない。AUの通知コードはToneも比較してPresentPreset通知を送るが、DAWのdirty判定とは区別する。
CUAのTone clickはhoverだけでmenuが開かないため、人の操作でTone 1を選択し、保存・復元まで確認した。
Program 11→3の操作依頼後、P3/T0を実画面で確認した。
「Tone 0にならない」という回答はチャット側の誤作動としてユーザーが訂正したため、不具合報告として扱わない。
fixture期待値はP3/T0とP11/T0だが、P11中間画面の実機確認はこの結果に含めない。

### AU独自UIの追試（2026-10-31、開始時記録）

- [x] ユーザーが実際のclickでLoad VABのファイル選択が開くと確認した。
- [x] 独自load確認用の`build/ui-check/garageband-ui-source.vab`を作成し、元fixtureとのhash一致を確認した。
- [x] 開かれたdialogから検証用VABを読み込み、初回のP3/T0とstatusを確認する（Ready→GarageBand再生でLoaded）。
- [x] 独自UI経由の選択をprojectへ保存し、embedded stateとeditor再表示を確認する（bank完全一致、GarageBand終了・再起動後もP3/T0/Gain99）。
- [ ] Program/Tone操作・変更通知を人の操作で追試する。CUAでAU独自controlへのclickが届かない制限と区別して記録する。
- [x] 結果・TODOをcommit / pushする。コード修正がなければ通過済み検証を繰り返さない。

停止中の`Ready. Waiting for an audio block.`は準備済み・採用待ちの正常表示。
今回の再生でLoadedへ遷移したためロード失敗として扱わない。
後続のUI改善では、内部処理名より「音を弾くか再生を開始する」案内を検討する。

### AU更新・GarageBand VAB確認の今回の区切り（2026-10-31、実装前）

- [x] 既存AUの既知manifestを照合し、退避・staging・更新・条件付きrollbackの手順を追加・検証する。
- [x] GarageBandなどのhostを保存・停止し、既存bundleをComponents外へ保持したまま新版へ更新する。
- [x] 元の検証project全体と未保存状態を別copyへ保持する。
- [x] GarageBand標準のAUプリセット読込から、VAB P3/T1/Gain99を読み、projectへ保存する。
- [ ] copyのAU独自ボタンでVAB読込・Program/Tone変更・dirty表示を確認する（人によるclickの確認待ち、CUAではBuiltinも反応せず）。
- [x] projectを保存・終了し、読み込み元の検証用aupresetとVAB copyを移動してから再起動し、音色と選択UIを確認する。
- [x] 前後の音声書出し・project内embedded stateを照合する（PCM最大差0、bank bytes完全一致）。editor再表示も確認する。
- [ ] dialog/popup中の取消・editor close/reopen時の保持を実DAWで確認する。
- [x] 回帰検査を行い、結果・復帰先・未確認項目をcommit / pushしてCIを確認する。11 installer testsを含み全job成功。

ユーザー自身の未保存projectが見つかった場合は勝手に閉じない。既存の`spu-au-check.band`は原本として保持する。
更新は既知の旧manifestと一致する場合だけ行い、未知のAU・projectを上書きしない。

### VAB UIの今回の区切り（2026-10-31、実装前チェックポイント）

- [x] complete VABをファイル選択し、1 MiB上限付きでworkerから読む入口を追加する。
- [x] Load VAB / Builtin、sparse Program / Tone選択、読込・準備・採用・失敗表示を720×360へ配置する。
- [x] editorを閉じた後の非同期dialog callbackを無効化し、古い読み込みが新しい選択・host restoreを上書きしないようにする。
- [x] host restore後とeditor再表示時に、保存された音色をUIへ反映する。UI反映だけではengineを再要求しない（実装・source review、実DAW追試は下記）。
- [x] UI起点の成功した音色変更をhostへ通知する。取消・失敗・host restoreでは変更通知しない（DAW固有dirty判定は下記）。
- [x] worker file入力・state/音源交換・実AU/VST3の回帰と、可能な範囲の実UI操作を検証する。
- [x] 結果、未確認の実DAW操作、再開手順を記録してcommit / pushし、CIを確認する。

complete `.vab`を最初の対象とする。分割VH/VBの複数file dialogは後続。
元pathは読み込み入力にだけ使用し、DAW stateには既存のembedded bank形式を使用する。

### DAW stateの今回の区切り（2026-10-31）

- [x] version / size上限付きの、bank bytes・Program/Tone選択・Gainの保存形式を実装する。
- [x] immutableな音色定義をengineと一緒に管理し、保存時に再生中のhandleへ触れない。
- [x] state復元を成功と返す前に解析・cache準備を完了する。失敗時は音源とGainを保持する。
- [x] pluginのcustom stateへ接続し、従来のGain-only状態も復元できるようにする。
- [x] 実VST3で埋め込みVABの別instance復元・直後の先頭block・不正state・持ち越しMIDIを検査する。
- [x] build / sanitizer / AU・VST3回帰とCIを確認し、結果・次のUI作業をcommit / pushする。

この区切りではファイル選択UIはまだ公開しない。VABを含むstateを実際のpluginへ復元する入口で検証する。

### 音源交換の今回の区切り（2026-10-31）

- [x] 準備済みengineの所有権を、audio block境界で非待機に交換する層を作る。
- [x] 古いrequestの結果・失敗した準備が現在の音源を上書きしない世代管理を実装する。
- [x] 旧engineの破棄をaudio callback外へ戻し、退避queue満杯時は交換を延期する。
- [x] workerでVAB解析・Tone準備を行う入口と、pluginへの交換層の接続を用意する。
- [x] 再生中の交換・sample rate変更・終了時の所有権・競合・callbackのnew/deleteを検証する。
- [x] 結果と次のstate/UI作業を記録し、commit / pushする。[CI全job成功](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41187048655)。

この区切りではVAB読み込みUIをまだ公開しない。DAW stateを復元した直後に正しい音が出る設計も別途検証する。

### VAB実装の今回の区切り（2026-10-31）

- [x] UI非依存のC++ VAB v6/v7パーサー（complete / 明示VH+VB）を実装する。
- [x] 選択ToneのADPCM・tuning・ADSR・loop・volume/panを検証・準備する。
- [x] オリジナルfixtureを独立repoへ取り込み、Web版との結果照合と不正入力の検査を追加する。
- [x] 準備したToneをNative音源でoffline演奏し、長いsample・loop・事前cache準備を検査する。
- [x] build / 既存pluginの回帰検査を行い、結果と次の入口をcommit / pushする。[CI全job成功](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41185732525)。

この区切りではファイル選択UI・再生中の音色交換・DAWへのVAB保存はまだ公開しない。
それぞれ音源準備と別に検証してから接続する。

### 今回進める範囲（2026-10-31）

- [x] AUをユーザー専用Componentsへ新規配置し、auvalで検査する。既存bundleは上書きしない。
- [x] GarageBandで新規の検証用projectを作り、AU選択・演奏・保存・再読込・書出しを試す。
- [x] VST3のMIDI境界・overflow復帰テストを追加する。
- [x] 上記の結果と手動確認が残る範囲を記録し、commit / pushする。

## 再開手順

1. `git status --short --branch` とこのファイルを読む。既存の変更を消さない。
2. [作業記録](../progress.md)の最終チェックポイントを読む。
3. 依存ソースの取得が途中なら、既存の取得状態と固定revisionを確認してから再実行する。
4. 完了した検証と未実行の検証を区別する。build成功をDAW動作確認済みとは扱わない。
5. 変更の区切りごとに結果・コマンド・次の作業を記録し、ローカルcommitで保存する。

## Phase 0: 再開可能な土台

- [x] 実装前にTODOと作業記録を作成する。
- [x] iPlug2OOSの構成・CI・依存取得方法を調査する。
- [x] iPlug2 / テンプレートのrevisionとlicenseを固定・記録する。
- [x] Native SDKのSPUソースを固定する。公開release後の修正が必要なため、公開commitを固定（dependencies.lock.json参照）。
- [x] プラグインID・名前・ビルド出力先・ignore設定を用意する。
- [x] macOS ARM64で未変更テンプレートのAPP / AU / VST3 buildを確認する。

## Phase 1: Native SPUの最小演奏

- [x] UIから独立したSPU演奏層を作る（発音割当・Note On/Off・panic）。
- [x] オリジナルの内蔵ADPCM波形を使って発音する。
- [x] DAWのsample offsetに沿ってMIDIを適用する。実VST3でoffset64より前の無音を検査。
- [x] 固定44.1kHz SPU出力をhost sample rateへ変換する（初期版は線形補間）。
- [x] built-in専用callbackでファイル読込・動的確保・待機lockを避ける。キャッシュ事前準備後のC++ new/deleteはテストで0。全mallocやhard RTの保証とは別。
- [x] 無音・音程・離鍵・和音・ブロック境界・複数instanceをオフラインで検査する。
- [x] macOS ARM64でStandalone / AUv2 / VST3をbuildする。

## Phase 2: プラグインとしての検証

- [x] AUをauvalで検証する。ユーザーComponentsへ新規配置し、sandbox外で成功。factory preset名のwarningは後続TODO。
- [x] VST3をSteinberg公式validatorで検証する（47成功、0失敗）。
- [x] 初期Gain状態の保存・復元と44.1 / 48 / 96kHzの実VST3 offline renderを検査する。block sizeは公式validatorとcore分割一致で検査。
- [x] wrapperのイベント境界・順序・queue overflow復帰を実VST3で追加検査する（44.1 / 48 / 96kHz）。
- [x] GarageBandで読込・MIDI録音・保存・再読込・WAVE書出しを確認する。Gain99%復元、前後のPCM差最大1LSB。実聴は別。
- [x] CIにオフライン検査とbuildを追加する。Linux core、macOS ARM /IntelのAU /VST3、Windows VST3のbuild・配布検査も成功。各OSの実聴は別途確認する。

## 初期版の既知の制約・後続改善

- [x] 新規Builtin Sineを5-zone ADPCMへ広げ、MIDI0〜127・±2半音を検証した。旧kind0の112標本・PITCH上限・過渡PCMを保持する（[記録](../builtin_sine_range_20261102.md)）。
- [ ] sample rate変換の品質・遅延報告を検討する。44.1kHz未満のanti-alias filterは未実装。
- [x] Pitch Bend / Sustainを追加する（[検証記録](../midi_expression_20261031.md)）。
- [x] CC120をチャンネル別の即時停止にする。CC123はpedalを尊重するchannel release、UI All sound offは全停止として区別する。
- [x] Gain変更を約5msで滑らかにする。sampleごとの出力補間と、既存block単位targetの違いを説明する。
- [ ] 通常parameter automationの各pointをsample offsetで処理する拡張を検討する（現在のGain smoothingはblock targetからの補間）。
  調査メモ：`plugin/MidiAdapters.cpp`は通常parameter queueをpinned baseへ渡し、`IPlugVST3_ProcessorBase.cpp`は最後のpointだけを読む。`ProcessBlock()`は26paramsをblock開始時に適用する。次に進めるならGainのbounded schedulerから設計し、複数point /非zero offsetを実VST3とAUで検証する。今回のOctave /PitchはMIDI経路なので、この制限の解消とは扱わない。
- [ ] AUでrender thread以外から届くMIDIのbounded ingressを検証・対応する。現状のhost MIDI / lifecycle操作は単一consumerとして直列化される前提。
- [x] AU factory preset名とPresent/Current/ClassInfoのユーザー名保持を実装し、配置済みAUのauvalでpreset-name warning消失を確認した（[記録](../preset_split_vab_20261101.md)）。
- [x] AU更新時は既存bundleと前回配置したhashを照合し、退避・更新できる手順を用意する（explicit update / 条件付きrollback、11 tests、実更新済み）。

## Phase 3: VAB楽器と音作り

- [x] C++ parser・選択Tone準備・新規Native instanceのoffline再生を実装・検証する。
- [x] オリジナルVABのTone一覧・WAV書出しCLIを用意する（`spu_vab_render`）。
- [x] workerで準備したengineを安全に交換する。世代管理・audio thread外の破棄・queue満杯時を検査する。
- [x] version付きembedded bank stateを実装する。復元直後の最初のrenderと不正stateのatomic failureを検査する。
- [x] VAB読み込み、Program / Tone選択を追加する。
- [x] サンプルを含む音色と設定のproject保存・復元を実装する。
- [x] 実DAWの画面からVABを読み、project保存・再起動・復元とUI表示を確認する（GarageBandの独自Load/Tone、P3/T0とP3/T1）。
- [x] GarageBand標準AUプリセット経由で、VAB embedded stateのproject保存・元fileなし再起動・UI復元・音声PCM一致を確認する。
- [ ] VAB/Toneのみの変更でDAWがprojectを変更済みと扱うことを確認する。VST3はoptional setDirty、AUはpublic PresentPreset通知を使用中。
- [ ] editorを開いたままのhost restoreと、file dialog / Program・Tone popup中のeditor close/reopenを実DAWで追試する。
- [x] 分割VH/VBのファイル選択を追加する。両順序 /取消 /不正pair /retryをStandaloneで確認、worker /state /Web exportも検証した。実DAW独自split panelは別途追試する。
- [x] Native SDKのrepeat先packetにflag 2がないloopを修正・検証した。flag4-only /markerless0の元bytesを保ち、実AU/VST3・sanitizer・CIで確認済み（[記録](../loop_repeat_20261101.md)）。[installed AU更新・旧project復元](../au_loop_update_20261101.md)も完了。新素材の独自UI操作・実聴は未確認。
- [x] ADSRをhost parameter / state / graph編集へ公開する（[実AU/VST3・GUI検証](../adsr_editing_20261031.md)）。
- [x] SPU Reverbをpreset / Enable / Return / sendとstateへ公開する（[検証記録](../reverb_20261031.md)）。
- [x] PCMの波形選択・描画、Noise、PMONを順に公開する。
- [ ] 実DAWでReverb automation録音・AU独自UIのpreset / Enable / Return操作を追試する。headless PCMとStandalone操作の検証とは分ける。
- [ ] 実DAWでADSR automation録音・graph drag中のeditor close / host restoreを追試する。headless PCMとStandalone操作の検証とは分ける。
- [x] Software MorphをSPU実機機能と区別して公開する（[検証記録](../morph_20261101.md)）。
- [ ] 実DAWでSoftware Morphの独自UI操作とautomation録音を追試する。Standalone操作・headless PCM・旧project復元の検証とは分ける。
- [x] Web `.spu.json` v1の通常ADPCM / PCM / Morphと設定をimportし、元fileなしのDAW stateへ保存する（[記録](../web_instrument_import_20261101.md)）。
- [ ] 実DAWの独自Load soundからWeb音色を読み込み、編集・保存・再起動・automation録音を追試する。Standaloneでのfile操作、実AU/VST3 state検査、GarageBand旧state復元とは区別する。

## 人による最終確認

- [ ] 実聴で音色・クリック・演奏latencyを確認する。
- [ ] 実DAW / 実機の長時間リアルタイム動作を確認する。オフライン試験と区別する。
- [x] 署名・公証・配布・Release公開は具体的な成果物を確認してから進める。`v0.0.0` Previewを公開し、公開後のdownload・hashも確認済み。
- [x] 公開配布時に実際にlinkした第三者コード・fontのnotice同梱を再確認する。`v0.0.0`のMac /WindowsのpackageにTHIRD_PARTY_NOTICESとlicenseを同梱・照合した。次versionの配布でも再確認する。
