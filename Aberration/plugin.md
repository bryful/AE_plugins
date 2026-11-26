# After Effects のプラグインの作成

## 対象

- After Effects CC 2025
- SDKは2025を使用 "./AfterEffectsSDK AE2025"
- Visual Studio 2026

## 作業フォルダ

- 作業フォルダは"./Aberration"フォルダで、ソースコード・VisalStudioプロジェクトファイル等はそこに保存。
- デバック時はVisalStudioソリューションファイルは"./Aberration"フォルダに置く
- 将来的には複数のプラグインプロジェクトファイルをまとめたソリューションファイルを作業フォルダのルートに作成しプロジェクトファイルを登録する。

## プラグイン

- Windows用　（できればMac版のファイルも用意してください）
- エフェクトプラグイン
- プラグインファイル名 "Aberration"
- MatchName "Aberration"
- name "Aberration"
- カテゴリ "MyPlugins"
- 8/16/32 bit対応
- PF_OutFlag_NON_PARAMVARYはOFFに（パラメータ変化がない場合は再レンダリングしないでキャッシュを使用）
- 高速処理を目指す。

## エフェクト内容

- 簡単な光収差。今回はテストなので単純にRGBチャンネルを移動させて実装する。
- 移動は小数単位。

## UIパラメータ
- RedOffset 赤チャンネルの移動量（ピクセル 0.0-100.0）デフォルト2.0
- GreenOffset 緑チャンネルの移動量（ピクセル 0.0-100.0）デフォルト0.0
- BlueOffset 青チャンネルの移動量（ピクセル 0.0-100.0）デフォルト2.0
- RedDir 赤の移動方向 (-360.0 360.0) デフオルト -90.0 左
- GreenDir 緑の移動方向 (-360.0 360.0) デフオルト 0.0　上
- blueDir 青の移動方向 (-360.0 360.0) デフオルト 90.0　右
- Channel (RGB|RGBA) 対象とするチャンネル
- Blend With Original オリジナルとのブレンド値。デフォルト0.0％　100.0％で元のまま

アルファーチャンネルは
Channel：RGBAの時は、RGBの移動量に合わせて分離。ALphaの値をR:A/3 G:A/3 B:A/3として扱い単純加算で求める。
Channel：RGBの時はアルファーは手を加えない

移動処理を行う前にRGBチャンネルマット合成（mult）にして移動処理を行った後にストレート合成(unmult)に戻す。

## デバッグ

- デバッグ時は"C:\Program Files\Adobe\Common\Plug-ins\7.0\MediaCore"にバイナリを書き出してVisual Studioでデバッグ作業を行う。
- 指示のない限りVisual Studioでデバッグは行わずビルドのみ。Visual Studioでデバッグは手作業で行います。デバッグできる状態になったらその事を表示。
- リリースビルドは指示のない限り行わない。
- リリースビルぢ時は作業フォルダのルートに"outputPlugins"フォルダへバイナリを書き出しする。
- 上記の設定はプロジェクトファイルに保存。
