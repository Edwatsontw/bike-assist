import 'dart:async';
import 'dart:ui' show FontFeature;

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../models/bike_data.dart';
import '../services/bike_data_source.dart';
import '../services/camera_source.dart';
import '../services/device_control.dart';
import '../services/device_discovery.dart';
import '../services/device_provisioning.dart';
import '../services/emergency_relay_service.dart';
import '../services/emergency_settings.dart';
import '../services/ride_frame_store.dart';
import '../services/ride_recorder.dart';
import '../services/ride_repository.dart';
import '../theme.dart';
import '../widgets/mjpeg_view.dart';
import 'device_wifi_setup_screen.dart';
import 'emergency_settings_screen.dart';
import 'no_connection_view.dart';
import 'ride_list_screen.dart';

/// Port the firmware serves the MJPEG `/stream` from — a second HTTP server,
/// separate from the port-80 control/telemetry server, so the blocking stream
/// loop can't starve `/api/status`. Must match the firmware's stream server.
const int _kCameraPort = 81;

/// Main screen: finds/connects the device, then shows the live view — the
/// collision alert (when latched), camera, speed, attitude, recording state and
/// device health — in one scrolling column.
///
/// Turn signals are intentionally absent: they are driven by the physical
/// handlebar switch on the bike.
class HomeScreen extends StatefulWidget {
  const HomeScreen({
    super.key,
    required this.dataSource,
    required this.cameraSource,
    required this.repository,
    required this.frameStore,
    required this.recorder,
    required this.emergencyRelay,
    required this.emergencySettings,
    this.autoDiscover = true,
  });

  final BikeDataSource dataSource;
  final CameraSource cameraSource;
  final RideRepository repository;
  final RideFrameStore frameStore;
  final RideRecorder recorder;
  final EmergencyRelayService emergencyRelay;
  final EmergencySettings emergencySettings;

  /// Search for the device once on launch.
  final bool autoDiscover;

