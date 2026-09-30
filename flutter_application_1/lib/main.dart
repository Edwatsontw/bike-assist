import 'package:flutter/material.dart';

import 'screens/home_screen.dart';
import 'services/bike_data_source.dart';
import 'services/camera_source.dart';
import 'services/emergency_relay_service.dart';
import 'services/emergency_settings.dart';
import 'services/keep_alive_controller.dart';
import 'services/recording_keep_alive.dart';
import 'services/ride_frame_store.dart';
import 'services/ride_recorder.dart';
import 'services/ride_repository.dart';
import 'theme.dart';

void main() {
  runApp(const BikeAssistApp());
}

class BikeAssistApp extends StatefulWidget {
  const BikeAssistApp({
    super.key,
    this.repository,
    this.frameStore,
    this.keepAlive,
    this.recordingEnabled = true,
    this.autoDiscover = true,
  });

  /// Injectable so tests can supply an in-memory database / temp directory.
  final RideRepository? repository;
  final RideFrameStore? frameStore;

  /// Holds the process open while recording so the screen can be off. Defaults
  /// to the Android foreground service, and to a no-op on every other platform.
  final RecordingKeepAlive? keepAlive;

  /// Enables ride recording: cleans up orphaned rides on launch and records
  /// while a device is connected. Widget tests turn this off — recording
  /// writes frames and database rows on real async I/O, which never completes
  /// under the widget-test fake-async clock and would leave operations queued
  /// on the sqflite isolate.
  final bool recordingEnabled;

  /// Search the network for the device on launch. Tests turn this off: the
  /// sweep does real socket I/O that never settles under fake-async.
  final bool autoDiscover;

  @override
  State<BikeAssistApp> createState() => _BikeAssistAppState();
}

class _BikeAssistAppState extends State<BikeAssistApp> with WidgetsBindingObserver {
  late final BikeDataSource _dataSource = BikeDataSource();
  late final CameraSource _cameraSource = CameraSource();
  late final RideRepository _repository = widget.repository ?? RideRepository();
  late final RideFrameStore _frameStore = widget.frameStore ?? RideFrameStore();
  late final RideRecorder _recorder = RideRecorder(
    dataService: _dataSource,
    repository: _repository,
    cameraSource: _cameraSource,
    frameStore: _frameStore,
  );
  /// Reference-counted so ride recording and the emergency relay can each keep
  /// the process alive without tearing the shared foreground service out from
  /// under the other.
  late final KeepAliveController _keepAlive = KeepAliveController(
    widget.keepAlive ?? RecordingKeepAlive.forPlatform(),
  );
  final EmergencySettings _emergencySettings = const EmergencySettings();
  late final EmergencyRelayService _emergencyRelay =
      EmergencyRelayService(_keepAlive);

  /// Tracks whether recording currently holds the keep-alive, so we release
  /// exactly the holds we took (recording toggles rapidly).
  bool _recordingHold = false;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
    if (widget.recordingEnabled) {
      // Close any ride left open by an unclean shutdown, then record only
      // while a device is connected.
      _repository.closeOrphanRides();
      _cameraSource.mode.addListener(_syncRecordingWithConnection);
      _dataSource.mode.addListener(_syncRecordingWithConnection);
      _recorder.isRecording.addListener(_syncKeepAliveWithRecording);

      // Resume the emergency BLE relay if it was left armed.
      WidgetsBinding.instance.addPostFrameCallback((_) => _resumeEmergency());
    }
  }

  Future<void> _resumeEmergency() async {
    final config = await _emergencySettings.load();
    if (config.enabled && config.serverUrl.isNotEmpty) {
      await _emergencyRelay.arm(config.serverUrl);
    }
  }

  /// Runs the foreground service for exactly as long as a ride is recording:
  /// without it Android freezes this process when the screen goes off and the
  /// ride stops capturing. Driven off [RideRecorder.isRecording] rather than the
  /// connection so it also covers a recording started by hand from the history
  /// screen.
  void _syncKeepAliveWithRecording() {
    final recording = _recorder.isRecording.value;
    if (recording && !_recordingHold) {
      _recordingHold = true;
      _keepAlive.acquire();
    } else if (!recording && _recordingHold) {
      _recordingHold = false;
      _keepAlive.release();
    }
  }

  /// Records only while a device is connected: starts when the camera or
  /// telemetry goes live, stops once both are down (disconnected/error).
  /// Neither starts nor stops during the transient connecting phase.
  void _syncRecordingWithConnection() {
    final connected = _cameraSource.mode.value == CameraMode.connected ||
        _dataSource.mode.value == TelemetryMode.connected;
    final connecting = _cameraSource.mode.value == CameraMode.connecting ||
        _dataSource.mode.value == TelemetryMode.connecting;

    if (connected && !_recorder.isRecording.value) {
      _recorder.start();
    } else if (!connected && !connecting && _recorder.isRecording.value) {
      _recorder.stop();
    }
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    if (state == AppLifecycleState.detached) {
      _recorder.stop();
    }
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    _cameraSource.mode.removeListener(_syncRecordingWithConnection);
    _dataSource.mode.removeListener(_syncRecordingWithConnection);
    _recorder.isRecording.removeListener(_syncKeepAliveWithRecording);
    _emergencyRelay.dispose();
    _recorder.dispose();
    _cameraSource.dispose();
    _dataSource.dispose();
    _repository.close();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'SafeWay',
      debugShowCheckedModeBanner: false,
      theme: buildSafeWayTheme(Brightness.light),
      darkTheme: buildSafeWayTheme(Brightness.dark),
      home: HomeScreen(
        dataSource: _dataSource,
        cameraSource: _cameraSource,
        repository: _repository,
        frameStore: _frameStore,
        recorder: _recorder,
        emergencyRelay: _emergencyRelay,
        emergencySettings: _emergencySettings,
        autoDiscover: widget.autoDiscover,
      ),
    );
  }
}
