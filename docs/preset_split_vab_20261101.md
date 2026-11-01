# AUプリセット名・分割VH/VB読み込み（2026-11-01）

AUのプリセット名warningを解消し、Load soundへ分割VH/VBの選択を追加した。
手元のAUも退避更新し、GarageBand旧VABの保存・完全終了・再起動後の復元を確認した。

作業前TODOは`6a83f8a`、開始Pluginは`56f6403`、初回実装は`7c1c411`、
最終sourceは`9e64eac`、Native SDKは`95cf71a`。
[source・binary・log hash /CI /current installed manifest](preset_split_vab_20261101.json)を参照。

## AUプリセット名

単一のfactory preset **TetoricaSPU**を初期化した。
ホストがPresentPreset /CurrentPresetへ設定したユーザー名と負番号を保持し、ClassInfoの名前にも反映する。
ClassInfoを読み込む場合は、全音源データの検証・復元成功後だけ名前をcommitし、user番号は-1とする。
名前は音源を選ぶ命令として扱わず、実際のfactory番号0のrecallで既定のBuiltin Sineへ戻す。

Unicode /空文字 /最大1024 UTF16 unitsを受け入れ、mutableな入力文字列はcopyする。
不正な名前・property size /scope /element /factory番号や壊れたstateは、名前・Gain /
pending音源・active PCMを保持して拒否する。メタデータだけのrenameは演奏をrestartしない。
通知はstate lockを解放してから行い、既存のquiescent /serialized host state契約を維持する。

pinned AU adapterのproperty methodが非virtualのため、このpluginのmodern 64bit AudioComponent
factory dispatchでPresent/Currentのみを補う。他property・MIDIを含むselectorは公式factoryへ委譲し、
vendorは変更しない。Get /SetのABIはApple公式typedefとのstatic_assertで固定する。
legacy ComponentManager経路は今回拡張していない。

初回候補は初期名とClassInfo往復が通っても、負番号PresentPresetへの名前設定を無視してwarningが残った。
旧候補で失敗する公開API回帰を追加し、最終AUではその回帰と実auvalの両方が成功した。
validation scriptも、auvalがexit0でも当warningを検出したら失敗する。

## 分割VABの操作

**Load sound...**から`.vh`または`.vb`を選ぶと、対応するもう一方のファイル選択が開く。
VH→VB、VB→VHのどちらも使える。2枚目は最初のファイルと同じdirectoryから始まり、
反対側の拡張子に絞る。取消なら両方が揃う前の新しい読込要求を出さず、既存pendingも取り消さない。

対応はSony標準VAB v6/v7。VHは最大68,128bytes、VBは512KiB、合計は既存VABの1MiB上限以内。
workerで確保前のfile size・exact read /EOF /読込後sizeを確認し、既存split parserで
headerの正確な境界・declared size・sample領域を検証する。ADPCMの再encodeやheader再構築は行わず、
元VH bytesと元VB bytesを連結して通常のembedded VABとして保存する。

Program /Tone、Morph A/B保持、元fileなしのproject復元、選択ToneのWeb exportは通常VABと同じ。
形式にpair checksumはないため、同サイズで構造上validな別VBの出自までは判定できない。
新しいworker要求後の失敗はactive音源を保持するが、古いpending要求は既存の世代契約どおりsupersedeする。

2枚目のpanelはOnIdleでstate lock解放後に開く。UI lifetime /serialとhost revision /
engine generationを確認して、閉じたeditorやhost restore前の応答を受理しない。
editor close前に両選択が完了したworker要求は、通常VABと同じ採用経路を使う。

## 検証範囲

- worker unitは3Tone×44.1 /48 /96kHzで完全VAB参照と左右PCMが一致。
  v6、first playable /exact Tone、UTF8 path、元padding /Morph A/B保持を検査した。
- 元VH/VB削除後のstate9復元と選択ToneのWeb exportも成功した。
  不正header /body、上限、両fileのgrow /shrink、最新要求 /host restoreとの世代競合、
  first callbackのordinary /aligned C++ new/delete0を検査した。
