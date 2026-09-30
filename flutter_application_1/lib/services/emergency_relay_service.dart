import 'dart:async';
import 'dart:convert';

import 'package:flutter/foundation.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:http/http.dart' as http;
import 'package:permission_handler/permission_handler.dart';

import 'keep_alive_controller.dart';

/// Manufacturer-specific-data company id the firmware advertises the fall alert
/// under (see `BLE_MFG_ID` in main.cpp). The 16-bit company id is stripped by
/// the BLE stack, so [ScanResult.advertisementData]'s `manufacturerData` value
/// is just the payload below.
const int _kFallCompanyId = 0xFFFF;

/// First payload byte — event type (see `BLE_EVT_*` in the v3 main.cpp):
/// 0x01 = fallen for 10 s (notice), 0x02 = still down after 5 min (emergency).
const int kEvtFallenNotice = 0x01;
const int kEvtFallenEmergency = 0x02;

/// Payload layout advertised by `broadcastFallenBLE`:
/// `[evt(1) | lat(float32 LE, 4) | lon(float32 LE, 4) | epoch(uint32 LE, 4)]`.
const int _kFallPayloadLen = 13;

/// A decoded fall alert broadcast by a bike device.
class FallAlert {
  const FallAlert({
    required this.lat,
    required this.lon,
    required this.epoch,
    required this.rssi,
    this.eventType = kEvtFallenNotice,
  });

  final double lat;
  final double lon;

  /// Unix seconds the fall was detected on the device.
  final int epoch;

  /// Signal strength of the advertisement that carried it (dBm).
  final int rssi;

  /// Raw event byte: [kEvtFallenNotice] or [kEvtFallenEmergency].
  final int eventType;

  bool get isEmergency => eventType == kEvtFallenEmergency;

  /// The `level` the v3 server's `POST /api/fallen` expects.
  String get level => isEmergency ? 'emergency' : 'notice';

  /// Whether the device had a GPS fix when it fell (it sends 0,0 otherwise).
  bool get hasPosition => !(lat == 0 && lon == 0);

  /// Identity of one alert: the same fall is advertised for ~2 minutes, and a
  /// fall that escalates re-broadcasts under a new event type.
  String get dedupeKey => '$eventType:$epoch';

  DateTime get time => DateTime.fromMillisecondsSinceEpoch(epoch * 1000);

  /// Decodes a fall alert from the manufacturer-data [payload] (the bytes after
  /// the 16-bit company id), or null if it isn't a well-formed fall payload.
  /// Layout matches `broadcastFallenBLE` in the firmware:
  /// `[evt(1) | lat(float32 LE) | lon(float32 LE) | epoch(uint32 LE)]`.
  static FallAlert? tryParse(List<int> payload, {int rssi = 0}) {
    if (payload.length < _kFallPayloadLen) return null;
    final evt = payload[0];
    if (evt != kEvtFallenNotice && evt != kEvtFallenEmergency) return null;
    final bytes = ByteData.sublistView(Uint8List.fromList(payload));
    return FallAlert(
      lat: bytes.getFloat32(1, Endian.little),
      lon: bytes.getFloat32(5, Endian.little),
      epoch: bytes.getUint32(9, Endian.little),
      rssi: rssi,
      eventType: evt,
    );
  }
}

/// Outcome of relaying one alert to the server, kept for the UI.
class RelayOutcome {
  const RelayOutcome({
    required this.alert,
    required this.at,
    required this.ok,
    this.detail,
  });

  final FallAlert alert;
  final DateTime at;
  final bool ok;
  final String? detail;
}

enum RelayState { idle, scanning, error }

/// Listens for the emergency BLE broadcast a bike device sends after a fall
/// (connectionless — we only scan advertisements, never pair/connect) and
/// relays the coordinates to a configured server over the phone's own network.
///
/// This is the phone side of the firmware's design: the crashed bike's own
/// uplink may be down, so any nearby phone running this app forwards the alert.
/// Scanning is kept running with the screen off via a foreground-service hold.
class EmergencyRelayService {
  EmergencyRelayService(this._keepAlive, {http.Client? client})
      : _client = client ?? http.Client();

  final KeepAliveController _keepAlive;
  final http.Client _client;

  final ValueNotifier<RelayState> state = ValueNotifier(RelayState.idle);
  final ValueNotifier<String?> errorMessage = ValueNotifier(null);
  final ValueNotifier<RelayOutcome?> lastRelay = ValueNotifier(null);

  bool get isArmed => _armed;
  bool _armed = false;
  String _serverUrl = '';
  bool _holdingKeepAlive = false;

  /// Alerts already relayed (see [FallAlert.dedupeKey]), so a device
  /// advertising the same fall for two minutes doesn't POST it dozens of times,
  /// while an escalation from notice to emergency is still reported.
  final Set<String> _relayed = <String>{};

  StreamSubscription<List<ScanResult>>? _scanSub;

  bool get _isAndroid =>
      !kIsWeb && defaultTargetPlatform == TargetPlatform.android;

  /// Starts scanning and relaying to [serverUrl]. Safe to call when already
  /// armed (just updates the target URL). A no-op scan off Android.
  Future<void> arm(String serverUrl) async {
    _serverUrl = serverUrl.trim();
    if (!_isAndroid) {
      // BLE relay is Android-only; elsewhere just record the intent.
      _armed = true;
      state.value = RelayState.idle;
      return;
    }
    if (_armed) return;
    _armed = true;

    try {
      if (await FlutterBluePlus.isSupported == false) {
        _fail('此裝置不支援藍牙');
        return;
      }
      if (!await _ensurePermissions()) {
        _fail('缺少藍牙權限');
        return;
      }
      if (!_holdingKeepAlive) {
        await _keepAlive.acquire(); // keep scanning alive with the screen off
        _holdingKeepAlive = true;
      }
      await _startScan();
    } catch (error) {
      _fail('啟動失敗:$error');
    }
  }

