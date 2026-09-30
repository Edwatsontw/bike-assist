import 'dart:async';

import 'package:flutter/foundation.dart';

import '../models/bike_data.dart';
import '../models/ride_event.dart';
import '../models/route_point.dart';
import 'bike_data_service.dart';
import 'camera_source.dart';
import 'ride_frame_store.dart';
import 'ride_repository.dart';

/// Records a ride: GPS/speed samples from [BikeDataService] into the database,
/// and camera frames from [CameraSource] onto disk (with their timestamps
/// indexed in the database), until [stop] is called.
class RideRecorder {
  RideRecorder({
    required this.dataService,
    required this.repository,
    required this.cameraSource,
    required this.frameStore,
    this.maxFramesPerSecond = 15,
    this.indexFlushInterval = const Duration(seconds: 1),
  });

  final BikeDataService dataService;
  final RideRepository repository;
  final CameraSource cameraSource;
  final RideFrameStore frameStore;

  /// Upper bound on recorded frames. The camera may push faster (a real ESP32
  /// can exceed this); surplus frames are dropped rather than stored.
  final int maxFramesPerSecond;

  /// How often buffered frame timestamps are flushed to the database, so we
  /// issue one batched transaction instead of an insert per frame.
  final Duration indexFlushInterval;

  final ValueNotifier<bool> isRecording = ValueNotifier(false);

  /// The ride currently being recorded, or null when not recording. Used to
  /// tell a genuinely in-progress ride apart from an orphan left by a crash.
  int? get currentRideId => _currentRideId;

  int? _currentRideId;
  StreamSubscription? _dataSubscription;

  /// Previous sample's accel event, so one brake/collision that stays visible
  /// across consecutive polls is stored once (on its rising edge).
  String? _lastAccelEvent;
  StreamSubscription<Uint8List>? _frameSubscription;

  /// True while a frame write is in flight — incoming frames are dropped
  /// rather than queued, so a slow disk can't grow an unbounded backlog.
  bool _writingFrame = false;
  DateTime? _lastFrameAt;
  final List<DateTime> _pendingFrameTimestamps = [];
  Timer? _indexFlushTimer;

  Duration get _minFrameGap =>
      Duration(microseconds: Duration.microsecondsPerSecond ~/ maxFramesPerSecond);

  Future<void> start() async {
    if (isRecording.value) return;
    final rideId = await repository.startRide();
    _currentRideId = rideId;
    _lastFrameAt = null;
    _lastAccelEvent = null;

    _dataSubscription = dataService.stream.listen((data) {
      final id = _currentRideId;
      if (id == null) return;
      _recordEventEdge(id, data);
      // Skip samples without a GPS fix — a device with no fix reports (0,0),
      // which would otherwise fill the ride with bogus points off West Africa.
      if (!_hasGpsFix(data)) return;
      repository.addPoint(
        id,
        RoutePoint(
          lat: data.lat,
          lng: data.lng,
          speedKmh: data.speedKmh,
          timestamp: data.timestamp,
        ),
      );
    });

    _frameSubscription = cameraSource.frames.listen(_onCameraFrame);
    _indexFlushTimer = Timer.periodic(indexFlushInterval, (_) => _flushFrameIndex());

    isRecording.value = true;
  }

  void _recordEventEdge(int rideId, BikeData data) {
    final event = data.accelEvent;
    final previous = _lastAccelEvent;
    _lastAccelEvent = event;
    if (event != RideEvent.brake && event != RideEvent.collision) return;
    if (event == previous) return; // same event still showing — already stored
    final fix = _hasGpsFix(data);
    repository.addEvent(
      rideId,
      RideEvent(
        type: event!,
        timestamp: data.timestamp,
        lat: fix ? data.lat : null,
        lng: fix ? data.lng : null,
        magnitude: data.accelMagnitude,
      ),
    );
  }

  /// Whether a telemetry sample carries a usable GPS position. The firmware
  /// reports (0,0) with `fix:false` when it hasn't locked on, so those are not
  /// recorded as route points.
  static bool _hasGpsFix(BikeData data) =>
      (data.gpsFix ?? true) &&
      data.lat.isFinite &&
      data.lng.isFinite &&
      !(data.lat == 0 && data.lng == 0);

  void _onCameraFrame(Uint8List bytes) {
    final rideId = _currentRideId;
    if (rideId == null || _writingFrame) return;

    final now = DateTime.now();
    final last = _lastFrameAt;
    if (last != null && now.difference(last) < _minFrameGap) return; // throttled

    _lastFrameAt = now;
    _writingFrame = true;
    frameStore.saveFrame(rideId, bytes, now).then((_) {
      _pendingFrameTimestamps.add(now);
    }).catchError((Object error) {
      debugPrint('[RideRecorder] 影格寫入失敗: $error');
    }).whenComplete(() {
      _writingFrame = false;
    });
  }

  Future<void> _flushFrameIndex() async {
    final rideId = _currentRideId;
    if (rideId == null || _pendingFrameTimestamps.isEmpty) return;
    final batch = List<DateTime>.from(_pendingFrameTimestamps);
    _pendingFrameTimestamps.clear();
    await repository.addFrames(rideId, batch);
  }

  Future<void> stop() async {
    if (!isRecording.value) return;
    await _dataSubscription?.cancel();
    await _frameSubscription?.cancel();
    _dataSubscription = null;
    _frameSubscription = null;
    _indexFlushTimer?.cancel();
    _indexFlushTimer = null;

    await _flushFrameIndex(); // persist whatever the last interval captured

    final rideId = _currentRideId;
    _currentRideId = null;
    isRecording.value = false;
    if (rideId != null) {
      await repository.endRide(rideId);
    }
  }

  void dispose() {
    _dataSubscription?.cancel();
    _frameSubscription?.cancel();
    _indexFlushTimer?.cancel();
    isRecording.dispose();
  }
}
