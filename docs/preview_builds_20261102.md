# macOS /Windows Preview配布候補（2026-11-02）

ユーザーの指示でrelease用の具体的な配布候補とActions検証を追加した。
Linuxは「macOS／Windowsを先に仕上げる」の回答により別TODOとする。
今回はtag /公開Release /Developer ID署名 /公証 /Authenticode署名を作成しない。

開始`85c8f4e`、作業前TODO `cfaf621`、初回実装`d4741a1`、検査修正`384df15`、CI登録修正`de98964` /`74903c5`。
Native SDK `95cf71a`と他の依存pin、音源 /保存schema10 /parameter ID0〜25は変更しない。
手元installed AUとGarageBand projectにも操作しない。

## 完了結果

配布候補のsourceは`74903c55271b89dd3ec103efc08e3c6f584726f4`。
[Preview CI全3job](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41351695066)と
[通常検証CI全2job](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41351695098)が成功した。

| Target | Actions artifact | ZIP |
| --- | --- | --- |
| macOS Universal | preview-macos-universal | tetorica-spu-plugin-0.1.0-preview-74903c55271b-macos-universal.zip |
| Windows x64 VST3 | preview-windows-x64-vst3 | tetorica-spu-plugin-0.1.0-preview-74903c55271b-windows-x64-vst3.zip |

両ZIPをdownloadし、sidecar /CI report /元commit /全manifestのbytes・mode・typeを照合した。
Macのmanifest対象22entryは安全に展開して全bundleを再検査し、Windowsのmanifest対象5entryはMacではDLLを展開・実行せず、
PE構造とWindows実検査reportへ照合した。ZIP /binary hash、CI metadata、証跡38filesのhashは
[検証JSON](preview_builds_20261102.json)に保存した。ローカルの保存先は`release/ci-41351695066/`。

Python64件、macOS /Windows /UbuntuのCTest各29件が成功した。
同じUniversal ZIPのAU /VST3をARMとIntelで実行し、両方のstrict auvalと公式VST3 validator47/0を確認した。
Windowsでも候補VST3の実MIDI /PCM /state検査とvalidator47/0が成功した。
hostログのVST3 57 PASS行は末尾summary1行を含み、AU19 PASS行も末尾summary1行を含む。
手元installed AUの全5filesは既存manifestと同一のまま。

## OS別の成果物と範囲

| OS | 配布候補 | 検証 |
| --- | --- | --- |
| macOS | Universal APP /AUv2 /VST3 | 全Mach-Oにarm64 /x86_64、minimum13.0、system-only dylib、font /notices、実行権限、resource seal /ad-hoc署名を検査 |
| Windows | x64 VST3 bundle | MSVC /MT /UTF8、PE AMD64、実UIと同じlookupで埋込みRobotoの全bytes、実VST3と公式validator |
| Linux | 今回Plugin ZIPは作らない | 既存Ubuntu core /offline rendererの29件を維持。Linux GUI /native wrapper移植は別作業 |

