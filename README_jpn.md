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

### 太陽光インバーターとバッテリーの監視：シリアルプロトコルとゲートウェイノードのメッセージ

<p align="center">
  <img src="https://img.shields.io/badge/License-GPL%203.0-blue.svg" alt="GPL 3.0">
  <img src="https://img.shields.io/badge/Language-C%2B%2B17-00599c.svg" alt="Language">
  <img src="https://img.shields.io/badge/Devices-Voltronic%20%C2%B7%20Pylontech-ffb020.svg" alt="Devices">
  <img src="https://img.shields.io/badge/Maturity-scaffolding-FFB020.svg" alt="Maturity">
</p>

---

**正直さのチェック - 今日動いているもの:** **成熟度：scaffolding。** プロトコルライブラリ（100 件のチェック）は、手書きの典型的な応答から、Voltronic / MPP Solar インバーターと Pylontech コンソールをセルごとに、容量も含めてデコードします。出力するメッセージは ARMOR-COMMON が検証し、ARMOR-SERVER が読み取り、Studio の「インバーター」と「バッテリー」メニューに表示されます。すべて生成した測定値によるものです。**インバーターもバッテリーも接続されたことはなく**、ノードのファームウェアもまだ無く、ANT-BMS はプロトコル文書がプロジェクトに無いためデコードしていません。

---

## 🎯 概要

* **Voltronic / MPP Solar インバーター**（Axpert、PIP、InfiniSolar とその互換機）、RS232 2400 ボー：フレームとその CRC、および読み取り値 `QPIGS`（系統、出力、バッテリー、PV、状態ビット）、`QMOD`（モード）、`QPIWS`（名前付きの警告と故障）、`QPIRI`（定格）。組み立てられるのは読み取りコマンドだけです。設定は家への給電方法を変えてしまうからです。
* **Pylontech バッテリー**（US2000、US3000、US5000）：コンソールの `pwr` 表（各モジュールの電圧、電流、温度、充電状態）、`bat <n>`（各セルの電圧と温度）、`info <n>`（型式、残容量と満容量、サイクル数）を、総容量と総エネルギーを持つ 1 つのスタックとして要約し、2 つのチェックを持つ RS485 フレームも扱います。書式は公開されているコンソールの記憶から書いたもので、ファームウェアによって異なる可能性があります。
* **ゲートウェイノードのメッセージ**（`armor/solar/<ノード>/<デバイス>/state`、インバーターまたはバッテリースタックごとに 1 つ）。ARMOR-COMMON でスキーマと適合性ベクトルとともに定義され、ここでシリアライズし、スクリプトで検査します。
* **残りの設計：** どのボード（Wi-Fi のみか PoE 付きイーサネットか）、RS232 と RS485 を安全に配線する方法（レベル変換と絶縁）、そして Studio メニューを作った順序。
* **まだ：** ANT-BMS、ノードのファームウェア、Android のタブ。実機には何も接続されていません。

## 📂 リポジトリの構成

```text
ARMOR-SOLAR/
├── core/    voltronic.hpp, pylontech.hpp, solar_json.hpp, json.hpp
├── tests/   test_solar.cpp, emit_samples.cpp, check_samples.py
└── docs/    PROTOCOLS.md, NODE_HARDWARE.md, SOLAR_MESSAGES.md, STUDIO_MENUS.md
```

## 🛠️ 開発環境

```bash
cmake -S tests -B build/host && cmake --build build/host
build/host/test_solar                                # 100 checks, -Werror
build/host/emit_samples | python tests/check_samples.py   # the messages have the fields of contract version 0
```

## 🔗 関連プロジェクト

**A.R.M.O.R.**（Autonomous Radar & Multimodal Observation Range）は、独立したリポジトリで構成される周辺警備システムです。それぞれに独自のバージョン、テスト、README があります。ファミリーは次のとおりです：

* **[ARMOR-COMMON](../ARMOR-COMMON)** - メッセージ契約、検証器、適合性ベクトル、生成された型
* **[ARMOR-RADAR](../ARMOR-RADAR)** - ESP32-S3 用フィールドノードのファームウェア。レーダー 3 基と独自の Web パネル付き
* **ARMOR-SOLAR** (このリポジトリ) - 太陽光インバーターとバッテリーのプロトコル、およびゲートウェイノードのメッセージ
* **[ARMOR-SERVER](../ARMOR-SERVER)** - 中央コーディネーター：テレメトリ、アラーム、デバイス、太陽光の測定値、カメラ
* **[ARMOR-STUDIO](../ARMOR-STUDIO)** - Web コンソール：カメラ、レーダー、アラーム、太陽光発電、2D/3D サイト設計
* **[ARMOR-ANDROID-CONTROL](../ARMOR-ANDROID-CONTROL)** - リアルタイム 2D/3D レーダー付きの Android オペレータークライアント
* **[ARMOR-SERVER-AI](../ARMOR-SERVER-AI)** - 判断を説明し、決して動作しない視覚推論ポリシー
* **[ARMOR-VOICE-AI](../ARMOR-VOICE-AI)** - 偽造できない確認を備えたオフライン音声インテント
* **[ARMOR-HARDWARE](../ARMOR-HARDWARE)** - 筐体、電子部品、ベンチ受け入れマトリクス
* **[ARMOR-DEVOPS](../ARMOR-DEVOPS)** - デプロイ、CM5 テストベンチ、バックアップ、TLS
* **[ARMOR-SIMULATOR](../ARMOR-SIMULATOR)** - 再現可能な故障を備えたオフラインのテレメトリシミュレーター
* **[ARMOR-DOCS](../ARMOR-DOCS)** - アーキテクチャ、セキュリティ基準、機能マトリクス

## 📚 ドキュメントとコミュニティ

詳しくは：

* [機能マトリクス：実証済みのものとそうでないもの](../ARMOR-DOCS/docs/CAPABILITY_MATRIX.md)
* [プロジェクト一覧：バージョンとリポジトリ間の依存関係](../ARMOR-DOCS/docs/PROJECT_CATALOG.md)
* [このリポジトリの変更履歴](CHANGELOG.md)
* [ライセンス（GPL-3.0-or-later）](LICENSE)
* 質問・提案・報告：electrohobby3d@gmail.com

## 👤 作者

**JuanenRac (Electro Hobby 3D)** · electrohobby3d@gmail.com

## 📜 ライセンス

GPL-3.0-or-later - [LICENSE](LICENSE) を参照。