  @override
  State<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen> {
  final _displayController = StreamController<Uint8List>.broadcast();
  final _ipController = TextEditingController();
  StreamSubscription<Uint8List>? _cameraSubscription;
  StreamSubscription<BikeData>? _eventSubscription;

  /// Whether the camera MJPEG stream is turned on. Off by default so connecting
  /// shows the dashboard without pulling the (bandwidth-heavy) video until the
  /// user asks for it.
  bool _streamingEnabled = false;

  /// Base URI of the connected device (null when disconnected). Kept so the
  /// stream switch and device commands don't need the address re-entered.
  Uri? _deviceBase;

  /// Display-only: pausing the live view does not pause ride recording.
  bool _paused = false;

  // ── Discovery ──────────────────────────────────────────────────────────
  DeviceDiscovery? _discovery;
  bool _searching = false;
  String? _searchProgress;
  String? _searchError;

  /// The host was saved as "last used" once telemetry proved it's the device.
  bool _hostSaved = false;

  // ── Live state derived from telemetry ──────────────────────────────────
  String? _lastAccelEvent;
  bool _commandBusy = false;

  @override
  void initState() {
    super.initState();
    _cameraSubscription = widget.cameraSource.frames.listen((frame) {
      if (!_paused) _displayController.add(frame);
    });
    _eventSubscription = widget.dataSource.stream.listen(_onTelemetry);
    widget.dataSource.mode.addListener(_onTelemetryMode);
    DeviceDiscovery.loadLastHost().then((host) {
      if (host != null && mounted && _ipController.text.isEmpty) {
        _ipController.text = host;
      }
    });
    if (widget.autoDiscover) {
      WidgetsBinding.instance.addPostFrameCallback((_) => _autoConnect());
    }
  }

  @override
  void dispose() {
    _discovery?.cancel();
    _discovery?.dispose();
    widget.dataSource.mode.removeListener(_onTelemetryMode);
    _eventSubscription?.cancel();
    _cameraSubscription?.cancel();
    _displayController.close();
    _ipController.dispose();
    super.dispose();
  }

  // ── Connection ─────────────────────────────────────────────────────────

  Future<void> _autoConnect() async {
    if (_searching) return;
    final discovery = _discovery ??= DeviceDiscovery();
    setState(() {
      _searching = true;
      _searchProgress = '準備搜尋…';
      _searchError = null;
    });
    final last = await DeviceDiscovery.loadLastHost();
    final host = await discovery.find(
      preferred: last,
      onProgress: (message) {
        if (mounted) setState(() => _searchProgress = message);
      },
    );
    if (!mounted) return;
    setState(() {
      _searching = false;
      _searchProgress = null;
      _searchError = host == null
          ? '請確認裝置已開機,且和手機在同一個 WiFi(或連上手機熱點)。也可以手動輸入 IP。'
          : null;
    });
    if (host != null) _connectToDevice(host);
  }

  /// Connects to a device. Accepts a bare IP/host, `host:port`, or a full URL.
  ///
  /// Telemetry (`/api/status`) always connects. The camera MJPEG stream only
  /// connects when streaming is switched on — it lives on port [_kCameraPort]
  /// so its blocking loop can't starve `/api/status` on the control port.
  void _connectToDevice(String input) {
    final trimmed = input.trim();
    if (trimmed.isEmpty) return;
    final normalized = trimmed.startsWith('http') ? trimmed : 'http://$trimmed';
    final base = Uri.parse(normalized).replace(path: '', query: '', fragment: '');
    _deviceBase = base;
    _hostSaved = false;
    _ipController.text = base.hasPort ? '${base.host}:${base.port}' : base.host;
    widget.dataSource.connect(base);
    if (_streamingEnabled) {
      widget.cameraSource.connect(_cameraUri(base));
    }
  }

  void _onTelemetryMode() {
    final base = _deviceBase;
    if (widget.dataSource.mode.value == TelemetryMode.connected &&
        base != null &&
        !_hostSaved) {
      _hostSaved = true;
      DeviceDiscovery.saveLastHost(
        base.hasPort ? '${base.host}:${base.port}' : base.host,
      );
    }
  }

  Uri _cameraUri(Uri base) => base.replace(port: _kCameraPort, path: '/stream');

  /// Drops both connections, returning to the no-connection view. Recording
  /// stops via the connection listener in main.
  void _disconnect() {
    _deviceBase = null;
    _lastAccelEvent = null;
    widget.cameraSource.disconnect();
    widget.dataSource.disconnect();
  }

  void _setStreamingEnabled(bool enabled) {
    if (enabled == _streamingEnabled) return;
    setState(() => _streamingEnabled = enabled);
    final base = _deviceBase;
    if (base == null) return; // takes effect on next connect
    if (enabled) {
      widget.cameraSource.connect(_cameraUri(base));
    } else {
      _paused = false;
      widget.cameraSource.disconnect();
    }
  }

  Future<void> _showManualConnectDialog() async {
    final isConnected =
        widget.dataSource.mode.value != TelemetryMode.disconnected ||
            widget.cameraSource.mode.value != CameraMode.disconnected;
    final result = await showDialog<String>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('連接裝置'),
        content: SingleChildScrollView(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              TextField(
                controller: _ipController,
                autofocus: true,
                decoration: const InputDecoration(
                  labelText: '裝置 IP 位址',
                  hintText: '例如 192.168.137.50 或 bike-assist.local',
                ),
                keyboardType: TextInputType.url,
              ),
              const SizedBox(height: 16),
              const _SetupHotspotInfo(),
              const SizedBox(height: 8),
              TextButton.icon(
                icon: const Icon(Icons.settings_ethernet, size: 18),
                label: const Text('裝置尚未連上 WiFi?設定裝置連線'),
                onPressed: () => Navigator.of(context).pop('setup'),
              ),
            ],
          ),
        ),
        actions: [
          if (isConnected)
            TextButton(
              onPressed: () => Navigator.of(context).pop('disconnect'),
              child: const Text('中斷連線'),
            ),
          FilledButton(
            onPressed: () => Navigator.of(context).pop(_ipController.text),
            child: const Text('連線'),
          ),
        ],
      ),
    );
    if (result == null) return;
    if (result == 'disconnect') {
      _disconnect();
    } else if (result == 'setup') {
      await _openDeviceSetup();
    } else {
      _discovery?.cancel();
      _connectToDevice(result);
    }
  }

  Future<void> _openDeviceSetup() async {
    final ip = await Navigator.of(context).push<String>(
      MaterialPageRoute(builder: (_) => const DeviceWifiSetupScreen()),
    );
    if (ip != null && ip.isNotEmpty) {
      _connectToDevice(ip);
    }
  }

  // ── Telemetry-driven alerts ────────────────────────────────────────────

  void _onTelemetry(BikeData data) {
    final event = data.accelEvent;
    final previous = _lastAccelEvent;
    _lastAccelEvent = event;
    if (!mounted || event == previous) return;
    if (data.isBrake) {
      _snack('偵測到急煞', color: SwColors.amber);
    } else if (data.isCollision) {
      _snack('偵測到碰撞,警示燈已鎖定', color: SwColors.red);
    }
  }

  void _snack(String message, {Color? color}) {
    final messenger = ScaffoldMessenger.maybeOf(context);
    if (messenger == null) return;
    messenger
      ..hideCurrentSnackBar()
      ..showSnackBar(SnackBar(
        content: Text(message),
        backgroundColor: color,
        duration: const Duration(seconds: 3),
      ));
  }

  // ── Device commands ────────────────────────────────────────────────────

  Future<void> _runCommand(
    Future<void> Function(DeviceControl control) command, {
    required String failMessage,
  }) async {
    final base = _deviceBase;
    if (base == null || _commandBusy) return;
    setState(() => _commandBusy = true);
    try {
      await command(DeviceControl(base));
    } catch (error) {
      _snack('$failMessage:$error', color: SwColors.red);
    } finally {
      if (mounted) setState(() => _commandBusy = false);
    }
  }

  Future<void> _confirmClearHazard() async {
    final ok = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('解除碰撞警示?'),
        content: const Text('請先確認人車都安全。解除後警示燈會關閉,方向燈回到控制桿操作。'),
        actions: [
          TextButton(
            onPressed: () => Navigator.of(context).pop(false),
            child: const Text('取消'),
          ),
          FilledButton(
            onPressed: () => Navigator.of(context).pop(true),
            child: const Text('解除警示'),
          ),
        ],
      ),
    );
    if (ok != true) return;
    await _runCommand((c) => c.clearCollisionHazard(), failMessage: '解除失敗');
  }

  Future<void> _toggleNight(bool current) => _runCommand(
        (c) => c.setNightMode(!current),
        failMessage: '夜間模式切換失敗',
      );

  // ── Navigation ─────────────────────────────────────────────────────────

  void _openHistory() {
    Navigator.of(context).push(
      MaterialPageRoute(
        builder: (_) => RideListScreen(
          repository: widget.repository,
          frameStore: widget.frameStore,
          recorder: widget.recorder,
        ),
      ),
    );
  }

  void _openEmergency() {
    Navigator.of(context).push(
      MaterialPageRoute(
        builder: (_) => EmergencySettingsScreen(
          service: widget.emergencyRelay,
          settings: widget.emergencySettings,
        ),
      ),
    );
  }

  // ── Build ──────────────────────────────────────────────────────────────

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('SafeWay',
            style: TextStyle(fontWeight: FontWeight.w800, letterSpacing: -0.3)),
        actions: [
          IconButton(
            tooltip: '連接裝置',
            icon: const Icon(Icons.wifi_tethering),
            onPressed: _showManualConnectDialog,
          ),
          IconButton(
            tooltip: '緊急回報設定',
            icon: const Icon(Icons.emergency_share_outlined),
            onPressed: _openEmergency,
          ),
          IconButton(
            tooltip: '歷史記錄',
            icon: const Icon(Icons.map_outlined),
            onPressed: _openHistory,
          ),
        ],
      ),
      body: ListenableBuilder(
        listenable: Listenable.merge(
          [widget.cameraSource.mode, widget.dataSource.mode],
        ),
        builder: (context, _) {
          final cameraMode = widget.cameraSource.mode.value;
          final telemetryMode = widget.dataSource.mode.value;
          final live = telemetryMode == TelemetryMode.connecting ||
              telemetryMode == TelemetryMode.connected ||
              cameraMode == CameraMode.connecting ||
              cameraMode == CameraMode.connected;
          if (!live) {
            final connectionError = telemetryMode == TelemetryMode.error
                ? '連線中斷:${widget.dataSource.errorMessage.value ?? ''}'
                : cameraMode == CameraMode.error
                    ? '鏡頭連線中斷:${widget.cameraSource.errorMessage.value ?? ''}'
                    : null;
            return NoConnectionView(
              onAutoConnect: _autoConnect,
              onManualConnect: _showManualConnectDialog,
              onSetup: _openDeviceSetup,
              searching: _searching,
              progress: _searchProgress,
              errorMessage: _searchError ?? connectionError,
            );
          }
          return _buildLive(context, telemetryMode);
        },
      ),
    );
  }

  Widget _buildLive(BuildContext context, TelemetryMode telemetryMode) {
    return StreamBuilder<BikeData>(
      stream: widget.dataSource.stream,
      builder: (context, snapshot) {
        final data = snapshot.data;
        return ListView(
          padding: const EdgeInsets.fromLTRB(16, 4, 16, 24),
          children: [
            _ConnectionLine(
              host: _deviceBase?.host ?? '',
              mode: telemetryMode,
            ),
            const SizedBox(height: 12),
            if (data?.hazardLocked == true) ...[
              _HazardBanner(busy: _commandBusy, onClear: _confirmClearHazard),
              const SizedBox(height: 12),
            ],
            _buildCamera(data),
            const SizedBox(height: 12),
            if (data == null)
              const Panel(
                child: SizedBox(
                  height: 120,
                  child: Center(child: CircularProgressIndicator()),
                ),
              )
            else ...[
              Row(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Expanded(child: _SpeedPanel(data: data)),
                  const SizedBox(width: 12),
                  Expanded(child: _RecordingPanel(recorder: widget.recorder, onOpen: _openHistory)),
                ],
              ),
              const SizedBox(height: 12),
              _AttitudePanel(data: data),
              const SizedBox(height: 12),
              _SystemPanel(data: data),
            ],
          ],
        );
      },
    );
  }

  Widget _buildCamera(BikeData? data) {
    final night = data?.nightMode ?? false;
    return ClipRRect(
      borderRadius: BorderRadius.circular(14),
      child: AspectRatio(
        aspectRatio: 4 / 3,
        child: ColoredBox(
          color: Colors.black,
          child: Stack(
            fit: StackFit.expand,
            children: [
              if (_streamingEnabled)
                MjpegView(frames: _displayController.stream)
              else
                const _StreamOffView(),
              Positioned(
                top: 8,
                left: 8,
                child: _PillSwitch(
                  icon: Icons.videocam_rounded,
                  label: '串流',
                  value: _streamingEnabled,
                  onChanged: _setStreamingEnabled,
                ),
              ),
              Positioned(
                top: 8,
                right: 8,
                child: _PillSwitch(
                  icon: Icons.nightlight_round,
                  label: '夜間',
                  value: night,
                  onChanged: data == null || _commandBusy
                      ? null
                      : (_) => _toggleNight(night),
                ),
              ),
              if (_streamingEnabled)
                Positioned(
                  left: 8,
                  bottom: 8,
                  child: ValueListenableBuilder<CameraMode>(
                    valueListenable: widget.cameraSource.mode,
                    builder: (context, mode, _) => _CameraStatusPill(
                      mode: mode,
                      paused: _paused,
                    ),
                  ),
                ),
              if (_streamingEnabled)
                Positioned(
                  right: 8,
                  bottom: 8,
                  child: IconButton.filled(
                    style: IconButton.styleFrom(
                      backgroundColor: Colors.black54,
                      foregroundColor: Colors.white,
                    ),
                    tooltip: _paused ? '繼續' : '暫停畫面',
                    onPressed: () => setState(() => _paused = !_paused),
                    icon: Icon(_paused ? Icons.play_arrow_rounded : Icons.pause_rounded),
                  ),
                ),
            ],
          ),
        ),
      ),
    );
  }
}

