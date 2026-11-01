# WAV素材読み込み — 2026-11-01

開始`ecd4681`、実装前TODOを`39c7276`へcommit / pushした。
WAV実装は`af20bcc`、Root表示の境界補正を含む最終実装は`a6f1a2e`。
ローカル検証、実Webとの往復、CI、installed AU更新を完了した。初回WAV対応AUで旧GarageBand復元を確認し、最終AUのeditor再表示は下記の人確認へ残す。
source / bundle hash、検査ログ、現在のinstalled AU manifestは[検証JSON](wav_import_20261101.json)に保存した。

## 今回の範囲

共通Load soundからPCM16 / IEEE Float32のmono / stereo WAVを読む。
softwareでstereoを平均mono化し、44.1kHzへ変換して通常SPU ADPCMとして再生する。
PS1実機のWAV decoderや、物理RAM外を使う長いSoftware audioとは区別する。
入力32MiB以内、変換後は既存SPU sample RAM内。超過はエラーとし、無断で切り捨てない。
AIFF・圧縮音声・WAVE extensible・RF64/RIFX・長いhost PCMとWeb v2は対象外。

初期rootはC4 / MIDI60、PlaybackはOne shot。読み込み後のRoot / Playbackを、
VABでProgram / Toneに使う位置へ表示する。Rootは基準鍵盤で元の速度を再生する設定。
Loop entire sampleは終端28-frame単位のzero paddingも含めて繰り返す。
録音の始端・終端によってloop境界のclickは生じ得る。

WAV読込時はSample / Morph Offとし、Gain・ADSR・Reverbと休止中PMON等の値・読込要求時のMorph A/Bは保持する。
Root / Playback編集は現在の音作り設定を保持する。音源採用時に保持音・残響は停止する。
file読込・変換・engine準備は専用workerで行い、成功時だけ新しいsourceを採用する。
host restore / 別source / editor closeで古い準備を採用せず、失敗時は正常pendingも保持する。

変換後sampleとroot / loopを既存AdpcmSample kind3 / state8へ埋め込む。
元WAVをDAW projectにそのまま保存するlossless backupではない。元fileなしで演奏を復元する。
26parameter ID・初期Sine・Native SDK固定commitを維持する。

## 検証

