<p align="center">
  <img src="images/ARMOR_BANNER.svg" alt="ARMOR-SOLAR banner" width="100%">
</p>

# ☀️ ARMOR-SOLAR

<p align="center">
  <a href="README.md">🇺🇸 English</a> |
  <a href="README_spa.md">🇪🇸 Español</a> |
  <a href="README_fra.md">🇫🇷 Français</a> |
  <a href="README_ita.md">🇮🇹 Italiano</a> |
  <a href="README_deu.md">🇩🇪 Deutsch</a> |
  <a href="README_zho.md">🇨🇳 简体中文</a> |
  🇯🇵 <b>日本語</b>
</p>

### 太陽光ゲートウェイノード：最大 10 個のシリアルポートでインバーターとバッテリーを読み取る（ESP32-S3 ファームウェア、その Web パネル、プロトコルライブラリ）

<p align="center">
  <img src="https://img.shields.io/badge/License-GPL%203.0-blue.svg" alt="GPL 3.0">
  <img src="https://img.shields.io/badge/Language-C%2B%2B17-00599c.svg" alt="Language">
  <img src="https://img.shields.io/badge/Devices-Voltronic%20%C2%B7%20Pylontech-ffb020.svg" alt="Devices">
  <img src="https://img.shields.io/badge/Maturity-scaffolding-FFB020.svg" alt="Maturity">
</p>

---

**正直さのチェック - 今日動いているもの:** **成熟度：scaffolding。** ファームウェアは ESP-IDF 5.4.2 コンテナーでビルドでき、そのコア（設定、各種機器とのやり取り、エミュレート UART の計算：2,548 件のチェック）は代役の機器を使ってコンピューターでテスト済みで、生成するメッセージは ARMOR-COMMON に受理され、パネルは代役ノードに対してブラウザーで試しました。**ボード上で動いたことはなく、インバーターもバッテリーも接続されたことはありません**：Wi-Fi、TLS のパネル、更新、ハードウェアとエミュレートの UART、そしてプロトコルの書式（公開文書と記憶から書いたもの）は未試験です。テストにある ANT-BMS のフレームは他の人が自分の機器で採取したもので、このプロジェクト自身はまだ 1 台も読み取っていません。

---

## 🎯 概要

