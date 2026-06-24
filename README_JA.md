# Traveler Weather Clock 2.0

<p align="center">
  <a href="./README.md">简体中文</a> ·
  <a href="./README_EN.md">English</a> ·
  <a href="./README_JA.md">日本語</a> ·
  <a href="./docs/archive/README_v1.md">旧 README</a>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Version-2.0-2f80ed?style=for-the-badge" alt="Version 2.0">
  <img src="https://img.shields.io/badge/ESP32--S3-Weather%20Clock-00bcd4?style=for-the-badge" alt="ESP32-S3">
  <img src="https://img.shields.io/badge/Mobile-Web%20Admin-46b36f?style=for-the-badge" alt="Mobile Web Admin">
  <img src="https://img.shields.io/badge/License-MIT-f5b642?style=for-the-badge" alt="MIT License">
</p>

<p align="center">
  <img src="./docs/assets/startup-screen.png" alt="Traveler Weather Clock 2.0 startup screen" width="100%">
</p>

<p align="center">
  <strong>旅人と白い小犬が天気の中を進む、ESP32-S3 向けアニメ風トラベル天気時計です。</strong>
</p>

---

## デモ動画

<p align="center">
  <a href="https://pullead.github.io/Traveler-Weather-Clock/demo.html">
    <img src="./docs/assets/demo-cover.jpg" alt="Traveler Weather Clock demo video cover" width="100%">
  </a>
</p>

<p align="center">
  <a href="https://pullead.github.io/Traveler-Weather-Clock/demo.html">▶ Web 動画プレイヤーを開く</a>
  ·
  <a href="https://github.com/pullead/Traveler-Weather-Clock/raw/refs/heads/main/docs/assets/demo.mp4">予備ダウンロード/再生</a>
</p>

---

## 概要

Traveler Weather Clock 2.0 は、ESP32-S3 TFT 開発ボード向けのカスタム天気時計ファームウェアです。単に時刻と天気を表示するだけではなく、リアルタイムの天気・日の出・日の入り・月相・降水確率・祝日情報を、2D 横スクロールの旅アニメーションとして表現します。

昼間は旅人が自転車で進み、白いビション犬が後ろを走ります。夜になると旅人はテントと焚き火のそばで休み、小犬はそばで眠りながら見守ります。初期地点は日本の兵庫県朝来市です。

---

## 2.0 の主な機能

| 分類 | 内容 |
| --- | --- |
| 旅アニメーション | 自転車の走行、土道と背景のスクロール、草むらの変化、昼夜の光 |
| 天気表現 | 晴れ、くもり、曇り、雨、雪、みぞれ、雷雨、台風、強風、霧、もやなど |
| 天文表示 | 日の出・日の入り、太陽高度、月相、月の軌道、月相の傾き |
| ステータスバー | 時刻、日付、天気名とアイコン、気温範囲、体感温度、Wi-Fi、祝日、太陽軌道、降水確率、月相、現在気温 |
| 小犬パートナー | 白いビション犬が昼は走り、夜はテントのそばで休む |
| BOOT プレビュー | 1 回押しでランダムな天気・昼夜・シーン、2 回押しでリアルタイム天気に戻る |
| 複数 Wi-Fi 記憶 | 自宅や会社など、接続済みネットワークを保存して自動接続 |
| スマホ管理画面 | 同じ Wi-Fi 内のスマートフォンから各種設定を変更 |
| 起動画面 | 専用の Traveler Weather Clock 2.0 起動画面 |

---

## スマホ版 Web 管理画面

<p align="center">
  <img src="./docs/assets/mobile-admin-ui.png" alt="Traveler Weather Clock mobile web admin UI" width="100%">
</p>

スマートフォンと ESP32 が同じ Wi-Fi に接続されている場合、次の URL を開きます。

- `http://travel-clock.local`
- または画面・シリアルモニタに表示される IP、例：`http://192.168.0.100`

管理画面では以下を設定できます。

