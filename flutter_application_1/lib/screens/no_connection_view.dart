import 'package:flutter/material.dart';

/// Shown in place of the camera + dashboard while no device is connected.
///
/// The primary action searches the network automatically (remembered host →
/// `bike-assist.local` → setup hotspot → subnet sweep); typing an IP by hand
/// and provisioning the device's WiFi are the fallbacks.
class NoConnectionView extends StatelessWidget {
  const NoConnectionView({
    super.key,
    required this.onAutoConnect,
    required this.onManualConnect,
    required this.onSetup,
    this.searching = false,
    this.progress,
    this.errorMessage,
  });

  final VoidCallback onAutoConnect;
  final VoidCallback onManualConnect;
  final VoidCallback onSetup;

  /// True while an automatic search is running.
  final bool searching;

  /// Short status line from the search ("嘗試 bike-assist.local" ...).
  final String? progress;

  /// Set when the last attempt failed.
  final String? errorMessage;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final muted = theme.colorScheme.onSurfaceVariant;
    final isError = errorMessage != null && !searching;

    return Center(
      child: SingleChildScrollView(
        padding: const EdgeInsets.all(32),
        child: ConstrainedBox(
          constraints: const BoxConstraints(maxWidth: 360),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              Icon(
                isError ? Icons.wifi_off_rounded : Icons.pedal_bike_rounded,
                size: 56,
                color: isError ? theme.colorScheme.error : muted,
              ),
              const SizedBox(height: 20),
              Text(
                searching ? '搜尋裝置中' : (isError ? '找不到裝置' : '尚未連接裝置'),
                style: theme.textTheme.titleLarge
                    ?.copyWith(fontWeight: FontWeight.w800),
                textAlign: TextAlign.center,
              ),
              const SizedBox(height: 8),
              Text(
                searching
                    ? (progress ?? '')
                    : isError
                        ? errorMessage!
                        : '連上裝置後即可看到即時鏡頭與儀表板,並自動記錄這趟騎乘。',
                style: theme.textTheme.bodyMedium?.copyWith(color: muted),
                textAlign: TextAlign.center,
              ),
              const SizedBox(height: 28),
              FilledButton.icon(
                onPressed: searching ? null : onAutoConnect,
                icon: searching
                    ? const SizedBox(
                        width: 16,
                        height: 16,
                        child: CircularProgressIndicator(strokeWidth: 2),
                      )
                    : const Icon(Icons.search_rounded),
                label: Text(searching ? '搜尋中…' : '自動搜尋裝置'),
                style: FilledButton.styleFrom(
                  padding: const EdgeInsets.symmetric(vertical: 14),
                ),
              ),
              const SizedBox(height: 10),
              OutlinedButton(
                onPressed: onManualConnect,
                style: OutlinedButton.styleFrom(
                  padding: const EdgeInsets.symmetric(vertical: 14),
                ),
                child: const Text('手動輸入 IP'),
              ),
              const SizedBox(height: 4),
              TextButton(
                onPressed: onSetup,
                child: const Text('裝置尚未連上 WiFi?設定裝置連線'),
              ),
              const SizedBox(height: 16),
              Text(
                '手機要和裝置在同一個 WiFi,或讓裝置連上手機熱點。',
                style: theme.textTheme.bodySmall?.copyWith(color: muted),
                textAlign: TextAlign.center,
              ),
            ],
          ),
        ),
      ),
    );
  }
}