// ── Live-view pieces ─────────────────────────────────────────────────────

class _ConnectionLine extends StatelessWidget {
  const _ConnectionLine({required this.host, required this.mode});

  final String host;
  final TelemetryMode mode;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final (color, label) = switch (mode) {
      TelemetryMode.connected => (SwColors.green, '已連線'),
      TelemetryMode.connecting => (SwColors.amber, '連線中…'),
      TelemetryMode.error => (SwColors.red, '連線不穩,重試中'),
      TelemetryMode.disconnected => (theme.colorScheme.outline, '未連線'),
    };
    return Row(
      children: [
        Container(
          width: 8,
          height: 8,
          decoration: BoxDecoration(color: color, shape: BoxShape.circle),
        ),
        const SizedBox(width: 8),
        Text(label, style: theme.textTheme.bodySmall?.copyWith(fontWeight: FontWeight.w700)),
        const SizedBox(width: 8),
        Expanded(
          child: Text(
            host,
            overflow: TextOverflow.ellipsis,
            style: theme.textTheme.bodySmall?.copyWith(color: theme.colorScheme.onSurfaceVariant),
          ),
        ),
      ],
    );
  }
}

class _HazardBanner extends StatelessWidget {
  const _HazardBanner({required this.busy, required this.onClear});

  final bool busy;
  final VoidCallback onClear;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.fromLTRB(16, 14, 12, 14),
      decoration: BoxDecoration(
        color: SwColors.red,
        borderRadius: BorderRadius.circular(14),
      ),
      child: Row(
        children: [
          const Icon(Icons.warning_amber_rounded, color: Colors.white),
          const SizedBox(width: 12),
          const Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text('碰撞警示已鎖定',
                    style: TextStyle(color: Colors.white, fontWeight: FontWeight.w800)),
                SizedBox(height: 2),
                Text('確認人車安全後再解除',
                    style: TextStyle(color: Colors.white70, fontSize: 12)),
              ],
            ),
          ),
          FilledButton(
            style: FilledButton.styleFrom(
              backgroundColor: Colors.white,
              foregroundColor: SwColors.red,
            ),
            onPressed: busy ? null : onClear,
            child: const Text('解除警示'),
          ),
        ],
      ),
    );
  }
}

