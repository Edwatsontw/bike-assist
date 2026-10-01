import 'dart:async';
import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:http/http.dart' as http;
import 'package:http/testing.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'package:sqflite_common_ffi/sqflite_ffi.dart';

import 'package:flutter_application_1/main.dart';
import 'package:flutter_application_1/models/bike_data.dart';
import 'package:flutter_application_1/models/ride_event.dart';
import 'package:flutter_application_1/models/route_point.dart';
import 'package:flutter_application_1/screens/device_wifi_setup_screen.dart';
import 'package:flutter_application_1/screens/ride_list_screen.dart';
import 'package:flutter_application_1/services/bike_data_service.dart';
import 'package:flutter_application_1/services/camera_source.dart';
import 'package:flutter_application_1/services/http_status_bike_data_service.dart';
import 'package:flutter_application_1/services/device_discovery.dart';
import 'package:flutter_application_1/services/device_provisioning.dart';
import 'package:flutter_application_1/services/emergency_relay_service.dart';
import 'package:flutter_application_1/services/phone_location_relay.dart';
import 'package:flutter_application_1/services/recording_keep_alive.dart';
import 'package:flutter_application_1/services/ride_export_service.dart';
import 'package:flutter_application_1/services/ride_frame_store.dart';
import 'package:flutter_application_1/services/ride_recorder.dart';
import 'package:flutter_application_1/services/ride_repository.dart';
import 'package:flutter_application_1/services/video_encoder.dart';
import 'package:flutter_application_1/utils/timeline.dart';

class _FakeBikeDataService implements BikeDataService {
  final _controller = StreamController<BikeData>.broadcast();

  @override
  Stream<BikeData> get stream => _controller.stream;

  void emit(BikeData data) => _controller.add(data);

  @override
  void dispose() => _controller.close();
}

/// Stands in for the native encoder: records what it was asked to encode and
/// writes a placeholder file at [outputPath] so the export's video File exists.
class _FakeVideoEncoder implements VideoEncoder {
  bool called = false;
  List<String>? lastFramePaths;
  List<int>? lastPtsMs;
  int? lastFps;

  @override
  Future<void> encodeJpegsToMp4({
    required List<String> framePaths,
    required List<int> ptsMs,
    required String outputPath,
    int fps = 15,
  }) async {
    called = true;
    lastFramePaths = framePaths;
    lastPtsMs = ptsMs;
    lastFps = fps;
    await File(outputPath).writeAsBytes(const [0, 0, 0]);
  }
}

