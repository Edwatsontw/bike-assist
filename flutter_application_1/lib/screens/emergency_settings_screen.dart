import 'package:flutter/material.dart';

import '../services/emergency_relay_service.dart';
import '../services/emergency_settings.dart';

/// Configures the emergency BLE relay: turn it on/off and set the server URL
/// that a scanned fall alert is forwarded to. Saving arms or disarms the
/// [EmergencyRelayService] to match.
class EmergencySettingsScreen extends StatefulWidget {
  const EmergencySettingsScreen({
    super.key,
    required this.service,
    required this.settings,
  });

  final EmergencyRelayService service;
  final EmergencySettings settings;

  @override
  State<EmergencySettingsScreen> createState() =>
      _EmergencySettingsScreenState();
}

class _EmergencySettingsScreenState extends State<EmergencySettingsScreen> {
  final _urlController = TextEditingController();
  bool _enabled = false;
  bool _loading = true;
  bool _saving = false;

  @override
  void initState() {
    super.initState();
    _load();
  }

  Future<void> _load() async {
    final config = await widget.settings.load();
    if (!mounted) return;
    setState(() {
      _enabled = config.enabled;
      _urlController.text = config.serverUrl;
      _loading = false;
    });
  }

  @override
  void dispose() {
    _urlController.dispose();
    super.dispose();
  }

  Future<void> _save() async {
    final url = _urlController.text.trim();
    if (_enabled) {
      final uri = Uri.tryParse(url);
      if (url.isEmpty || uri == null || !uri.hasScheme || !uri.hasAuthority) {
        _showMessage('請先填入有效的伺服器網址(需含 http:// 或 https://)');
        return;
      }
    }
    setState(() => _saving = true);
    final config = EmergencyConfig(enabled: _enabled, serverUrl: url);
    await widget.settings.save(config);
    if (_enabled) {
      await widget.service.arm(url);
    } else {
      await widget.service.disarm();
    }
    if (!mounted) return;
    setState(() => _saving = false);
    _showMessage('已儲存');
  }

  void _showMessage(String message) {
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(message)));
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('緊急回報設定')),
      body: _loading
          ? const Center(child: CircularProgressIndicator())
          : ListView(
              padding: const EdgeInsets.all(16),
              children: [
                Card(
                  child: Padding(
                    padding: const EdgeInsets.all(12),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Row(
                          children: [
                            const Icon(Icons.emergency_share, size: 20),
                            const SizedBox(width: 8),
                            Expanded(
                              child: Text('緊急座標回報',
                                  style: Theme.of(context).textTheme.titleMedium),
                            ),
                          ],
                        ),
                        const SizedBox(height: 8),
                        const Text(
                          '開啟後,App 會用藍牙掃描附近單車裝置在摔車時發出的緊急'
                          '廣播,收到就把當時的 GPS 座標轉發到你設定的伺服器。'
                          '就算不是你自己的車,只要附近有裝置摔車也會代為回報。',
                          style: TextStyle(fontSize: 13, height: 1.5),
                        ),
                      ],
                    ),
                  ),
                ),
                const SizedBox(height: 8),
                SwitchListTile(
                  title: const Text('啟用緊急回報'),
                  subtitle: const Text('背景常駐掃描藍牙緊急廣播'),
                  value: _enabled,
                  onChanged: _saving ? null : (v) => setState(() => _enabled = v),
                ),
                const SizedBox(height: 8),
                TextField(
                  controller: _urlController,
                  enabled: !_saving,
                  keyboardType: TextInputType.url,
                  decoration: const InputDecoration(
                    labelText: '伺服器網址',
                    hintText: '例如 http://192.168.137.1:8000/api/fallen',
                    helperText: '收到緊急廣播時會 POST JSON 到這個網址',
                    border: OutlineInputBorder(),
                  ),
                ),
                const SizedBox(height: 16),
                FilledButton(
                  onPressed: _saving ? null : _save,
                  child: _saving
                      ? const SizedBox(
                          height: 18,
                          width: 18,
                          child: CircularProgressIndicator(strokeWidth: 2),
                        )
                      : const Text('儲存'),
                ),
                const SizedBox(height: 24),
                _StatusPanel(service: widget.service),
              ],
            ),
    );
  }
}

/// Live status of the relay: scanning/idle/error, plus the last alert relayed.
class _StatusPanel extends StatelessWidget {
  const _StatusPanel({required this.service});

  final EmergencyRelayService service;

  @override
  Widget build(BuildContext context) {
    return ValueListenableBuilder<RelayState>(
      valueListenable: service.state,
      builder: (context, state, _) {
        final (icon, color, label) = switch (state) {
          RelayState.scanning => (Icons.bluetooth_searching, Colors.green, '掃描中'),
          RelayState.error => (Icons.error_outline, Colors.red, '無法掃描'),
          RelayState.idle => (Icons.bluetooth_disabled, Colors.grey, '未啟用'),
        };
        return Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text('狀態', style: Theme.of(context).textTheme.titleSmall),
            const SizedBox(height: 4),
            Row(
              children: [
                Icon(icon, size: 18, color: color),
                const SizedBox(width: 8),
                Text(label),
              ],
            ),
            ValueListenableBuilder<String?>(
              valueListenable: service.errorMessage,
              builder: (context, error, _) => error == null
                  ? const SizedBox.shrink()
                  : Padding(
                      padding: const EdgeInsets.only(top: 4, left: 26),
                      child: Text(error,
                          style: TextStyle(
                              color: Theme.of(context).colorScheme.error,
                              fontSize: 12)),
                    ),
            ),
            const SizedBox(height: 12),
            ValueListenableBuilder<RelayOutcome?>(
              valueListenable: service.lastRelay,
              builder: (context, outcome, _) {
                if (outcome == null) {
                  return const Text('尚未收到任何緊急廣播',
                      style: TextStyle(color: Colors.grey, fontSize: 13));
                }
                final a = outcome.alert;
                return Card(
                  color: outcome.ok
                      ? Colors.green.withValues(alpha: 0.08)
                      : Theme.of(context).colorScheme.errorContainer,
                  child: Padding(
                    padding: const EdgeInsets.all(12),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(
                            '${outcome.ok ? '最後一次已回報' : '最後一次回報失敗'}'
                            '・${a.isEmergency ? '緊急事故(倒地 5 分鐘)' : '一般倒車通知'}',
                            style:
                                Theme.of(context).textTheme.labelLarge),
                        const SizedBox(height: 4),
                        Text(a.hasPosition
                            ? '座標:${a.lat.toStringAsFixed(6)}, '
                                '${a.lon.toStringAsFixed(6)}'
                            : '座標:裝置當時沒有 GPS 定位'),
                        Text('裝置時間:${a.time.toLocal()}'),
                        if (outcome.detail != null) Text('結果:${outcome.detail}'),
                      ],
                    ),
                  ),
                );
              },
            ),
          ],
        );
      },
    );
  }
}
