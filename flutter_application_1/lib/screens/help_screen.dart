import 'package:flutter/material.dart';

/// A static, scrollable how-to page. It explains the flows that aren't obvious
/// from the UI alone — connecting, recording with the screen off, and
/// exporting a ride as a video + coordinate CSV via the system share sheet.
class HelpScreen extends StatelessWidget {
  const HelpScreen({super.key});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('使用說明')),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: const [
          _HelpSection(
            icon: Icons.sensors,
            title: '連接裝置',
            steps: [
              '確認手機和裝置(ESP32)連在同一個 WiFi,或讓裝置連上手機熱點。',
              '打開 App 會自動搜尋裝置;找不到時按「自動搜尋裝置」再試一次,'
                  '或按「手動輸入 IP」。連上過的位址會被記住。',
              '若裝置還沒連上 WiFi,先用「設定裝置連線」把它加入你的 WiFi。',
              '連上後 App 會把手機定位傳給車機使用(需允許定位權限);'
                  '斷線時車機會自動改用自己的 GPS 天線。',
            ],
            note: '連上後會顯示速度、GPS、姿態(Roll/Pitch/G)與裝置狀態。'
                '鏡頭串流「預設關閉」,要看即時影像時打開鏡頭區左上角的「串流」開關;'
                '右上角的「夜間」開關可切換鏡頭的低光增強。',
          ),
          _HelpSection(
            icon: Icons.warning_amber_rounded,
            title: '急煞與碰撞',
            steps: [
              '裝置偵測到急煞或碰撞時,App 會跳出提示,並把事件記進這趟騎乘。',
              '碰撞後警示燈會一直閃,直到解除為止。主畫面最上方會出現紅色'
                  '「碰撞警示已鎖定」,確認人車安全後按「解除警示」。',
            ],
            note: '方向燈由車上的控制桿操作,App 不會控制方向燈。',
          ),
          _HelpSection(
            icon: Icons.fiber_manual_record,
            title: '記錄騎乘',
            steps: [
              '連上裝置就會自動開始記錄這趟騎乘,中斷連線即結束。',
              '記錄期間可以關螢幕、把手機放口袋 — 通知列會顯示「記錄中」,'
                  '資料會持續接收。',
              '也可以到「歷史記錄」頁,用上方的「開始記錄 / 結束記錄」手動控制。',
            ],
          ),
          _HelpSection(
            icon: Icons.route,
            title: '查看與回放',
            steps: [
              '點右上角的地圖圖示進入「歷史記錄」。',
              '點任一筆記錄即可回放:上方是當時的鏡頭畫面,下方是路線地圖,'
                  '會隨播放同步前進。',
              '地圖上的橘色點是急煞、紅色點是碰撞的位置。',
              '向左滑一筆記錄可以刪除它(連同影像)。',
            ],
          ),
          _HelpSection(
            icon: Icons.ios_share,
            title: '匯出記錄(影片 + 座標)',
            steps: [
              '在「歷史記錄」頁,每筆記錄右側都有分享圖示,點它。',
              'App 會把當時錄下的鏡頭畫面整理成一個 MP4 影片檔,'
                  '並把整趟的 GPS 座標整理成一個 CSV 檔。',
              '系統分享面板會打開,選 LINE、Gmail、雲端硬碟,'
                  '或「儲存到裝置 / Files」,兩個檔案會一起分享出去。',
            ],
            note: '這趟若沒有錄到鏡頭畫面,就只會匯出座標 CSV。'
                'CSV 每一列是:時間、緯度、經度、時速(km/h)。',
          ),
          _HelpSection(
            icon: Icons.emergency_share_outlined,
            title: '緊急回報',
            steps: [
              '在主畫面右上角的緊急圖示開啟「啟用緊急回報」,並填入伺服器網址'
                  '(例如 http://伺服器IP:8000/api/fallen)。',
              '車倒地 10 秒,裝置會發出一般倒車通知;倒地 5 分鐘仍未扶正,'
                  '會升級為緊急事故。附近開著 App 的手機收到後會代為回報座標。',
            ],
          ),
        ],
      ),
    );
  }
}

class _HelpSection extends StatelessWidget {
  const _HelpSection({
    required this.icon,
    required this.title,
    required this.steps,
    this.note,
  });

  final IconData icon;
  final String title;
  final List<String> steps;

  /// Optional caveat shown below the steps in a subdued style.
  final String? note;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return Card(
      margin: const EdgeInsets.only(bottom: 16),
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                Icon(icon, color: scheme.primary),
                const SizedBox(width: 12),
                Expanded(
                  child: Text(
                    title,
                    style: Theme.of(context).textTheme.titleMedium,
                  ),
                ),
              ],
            ),
            const SizedBox(height: 12),
            for (var i = 0; i < steps.length; i++)
              Padding(
                padding: const EdgeInsets.only(bottom: 8),
                child: Row(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    _StepNumber(i + 1),
                    const SizedBox(width: 12),
                    Expanded(
                      child: Text(
                        steps[i],
                        style: Theme.of(context).textTheme.bodyMedium,
                      ),
                    ),
                  ],
                ),
              ),
            if (note != null) ...[
              const SizedBox(height: 4),
              Container(
                padding: const EdgeInsets.all(12),
                decoration: BoxDecoration(
                  color: scheme.surfaceContainerHighest,
                  borderRadius: BorderRadius.circular(8),
                ),
                child: Row(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Icon(Icons.info_outline, size: 18, color: scheme.onSurfaceVariant),
                    const SizedBox(width: 8),
                    Expanded(
                      child: Text(
                        note!,
                        style: Theme.of(context).textTheme.bodySmall?.copyWith(
                              color: scheme.onSurfaceVariant,
                            ),
                      ),
                    ),
                  ],
                ),
              ),
            ],
          ],
        ),
      ),
    );
  }
}

class _StepNumber extends StatelessWidget {
  const _StepNumber(this.number);

  final int number;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return Container(
      width: 24,
      height: 24,
      alignment: Alignment.center,
      decoration: BoxDecoration(
        color: scheme.primaryContainer,
        shape: BoxShape.circle,
      ),
      child: Text(
        '$number',
        style: Theme.of(context).textTheme.labelMedium?.copyWith(
              color: scheme.onPrimaryContainer,
              fontWeight: FontWeight.bold,
            ),
      ),
    );
  }
}
