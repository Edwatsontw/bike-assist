# Tailscale 設定（選用）

> 目的：展場當天，你**不在現場或用自己的電腦**時，也能連到**組員筆電上的伺服器**看畫面、幫忙除錯；
> 也讓組員的手機用 4G 時仍能開系統端網頁。
> **不需要 Tailscale 也能展示**——展場現場一律走組員筆電的熱點即可（見 [展場操作SOP.md](展場操作SOP.md)）。

## 它能做 / 不能做什麼

| | 說明 |
|---|------|
| ✅ 能 | 把你的電腦、組員筆電、手機連成一個私人網路，不管各自在哪個 WiFi 或 4G，都能用固定的 `100.x.y.z` 位址互連 |
| ✅ 能 | 你在家打開 `http://<組員筆電的Tailscale名稱>:8000` 看系統端網頁 |
| ❌ 不能 | **ESP32 車機裝不了 Tailscale**。車機仍然只透過組員筆電的熱點把資料送到 `192.168.137.1:8000` |
| ❌ 不能 | 讓沒裝 Tailscale 的路人/評審手機連進來 |

## 一、建立帳號與網路（你做一次）

1. 到 <https://tailscale.com>，按「Get Started」，用 Google 或 GitHub 帳號登入（選 Personal）。
2. 這會建立你們小組的私人網路（tailnet）。免費的 Personal 方案目前最多 6 位使用者。

## 二、每台裝置安裝

到 <https://tailscale.com/download> 下載對應版本：

| 裝置 | 步驟 |
|------|------|
| 組員筆電（伺服器） | 安裝 Windows 版 → 登入**同一個 tailnet**（見下方「組員怎麼加入」） → 右下角圖示顯示已連線 |
| 你的電腦 | 安裝 Windows 版 → 用你的帳號登入 |
| 手機（選用） | App Store / Google Play 搜尋 Tailscale → 登入 → 打開開關 |

**組員怎麼加入**（二選一）：
- **邀請成使用者**：Tailscale 管理頁 → Users → Invite users，輸入組員 email。組員用自己的帳號登入後就在同一個網路裡。
- **只分享組員筆電一台**：管理頁 → Machines → 組員筆電右邊「…」→ Share，把連結傳給你。被分享的機器只能被連、不能主動連別人，比較安全。

## 三、連到伺服器

1. 打開 <https://login.tailscale.com/admin/machines>，找到組員筆電，記下它的**名稱**和 **100.x.y.z 位址**。
2. 建議把組員筆電改名成 `safeway-server`（Machines → 「…」→ Edit machine name）。
3. 在你的電腦或手機（都開著 Tailscale）打開：
   - `http://safeway-server:8000`（MagicDNS 名稱，預設已開啟）
   - 或 `http://100.x.y.z:8000`

> 伺服器本來就聽 `0.0.0.0:8000`，`start_server.bat` 開的防火牆規則也涵蓋所有網路，**不需要改程式**。

## 四、APP 的緊急回報網址（選用）

手機若裝了 Tailscale 並保持開啟，緊急回報網址可以改填：

```
http://safeway-server:8000/api/fallen
```

這樣手機就算離開展場熱點、用 4G，收到倒車廣播時仍能回報到伺服器。
**展場內建議維持** `http://192.168.137.1:8000/api/fallen`（少一層依賴）。

## 五、安全注意

- **不要把 auth key（`tskey-…`）或任何登入連結放進 GitHub**。這個 repo 是公開的。`.gitignore` 已排除 `*.tskey` 與 `.env`。
- 展覽結束後，可在管理頁把組員筆電移除（Machines → Remove），或撤銷分享。
- 系統端網頁沒有登入功能：tailnet 裡的任何裝置都能開，包括「標記已扶正」按鈕。只邀請信任的人。

---

資料來源：[Tailscale 安裝說明](https://tailscale.com/kb/1017/install)、[分享裝置](https://tailscale.com/kb/1084/sharing)、[方案更新說明](https://tailscale.com/blog/pricing-v4)（查詢日期 2026-10-02，方案內容可能會變動）
