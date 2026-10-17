# LiveStem ARA

Logic Pro と Studio One で共同制作するときに、相手のトラック（共有WAV）を、プロジェクトを開き直さずに最新の内容へ差し替える ARA2 プラグインです。

受け側はDAWで普段どおりクリップをカット・移動・複製できます。中身だけが共有フォルダにある最新のWAVに追従します。DAWが持つ音声データは使わず、プラグインが同期フォルダの最新ファイルから該当範囲を読んで鳴らします。

> **現在の状態：フェーズ1（試作と検証）**
> 自動テストと疑似ARAホストでの確認は済んでいます。Logic Pro／Studio One の実機での確認はまだです。結果は [docs/phase1-report.md](docs/phase1-report.md) にあります。

## 採用バージョン

| 依存 | バージョン | 取得方法 |
| --- | --- | --- |
| JUCE | 9.0.4 | CMake の FetchContent で自動取得（`-DLIVESTEM_JUCE_DIR=...` でローカルのものも使えます） |
| ARA SDK | 2.3.0（`releases/2.3.0`） | FetchContent で自動取得（`-DLIVESTEM_ARA_SDK_DIR=...`） |
| doctest | 2.4.11 | テスト用。FetchContent で自動取得 |
| CMake | 3.22 以上 | |
| C++ | C++17 | |

## フォルダ構成

```
livestem-ara/
  core/        ファイル監視・WAV読み込み・バッファ差し替え・範囲計算（JUCE/ARA非依存、単体テスト対象）
  plugin/      JUCE＋ARAのプラグイン層（ドキュメントコントローラー、再生レンダラー、画面）
  tests/       単体テスト（doctest）とストレステスト
  hosttest/    疑似ARAホスト。ビルドしたVST3をARAモードで読み込んで動作を自動確認する
  docs/        運用ガイド、DAW手動確認チェックリスト、フェーズ1報告
```

## ビルド

### macOS（AU＋VST3、Universal Binary）

Xcode（コマンドラインツール）と CMake が必要です。Ninja があれば速くなります（`brew install cmake ninja`）。

```sh
cd livestem-ara
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target LiveStemARA_AU LiveStemARA_VST3
```

- arm64＋x86_64 の Universal Binary がデフォルトです（`CMAKE_OSX_ARCHITECTURES`）。
- できるもの：
  - `build/plugin/LiveStemARA_artefacts/Release/AU/LiveStem ARA.component`
  - `build/plugin/LiveStemARA_artefacts/Release/VST3/LiveStem ARA.vst3`

### Windows（VST3 x64）

Visual Studio 2022（C++ デスクトップ開発）と CMake が必要です。

```bat
cd livestem-ara
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target LiveStemARA_VST3
```

できるもの：`build\plugin\LiveStemARA_artefacts\Release\VST3\LiveStem ARA.vst3`

### ビルド済みのものを使う（GitHub Actions）

`livestem-ara/` を変更して push すると、[`.github/workflows/livestem-ara.yml`](../.github/workflows/livestem-ara.yml) が macOS／Windows／Linux でビルドとテストを実行します。成果物（`LiveStemARA-macOS`、`LiveStemARA-Windows`）は、Actions の実行結果ページからダウンロードできます。

## インストール先

| OS | 形式 | 置き場所 |
| --- | --- | --- |
| macOS | AU | `~/Library/Audio/Plug-Ins/Components/LiveStem ARA.component` |
| macOS | VST3 | `~/Library/Audio/Plug-Ins/VST3/LiveStem ARA.vst3` |
| Windows | VST3 | `C:\Program Files\Common Files\VST3\LiveStem ARA.vst3` |

macOS では、コピーしたあとに一度アドホック署名をし直してください（ダウンロードしたものは quarantine 属性も外します）。

```sh
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/"LiveStem ARA.component"
codesign --force --deep --sign - ~/Library/Audio/Plug-Ins/Components/"LiveStem ARA.component"
killall -9 AudioComponentRegistrar   # AUの一覧を更新
```

Logic Pro は起動時に AU を検証します。表示されない場合は、Logic Pro の「プラグインマネージャ」で再スキャンしてください。

## DAWでの使い方（フェーズ1）

1. 相手の共有WAV（`<担当者>_<スロット名>.wav`）を、曲頭（小節1）から1つのクリップとして置きます。**取り込み時に音声をコピーしない設定と、タイムストレッチを無効にする設定**にしてください（[運用ガイド](docs/operations-guide.md)）。
2. そのトラックに LiveStem ARA を ARA プラグインとして挿します。
   - Logic Pro：トラックのオーディオFXスロットに AU の「LiveStem ARA」を挿す（ARA対応AUとして認識されます）。
   - Studio One：イベントを右クリック →「LiveStem ARA」を選ぶ、またはトラックのインサートに挿す（VST3）。
3. プラグイン画面で、対象の音声ソースを選んで「WAVを選択...」から共有フォルダのファイルを指定します。
4. 共有フォルダのWAVが更新されると、0.5〜1秒ほどで新しい内容が鳴ります。

画面には、ファイル名・状態（最新／更新待ち／読み込み中／エラー／ファイルなし）・最終読み込み時刻・内容（サンプルレート、ch数、長さ）が表示されます。