- ホーム：起動画面、現在天気、24 時間降水確率、日の出・日の入り、月相、デバイス状態
- 天気予報：リアルタイム、24 時間、7 日間、太陽軌道、月相
- キャラクター：旅人の服装、小犬の毛色・速度・夜間明度、アニメ速度、シーンプレビュー
- ステータスバー：Wi-Fi、祝日、太陽軌道、降水確率、体感温度、月相などの表示切替
- システム：明るさ、テーマ色、アニメ速度、夜間モード、再起動
- Wi-Fi：保存済みネットワークの追加・削除・全消去

設定は ESP32 の NVS/Preferences に保存され、再起動後も自動的に読み込まれます。

---

## データソース

- 初期地点：日本 兵庫県 朝来市
- タイムゾーン：Asia/Tokyo / JST
- 天気：Open-Meteo
- 日本の祝日：holidays-jp
- 時刻同期：NTP
- 太陽・月の表示：ファームウェア内の天文計算 + ネットワーク時刻

---

## ハードウェア

2.0 ファームウェアは主に以下に合わせて調整されています。

- Adafruit Feather ESP32-S3 TFT
- 240 × 135 ST7789 TFT ディスプレイ
- ESP32-S3 Wi-Fi
- BOOT ボタン

他の ESP32-S3 + TFT ボードへ移植する場合は、画面ドライバ、ピン設定、解像度レイアウトの調整が必要です。

---

## クイックスタート

### ビルド

```bash
arduino-cli compile \
  --fqbn esp32:esp32:adafruit_feather_esp32s3_tft \
  firmware/TravelWeatherClockV2
```

### 書き込み

```bash
arduino-cli upload \
  --fqbn esp32:esp32:adafruit_feather_esp32s3_tft \
  --port /dev/cu.usbmodem1101 \
  firmware/TravelWeatherClockV2
```

シリアルポートは環境に合わせて変更してください。

### 初回 Wi-Fi 設定

初回起動時、または Wi-Fi を消去した後は、画面の案内に従って設定ポータルに接続します。必要に応じて次を開きます。

```text
http://192.168.4.1
```

一度保存した Wi-Fi は記憶されます。自宅・会社などを移動しても、起動時に利用可能な保存済みネットワークへ自動接続します。

---

## BOOT ボタン

| 操作 | 動作 |
| --- | --- |
| 1 回押し | ランダムな天気 + 昼夜 + アニメーションシーンを表示 |
| 2 回押し | 現在のリアルタイム天気と時刻に戻る |
| 約 4 秒長押し | 保存済み Wi-Fi 設定を消去 |

---

## プロジェクト構成

```text
weather-micro-station/
├── README.md
├── README_EN.md
├── README_JA.md
├── docs/
│   ├── assets/
│   │   ├── startup-screen.png
│   │   ├── mobile-admin-ui.png
│   │   ├── demo-cover.jpg
│   │   └── demo.mp4
│   ├── demo.html
│   ├── index.html
│   └── archive/
│       └── README_v1.md
├── firmware/
│   └── TravelWeatherClockV2/
├── include/
├── src/
└── tools/
```

---

## 旧バージョン

旧 README は [docs/archive/README_v1.md](./docs/archive/README_v1.md) に保存されています。

Git の履歴にも旧版の説明は残っています。今後は `v1.x` や `v2.0.0` のような GitHub Release / Tag を作ると、旧版と新版をより明確に管理できます。

---

## 多言語 README について

GitHub README では任意の JavaScript を実行できないため、同じページ内で完全な自動言語切替を行うことはできません。本プロジェクトでは、各 README の上部に安定した言語リンクを用意しています。

- [简体中文](./README.md)
- [English](./README_EN.md)
- [日本語](./README_JA.md)

各言語ページに画像と再生可能なデモ動画リンクを含めています。

---

## Credits

このプロジェクトは sfrechette/weather-micro-station の天気時計アイデアを出発点に、ESP32-S3、中国語 UI、旅テーマのアニメーション、スマホ Web 管理画面、朝来市のリアルタイム天気に合わせて大きく拡張したものです。

天気データと祝日データは Open-Meteo と holidays-jp を利用しています。

---

## License

MIT License. See [LICENSE](./LICENSE).