class _SpeedPanel extends StatelessWidget {
  const _SpeedPanel({required this.data});

  final BikeData data;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final fix = data.gpsFix == true;
    final chars = data.gpsChars ?? 0;
    final (gpsColor, gpsLabel) = fix
        ? (SwColors.green, 'GPS 已定位')
        : chars > 0
            ? (SwColors.amber, 'GPS 搜尋中')
            : (SwColors.red, 'GPS 無訊號');
    return Panel(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const SectionLabel('速度'),
          Row(
            crossAxisAlignment: CrossAxisAlignment.baseline,
            textBaseline: TextBaseline.alphabetic,
            children: [
              Text(
                fix ? data.speedKmh.toStringAsFixed(1) : '–',
                style: theme.textTheme.displaySmall?.copyWith(
                  fontWeight: FontWeight.w800,
                  fontFeatures: const [FontFeature.tabularFigures()],
                ),
              ),
              const SizedBox(width: 4),
              Text('km/h', style: theme.textTheme.bodySmall?.copyWith(color: theme.colorScheme.onSurfaceVariant)),
            ],
          ),
          const SizedBox(height: 8),
          Row(
            children: [
              Icon(Icons.circle, size: 8, color: gpsColor),
              const SizedBox(width: 6),
              Flexible(
                child: Text(gpsLabel,
                    overflow: TextOverflow.ellipsis,
                    style: theme.textTheme.bodySmall),
              ),
            ],
          ),
          if (fix) ...[
            const SizedBox(height: 2),
            Text(
              '${data.lat.toStringAsFixed(5)}, ${data.lng.toStringAsFixed(5)}',
              style: theme.textTheme.bodySmall?.copyWith(color: theme.colorScheme.onSurfaceVariant),
            ),
          ],
        ],
      ),
    );
  }
}