**フェーズ1の制約**
- ファイルは手動で選びます（共有フォルダの自動対応付けはフェーズ2で作ります）。
- ファイルとプロジェクトのサンプルレートが違う場合は、警告を出したうえでDAWが持つ元の音声を鳴らします（リサンプリングはフェーズ2で作ります）。
- 変更範囲の表示はフェーズ2で作ります。

## メモリ使用量の概算

共有WAVは 32bit float に展開してメモリに置きます。

| 条件 | 1トラックあたり | 計算 |
| --- | --- | --- |
| 48kHz・ステレオ・10分 | 約 230 MB | 48,000 × 2ch × 4byte × 600秒 = 230.4 MB |
| 48kHz・ステレオ・5分 | 約 115 MB | |
| 48kHz・モノラル・10分 | 約 115 MB | |

| 共有トラック数（48kHz・ステレオ・10分） | 合計 |
| --- | --- |
| 8本 | 約 1.8 GB |
| 16本 | 約 3.7 GB |
| 32本 | 約 7.4 GB |

- 差し替えの瞬間は、そのトラックの新旧2つのバッファが一時的に同時に存在します。読み込みは1本ずつ順番に行うので、上乗せされるのは最大でも1トラック分（約230 MB）です。
- DAW自身も元のWAVを（ディスクストリーミングまたはメモリに）持ちます。これはDAWの設定次第です。
- 32本×10分を扱うなら、16 GB 以上のメモリを推奨します。

## テスト

```sh
cd livestem-ara
# コアの単体テスト＋ストレステスト（プラグインを作らないので速い）
cmake -S . -B build-core -G Ninja -DLIVESTEM_BUILD_PLUGIN=OFF
cmake --build build-core && ctest --test-dir build-core --output-on-failure

# 再生中にファイルを1秒ごとに書き換え続けるストレステスト（秒数、書き換え間隔ms）
./build-core/tests/livestem_stress 60 1000

# 疑似ARAホストでプラグインを確認（プラグインのビルド後）
./build/hosttest/livestem_ara_hosttest_artefacts/Release/livestem_ara_hosttest "build/plugin/LiveStemARA_artefacts/Release/VST3/LiveStem ARA.vst3"
```

| 種類 | 内容 |
| --- | --- |
| 単体テスト | 範囲計算、書き込み完了判定、バッファ差し替え（並行読み出し）、WAV読み込み（16/24bit整数、32bit float、壊れたファイル）、短い／長いファイル、読み込み失敗時の継続、パス変更 |
| ストレステスト | 再生スレッドが読み続ける間にファイルを書き換え続け、ブロックの途中で内容が混ざらないこと、古いバッファが解放されることを確認。ASan／TSan付きでも実行 |
| 疑似ARAホスト | プラグインをARAモードで読み込み、未対応付け時の元音声、対応付け、再生中の差し替え、ホストへの変更通知、カット・移動・複製、オフラインレンダリング、短い／長いファイル、壊れた／消えた／レート違いのファイル、アーカイブ保存と別ドキュメントでの復元を確認 |
| プラグイン検証 | pluginval（厳しさ5以上、Linuxでは8でも通過）、macOS では `auval -strict` |
| DAW手動確認 | [docs/daw-checklist.md](docs/daw-checklist.md) |

## リアルタイム安全性

- オーディオスレッドは、ロック・メモリ確保／解放・ファイルI/O・ログ出力をしません。
- 読み込んだバッファは `std::shared_ptr` で保持します。オーディオスレッドが参照するのは生ポインタで、`SharedSlot` がアトミックに差し替えます（[core/include/livestem/SharedSlot.h](core/include/livestem/SharedSlot.h)）。
- 古いバッファは「読み手が0になったのを差し替え後に確認してから」監視スレッドが解放します。オーディオスレッドで解放されることはありません。
- ARAの編集中（ホストがモデルを変更している間）は、ロックではなくアトミックなフラグで再生処理をスキップします（[core/include/livestem/EditGuard.h](core/include/livestem/EditGuard.h)）。
- 例外：ファイル未読み込み時にDAWの元音声を読むフォールバック経路は、JUCE の `BufferingAudioReader` を使います（JUCEのARAサンプルと同じ方式）。

## 署名と公証

個人利用ではアドホック署名（上記の `codesign --sign -`）で十分です。他の人に配る場合は、Apple Developer Program（年額）に登録したうえで次の手順を踏みます。

1. 「Developer ID Application」証明書を作り、キーチェーンに入れる。
2. Hardened Runtime を有効にして署名する。
   ```sh
   codesign --force --deep --options runtime --timestamp \
     --sign "Developer ID Application: <名前> (<TEAMID>)" "LiveStem ARA.component"
   ```
3. zip にして公証に出す。
   ```sh
   ditto -c -k --keepParent "LiveStem ARA.component" LiveStemARA-AU.zip
   xcrun notarytool submit LiveStemARA-AU.zip --apple-id <AppleID> --team-id <TEAMID> --password <App用パスワード> --wait
   ```
4. 公証の結果をバンドルに添付する。
   ```sh
   xcrun stapler staple "LiveStem ARA.component"
   ```
5. VST3 も同じ手順で署名・公証する。

Windows はコード署名がなくても読み込めます。配布時に SmartScreen の警告を避けたい場合は、コード署名証明書で `signtool sign` します。