* **2 種類のボード、1 つのファームウェア：** Wi-Fi の ESP32-S3-WROOM-1 N16R8（標準）と、Ethernet ケーブルの Waveshare ESP32-S3-ETH（DHCP または固定アドレス。スマートフォンから接続できるよう独自の Wi-Fi ネットワークは残ります）。イメージはビルド時に選びます（`tools/build_node.sh generic s3-wifi` または `generic s3-eth`）。ピン表、ポートの既定ピン、アクセス方法はボードに従い、イメージは対応するボード専用です。どちらもビルドでき、コンピューター上でテスト済みですが、実機では動かしていません。
* **ノードのファームウェア**（Wi-Fi の ESP32-S3-WROOM-1 N16R8、または有線の Waveshare ESP32-S3-ETH）：シリアルポートは 10 個（ハードウェア UART 3 つとエミュレート 7 つ、最大 19200 ボー）。それぞれ独立し、インバーター、バッテリー、ローモニターのいずれかを読むため、ノードはインバーターのみ、バッテリーのみ、または両方を読めます。
* **ノードの Web パネル**（レーダーノードと同じもの）：USB コンソールに表示されるコードによるセットアップ、ログインとユーザー、Wi-Fi（ステーションとアクセスポイント）、ブローカー、ロールバック付きのオーバーザエア更新、ログ、HTTPS、そしてポート（各ポートが受信している内容を表示。未デコードのプロトコル用）と測定値のページ。7 言語対応。
* **Voltronic / MPP Solar インバーター**（Axpert、PIP、InfiniSolar とその互換機）、RS232 2400 ボー：フレームとその CRC、および読み取り値 `QPIGS`（系統、出力、バッテリー、PV、状態ビット）、`QMOD`（モード）、`QPIWS`（名前付きの警告と故障）、`QPIRI`（定格）。組み立てられるのは読み取りコマンドだけです。設定は家への給電方法を変えてしまうからです。3 つの方言（ポートで選ぶか、ポート自身が見つけます＝*auto*）：PI30、REVO（別の配置の `QPIGS`、チェックサムで終わる応答）、PI18（`^P005GS`：InfiniSolar V、LV5048、SunGoldPower）。読み取り専用。
* **Pylontech バッテリー**（US2000、US3000、US5000）：コンソールの `pwr` 表（各モジュールの電圧、電流、温度、充電状態）、`bat <n>`（各セルの電圧と温度）、`info <n>`（型式、残容量と満容量、サイクル数）を、総容量と総エネルギーを持つ 1 つのスタックとして要約し、2 つのチェックを持つ RS485 フレームも扱います。書式は公開されているコンソールの記憶から書いたもので、ファームウェアによって異なる可能性があります。列はヘッダーの名前で見つけます（Id 列、MosTempr、SysAlarm.St を持つ US5000 V2.3 の形式を含みます）。残りの充電量とバランス状態は `bat` から、型式・定格容量・サイクル数は `info` と `stat` から得て、1 回だけ問い合わせて 30 分間保持します。
* **ノードのメッセージ**（`armor/solar/<ノード>/<デバイス>/state`、インバーターまたはバッテリースタックごとに 1 つ）。ARMOR-COMMON でスキーマと適合性ベクトルとともに定義され、各ポートが作るものはそれに照らして検査されます。
* **ANT-BMS バッテリー**（自作バッテリーパック用の黒い基板、7S～32S）、3.3 V UART・19200 ボー：ファームウェアの 2 種類のプロトコルの両方に対応（ノードは一方で問い合わせ、答えがなければもう一方で問い合わせ、答えたほうを使い続けます）。セル、温度、充電状態、電流、容量、MOSFET とバランサーの状態を読み取り、実機 4 機種で採取されたフレームで検証しています。読み取り専用：書き込みコマンドは負荷のかかった電池を切断しかねません。ポートのページのボタンで BMS の型式、バージョン、保護とバランスの 56 の設定も読み取れます（新しいプロトコル、読み取り専用）。
* **スマートフォンから Bluetooth で設定：** レーダーノードと同じチャンネルです。ARMOR アプリがノードを `ARMOR-XXXXXX` として見つけ、パネルのユーザーとセットアップコードを使って、名前、Wi-Fi、アドレス、ブローカー、Bluetooth モードを設定します（[プロトコル](docs/BLE_PROVISIONING.md)）。設定を変えない限り、ノードにユーザーがいない間だけ待ち受けます。無線部分は基板ではまだ動作していません。
* **マルチプレクサ付きベース基板（*mux* プロファイル）：** 3 つのハードウェア UART が、それぞれ 74HC4052 を介して、最大 8 ポート（4・2・2 のグループ）を回線ごとに順番に受け持ちます。各ポートは独自の速度と極性を持ち、74HC595 でポートごとに LED が 1 つ付きます。背の高い Pylontech スタックのセルは順番に読めます。標準方言のインバーターでは、**2 つ目の PV 入力**（`QPIGS2`）と**並列システムのユニット**（`QPGS`）が任意の読み取りです。いずれも基板ではまだ動作していません。
* **まだ：** ANT-BMS の Bluetooth 接続（ノードは、より安定なケーブルを使います）、そして実機と実際の機器での動作確認。

## 📂 リポジトリの構成

```text
ARMOR-SOLAR/
├── main/    the ESP-IDF component: app_main, solar_manager (one task per port, or per group in the mux profile), uart_ports, mux_board, port_leds_hw, network, web_server, api_shared, mqtt_link, node_store, tls_cert, ble_provision, board_ethernet
├── core/    voltronic, voltronic_pi18, pylontech, ant_bms, ant_registers, ant_settings, solar_json + solar_config, poller, soft_uart, netplan, auth, board_s3, ble_frame, ble_dispatch, mux_group, port_leds, console_probe, json (no hardware in them)
├── panel/   the web panel: index.html, app.js, text.js (7 languages), style.css
├── tools/   build_node.sh, pack_panel.py, panel_mock.mjs, panel_browser_test.mjs
├── tests/   test_solar.cpp, test_node.cpp, test_board_eth.cpp, test_ble.cpp, test_mux.cpp, test_parallel.cpp, test_console.cpp, emit_samples.cpp, emit_poller_samples.cpp, check_samples.py (+ the ANT-BMS frames)
└── docs/    NODE_FIRMWARE, NODE_HARDWARE, BLE_PROVISIONING, PROTOCOLS, SOLAR_MESSAGES, STUDIO_MENUS
```

## 🛠️ 開発環境

```bash
cmake -S tests -B build/host && cmake --build build/host
build/host/test_solar && build/host/test_node && build/host/test_board_eth && build/host/test_ble && build/host/test_mux && build/host/test_parallel && build/host/test_console      # 2,548 checks, -Werror
build/host/emit_poller_samples | python tests/check_samples.py   # what the ports make is accepted by ARMOR-COMMON
tools/build_node.sh generic                       # the firmware image for the N16R8 board in the ESP-IDF container: dist/generic-s3-wifi.bin
tools/build_node.sh generic s3-eth               # the same firmware for the Waveshare ESP32-S3-ETH (Ethernet): dist/generic-s3-eth.bin
node tools/panel_mock.mjs --user admin:adminpass123   # the panel without a board
```