class _RecordingPanel extends StatelessWidget {
  const _RecordingPanel({required this.recorder, required this.onOpen});

  final RideRecorder recorder;
  final VoidCallback onOpen;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return ValueListenableBuilder<bool>(
      valueListenable: recorder.isRecording,
      builder: (context, recording, _) => Panel(
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const SectionLabel('騎乘記錄'),
            Row(
              children: [
                Icon(
                  recording ? Icons.fiber_manual_record : Icons.stop_circle_outlined,
                  size: 16,
                  color: recording ? SwColors.red : theme.colorScheme.outline,
                ),
                const SizedBox(width: 6),
                Text(
                  recording ? '記錄中' : '未記錄',
                  style: theme.textTheme.titleMedium?.copyWith(fontWeight: FontWeight.w800),
                ),
              ],
            ),
            const SizedBox(height: 10),
            Text(
              recording ? '連線期間持續記錄,可關螢幕' : '到歷史記錄頁可手動開始',
              style: theme.textTheme.bodySmall?.copyWith(color: theme.colorScheme.onSurfaceVariant),
            ),
            const SizedBox(height: 4),
            TextButton(
              style: TextButton.styleFrom(padding: EdgeInsets.zero, minimumSize: const Size(0, 32)),
              onPressed: onOpen,
              child: const Text('歷史記錄 →'),
            ),
          ],
        ),
      ),
    );
  }
}

