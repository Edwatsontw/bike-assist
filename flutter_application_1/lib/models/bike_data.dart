/// Telemetry snapshot matching the ESP32-S3 v3 firmware's `GET /api/status`.
///
/// Firmware payload (see `statusHandler()` in bike-assist-v3/src/main.cpp):
/// ```json
/// {"wifi":"sta","ip":"192.168.137.50",
///  "imu":{"ok":true,"roll":1.2,"pitch":-0.5,"ax":0.01,"ay":0.02,"az":0.99,"i2cStale":0},
///  "accel":{"event":"BRAKE","g":2.3},
///  "gps":{"chars":1234,"fix":true,"lat":23.99,"lon":121.60,"speed":12.5},
///  "time":{"now":"...","source":"gps"},
///  "sd":{"ok":true},"camera":true,"night":false,
///  "led":"left","hazard":false,
///  "loc":{"src":"phone","valid":true,"lat":23.99,"lon":121.60,"speed":12.5,"phoneAgeMs":420}}
/// ```
class BikeData {
  final double lat;
  final double lng;
  final double speedKmh;
  final DateTime timestamp;

  /// Accelerometer event classification: `NORMAL` | `BRAKE` | `COLLISION`.
  final String? accelEvent;

  /// Combined acceleration magnitude, in G (firmware field `accel.g`).
  final double? accelMagnitude;

  /// Current indicator state, upper-cased: `NONE` | `LEFT` | `RIGHT` | `HAZARD`.
  /// Received for completeness only — the turn signals are driven by the
  /// physical handlebar switch, so the app neither shows nor controls them.
  final String? ledDirection;

  /// Legacy field from the older firmware's `led` object; null on v3.
  final bool? ledManual;

  /// Whether the GPS currently has a valid fix.
  final bool? gpsFix;

  /// Count of NMEA characters the firmware has parsed from the GPS module.
  /// 0 while connected means no data is reaching the ESP32 (wiring), whereas a
  /// rising count with [gpsFix] false means it's receiving but not yet locked.
  final int? gpsChars;

  // ── Attitude (v3) ───────────────────────────────────────────────────────
  /// Whether the IMU (MPU6050) is responding.
  final bool? imuOk;

  /// Roll / pitch in degrees from the firmware's filtered IMU output.
  final double? roll;
  final double? pitch;

  // ── Device state (v3) ───────────────────────────────────────────────────
  /// True when a detected collision has latched the hazard lights on. The
  /// firmware keeps them on until the app sends `/api/led?mode=off`.
  final bool? hazardLocked;

  /// Whether the camera's software low-light enhancement is on.
  final bool? nightMode;

  final bool? cameraOk;
  final bool? sdOk;

  /// `sta` (joined a WiFi network) or `ap` (serving its setup hotspot).
  final String? wifiMode;
  final String? deviceIp;

  /// Where the device clock came from (e.g. `gps`, `ntp`, `none`).
  final String? timeSource;

  /// Which position the firmware is using: `phone` (relayed from this app),
  /// `gps` (its own antenna) or `none`. Null on firmware without the `loc`
  /// object, in which case [lat]/[lng]/[speedKmh] come straight from the GPS.
  final String? locationSource;

  /// The GPS antenna's own fix state, independent of [locationSource].
  final bool? antennaFix;

  const BikeData({
    required this.lat,
    required this.lng,
    required this.speedKmh,
    required this.timestamp,
    this.accelEvent,
    this.accelMagnitude,
    this.ledDirection,
    this.ledManual,
    this.gpsFix,
    this.gpsChars,
    this.imuOk,
    this.roll,
    this.pitch,
    this.hazardLocked,
    this.nightMode,
    this.cameraOk,
    this.sdOk,
    this.wifiMode,
    this.deviceIp,
    this.timeSource,
    this.locationSource,
    this.antennaFix,
  });

  bool get isBrake => accelEvent == 'BRAKE';
  bool get isCollision => accelEvent == 'COLLISION';

  /// Parses the firmware's `/api/status` JSON into a [BikeData]. Missing
  /// numeric position fields default to 0; everything else defaults to null.
  ///
  /// Accepts both the v3 shape (`accel.g`, `led` as a string) and the older
  /// shape (`accel.magnitude`, `led` as `{direction, manual}`).
  factory BikeData.fromStatusJson(Map<String, dynamic> json, {DateTime? timestamp}) {
    Map<String, dynamic> obj(String key) {
      final value = json[key];
      return value is Map ? value.cast<String, dynamic>() : const {};
    }

    final gps = obj('gps');
    final accel = obj('accel');
    final imu = obj('imu');
    final sd = obj('sd');
    final time = obj('time');
    final loc = obj('loc');

    double num0(dynamic v) => (v as num?)?.toDouble() ?? 0.0;
    double? numOrNull(dynamic v) => (v as num?)?.toDouble();
    bool? boolOrNull(dynamic v) => v is bool ? v : null;
    String? strOrNull(dynamic v) => v is String ? v : null;

    // `led` is a plain string on v3 ("left"/"LEFT"/"none"...), an object before.
    final ledRaw = json['led'];
    String? ledDirection;
    bool? ledManual;
    if (ledRaw is String) {
      ledDirection = ledRaw.toUpperCase();
    } else if (ledRaw is Map) {
      ledDirection = strOrNull(ledRaw['direction'])?.toUpperCase();
      ledManual = boolOrNull(ledRaw['manual']);
    }

    // v3 (2026-10-01) firmware reports the position it actually uses in `loc`
    // (phone first, GPS antenna as fallback). Older firmware only has `gps`.
    final hasLoc = loc.isNotEmpty;
    final pos = hasLoc ? loc : gps;
    final antennaFix = boolOrNull(gps['fix']);

    return BikeData(
      lat: num0(pos['lat']),
      lng: num0(pos['lon']),
      speedKmh: num0(pos['speed']),
      timestamp: timestamp ?? DateTime.now(),
      accelEvent: strOrNull(accel['event']),
      accelMagnitude: numOrNull(accel['g'] ?? accel['magnitude']),
      ledDirection: ledDirection,
      ledManual: ledManual,
      // "Has a usable position" — from either source when `loc` is present.
      gpsFix: hasLoc ? boolOrNull(loc['valid']) : antennaFix,
      gpsChars: (gps['chars'] as num?)?.toInt(),
      imuOk: boolOrNull(imu['ok']),
      roll: numOrNull(imu['roll']),
      pitch: numOrNull(imu['pitch']),
      hazardLocked: boolOrNull(json['hazard']),
      nightMode: boolOrNull(json['night']),
      cameraOk: boolOrNull(json['camera']),
      sdOk: boolOrNull(sd['ok']),
      wifiMode: strOrNull(json['wifi']),
      deviceIp: strOrNull(json['ip']),
      timeSource: strOrNull(time['source']),
      locationSource: strOrNull(loc['src']),
      antennaFix: antennaFix,
    );
  }
}
