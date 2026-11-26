# After Effects プラグイン開発

## PiPL リソース

- エフェクトプラグインの新規作成・登録情報変更時は、PiPL の生成とバイナリへの組み込みを必ず確認する。
- Windows の Visual Studio プロジェクトでは、`CustomBuild` で `.r` をプリプロセスして `.rr` を生成し、`./AfterEffectsSDK AE2025/Examples/Resources/PiPLtool.exe` で `.rrc` に変換する。さらに `MSWindows` を定義してプリプロセスした `.rc` をリソースとしてコンパイルする。実装例は `Aberration/Aberration.vcxproj`。
- macOS の Xcode プロジェクトでは、同じ `.r` を Rez でコンパイルしてバンドルに組み込む。
- ビルド時は、生成したバイナリに PiPL が含まれ、表示名、Match Name、カテゴリ、エントリーポイント、バージョン、グローバルフラグがコードの登録情報・初期化情報と一致することを確認する。