BikeData _bikeAt({
  required double lat,
  required double lng,
  bool? fix,
  DateTime? at,
}) =>
    BikeData(
      lat: lat,
      lng: lng,
      speedKmh: 10,
      timestamp: at ?? DateTime.now(),
      gpsFix: fix,
    );

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  setUpAll(() {
    sqfliteFfiInit();
    databaseFactory = databaseFactoryFfi;
  });

  // The home screen reads the last-used device host on startup.
  setUp(() => SharedPreferences.setMockInitialValues({}));

  /// An app wired to an in-memory database and a temp frame directory, with
  /// background recording off. Recording performs real file + database writes,
  /// which never complete under the widget-test fake-async clock and would
  /// leave operations stuck on the sqflite isolate, hanging every later test.
  BikeAssistApp testApp() => BikeAssistApp(
        repository: RideRepository(path: inMemoryDatabasePath),
        frameStore: RideFrameStore(baseDir: Directory.systemTemp),
        recordingEnabled: false,
        autoDiscover: false,
      );

  testWidgets('starts on the no-connection view before a device is connected',
      (WidgetTester tester) async {
    await tester.pumpWidget(testApp());
    await tester.pump(const Duration(milliseconds: 600));

    expect(find.text('SafeWay'), findsOneWidget);
    // No mock data any more: the app opens disconnected.
    expect(find.text('尚未連接裝置'), findsOneWidget);
    expect(find.text('自動搜尋裝置'), findsOneWidget);
    expect(find.widgetWithText(OutlinedButton, '手動輸入 IP'), findsOneWidget);
    // The live dashboard is not shown until connected.
    expect(find.text('傾角'), findsNothing);

    // Force the BikeAssistApp (and its sqflite-backed RideRepository) to
    // dispose now, instead of leaking its DB isolate past this test.
    await tester.pumpWidget(const SizedBox());
  });

  testWidgets('navigates to the ride history list screen', (WidgetTester tester) async {
    await tester.pumpWidget(testApp());
    await tester.pump(const Duration(milliseconds: 600));

    await tester.tap(find.byIcon(Icons.map_outlined));
    await tester.pump(); // start the page route transition
    await tester.pump(const Duration(milliseconds: 300)); // let it finish + load the ride list

    expect(find.text('歷史記錄'), findsOneWidget);

    await tester.pumpWidget(const SizedBox());
  });

  group('RideRepository', () {
    late RideRepository repository;

    setUp(() {
      repository = RideRepository(path: inMemoryDatabasePath);
    });

    tearDown(() => repository.close());

    test('records points and lists/loads a completed ride', () async {
      final rideId = await repository.startRide();
      await repository.addPoint(
        rideId,
        RoutePoint(lat: 25.0, lng: 121.0, speedKmh: 10, timestamp: DateTime(2026, 1, 1, 8)),
      );
      await repository.addPoint(
        rideId,
        RoutePoint(lat: 25.001, lng: 121.001, speedKmh: 12, timestamp: DateTime(2026, 1, 1, 8, 1)),
      );
      await repository.endRide(rideId, at: DateTime(2026, 1, 1, 8, 5));

      final rides = await repository.listRides();
      expect(rides, hasLength(1));
      expect(rides.first.isActive, isFalse);

      final points = await repository.loadPoints(rideId);
      expect(points, hasLength(2));
      expect(points.first.lat, 25.0);
    });

    test('closeOrphanRides stamps end_time using the last recorded point', () async {
      final rideId = await repository.startRide(at: DateTime(2026, 1, 1, 8));
      await repository.addPoint(
        rideId,
        RoutePoint(lat: 25.0, lng: 121.0, speedKmh: 10, timestamp: DateTime(2026, 1, 1, 8, 3)),
      );

      var rides = await repository.listRides();
      expect(rides.single.isActive, isTrue);

      await repository.closeOrphanRides();

      rides = await repository.listRides();
      expect(rides.single.isActive, isFalse);
      expect(rides.single.endTime, DateTime(2026, 1, 1, 8, 3));
    });

    test('closeOrphanRides leaves the currently-recording ride open', () async {
      final active = await repository.startRide(at: DateTime(2026, 1, 1, 9));
      final orphan = await repository.startRide(at: DateTime(2026, 1, 1, 8));
      await repository.addPoint(
        orphan,
        RoutePoint(lat: 25, lng: 121, speedKmh: 10, timestamp: DateTime(2026, 1, 1, 8, 5)),
      );

      await repository.closeOrphanRides(exceptRideId: active);

      final rides = await repository.listRides();
      expect(rides.firstWhere((r) => r.id == active).isActive, isTrue);
      expect(rides.firstWhere((r) => r.id == orphan).isActive, isFalse);
    });
  });

  testWidgets('leaving the list screen while stopped prompts to restart', (tester) async {
    final repository = RideRepository(path: inMemoryDatabasePath);
    final dataService = _FakeBikeDataService();
    final cameraSource = CameraSource();
    final frameStore = RideFrameStore(baseDir: Directory.systemTemp);
    final recorder = RideRecorder(
      dataService: dataService,
      repository: repository,
      cameraSource: cameraSource,
      frameStore: frameStore,
    );
    // recorder.isRecording defaults to false — the "manually stopped" state.
    // We deliberately avoid awaiting any sqflite call inside this testWidgets
    // body: under the fake-async clock the DB isolate's reply is never
    // delivered, so such an await would hang the whole test (10-min timeout).

    await tester.pumpWidget(
      MaterialApp(
        home: Builder(
          builder: (context) => Scaffold(
            body: Center(
              child: FilledButton(
                onPressed: () => Navigator.of(context).push(
                  MaterialPageRoute(
                    builder: (_) => RideListScreen(
                      repository: repository,
                      frameStore: frameStore,
                      recorder: recorder,
                    ),
                  ),
                ),
                child: const Text('open'),
              ),
            ),
          ),
        ),
      ),
    );

    // Bounded pumps (not pumpAndSettle): the ride-list FutureBuilder shows a
    // perpetual CircularProgressIndicator while sqflite resolves, which would
    // make pumpAndSettle never settle.
    await tester.tap(find.text('open'));
    await tester.pump(); // start the route transition
    await tester.pump(const Duration(milliseconds: 500)); // finish it

    // The status card sits above the FutureBuilder, so it renders regardless.
    expect(find.text('已停止'), findsOneWidget);

    // Leaving while stopped triggers the PopScope confirmation dialog.
    await tester.pageBack();
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 500));

    expect(find.text('尚未開始新的記錄'), findsOneWidget);
    expect(find.text('重新開始'), findsOneWidget);
    expect(find.text('先不要'), findsOneWidget);

    // Dismiss with 先不要 so no route/dialog is left pending.
    await tester.tap(find.text('先不要'));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 500));

    dataService.dispose();
    cameraSource.dispose(); // its mock camera runs a Timer.periodic
    // Close the DB via runAsync so the ffi isolate's reply is delivered and
    // the process can exit cleanly.
    await tester.runAsync(() => repository.close());
  });

  testWidgets('device setup submits SSID/password and returns the device IP', (tester) async {
    String? sentSsid;
    String? sentPassword;
    String? returnedIp;

    await tester.pumpWidget(
      MaterialApp(
        home: Builder(
          builder: (context) => Scaffold(
            body: Center(
              child: FilledButton(
                onPressed: () async {
                  returnedIp = await Navigator.of(context).push<String>(
                    MaterialPageRoute(
                      builder: (_) => DeviceWifiSetupScreen(
                        provision: (ssid, password) async {
                          sentSsid = ssid;
                          sentPassword = password;
                          return const ProvisionResult(ok: true, ip: '192.168.50.12');
                        },
                      ),
                    ),
                  );
                },
                child: const Text('open'),
              ),
            ),
          ),
        ),
      ),
    );

    await tester.tap(find.text('open'));
    await tester.pumpAndSettle();

    expect(find.text('設定裝置連線'), findsOneWidget);

    await tester.enterText(find.widgetWithText(TextField, '網路名稱 (SSID)'), 'MyHotspot');
    await tester.enterText(find.widgetWithText(TextField, '網路密碼'), 'secret123');
    await tester.tap(find.text('送出設定'));
    await tester.pumpAndSettle();

    expect(sentSsid, 'MyHotspot');
    expect(sentPassword, 'secret123');
    expect(returnedIp, '192.168.50.12'); // popped back with the device IP
    expect(find.text('設定裝置連線'), findsNothing); // setup screen closed
  });

  testWidgets('device setup shows saved message when device not yet connected', (tester) async {
    await tester.pumpWidget(
      MaterialApp(
        home: DeviceWifiSetupScreen(
          provision: (ssid, password) async =>
              const ProvisionResult(ok: true, saved: true),
        ),
      ),
    );

    await tester.enterText(find.widgetWithText(TextField, '網路名稱 (SSID)'), 'MyHotspot');
    await tester.tap(find.text('送出設定'));
    await tester.pumpAndSettle();

    expect(find.textContaining('帳密已儲存'), findsOneWidget);
  });

  group('latestIndexAtOrBefore', () {
    final times = [
      DateTime(2026, 1, 1, 8, 0, 0),
      DateTime(2026, 1, 1, 8, 0, 1),
      DateTime(2026, 1, 1, 8, 0, 2),
    ];

    test('returns -1 for an empty list', () {
      expect(latestIndexAtOrBefore([], DateTime(2026)), -1);
    });

    test('returns -1 when the target precedes every entry', () {
      expect(latestIndexAtOrBefore(times, DateTime(2026, 1, 1, 7, 59, 59)), -1);
    });

    test('returns the last index when the target is past the end', () {
      expect(latestIndexAtOrBefore(times, DateTime(2026, 1, 1, 9)), 2);
    });

    test('returns the exact index on a direct hit', () {
      expect(latestIndexAtOrBefore(times, DateTime(2026, 1, 1, 8, 0, 1)), 1);
    });

    test('returns the preceding index when the target falls between entries', () {
      expect(
        latestIndexAtOrBefore(times, DateTime(2026, 1, 1, 8, 0, 1, 500)),
        1,
      );
    });
  });

  group('RideFrameStore', () {
    late Directory tempDir;
    late RideFrameStore store;

    setUp(() async {
      tempDir = await Directory.systemTemp.createTemp('bike_frames_test');
      store = RideFrameStore(baseDir: tempDir);
    });

    tearDown(() async {
      if (await tempDir.exists()) await tempDir.delete(recursive: true);
    });

    test('saves a frame and derives its path from rideId + timestamp', () async {
      final timestamp = DateTime(2026, 1, 1, 8, 0, 0);
      await store.saveFrame(7, Uint8List.fromList([1, 2, 3]), timestamp);

      final file = await store.frameFile(7, timestamp);
      expect(await file.exists(), isTrue);
      expect(await file.readAsBytes(), [1, 2, 3]);
      expect(file.path, endsWith('${timestamp.millisecondsSinceEpoch}.jpg'));
    });

    test('deleteRideFrames removes the whole ride directory', () async {
      await store.saveFrame(7, Uint8List.fromList([1]), DateTime(2026, 1, 1, 8));
      await store.saveFrame(7, Uint8List.fromList([2]), DateTime(2026, 1, 1, 8, 0, 1));

      await store.deleteRideFrames(7);

      expect(await (await store.rideDir(7)).exists(), isFalse);
    });

    test('deleteRideFrames is a no-op when the ride has no frames', () async {
      await store.deleteRideFrames(999); // must not throw
    });
  });

  group('RideRepository frames & delete', () {
    late RideRepository repository;

    setUp(() {
      repository = RideRepository(path: inMemoryDatabasePath);
    });

    tearDown(() => repository.close());

    test('addFrames indexes timestamps and loads them in order', () async {
      final rideId = await repository.startRide();
      await repository.addFrames(rideId, [
        DateTime(2026, 1, 1, 8, 0, 2),
        DateTime(2026, 1, 1, 8, 0, 0),
        DateTime(2026, 1, 1, 8, 0, 1),
      ]);

      final frames = await repository.loadFrameTimestamps(rideId);
      expect(frames, [
        DateTime(2026, 1, 1, 8, 0, 0),
        DateTime(2026, 1, 1, 8, 0, 1),
        DateTime(2026, 1, 1, 8, 0, 2),
      ]);
    });

    test('addFrames with an empty list is a no-op', () async {
      final rideId = await repository.startRide();
      await repository.addFrames(rideId, []);
      expect(await repository.loadFrameTimestamps(rideId), isEmpty);
    });

    test('deleteRide clears the ride, its points and its frame index', () async {
      final rideId = await repository.startRide();
      await repository.addPoint(
        rideId,
        RoutePoint(lat: 25, lng: 121, speedKmh: 10, timestamp: DateTime(2026, 1, 1, 8)),
      );
      await repository.addFrames(rideId, [DateTime(2026, 1, 1, 8)]);

      await repository.deleteRide(rideId);

      expect(await repository.listRides(), isEmpty);
      expect(await repository.loadPoints(rideId), isEmpty);
      expect(await repository.loadFrameTimestamps(rideId), isEmpty);
    });

    test('deleting one ride leaves other rides intact', () async {
      final keep = await repository.startRide();
      final drop = await repository.startRide();
      await repository.addFrames(keep, [DateTime(2026, 1, 1, 8)]);
      await repository.addFrames(drop, [DateTime(2026, 1, 1, 9)]);

      await repository.deleteRide(drop);

      expect((await repository.listRides()).single.id, keep);
      expect(await repository.loadFrameTimestamps(keep), hasLength(1));
    });
  });

  group('BikeData.fromStatusJson', () {
    // A representative /api/status payload from the v3 firmware
    // (statusHandler() in bike-assist-v3/src/main.cpp).
    final status = <String, dynamic>{
      'wifi': 'sta',
      'ip': '192.168.137.34',
      'imu': {'ok': true, 'roll': 1.2, 'pitch': -0.5, 'ax': 0.01, 'ay': 0.02, 'az': 0.99, 'i2cStale': 0},
      'accel': {'event': 'BRAKE', 'g': 2.3},
      'gps': {'chars': 1234, 'fix': true, 'lat': 23.99, 'lon': 121.60, 'speed': 12.5},
      'time': {'now': '2026-09-30 11:00:00', 'source': 'gps'},
      'sd': {'ok': true},
      'camera': true,
      'night': false,
      'led': 'left',
      'hazard': true,
    };

    test('maps GPS and speed fields (lon -> lng)', () {
      final data = BikeData.fromStatusJson(status);
      expect(data.lat, 23.99);
      expect(data.lng, 121.60); // firmware sends longitude as "lon"
      expect(data.speedKmh, 12.5);
    });

    test('reads the v3 accel, attitude and device-state fields', () {
      final data = BikeData.fromStatusJson(status);
      expect(data.accelEvent, 'BRAKE');
      expect(data.isBrake, isTrue);
      expect(data.accelMagnitude, 2.3); // v3 sends "g"
      expect(data.imuOk, isTrue);
      expect(data.roll, 1.2);
      expect(data.pitch, -0.5);
      expect(data.hazardLocked, isTrue);
      expect(data.nightMode, isFalse);
      expect(data.cameraOk, isTrue);
      expect(data.sdOk, isTrue);
      expect(data.wifiMode, 'sta');
      expect(data.deviceIp, '192.168.137.34');
      expect(data.timeSource, 'gps');
      expect(data.ledDirection, 'LEFT'); // v3 "led" is a plain string
      expect(data.gpsFix, isTrue);
      expect(data.gpsChars, 1234);
    });

    test('still accepts the older accel.magnitude / led object shape', () {
      final data = BikeData.fromStatusJson(const {
        'accel': {'event': 'COLLISION', 'magnitude': 3.4},
        'led': {'direction': 'hazard', 'manual': true},
      });
      expect(data.isCollision, isTrue);
      expect(data.accelMagnitude, 3.4);
      expect(data.ledDirection, 'HAZARD');
      expect(data.ledManual, isTrue);
    });

    test('uses the firmware-chosen position from "loc" (phone first)', () {
      final data = BikeData.fromStatusJson({
        ...status,
        'gps': {'chars': 900, 'fix': false, 'lat': 0, 'lon': 0, 'speed': 0},
        'loc': {'src': 'phone', 'valid': true, 'lat': 24.01, 'lon': 121.61, 'speed': 18.2, 'phoneAgeMs': 300},
      });
      expect(data.locationSource, 'phone');
      expect(data.lat, 24.01);
      expect(data.lng, 121.61);
      expect(data.speedKmh, 18.2);
      expect(data.gpsFix, isTrue); // has a usable position
      expect(data.antennaFix, isFalse); // antenna itself still searching
    });

    test('loc with no source means no usable position', () {
      final data = BikeData.fromStatusJson({
        'gps': {'fix': false},
        'loc': {'src': 'none', 'valid': false, 'lat': 0, 'lon': 0, 'speed': 0},
      });
      expect(data.gpsFix, isFalse);
      expect(data.locationSource, 'none');
    });

    test('missing objects default numbers to 0 and extras to null', () {
      final data = BikeData.fromStatusJson(const {});
      expect(data.lat, 0);
      expect(data.speedKmh, 0);
      expect(data.accelEvent, isNull);
      expect(data.ledManual, isNull);
      expect(data.gpsFix, isNull);
      expect(data.roll, isNull);
      expect(data.hazardLocked, isNull);
    });
  });

  group('HttpStatusBikeDataService', () {
    test('polls /api/status and emits parsed BikeData', () async {
      Uri? requested;
      final client = MockClient((request) async {
        requested = request.url;
        return http.Response(
          '{"imu":{"ax":0.1,"ay":0.0,"az":1.0,"roll":5.0},'
          '"accel":{"event":"NORMAL","magnitude":1.0},'
          '"gps":{"fix":true,"lat":25.0,"lon":121.5,"speed":8.0},'
          '"led":{"direction":"NONE","manual":false}}',
          200,
        );
      });
      final service = HttpStatusBikeDataService(
        Uri.parse('http://192.168.1.42'),
        client: client,
        pollInterval: const Duration(milliseconds: 50),
      );
      addTearDown(service.dispose);

      final data = await service.stream.first;
      expect(requested.toString(), 'http://192.168.1.42/api/status');
      expect(data.lat, 25.0);
      expect(data.lng, 121.5);
      expect(data.speedKmh, 8.0);
    });

    test('forwards a non-200 response as a stream error', () async {
      final client = MockClient((request) async => http.Response('nope', 503));
      final service = HttpStatusBikeDataService(
        Uri.parse('http://192.168.1.42'),
        client: client,
        pollInterval: const Duration(milliseconds: 50),
      );
      addTearDown(service.dispose);

      await expectLater(service.stream.first, throwsA(isA<String>()));
    });
  });

  group('RideRecorder GPS filtering', () {
    late RideRepository repository;
    late _FakeBikeDataService data;
    late CameraSource cameraSource;
    late RideRecorder recorder;

    setUp(() {
      repository = RideRepository(path: inMemoryDatabasePath);
      data = _FakeBikeDataService();
      cameraSource = CameraSource();
      recorder = RideRecorder(
        dataService: data,
        repository: repository,
        cameraSource: cameraSource,
        frameStore: RideFrameStore(baseDir: Directory.systemTemp),
      );
    });

    tearDown(() async {
      recorder.dispose();
      cameraSource.dispose();
      data.dispose();
      await repository.close();
    });

    test('records fixed points but skips no-fix (0,0) samples', () async {
      await recorder.start();
      final rideId = (await repository.listRides()).first.id;

      data.emit(_bikeAt(lat: 0, lng: 0, fix: false)); // no fix → skipped
      data.emit(_bikeAt(lat: 25.0, lng: 121.0, fix: true)); // recorded
      data.emit(_bikeAt(lat: 0, lng: 0, fix: false)); // no fix → skipped
      await Future<void>.delayed(const Duration(milliseconds: 200));
      await recorder.stop();

      final points = await repository.loadPoints(rideId);
      expect(points, hasLength(1));
      expect(points.single.lat, 25.0);
      expect(points.single.lng, 121.0);
    });
  });

  group('RideExportService', () {
    late Directory tmpRoot;

    setUp(() => tmpRoot = Directory.systemTemp.createTempSync('ride_export_test'));
    tearDown(() {
      if (tmpRoot.existsSync()) tmpRoot.deleteSync(recursive: true);
    });

    test('writes a coordinate CSV and encodes a video from the frames',
        () async {
      final store = RideFrameStore(baseDir: Directory('${tmpRoot.path}/src'));
      final repo = RideRepository(path: inMemoryDatabasePath);
      addTearDown(repo.close);

      // Build a ride: two points and two camera frames.
      final start = DateTime(2026, 7, 16, 8, 30, 0);
      final rideId = await repo.startRide(at: start);
      await repo.addPoint(rideId,
          RoutePoint(lat: 25.0, lng: 121.0, speedKmh: 12, timestamp: start));
      await repo.addPoint(
          rideId,
          RoutePoint(
              lat: 25.001,
              lng: 121.001,
              speedKmh: 18,
              timestamp: start.add(const Duration(seconds: 1))));
      final frameA = start.add(const Duration(milliseconds: 100));
      final frameB = start.add(const Duration(milliseconds: 900));
      await store.saveFrame(rideId, Uint8List.fromList([1, 2, 3, 4]), frameA);
      await store.saveFrame(rideId, Uint8List.fromList([9, 8, 7]), frameB);
      await repo.addFrames(rideId, [frameA, frameB]);
      await repo.endRide(rideId, at: start.add(const Duration(seconds: 2)));

      final encoder = _FakeVideoEncoder();
      final exporter = RideExportService(
        repository: repo,
        frameStore: store,
        videoEncoder: encoder,
        workDir: Directory('${tmpRoot.path}/out'),
      );

      final export = await exporter.exportRide(rideId);

      // CSV: header + one row per point, coordinates preserved.
      final csvLines = (await export.csv.readAsString()).trim().split('\n');
      expect(csvLines.first, 'timestamp,latitude,longitude,speed_kmh');
      expect(csvLines, hasLength(3));
      expect(csvLines[1], contains('25.0000000,121.0000000,12.00'));
      expect(csvLines[2], contains('25.0010000,121.0010000,18.00'));

      // Video: encoder was asked to build it from both frames, timed from 0.
      expect(export.video, isNotNull);
      expect(await export.video!.exists(), isTrue);
      expect(encoder.lastFramePaths, hasLength(2));
      expect(encoder.lastPtsMs, [0, 800]); // relative to the first frame
      expect(export.files, [export.video, export.csv]);
    });

    test('exports CSV only (no video) when the ride has no frames', () async {
      final store = RideFrameStore(baseDir: Directory('${tmpRoot.path}/src'));
      final repo = RideRepository(path: inMemoryDatabasePath);
      addTearDown(repo.close);

      final start = DateTime(2026, 7, 16, 9);
      final rideId = await repo.startRide(at: start);
      await repo.addPoint(rideId,
          RoutePoint(lat: 24.5, lng: 120.9, speedKmh: 5, timestamp: start));
      await repo.endRide(rideId, at: start.add(const Duration(seconds: 1)));

      final encoder = _FakeVideoEncoder();
      final exporter = RideExportService(
        repository: repo,
        frameStore: store,
        videoEncoder: encoder,
        workDir: Directory('${tmpRoot.path}/out'),
      );

      final export = await exporter.exportRide(rideId);

      expect(export.video, isNull);
      expect(encoder.called, isFalse); // no frames → encoder never invoked
      expect(export.files, [export.csv]);
      expect(await export.csv.exists(), isTrue);
    });

    test('throws when the ride does not exist', () async {
      final repo = RideRepository(path: inMemoryDatabasePath);
      addTearDown(repo.close);
      final exporter = RideExportService(
        repository: repo,
        frameStore: RideFrameStore(baseDir: tmpRoot),
        videoEncoder: _FakeVideoEncoder(),
        workDir: tmpRoot,
      );

      await expectLater(
        exporter.exportRide(999),
        throwsA(isA<RideExportException>()),
      );
    });
  });

  group('FallAlert.tryParse', () {
    // Builds the exact 13-byte manufacturer payload broadcastFallenBLE emits:
    // [evt(1) | lat(float32 LE) | lon(float32 LE) | epoch(uint32 LE)].
    List<int> payload(int evt, double lat, double lon, int epoch) {
      final b = ByteData(13);
      b.setUint8(0, evt);
      b.setFloat32(1, lat, Endian.little);
      b.setFloat32(5, lon, Endian.little);
      b.setUint32(9, epoch, Endian.little);
      return b.buffer.asUint8List();
    }

    test('decodes a well-formed fall payload', () {
      final alert = FallAlert.tryParse(payload(0x01, 24.1477, 120.6736, 1752000000),
          rssi: -60);
      expect(alert, isNotNull);
      expect(alert!.lat, closeTo(24.1477, 1e-3)); // float32 precision
      expect(alert.lon, closeTo(120.6736, 1e-3));
      expect(alert.epoch, 1752000000);
      expect(alert.rssi, -60);
    });

    test('decodes the 5-minute emergency escalation (0x02)', () {
      final alert = FallAlert.tryParse(payload(0x02, 24.0, 121.0, 1752000300));
      expect(alert, isNotNull);
      expect(alert!.isEmergency, isTrue);
      expect(alert.level, 'emergency');
    });

    test('a 10-second fall (0x01) is a notice', () {
      final alert = FallAlert.tryParse(payload(0x01, 24.0, 121.0, 1752000000))!;
      expect(alert.isEmergency, isFalse);
      expect(alert.level, 'notice');
    });

    test('notice and emergency for the same fall are relayed separately', () {
      final notice = FallAlert.tryParse(payload(0x01, 24.0, 121.0, 1752000000))!;
      final emergency = FallAlert.tryParse(payload(0x02, 24.0, 121.0, 1752000000))!;
      expect(notice.dedupeKey, isNot(emergency.dedupeKey));
    });

    test('relay body matches the v3 server FallenReport model', () {
      final alert = FallAlert.tryParse(payload(0x02, 24.0, 121.0, 1752000300))!;
      final body = EmergencyRelayService.buildRelayBody(alert);
      expect(body['lat'], closeTo(24.0, 1e-3));
      expect(body['lon'], closeTo(121.0, 1e-3));
      expect(body['device_ts'], 1752000300);
      expect(body['level'], 'emergency');
    });

    test('flags a fall broadcast without a GPS fix', () {
      final alert = FallAlert.tryParse(payload(0x01, 0, 0, 1752000000))!;
      expect(alert.hasPosition, isFalse);
    });

    test('rejects an unknown event type', () {
      expect(FallAlert.tryParse(payload(0x03, 1, 2, 3)), isNull);
    });

    test('rejects a too-short payload', () {
      expect(FallAlert.tryParse([0x01, 0x00, 0x00]), isNull);
    });
  });

  group('RecordingKeepAlive.forPlatform', () {
    tearDown(() => debugDefaultTargetPlatformOverride = null);

    test('uses the Android foreground service on Android', () {
      debugDefaultTargetPlatformOverride = TargetPlatform.android;
      expect(RecordingKeepAlive.forPlatform(), isA<ForegroundServiceKeepAlive>());
    });

    test('is a no-op elsewhere, so tests and desktop never call the plugin', () {
      debugDefaultTargetPlatformOverride = TargetPlatform.linux;
      expect(RecordingKeepAlive.forPlatform(), isA<NoopKeepAlive>());
    });
  });

  group('Ride events', () {
    late RideRepository repository;
    late _FakeBikeDataService data;
    late CameraSource cameraSource;
    late RideRecorder recorder;

    setUp(() {
      repository = RideRepository(path: inMemoryDatabasePath);
      data = _FakeBikeDataService();
      cameraSource = CameraSource();
      recorder = RideRecorder(
        dataService: data,
        repository: repository,
        cameraSource: cameraSource,
        frameStore: RideFrameStore(baseDir: Directory.systemTemp),
      );
    });

    tearDown(() async {
      recorder.dispose();
      cameraSource.dispose();
      data.dispose();
      await repository.close();
    });

    BikeData sample(String event, {bool fix = true}) => BikeData(
          lat: fix ? 24.0 : 0,
          lng: fix ? 121.0 : 0,
          speedKmh: 15,
          timestamp: DateTime.now(),
          gpsFix: fix,
          accelEvent: event,
          accelMagnitude: 2.6,
        );

    test('stores each brake/collision once, on its rising edge', () async {
      await recorder.start();
      final rideId = (await repository.listRides()).first.id;

      data.emit(sample('NORMAL'));
      data.emit(sample('BRAKE'));
      data.emit(sample('BRAKE')); // same brake still showing → not stored again
      data.emit(sample('NORMAL'));
      data.emit(sample('COLLISION', fix: false)); // no fix → stored without position
      await Future<void>.delayed(const Duration(milliseconds: 200));
      await recorder.stop();

      final events = await repository.loadEvents(rideId);
      expect(events.map((e) => e.type), ['BRAKE', 'COLLISION']);
      expect(events.first.hasPosition, isTrue);
      expect(events.first.magnitude, 2.6);
      expect(events.last.hasPosition, isFalse);

      final counts = await repository.eventCounts();
      expect(counts[rideId]!.brakes, 1);
      expect(counts[rideId]!.collisions, 1);
    });

    test('deleteRide removes its events too', () async {
      final rideId = await repository.startRide();
      await repository.addEvent(
        rideId,
        RideEvent(type: RideEvent.brake, timestamp: DateTime(2026, 1, 1)),
      );
      await repository.deleteRide(rideId);
      expect(await repository.loadEvents(rideId), isEmpty);
      expect(await repository.eventCounts(), isEmpty);
    });
  });

  group('DeviceDiscovery', () {
    test('privatePrefix24 keeps private ranges only', () {
      expect(DeviceDiscovery.privatePrefix24('192.168.137.1'), '192.168.137');
      expect(DeviceDiscovery.privatePrefix24('10.4.2.9'), '10.4.2');
      expect(DeviceDiscovery.privatePrefix24('172.20.1.5'), '172.20.1');
      expect(DeviceDiscovery.privatePrefix24('172.32.1.5'), isNull);
      expect(DeviceDiscovery.privatePrefix24('8.8.8.8'), isNull);
      expect(DeviceDiscovery.privatePrefix24('nope'), isNull);
    });

    test('finds the remembered host before trying anything else', () async {
      final asked = <String>[];
      final discovery = DeviceDiscovery(
        client: MockClient((request) async {
          asked.add(request.url.host);
          return request.url.host == '192.168.137.50'
              ? http.Response('{"imu":{"ok":true},"gps":{}}', 200)
              : http.Response('no', 404);
        }),
        listInterfaces: () async => [],
      );
      final host = await discovery.find(preferred: '192.168.137.50');
      expect(host, '192.168.137.50');
      expect(asked, ['192.168.137.50']);
    });

    test('ignores web servers that are not the bike', () async {
      final discovery = DeviceDiscovery(
        client: MockClient((request) async => http.Response('{"hello":1}', 200)),
        listInterfaces: () async => [],
      );
      expect(await discovery.find(preferred: '192.168.1.1'), isNull);
    });
  });

  group('PhoneLocationRelay', () {
    test('builds the /api/phoneloc query the firmware parses', () {
      final uri = PhoneLocationRelay.buildUri(
        Uri.parse('http://192.168.137.50'),
        const PhoneFix(lat: 23.9871234, lon: 121.6012345, speedKmh: 12.34, accuracyM: 7.6),
      );
      expect(uri.path, '/api/phoneloc');
      expect(uri.queryParameters, {
        'lat': '23.987123',
        'lon': '121.601235',
        'spd': '12.3',
        'acc': '8',
      });
    });

    test('omits speed/accuracy the phone did not report', () {
      final uri = PhoneLocationRelay.buildUri(
        Uri.parse('http://bike-assist.local'),
        const PhoneFix(lat: 24, lon: 121),
      );
      expect(uri.queryParameters.keys, ['lat', 'lon']);
    });

    test('sends fixes to the device, at most once per interval', () async {
      final fixes = StreamController<PhoneFix>();
      final sent = <Uri>[];
      final relay = PhoneLocationRelay(
        source: _FakeFixSource(fixes.stream),
        client: MockClient((r) async {
          sent.add(r.url);
          return http.Response('{"ok":true,"src":"phone"}', 200);
        }),
        minInterval: const Duration(milliseconds: 300),
      );
      await relay.start(Uri.parse('http://192.168.137.50'));
      fixes.add(const PhoneFix(lat: 24.0, lon: 121.0));
      await Future<void>.delayed(const Duration(milliseconds: 50));
      fixes.add(const PhoneFix(lat: 24.1, lon: 121.1)); // inside interval → dropped
      await Future<void>.delayed(const Duration(milliseconds: 400));
      fixes.add(const PhoneFix(lat: 24.2, lon: 121.2));
      await Future<void>.delayed(const Duration(milliseconds: 50));

      expect(sent.map((u) => u.queryParameters['lat']), ['24.000000', '24.200000']);
      expect(relay.state.value, PhoneLocState.active);

      await relay.stop();
      fixes.add(const PhoneFix(lat: 25, lon: 122)); // after stop → not sent
      await Future<void>.delayed(const Duration(milliseconds: 50));
      expect(sent, hasLength(2));
      await relay.dispose();
      await fixes.close();
    });

    test('reports why phone location is unavailable', () async {
      final relay = PhoneLocationRelay(
        source: _FakeFixSource(const Stream.empty(), problem: '未允許定位權限'),
        client: MockClient((r) async => http.Response('', 200)),
      );
      await relay.start(Uri.parse('http://192.168.137.50'));
      expect(relay.state.value, PhoneLocState.error);
      expect(relay.message.value, contains('未允許定位權限'));
      await relay.dispose();
    });
  });
}

class _FakeFixSource implements PhoneFixSource {
  _FakeFixSource(this._stream, {this.problem});
  final Stream<PhoneFix> _stream;
  final String? problem;
  @override
  Future<String?> prepare() async => problem;
  @override
  Stream<PhoneFix> fixes() => _stream;
}