Mac版はこのMacからUniversalを構築できる。Windows版はWindows runnerで構築し、Macからのcross-buildを採用しない。
GitHubの[runner表](https://docs.github.com/en/actions/reference/runners/github-hosted-runners)に合わせ、
Apple Siliconはmacos-15、Intelはmacos-15-intel、Windowsはwindows-2022を使う。
macOSの配布候補ZIPはARM runnerで一度だけ作り、Intel jobが同じZIPをdownload /検証 /展開して実行する。

Linux未対応は、固定iPlug2のIGraphicsLinux.cppがTODO、APP.cmake /IGraphics.cmake /VST3.cmakeが
Linux未実装で、Timerにも実装がないことを確認した。[公式CMake説明](https://iplug2.github.io/docs/md_cmake.html)も参照。
音源コアをbuildできることとは区別する。

Windows APPも内部build /resource検査には含めるが、配布ZIPへ入れない。
APP.cmakeがリンクする旧Steinberg ASIO sourceは別SDK agreementを案内しており、その本文はcheckoutにない。
VST3のlinkにはこのAPP /ASIO sourceがなく、DAWのdriverを使う。
Windows向けGLAD /Khronosの追加noticeを別fileで同梱した。

## 配布処理

`macos-release` presetは、SDK /core target生成前からCMAKE_OSX_ARCHITECTURESへarm64;x86_64を指定する。
`windows` presetはVS2022 x64 Release、SDK /core /PluginとhostのCRTをstaticへ揃える。
Windows APPのmenu /preferences、APP /VST3共通Roboto resource、権限不足時のSDK junction fallbackを追加した。
既存SDKやlinkを無断で置き換えない。

package_preview.pyはclean treeを要求し、元buildを変更せずstagingへcopyする。
Macのstaging3bundleだけをad-hoc署名した後、architecture /外部依存 /resources /署名を検査する。
これはDeveloper ID /公証の代替ではなく、Gatekeeperの信頼判定済みとは扱わない。

ZIPはwhole bundleとINSTALL /LICENSE /notices /source・file hash manifestを保持する。
ZIP全体のSHA256も別fileへ書く。展開検査はbytes /mode /typeを照合し、path traversal、
Windows drive /ADS /予約名 /case alias、外部symlinkを拒否する。
APP /AU /VST3のバイナリファイルだけを単独配布しない。

実host検査はZIPと同じstaging（IntelはZIP展開先）を読み、MIDI・PCM・26parameter・旧state互換を確認する。
Macのauval用installはGITHUB_ACTIONS=trueの一時runner限定で、--sourceに候補AUを明示する。
ローカルの通常host検査はdevice /editor /installを使わない。

## 検査経過

ローカルUniversal buildとCTest29件、初回候補のarchitecture /ad-hoc署名・実AU /VST3・
公式validator47/0が成功した。最終toolingのPython検査は64件成功（1.312秒）。

初回Actionsは両OSでbuild /CTestまで通過した後、配布検査で2点を検出した。
Macはhelper configure後に既存出力folderへcopyしようとして失敗したため、installerへ明示--sourceを追加した。
Windowsはplain font名で検査して実UIのresource検索と違っていたため、pinned実装と同じTTF列挙・
先頭末尾文字除去・lowercase比較・元のexact名での読込へ修正した。

修正後の[Preview CI](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41347591104)は
WindowsとARMが成功し、Intelでも実AU /VST3 /validatorは成功したが、最後のAU自動検出でtimeoutした。
[通常検証CI](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41347591151)は全job成功した。
Intelの失敗を隠さず、AU install /auvalを直接登録host検査より先に行い、一時runnerに限って
Components directoryのmtimeを更新する。Appleの[AU登録更新条件](https://developer.apple.com/library/archive/documentation/MusicAudio/Conceptual/AudioUnitProgrammingGuide/AudioUnitDevelopmentFundamentals/AudioUnitDevelopmentFundamentals.html)に沿った処理であり、元のtimeoutの原因を断定したものではない。
直接AudioComponentRegisterの登録はprocess内限定なので、global登録を検証したとは扱わない。
検出失敗時はauval一覧・plist /権限 /mtime /署名・registrarの情報を残し、実validation失敗をretryで成功扱いにしない。
続く`de98964`の[Preview CI](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41350143700)も
Windows /ARMは成功し、Intelは検出timeoutした。新しい診断から、SIGTERM送信後も同じregistrar PID409が
一覧要求へ応答し続けていることを確認した。ローカルAppleの
`/System/Library/LaunchAgents/com.apple.audio.AudioComponentRegistrar.plist`はEnablePressuredExit=true、
`/usr/share/man/man5/launchd.plist.5`はそのjobがデフォルトでSIGTERMを無視することを明記している。
そのため送信exit0だけを再起動完了とは扱わず、一時runnerの対象UID /process名だけを終了し、
古いPIDが消えたことを確認する処理へ修正した。ユーザー環境やroot daemonには適用しない。
最終IntelのログではUID501 /PID428だけへSIGKILLを送り、元PIDの消失後にTspuを検出し、
auval /実VST3 /validator /実AUの全検査が成功した。失敗した3runと成功した最終runをJSONで区別した。

## 残る確認

実deviceでの音、演奏latency、各OSのGUI・native file panel、長時間DAW動作は未確認。
Mac13.0へのdeployment設定は検査するが、実macOS13上の演奏確認済みとはしない。
WindowsはWindows Server2022のCIであり、一般PCのdriverや実DAW GUIまで確認したものではない。
新しいschema10保存を旧Pluginで読むことはできないため、配布候補を試す場合も元project copyを保持する。

手元AUの退避更新・GarageBand新旧projectの保存復元、Windows Standaloneの配布条件、
Linux Pluginのplatform /UI設計、正式署名・公証・Release公開は別TODOとする。