class _AttitudePanel extends StatelessWidget {
  const _AttitudePanel({required this.data});

  final BikeData data;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final imuOk = data.imuOk ?? true;
    String fmt(double? v, int digits) =>
        (!imuOk || v == null) ? '–' : v.toStringAsFixed(digits);

    final (evtColor, evtLabel) = data.isCollision
        ? (SwColors.red, '碰撞')
        : data.isBrake
            ? (SwColors.amber, '急煞')
            : (SwColors.green, '正常');

    return Panel(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          SectionLabel(
            '姿態',
            trailing: Container(
              padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 3),
              decoration: BoxDecoration(
                color: evtColor.withValues(alpha: 0.12),
                borderRadius: BorderRadius.circular(999),
              ),
              child: Text(evtLabel,
                  style: TextStyle(color: evtColor, fontWeight: FontWeight.w800, fontSize: 12)),
            ),
          ),
          Row(
            children: [
              Expanded(child: _Tile(label: 'Roll °', value: fmt(data.roll, 1))),
              const SizedBox(width: 8),
              Expanded(child: _Tile(label: 'Pitch °', value: fmt(data.pitch, 1))),
              const SizedBox(width: 8),
              Expanded(child: _Tile(label: '合成 G', value: data.accelMagnitude?.toStringAsFixed(2) ?? '–')),
            ],
          ),
          if (!imuOk) ...[
            const SizedBox(height: 8),
            Text('IMU 沒有回應', style: theme.textTheme.bodySmall?.copyWith(color: SwColors.red)),
          ],
        ],
      ),
    );
  }
}

class _Tile extends StatelessWidget {
  const _Tile({required this.label, required this.value});

  final String label;
  final String value;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Container(
      padding: const EdgeInsets.symmetric(vertical: 10),
      decoration: BoxDecoration(
        color: theme.scaffoldBackgroundColor,
        borderRadius: BorderRadius.circular(10),
      ),
      child: Column(
        children: [
          Text(value,
              style: theme.textTheme.titleMedium?.copyWith(
                fontWeight: FontWeight.w800,
                fontFeatures: const [FontFeature.tabularFigures()],
              )),
          const SizedBox(height: 2),
          Text(label,
              style: theme.textTheme.labelSmall?.copyWith(color: theme.colorScheme.onSurfaceVariant)),
        ],
      ),
    );
  }
}

class _SystemPanel extends StatelessWidget {
  const _SystemPanel({required this.data});

  final BikeData data;

  @override
  Widget build(BuildContext context) {
    Widget row(String label, Widget value) => Padding(
          padding: const EdgeInsets.symmetric(vertical: 6),
          child: Row(
            children: [
              Expanded(child: Text(label)),
              value,
            ],
          ),
        );
    Widget ok(bool? v) {
      if (v == null) return const Text('–');
      return Text(v ? '正常' : '異常',
          style: TextStyle(
            color: v ? SwColors.green : SwColors.red,
            fontWeight: FontWeight.w700,
          ));
    }

    final wifi = switch (data.wifiMode) {
      'sta' => '連線模式',
      'ap' => '熱點模式',
      _ => '–',
    };

    return Panel(
      padding: const EdgeInsets.fromLTRB(16, 16, 16, 10),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const SectionLabel('裝置狀態'),
          row('IMU', ok(data.imuOk)),
          const Divider(),
          row('相機', ok(data.cameraOk)),
          const Divider(),
          row('SD 卡', ok(data.sdOk)),
          const Divider(),
          row('WiFi', Text(data.deviceIp == null ? wifi : '$wifi · ${data.deviceIp}')),
          const Divider(),
          row('時間來源', Text(data.timeSource ?? '–')),
        ],
      ),
    );
  }
}

