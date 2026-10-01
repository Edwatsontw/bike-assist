import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:geolocator/geolocator.dart';
import 'package:http/http.dart' as http;

/// One location fix from the phone.
class PhoneFix {
  const PhoneFix({
    required this.lat,
    required this.lon,
    this.speedKmh,
    this.accuracyM,
  });

  final double lat;
  final double lon;

  /// Null when the phone doesn't know its speed.
  final double? speedKmh;

  /// Horizontal accuracy in metres, when known.
  final double? accuracyM;
}

/// Where phone fixes come from. Abstracted so tests can feed fixes without
/// the platform plugin.
abstract class PhoneFixSource {
  /// Asks for permission / checks the location service. Returns a
  /// human-readable reason when location can't be used, or null when ready.
  Future<String?> prepare();

  Stream<PhoneFix> fixes();
}

/// Real source: the phone's GPS/fused location via geolocator. On Android it
/// runs geolocator's own foreground notification so fixes keep flowing with
/// the screen off while riding.
class GeolocatorFixSource implements PhoneFixSource {
  @override
  Future<String?> prepare() async {
    if (!await Geolocator.isLocationServiceEnabled()) {
      return '手機定位已關閉';
    }
    var permission = await Geolocator.checkPermission();
    if (permission == LocationPermission.denied) {
      permission = await Geolocator.requestPermission();
    }
    if (permission == LocationPermission.denied ||
        permission == LocationPermission.deniedForever) {
      return '未允許定位權限';
    }
    return null;
  }

  @override
  Stream<PhoneFix> fixes() {
    final LocationSettings settings =
        defaultTargetPlatform == TargetPlatform.android
            ? AndroidSettings(
                accuracy: LocationAccuracy.high,
                distanceFilter: 0,
                intervalDuration: const Duration(seconds: 1),
                foregroundNotificationConfig: const ForegroundNotificationConfig(
                  notificationTitle: 'SafeWay 定位中',
                  notificationText: '把手機定位傳給車機',
                  enableWakeLock: true,
                ),
              )
            : const LocationSettings(
                accuracy: LocationAccuracy.high,
                distanceFilter: 0,
              );
    return Geolocator.getPositionStream(locationSettings: settings).map(
      (p) => PhoneFix(
        lat: p.latitude,
        lon: p.longitude,
        // geolocator reports m/s, negative/NaN when unknown.
        speedKmh: (p.speed.isFinite && p.speed >= 0) ? p.speed * 3.6 : null,
        accuracyM: p.accuracy.isFinite && p.accuracy > 0 ? p.accuracy : null,
      ),
    );
  }
}

enum PhoneLocState { off, starting, active, error }

/// Sends the phone's location to the bike (`GET /api/phoneloc`) while
/// connected, so the firmware can use it instead of its own GPS antenna. The
/// firmware falls back to the antenna by itself once updates stop arriving
/// (about 5 s), so stopping here needs no extra handshake.
class PhoneLocationRelay {
  PhoneLocationRelay({
    PhoneFixSource? source,
    http.Client? client,
    this.minInterval = const Duration(seconds: 1),
  })  : _source = source ?? GeolocatorFixSource(),
        _client = client ?? http.Client();

  final PhoneFixSource _source;
  final http.Client _client;

  /// Upper bound on how often a fix is sent to the device.
  final Duration minInterval;

  final ValueNotifier<PhoneLocState> state = ValueNotifier(PhoneLocState.off);
  final ValueNotifier<String?> message = ValueNotifier(null);

  Uri? _base;
  StreamSubscription<PhoneFix>? _sub;
  DateTime? _lastSent;
  bool _inFlight = false;
  int _generation = 0;

  bool get isRunning => _base != null;

  /// Starts relaying to the device at [base] (e.g. `http://192.168.137.50`).
  /// Calling again with the same base is a no-op.
  Future<void> start(Uri base) async {
    if (_base == base && state.value != PhoneLocState.error) return;
    await stop();
    final gen = ++_generation;
    _base = base;
    state.value = PhoneLocState.starting;
    message.value = null;

    final String? problem;
    try {
      problem = await _source.prepare();
    } catch (e) {
      if (gen == _generation) _fail('手機定位無法啟動:$e');
      return;
    }
    if (gen != _generation) return; // stopped/restarted meanwhile
    if (problem != null) {
      _fail('$problem,改用車機 GPS 天線');
      return;
    }
    _sub = _source.fixes().listen(
      (fix) => _send(gen, fix),
      onError: (Object e) {
        if (gen == _generation) _fail('手機定位中斷,改用車機 GPS 天線');
      },
    );
  }

  Future<void> stop() async {
    _generation++;
    _base = null;
    await _sub?.cancel();
    _sub = null;
    _lastSent = null;
    state.value = PhoneLocState.off;
    message.value = null;
  }

  Future<void> _send(int gen, PhoneFix fix) async {
    final base = _base;
    if (base == null || gen != _generation || _inFlight) return;
    final now = DateTime.now();
    final last = _lastSent;
    if (last != null && now.difference(last) < minInterval) return;
    _lastSent = now;
    _inFlight = true;
    try {
      final uri = buildUri(base, fix);
      final response = await _client.get(uri).timeout(const Duration(seconds: 2));
      if (gen != _generation) return;
      if (response.statusCode == 200) {
        final lowAccuracy = response.body.contains('low_accuracy');
        state.value = PhoneLocState.active;
        message.value = lowAccuracy ? '手機定位精度不足,暫用 GPS 天線' : null;
      }
    } catch (_) {
      // Telemetry polling already reports connection problems; a dropped
      // location update just means the firmware keeps its last source.
    } finally {
      _inFlight = false;
    }
  }

  @visibleForTesting
  static Uri buildUri(Uri base, PhoneFix fix) => base.replace(
        path: '/api/phoneloc',
        queryParameters: {
          'lat': fix.lat.toStringAsFixed(6),
          'lon': fix.lon.toStringAsFixed(6),
          if (fix.speedKmh != null) 'spd': fix.speedKmh!.toStringAsFixed(1),
          if (fix.accuracyM != null) 'acc': fix.accuracyM!.toStringAsFixed(0),
        },
      );

  void _fail(String text) {
    state.value = PhoneLocState.error;
    message.value = text;
  }

  Future<void> dispose() async {
    await stop();
    state.dispose();
    message.dispose();
    _client.close();
  }
}
