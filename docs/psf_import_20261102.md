# PSF1 /MiniPSF静的VAB抽出（2026-11-02 JST）

開始`c46c378`、実装前TODO `ed5a889`。
Web版と同じPSF1 /MiniPSFからの静的VAB抽出をNative Pluginへ移植した。公開済みv0.0.1には含まれない開発版の機能。
version /parameter ID /state schema11 /固定SDK /手元installed AUは変更していない。

## 操作

1. **PSF... → Open PSF folder...**でPSF /MiniPSFと必要なPSFLibが入ったフォルダを選ぶ。サブフォルダ構成を保って索引を作り、最初のEntryを抽出する。
2. 最初の対応bank /再生可能Toneを自動で読み込む。**PSF → Entry**で別PSF、**PSF → Bank**で別bankへ切り替える。Program /Toneは既存menuを使う。
3. **Load sound...**からPSF単体も選べる。その場合は選んだfileの親folderがroot。`../`でrootより外のPSFLibが必要なら、PSF menuからそれらを含む上位folderを明示的に選ぶ。
4. DAW保存には選んだVAB全bytesとProgram /Toneが入る。復元後は元PSF不要。他のEntry /Bankを再び探すにはfolderを開き直す。

依存不足や展開失敗では以前の音を保持する。有効な索引ができた後の抽出失敗でもEntry menuは残る。
hover時のPSF tooltipにEntry・bank数・棄却候補の理由を表示する。folder取消と古いdialog /worker結果は音源を変えない。
Builtin /別素材へ移るとPSF bankの表示を解除し、残っているcatalogを現在の音源と取り違えない。

## 処理と制限

`PsfImport`はfile bytesだけを受け取る。PSF1 header /圧縮CRC /zlib stream /PS-X EXEを検証し、
`_lib → 本体 → _lib2 /_lib3...`の順でPS1の2MiB RAMとcoverageを合成する。
alias、depth10 /visit256、32MiB展開総量、1 EXE 2,033,664bytesを制限する。
256 VAB候補 /256 structured SEQ /32 banks /抽出8MiBが上限。
未読込領域、別の妥当なVHやSEQをbodyと誤認する配置、対応Toneがないbankを棄却する。
Webより厳密に、zlib stream終了後の余分な圧縮section bytesも拒否する。

`PsfLoader`は非audio workerで選択folderをsnapshotする。4MiB/file、512files /64MiB総量、8192 directory entriesを上限とし、
symlinkを追わず、file変化も検出する。PSFLib参照から追加のfilesystem /networkアクセスはしない。
新しいticketだけを公開し、UIはさらにrestore revision /engine generation /menuのcatalog identityを照合する。
抽出成功後にだけ既存`VabLoader::requestFirstPlayable`へ渡す。展開・探索・native音源準備をaudio callbackへ入れていない。

圧縮処理は固定iPlug2に含まれる**1.3.1-WDL zlib**をprivate static targetとして使用する。
`Z_PREFIX`とhidden visibilityでsystem /host zlibと分け、追加DLLや依存pin変更はない。noticeを同梱する。

PSF2、曲再生、CPU /game driver実行、実行時に展開・生成されるbank、不確定なVH /VB対応付けは未対応。
選ぶfolderは必要な一式へ絞る。不正PSFを含むsetや上限を超える大きなarchive folderは読み込めない。
path照合は完全一致を優先し、ASCII大文字小文字のfallbackは曖昧な候補を拒否する。

## 検証

- macOS APP /AU /VST3 build成功。CTest35/35成功、最終修正後に関連3/3を追試した。
- 固定zlibを含むASan /UBSan buildでPSF関連3/3成功。上限・CRC・zlib破損 /bomb・依存順 /欠損 /cycle /path /RAM coverage /曖昧body /取消 /世代を検証した。
- PSF worker → 2banks → first-playable /Program /Tone → embedded state保存復元を検証。元folder削除後もVAB bytesと44.1 /48kHz PCMが完全一致。失敗 /取消 /古いticketは現音と受理済みpending importを変更しない。
- 実AU hostは22 PASS行、実VST3は44.1 /48 /96kHzの既存suiteと公式validator47 passed /0 failedが成功した。これは既存host機能の回帰検査で、DAW内file panelのPSF操作確認とは区別する。
- Standalone実画面でDemo folderのMiniPSF＋階層PSFLib読込、PSF1/2 → 2/2、Builtin復帰で選択表示解除、Load soundからstandalone.psf直接読込を確認した。最後は検証アプリを終了した。

自作fixtureのみを使用し、ゲーム素材は同梱していない。実聴・実DAW独自file panelでのPSF操作・実ゲームPSFの追試は未実施。
Windowsを含むCI結果は実行後にここへ追記する。証跡は`build/psf-import-20261102/`。

## 再開点

手元installed AU /GarageBand projectと公開v0.0.1 assetsは今回更新していない。
CI成功後、次は既知manifest照合・退避付きのAU更新と、実DAWでPSF folderを読み込む操作確認。
新しい公開versionの作成は別段階とする。
