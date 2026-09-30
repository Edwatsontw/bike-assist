import 'package:http/http.dart' as http;

/// Commands sent to the ESP32 over its port-80 HTTP API (see the v3
/// firmware's `ledHandler` / `nightHandler`). Turn signals are deliberately
/// absent: they are driven by the physical handlebar switch, and a manual
/// `/api/led?mode=left|right` from the app would override that switch.
class DeviceControl {
  DeviceControl(this.baseUri, {http.Client? client})
      : _client = client ?? http.Client();

  /// Device base, e.g. `http://192.168.137.50`.
  final Uri baseUri;
  final http.Client _client;

  static const _timeout = Duration(seconds: 4);

  /// Releases the hazard lights a detected collision latched on
  /// (`/api/led?mode=off` also returns the indicators to switch control).
  Future<void> clearCollisionHazard() =>
      _get('/api/led', {'mode': 'off'});

  /// Turns the camera's software low-light enhancement on or off.
  Future<void> setNightMode(bool on) =>
      _get('/api/night', {'mode': on ? 'on' : 'off'});

  Future<void> _get(String path, Map<String, String> query) async {
    final uri = baseUri.replace(path: path, queryParameters: query);
    final response = await _client.get(uri).timeout(_timeout);
    if (response.statusCode != 200) {
      throw DeviceControlException('HTTP ${response.statusCode}');
    }
  }
}

class DeviceControlException implements Exception {
  DeviceControlException(this.message);
  final String message;
  @override
  String toString() => message;
}