[ファームウェアガイド](docs/NODE_FIRMWARE.md)（ボード、ポート、初回起動、ベンチ確認リスト）と[配線メモ](docs/NODE_HARDWARE.md)を参照。

## 🔗 関連プロジェクト

**A.R.M.O.R.**（Autonomous Radar & Multimodal Observation Range）は、独立したリポジトリで構成される周辺警備システムです。それぞれに独自のバージョン、テスト、README があります。ファミリーは次のとおりです：

* **[ARMOR-COMMON](https://github.com/JuanenRac/ARMOR-COMMON)** - メッセージ契約、検証器、適合性ベクトル、生成された型
* **[ARMOR-RADAR](https://github.com/JuanenRac/ARMOR-RADAR)** - ESP32-S3 用フィールドノードのファームウェア。レーダー 3 基と独自の Web パネル付き
* **ARMOR-SOLAR** (このリポジトリ) - 太陽光インバーターとバッテリーのプロトコル、およびゲートウェイノードのメッセージ
* **[ARMOR-ELECTRICAL](https://github.com/JuanenRac/ARMOR-ELECTRICAL)** - 電気ノード：電力量計、電力網の計測メッセージ、開閉のルール
* **[ARMOR-HMI](https://github.com/JuanenRac/ARMOR-HMI)** - タッチパネル：壁面ディスプレイでのシステム状態表示、警戒・確認操作、音声アシスタントの拠点
* **[ARMOR-NETWORK](https://github.com/JuanenRac/ARMOR-NETWORK)** - ローカルネットワーク：機器、インターネット、そして変化
* **[ARMOR-SERVER](https://github.com/JuanenRac/ARMOR-SERVER)** - 中央コーディネーター：テレメトリ、アラーム、デバイス、太陽光の測定値、カメラ
* **[ARMOR-STUDIO](https://github.com/JuanenRac/ARMOR-STUDIO)** - Web コンソール：カメラ、レーダー、アラーム、太陽光発電、2D/3D サイト設計
* **[ARMOR-ANDROID-CONTROL](https://github.com/JuanenRac/ARMOR-ANDROID-CONTROL)** - リアルタイム 2D/3D レーダー付きの Android オペレータークライアント
* **[ARMOR-SERVER-AI](https://github.com/JuanenRac/ARMOR-SERVER-AI)** - 判断を説明し、決して動作しない視覚推論ポリシー
* **[ARMOR-VOICE-AI](https://github.com/JuanenRac/ARMOR-VOICE-AI)** - 偽造できない確認を備えたオフライン音声インテント
* **[ARMOR-HARDWARE](https://github.com/JuanenRac/ARMOR-HARDWARE)** - 筐体、電子部品、ベンチ受け入れマトリクス
* **[ARMOR-DEVOPS](https://github.com/JuanenRac/ARMOR-DEVOPS)** - デプロイ、CM5 テストベンチ、バックアップ、TLS
* **[ARMOR-SIMULATOR](https://github.com/JuanenRac/ARMOR-SIMULATOR)** - 再現可能な故障を備えたオフラインのテレメトリシミュレーター
* **[ARMOR-UPDATER](https://github.com/JuanenRac/ARMOR-UPDATER)** - エコシステム自身のリポジトリを検出し、インストールし、更新する
* **[ARMOR-DOCS](https://github.com/JuanenRac/ARMOR-DOCS)** - アーキテクチャ、セキュリティ基準、機能マトリクス

## 📚 ドキュメントとコミュニティ

詳しくは：

* [機能マトリクス：実証済みのものとそうでないもの](https://github.com/JuanenRac/ARMOR-DOCS/blob/main/docs/CAPABILITY_MATRIX.md)
* [プロジェクト一覧：バージョンとリポジトリ間の依存関係](https://github.com/JuanenRac/ARMOR-DOCS/blob/main/docs/PROJECT_CATALOG.md)
* [このリポジトリの変更履歴](CHANGELOG.md)
* [ライセンス（GPL-3.0-or-later）](LICENSE)
* 質問・提案・報告：electrohobby3d@gmail.com

## 👤 作者

**JuanenRac (Electro Hobby 3D)** · electrohobby3d@gmail.com

## 📜 ライセンス

GPL-3.0-or-later - [LICENSE](LICENSE) を参照。
