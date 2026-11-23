# TestNoise

After Effects用のノイズエフェクト。Windows x64向け。

| 項目 | 値 |
| --- | --- |
| ファイル名 | TestNoise.aex |
| エフェクト名 | Noise |
| カテゴリ | Test |
| Match Name | Test TestNoise |
| 対応深度 | 8 / 16 / 32 bpc（32 bpcはSmartFX） |

## ビルド

Visual Studio 2026で `TestNoise.sln` を開き、`Release / x64` または `Debug / x64` をビルドする。
「C++によるデスクトップ開発」、MSVC v145、Windows SDKが必要。
AE本体の起動はビルドに不要。

Developer PowerShellでは次のコマンドでもビルドできる。

```powershell
msbuild .\TestNoise.sln /p:Configuration=Release /p:Platform=x64
```

出力先：`bin/x64/Release/TestNoise.aex`（Debugは `bin/x64/Debug`）。
自動インストールは行わない。

## SDK切り替え

全プラグイン共通の `../Build/SDK.props` の `AESDKRoot` を変更する。
初期値はリポジトリ直下の `AfterEffectsSDK AE2025`。
`Examples/Headers` と `Examples/Resources` を含むSDKのルートを指定する。
切り替え後はVisual Studioでソリューションを再読み込みし、リビルドする。

一時的な切り替えはコマンドラインで指定できる。

```powershell
msbuild .\TestNoise.sln /t:Rebuild /p:Configuration=Release /p:Platform=x64 /p:AESDKRoot="D:\SDK\AfterEffectsSDK AE2026"
```

追加のSDK設定は `../Build/SDK.local.props` に記載できる（Git管理対象）。
初期状態は設定なしで、`SDK.props` の既定値を使用する。
共有する設定には、個人環境の絶対パスではなく相対パスを使用する。

```xml
<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <PropertyGroup>
    <AESDKRoot>$(MSBuildThisFileDirectory)..\AfterEffectsSDK AE2025</AESDKRoot>
  </PropertyGroup>
</Project>
```

将来のSDKとの互換性は未確認。パス変更後にリビルドして確認する。

## パラメータと描画

- ノイズの強さ：0〜100%、小数点以下2桁、初期値10%。
- カラーノイズ：初期値オン。オフではRGBに共通の乱数を加える。
- オリジナルとブレンド：0〜100%、小数点以下2桁、初期値0%。100%で入力と同じ。

一様分布の加算ノイズ。フレームごとに変化し、同じ時刻・座標では同じ乱数になる。
アルファは保持し、ノイズ量にはアルファを乗算する。完全透明画素は変更しない。
8/16 bpcは有効範囲にクランプ。32 bpcは負値・1を超えるHDR値を保持する。
モノ設定でも、8/16 bpcではチャンネルごとのクランプにより端の色が変わる場合がある。
状態を共有しない実装でマルチフレームレンダリングに対応。

## AEへの導入と確認

AEを終了し、生成した `TestNoise.aex` をAEの `Support Files/Plug-ins/Test` フォルダにコピーする。
AE起動後、エフェクトの `Test > Noise` を適用する。
管理者権限が必要な場所へのコピーは手動で行う。

AE上でのロード、UI表示、8/16/32 bpcの実描画は未確認。
本体を利用できる環境で、強さ0%、ブレンド100%の入力一致と各深度の描画を確認する。

## Git管理

SDK、生成バイナリ、ビルド中間物はルートの `.gitignore` で除外。
`Build/SDK.local.props` はGit管理対象。
SDKは各PCに別途配置する。SDK自体は変更していない。