  /// Stops scanning and releases the process hold.
  Future<void> disarm() async {
    _armed = false;
    _serverUrl = '';
    try {
      await _scanSub?.cancel();
      _scanSub = null;
      if (_isAndroid && FlutterBluePlus.isScanningNow) {
        await FlutterBluePlus.stopScan();
      }
    } catch (_) {
      // best-effort teardown
    }
    if (_holdingKeepAlive) {
      await _keepAlive.release();
      _holdingKeepAlive = false;
    }
    state.value = RelayState.idle;
    errorMessage.value = null;
  }

  Future<void> _startScan() async {
    await _scanSub?.cancel();
    // continuousUpdates so a device advertising the same alert repeatedly keeps
    // reaching us; withMsd filters to our company id at the OS scanner level.
    _scanSub = FlutterBluePlus.onScanResults.listen(
      _onScanResults,
      onError: (Object e) => _fail('掃描錯誤:$e'),
    );
    // Optimistic — a scan failure arrives asynchronously on the results stream
    // (handled by the listener above), so we don't block arming on it.
    state.value = RelayState.scanning;
    errorMessage.value = null;
    try {
      await FlutterBluePlus.startScan(
        withMsd: [MsdFilter(_kFallCompanyId)],
        continuousUpdates: true,
        androidScanMode: AndroidScanMode.lowLatency,
      ).timeout(const Duration(seconds: 6));
    } on TimeoutException {
      // Some platforms (notably the Android emulator's virtual adapter) never
      // return from the start handshake. The listener is already attached, so
      // leave the scan running rather than hanging the caller forever.
      debugPrint('[Emergency] startScan handshake timed out; continuing');
    }
  }

  void _onScanResults(List<ScanResult> results) {
    for (final result in results) {
      final alert = _decode(result);
      if (alert != null) _relay(alert);
    }
  }

  /// Extracts a [FallAlert] from an advertisement, or null if it isn't one.
  FallAlert? _decode(ScanResult result) {
    final payload = result.advertisementData.manufacturerData[_kFallCompanyId];
    if (payload == null) return null;
    return FallAlert.tryParse(payload, rssi: result.rssi);
  }

  Future<void> _relay(FallAlert alert) async {
    if (_relayed.contains(alert.dedupeKey)) return; // already sent this alert
    final url = _serverUrl;
    if (url.isEmpty) return;
    final uri = Uri.tryParse(url);
    if (uri == null || !uri.hasScheme) {
      _recordOutcome(alert, ok: false, detail: '伺服器網址無效');
      return;
    }
    // Reserve the epoch before the request so concurrent adverts don't
    // double-post; on failure release it so a later advert can retry.
    _relayed.add(alert.dedupeKey);
    try {
      final response = await _client
          .post(
            uri,
            headers: const {'Content-Type': 'application/json'},
            body: jsonEncode(buildRelayBody(alert)),
          )
          .timeout(const Duration(seconds: 8));
      if (response.statusCode >= 200 && response.statusCode < 300) {
        _recordOutcome(alert, ok: true, detail: 'HTTP ${response.statusCode}');
      } else {
        _relayed.remove(alert.dedupeKey);
        _recordOutcome(alert, ok: false, detail: 'HTTP ${response.statusCode}');
      }
    } catch (error) {
      _relayed.remove(alert.dedupeKey);
      _recordOutcome(alert, ok: false, detail: error.toString());
    }
  }

  /// JSON sent to the server. `lat`/`lon`/`device_ts`/`level` match the v3
  /// server's `FallenReport` model (POST /api/fallen); `event`/`epoch`/`time`
  /// are kept for any older endpoint that read them.
  @visibleForTesting
  static Map<String, dynamic> buildRelayBody(FallAlert alert) => {
        'lat': alert.lat,
        'lon': alert.lon,
        'device_ts': alert.epoch,
        'level': alert.level,
        'event': 'fallen',
        'epoch': alert.epoch,
        'time': alert.time.toUtc().toIso8601String(),
      };

  void _recordOutcome(FallAlert alert, {required bool ok, String? detail}) {
    lastRelay.value =
        RelayOutcome(alert: alert, at: DateTime.now(), ok: ok, detail: detail);
    debugPrint('[Emergency] relay ${ok ? "OK" : "FAIL"} '
        'level=${alert.level} lat=${alert.lat} lon=${alert.lon} epoch=${alert.epoch} $detail');
  }

  Future<bool> _ensurePermissions() async {
    // API 31+: BLUETOOTH_SCAN (declared neverForLocation) + BLUETOOTH_CONNECT.
    // Older devices fall back to location, which the manifest scopes to <=30.
    final statuses = await [
      Permission.bluetoothScan,
      Permission.bluetoothConnect,
      Permission.locationWhenInUse,
    ].request();
    // Scan is the one we truly need; connect/location may be irrelevant per OS
    // version, so treat a granted-or-not-applicable scan as success.
    final scan = statuses[Permission.bluetoothScan];
    return scan == null || scan.isGranted || scan.isLimited;
  }

  void _fail(String message) {
    state.value = RelayState.error;
    errorMessage.value = message;
  }

  Future<void> dispose() async {
    await disarm();
    state.dispose();
    errorMessage.dispose();
    lastRelay.dispose();
    _client.close();
  }
}