/// A compact on/off pill overlaid on the black camera area.
class _PillSwitch extends StatelessWidget {
  const _PillSwitch({
    required this.icon,
    required this.label,
    required this.value,
    required this.onChanged,
  });

  final IconData icon;
  final String label;
  final bool value;
  final ValueChanged<bool>? onChanged;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.only(left: 10, right: 2),
      decoration: BoxDecoration(
        color: Colors.black54,
        borderRadius: BorderRadius.circular(24),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(icon, size: 15, color: Colors.white),
          const SizedBox(width: 5),
          Text(label, style: const TextStyle(color: Colors.white, fontSize: 13)),
          Transform.scale(
            scale: 0.8,
            child: Switch(
              value: value,
              onChanged: onChanged,
              activeTrackColor: SwColors.green,
              materialTapTargetSize: MaterialTapTargetSize.shrinkWrap,
            ),
          ),
        ],
      ),
    );
  }
}

class _CameraStatusPill extends StatelessWidget {
  const _CameraStatusPill({required this.mode, required this.paused});

  final CameraMode mode;
  final bool paused;

  @override
  Widget build(BuildContext context) {
    final (color, label) = paused
        ? (SwColors.amber, '已暫停')
        : switch (mode) {
            CameraMode.connected => (SwColors.green, '即時'),
            CameraMode.connecting => (SwColors.amber, '連線中'),
            CameraMode.error => (SwColors.red, '中斷'),
            CameraMode.disconnected => (Colors.grey, '未連線'),
          };
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 5),
      decoration: BoxDecoration(
        color: Colors.black54,
        borderRadius: BorderRadius.circular(24),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(Icons.circle, size: 8, color: color),
          const SizedBox(width: 6),
          Text(label, style: const TextStyle(color: Colors.white, fontSize: 12)),
        ],
      ),
    );
  }
}

/// Placeholder shown in the camera area while the stream is switched off, so
/// the black panel doesn't look like a failed connection.
class _StreamOffView extends StatelessWidget {
  const _StreamOffView();

  @override
  Widget build(BuildContext context) {
    return const Center(
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(Icons.videocam_off_rounded, size: 40, color: Colors.white54),
          SizedBox(height: 10),
          Text('鏡頭串流已關閉', style: TextStyle(color: Colors.white70, fontSize: 15)),
          SizedBox(height: 4),
          Text('開啟左上角「串流」開關以檢視即時影像',
              style: TextStyle(color: Colors.white38, fontSize: 12)),
        ],
      ),
    );
  }
}

/// Shows the firmware's setup hotspot name and password inside the connect
/// dialog. Each value can be tapped to copy it.
class _SetupHotspotInfo extends StatelessWidget {
  const _SetupHotspotInfo();

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Container(
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: theme.scaffoldBackgroundColor,
        borderRadius: BorderRadius.circular(10),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text('裝置設定熱點', style: theme.textTheme.labelLarge),
          const SizedBox(height: 8),
          const _CopyableField(label: '熱點名稱', value: kSetupApSsid),
          const SizedBox(height: 4),
          const _CopyableField(label: '密碼', value: kSetupApPassword),
        ],
      ),
    );
  }
}

class _CopyableField extends StatelessWidget {
  const _CopyableField({required this.label, required this.value});

  final String label;
  final String value;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return InkWell(
      borderRadius: BorderRadius.circular(4),
      onTap: () async {
        await Clipboard.setData(ClipboardData(text: value));
        if (!context.mounted) return;
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('已複製$label:$value'), duration: const Duration(seconds: 1)),
        );
      },
      child: Padding(
        padding: const EdgeInsets.symmetric(vertical: 2),
        child: Row(
          children: [
            SizedBox(width: 64, child: Text(label, style: theme.textTheme.bodySmall)),
            Expanded(
              child: Text(
                value,
                style: theme.textTheme.bodyMedium?.copyWith(
                  fontFamily: 'monospace',
                  fontWeight: FontWeight.w600,
                ),
              ),
            ),
            Icon(Icons.copy, size: 14, color: theme.colorScheme.outline),
          ],
        ),
      ),
    );
  }
}