- StandaloneでVB→VH /VH→VB、2枚目取消、不正pair時のP3/T0保持、その後の正常retryを画面確認した。
- 実DAWのsplit file panelや選択途中のrestore /closeは、Standaloneの確認・source reviewとは分けて未確認とする。
  実聴・長時間使用・DAW automation録音も既存TODOに残す。

Offline /macOS /ASan+UBSanのCTest各28件、実AU48kHzの16 PASS行、実VST3全3rateの55 PASS行、
公式VST3 validator47 passed /0 failedが成功した。AU専用の追加修正後もcore /CTest sourceと
VST3 /Standaloneの全bundle filesは不変で、既存の該当検証を引き継いだ。
最終macOS build /CTest、配置済みAUのproperty /PCM /state検査とauvalも成功した。

最終head `9e64eac`の[GitHub Actions](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41308675010)は
offline /macosの全2job・全stepが成功した。artifact metadata /digestは記録したが、
artifact /log archiveのdownloadや配布artifact実行は行っていない。

## 配置と戻し方

更新前の全5filesは[CC120版manifest](au_cc120_update_20261101.json)と一致した。
GarageBandの既知検証copyを保存前に全11files退避し、host保存終了後に更新した。

```sh
python3 scripts/install_au.py --update \
  --expected docs/au_cc120_update_20261101.json --backup-root build/au-backups
```

最終journalは`build/au-backups/au-update-260st6dc/manifest.json`、状態は`installed`。
新source /installedの全5files /2,078,353bytesと、旧backup2組の全5filesを照合した。

| 対象binary | SHA256 |
| --- | --- |
| 旧CC120 /最終退避2組 | `d9c6033f221c7f32e3efbd4c01762b15f378132cc0426ff08088246e54985e04` |
| 最終source /installed | `3f82bcff46f182c2bd370f92516759796aa72aa8d4cf3474716376037d94aa81` |

必要時はaudio hostを保存・終了し、次の条件付きrollbackを使う。
最終更新のrollbackは実行していない。

```sh
python3 scripts/install_au.py --rollback build/au-backups/au-update-260st6dc/manifest.json
```

初回候補のjournal `au-update-hh1zg247`は、warning残留を見て既にrollbackした記録。
その後、既知CC120版へ戻ったことを全file照合し、最終候補を別journalで更新した。
**次回更新のexpectedは`docs/preset_split_vab_20261101.json`。**
両journalと退避bundleはローカル`build/au-backups`に保持する。

## GarageBand互換性

`build/preset-split-20261101/spu-preset-split-restore-check.band`をknown sourceから複製した。
保存・完全終了・再起動後に、実AUのEmbedded VAB /P3/T0 /Gain99 /Project sound restored、
Reverb Off /Morph Offと、main windowの正しいproject URL・再生/録音停止を確認した。

schema9 payload4,768bytesとVAB3,744bytesは、保存前・保存後・再起動後で完全一致した。
payload SHAは`35a6b2cd76ff553bd121a87b5a46513cbb78b953da1f673cedd863447fb33e1c`、
VAB SHAは`71a7de205c0ebe1b8743288d90cd3b0204da9bd81aed9159af31f404d7cf2be6`。
保護原本11files /known source7files /開始時copyの保存前退避11filesも不変だった。

開始時に開いていたCC120検証copyは退避後に意図的に保存したため、UI /project /autosave filesは
変わり得る。埋め込みpayloadは保持した。原本とknown sourceを上書き保存したものではない。
保存XML inspectorは候補とbytesの検査で、active bindingの解析器ではない。
active復元は再起動後の実画面で別途確認した。今回はGarageBandでの演奏・録音・WAVE書出しや
独自split Load sound操作は行っていない。新copyのAU editorを表示、再生・録音停止で残した。

parameter ID0〜25、state schema9、Gain範囲、SDK pinは維持する。
Web /Tauri /npm /version /tag /公開releaseは変更しない。
ログ・画面・project copy・rollback bundleはignored `build/`のローカル証跡で、docs commitには含めない。