- Offline全20成功（9.80s）、macOS全20成功（5.20s）、ASan全20成功（60.89s）。pure3modules / 新3testsのstrict検査も成功した。
- pure decoderの不正RIFF / chunk / format / Float32値、最大RAM / 全切断位置、独立ADPCM decodeのSNR、8k〜384kのpassband・alias / image抑制を検証した。PCM16と短いloopのADPCMは既存Web encoderと一致。
- 実AU48k、実VST3 44.1 / 48 / 96kで4format・元WAVなし復元・全26設定・root / playback編集・One shot終了・loop継続・1packet flag7・不正入力時のactive / pending / MIDI保持を確認した。既存suiteと公式VST3 validator47/0も成功。
- 初期Sine WAV `/private/tmp/tetorica-spu-wav-demo.wav` は従来SHA256 `7a61b56d356ac198c35808b503cf0b61b55a9547b21770eb2a67c97d0cc4aef3`と一致。
- 最終点検で、既存ADPCM音色の正の最小subnormal rootが`hz/440`で0になり、表示用log2からintへの変換が範囲外になる境界を見つけた。表示式を`log2(hz)-log2(440)`へ補正し、実sourceから抜いた関数をUBSan / float-cast-overflowで極値・A4・C4検査した。`af20bcc`以降の変更はこのUI表示関数だけで、上記ローカルの音声・pure検査対象sourceは同一。
- StandaloneでPMON＋MorphのWeb音色からWAVを読み、C4 / One shot / Sample / Morph OffとGain100%保持を確認した。native Root submenuでC3 / MIDI48、PlaybackでLoop entire sampleへ変更し、Save Webで`build/wav-ui-04u_5txf/standalone.spu.json`を保存した。
- このfileを実Web readerとChromium / WebKit公開UIでload→実engine→Save downloadし、C3 root / loop0・sample bytes・Reverb33/27・休止PMON / Morph設定 / A/B一致を確認した。物理出力はgain0。元ADPCM2528bytesはloop flags2か所以外一致。
- 不正Float32 WAVはエラーを表示して前のC3 / Loop / sample名 / Gainを保持した。Load dialogのCancel後にStandaloneを終了した。
- 作業中の旧test compileと不正UTF8名fixture修正が重なり、macOS初回loader testが旧fixtureで失敗した。最終sourceから再buildした全20件は成功。production loaderの動作変更による回避は行っていない。
- 最終buildで、書き出したWeb音色を再読込しC3 / Loop / Reverb Studio Small / return33%・27% / Gain100%を確認した。Standaloneは終了した。
- 初回[CI41265269673](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41265269673)、最終commit `a6f1a2e`の[CI41266519833](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41266519833)ともoffline / macOS両job・全記録step成功。metadataを保存し、job log / artifact downloadと別環境実行は行っていない。
- 初回WAV対応AUを既知manifest照合・退避付きで更新し、auval26params成功。GarageBand旧schema8 projectを再openしてEmbedded VAB / Program3 / Tone0 / Gain99% / Project sound restored / Reverb Off / return25%ずつ / Morph Offを確認した。
- 表示境界補正後の最終AUも照合・退避更新し、auval26params成功。GarageBand projectを開き直して保存bytesを確認したが、CUAのbutton / coordinate / double-click操作ではAU editorを開けず、最終editorの再表示は人確認へ残した。再生・録音は停止し、最後はGarageBandのmain画面。
- 保存済みpayload4768bytesのSHA256は`477fd0b00a6b7170fb6ef6c6e525ddce80d9a664c21dd4622ad4efd84340e534`で前後一致。埋込VAB3744bytesも一致。更新前project7filesのbackupを照合し、別の原本project11filesも不変。更新後projectは再保存していない。
- 実聴・長時間使用・最終AUのeditor再表示・実DAW独自WAV UIとautomation録音は未確認。

## AU更新・退避

更新前installed AU manifestは[export記録](web_instrument_export_20261101.json)、現在のmanifestは[WAV JSON](wav_import_20261101.json)の`au.file_sha256`。
最終更新journalは`build/au-backups/au-update-91awn33w/manifest.json`。初回WAV更新は`build/au-backups/au-update-fdw1qghw/manifest.json`に保持した。
source / installedの全5files、両段階の新旧backupの全5filesとupdate chainを照合した。
最終AU binary SHA256は`ab32cbb34b23441053b92238fd3cd7e86fcc7ef379ab40371711725ee03d2055`。
local registrarの強制終了とrollbackは行っていない。

戻す必要がある場合はGarageBand等のhostを終了してから、次を実行する。
installerは更新後の既知hashと一致する場合だけ退避済みAUへ戻す。
このrollbackの戻り先は初回WAV版`af20bcc`。

```sh
python3 scripts/install_au.py --rollback build/au-backups/au-update-91awn33w/manifest.json
```

## 形式資料

chunkのWORD paddingとRIFF構造は[Microsoft RIFF資料](https://learn.microsoft.com/en-us/windows/win32/xaudio2/resource-interchange-file-format--riff-)を、format fieldの関係は[WAVEFORMATEX](https://learn.microsoft.com/en-us/windows/win32/api/mmreg/ns-mmreg-waveformatex)を確認した。
受け付けるfmtは16bytes、またはcbSize0の18bytes。fmt / data / factの重複、欠落paddingとtrailing bytesを拒否する。任意factがある場合はframe数を照合する。
再標本化は1024phaseのBlackman-windowed sincを使い、downsample時はsupportを拡げる。44.1kHz入力はそのまま使う。sinc overshootはADPCM量子化範囲へ収め、自動normalizeは行わない。
