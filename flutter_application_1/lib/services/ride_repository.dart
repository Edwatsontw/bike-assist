import 'package:path/path.dart' show join;
import 'package:sqflite/sqflite.dart';

import '../models/ride.dart';
import '../models/ride_event.dart';
import '../models/route_point.dart';

/// Persists recorded rides (and their GPS/speed points) to a local SQLite
/// database via sqflite, so multiple ride recordings can accumulate across
/// app sessions.
class RideRepository {
  RideRepository({String? path}) : _explicitPath = path;

  final String? _explicitPath;
  Database? _db;

  Future<Database> get _database async {
    return _db ??= await _open();
  }

  Future<Database> _open() async {
    final path = _explicitPath ?? join(await getDatabasesPath(), 'bike_assist_rides.db');
    return openDatabase(
      path,
      version: 3,
      onCreate: (db, version) async {
        await db.execute('''
          CREATE TABLE rides (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            start_time TEXT NOT NULL,
            end_time TEXT
          )
        ''');
        await db.execute('''
          CREATE TABLE ride_points (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            ride_id INTEGER NOT NULL,
            lat REAL NOT NULL,
            lng REAL NOT NULL,
            speed_kmh REAL NOT NULL,
            timestamp TEXT NOT NULL
          )
        ''');
        await _createFramesTable(db);
        await _createEventsTable(db);
      },
      onUpgrade: (db, oldVersion, newVersion) async {
        // v1 → v2 added camera frame recording. Rides recorded before the
        // upgrade simply have no frames, which playback handles.
        if (oldVersion < 2) {
          await _createFramesTable(db);
        }
        // v2 → v3 added brake/collision events. Older rides have none.
        if (oldVersion < 3) {
          await _createEventsTable(db);
        }
      },
    );
  }

  /// Index of recorded camera frames. Only the timestamp is stored — the image
  /// bytes live on disk (see [RideFrameStore]) and the path is derived, since
  /// absolute paths can change between app installs.
  static Future<void> _createFramesTable(Database db) async {
    await db.execute('''
      CREATE TABLE ride_frames (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        ride_id INTEGER NOT NULL,
        timestamp TEXT NOT NULL
      )
    ''');
    await db.execute(
      'CREATE INDEX idx_ride_frames_ride_time ON ride_frames (ride_id, timestamp)',
    );
  }

  /// Brake/collision events detected during a ride. Position is nullable:
  /// an event without a GPS fix is still worth keeping on the timeline.
  static Future<void> _createEventsTable(Database db) async {
    await db.execute('''
      CREATE TABLE ride_events (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        ride_id INTEGER NOT NULL,
        type TEXT NOT NULL,
        lat REAL,
        lng REAL,
        magnitude REAL,
        timestamp TEXT NOT NULL
      )
    ''');
    await db.execute(
      'CREATE INDEX idx_ride_events_ride_time ON ride_events (ride_id, timestamp)',
    );
  }

  Future<void> addEvent(int rideId, RideEvent event) async {
    final db = await _database;
    await db.insert('ride_events', {
      'ride_id': rideId,
      'type': event.type,
      'lat': event.lat,
      'lng': event.lng,
      'magnitude': event.magnitude,
      'timestamp': event.timestamp.toIso8601String(),
    });
  }

  Future<List<RideEvent>> loadEvents(int rideId) async {
    final db = await _database;
    final rows = await db.query(
      'ride_events',
      where: 'ride_id = ?',
      whereArgs: [rideId],
      orderBy: 'timestamp ASC',
    );
    return rows
        .map((row) => RideEvent(
              type: row['type'] as String,
              timestamp: DateTime.parse(row['timestamp'] as String),
              lat: (row['lat'] as num?)?.toDouble(),
              lng: (row['lng'] as num?)?.toDouble(),
              magnitude: (row['magnitude'] as num?)?.toDouble(),
            ))
        .toList();
  }

  /// Brake/collision totals for every ride that has any, keyed by ride id.
  Future<Map<int, RideEventCounts>> eventCounts() async {
    final db = await _database;
    final rows = await db.rawQuery('''
      SELECT ride_id,
             SUM(CASE WHEN type = 'BRAKE' THEN 1 ELSE 0 END) AS brakes,
             SUM(CASE WHEN type = 'COLLISION' THEN 1 ELSE 0 END) AS collisions
      FROM ride_events GROUP BY ride_id
    ''');
    return {
      for (final row in rows)
        row['ride_id'] as int: RideEventCounts(
          brakes: (row['brakes'] as num?)?.toInt() ?? 0,
          collisions: (row['collisions'] as num?)?.toInt() ?? 0,
        ),
    };
  }

  Future<int> startRide({DateTime? at}) async {
    final db = await _database;
    return db.insert('rides', {
      'start_time': (at ?? DateTime.now()).toIso8601String(),
      'end_time': null,
    });
  }

  Future<void> endRide(int rideId, {DateTime? at}) async {
    final db = await _database;
    await db.update(
      'rides',
      {'end_time': (at ?? DateTime.now()).toIso8601String()},
      where: 'id = ?',
      whereArgs: [rideId],
    );
  }

  Future<void> addPoint(int rideId, RoutePoint point) async {
    final db = await _database;
    await db.insert('ride_points', {
      'ride_id': rideId,
      'lat': point.lat,
      'lng': point.lng,
      'speed_kmh': point.speedKmh,
      'timestamp': point.timestamp.toIso8601String(),
    });
  }

  /// Reads a single ride's metadata, or null if it doesn't exist. Used by
  /// export, which needs the start/end times alongside the points and frames.
  Future<Ride?> loadRide(int rideId) async {
    final db = await _database;
    final rows = await db.query('rides', where: 'id = ?', whereArgs: [rideId], limit: 1);
    if (rows.isEmpty) return null;
    final row = rows.first;
    return Ride(
      id: row['id'] as int,
      startTime: DateTime.parse(row['start_time'] as String),
      endTime: row['end_time'] == null
          ? null
          : DateTime.parse(row['end_time'] as String),
    );
  }

  /// Inserts an imported ride and all of its points in one transaction, under a
  /// fresh auto-increment id (the exported id is not reused — it could collide
  /// with a ride already on this device). Returns the new ride id so the caller
  /// can write the frame files and index under it. Frame index rows are added
  /// separately via [addFrames], only for frames whose image actually landed.
  Future<int> insertImportedRide({
    required DateTime startTime,
    DateTime? endTime,
    required List<RoutePoint> points,
  }) async {
    final db = await _database;
    return db.transaction((txn) async {
      final rideId = await txn.insert('rides', {
        'start_time': startTime.toIso8601String(),
        'end_time': endTime?.toIso8601String(),
      });
      final batch = txn.batch();
      for (final point in points) {
        batch.insert('ride_points', {
          'ride_id': rideId,
          'lat': point.lat,
          'lng': point.lng,
          'speed_kmh': point.speedKmh,
          'timestamp': point.timestamp.toIso8601String(),
        });
      }
      await batch.commit(noResult: true);
      return rideId;
    });
  }

  Future<List<Ride>> listRides() async {
    final db = await _database;
    final rows = await db.query('rides', orderBy: 'start_time DESC');
    return rows
        .map((row) => Ride(
              id: row['id'] as int,
              startTime: DateTime.parse(row['start_time'] as String),
              endTime: row['end_time'] == null
                  ? null
                  : DateTime.parse(row['end_time'] as String),
            ))
        .toList();
  }

  Future<List<RoutePoint>> loadPoints(int rideId) async {
    final db = await _database;
    final rows = await db.query(
      'ride_points',
      where: 'ride_id = ?',
      whereArgs: [rideId],
      orderBy: 'timestamp ASC',
    );
    return rows
        .map((row) => RoutePoint(
              lat: row['lat'] as double,
              lng: row['lng'] as double,
              speedKmh: row['speed_kmh'] as double,
              timestamp: DateTime.parse(row['timestamp'] as String),
            ))
        .toList();
  }

  /// Indexes a batch of recorded frame timestamps in one transaction. Frames
  /// arrive at up to 15/s, so [RideRecorder] buffers them and flushes here
  /// rather than issuing an insert per frame.
  Future<void> addFrames(int rideId, List<DateTime> timestamps) async {
    if (timestamps.isEmpty) return;
    final db = await _database;
    final batch = db.batch();
    for (final timestamp in timestamps) {
      batch.insert('ride_frames', {
        'ride_id': rideId,
        'timestamp': timestamp.toIso8601String(),
      });
    }
    await batch.commit(noResult: true);
  }

  /// Timestamps of every frame recorded for [rideId], oldest first. Combine
  /// with [RideFrameStore.frameFile] to get the image on disk.
  Future<List<DateTime>> loadFrameTimestamps(int rideId) async {
    final db = await _database;
    final rows = await db.query(
      'ride_frames',
      columns: ['timestamp'],
      where: 'ride_id = ?',
      whereArgs: [rideId],
      orderBy: 'timestamp ASC',
    );
    return rows.map((row) => DateTime.parse(row['timestamp'] as String)).toList();
  }

  /// Removes the ride and all of its points, events and frame index rows. The frame
  /// image files are deleted separately via [RideFrameStore.deleteRideFrames].
  Future<void> deleteRide(int rideId) async {
    final db = await _database;
    await db.transaction((txn) async {
      await txn.delete('ride_frames', where: 'ride_id = ?', whereArgs: [rideId]);
      await txn.delete('ride_events', where: 'ride_id = ?', whereArgs: [rideId]);
      await txn.delete('ride_points', where: 'ride_id = ?', whereArgs: [rideId]);
      await txn.delete('rides', where: 'id = ?', whereArgs: [rideId]);
    });
  }

  /// Closes out any ride left with a null `end_time` from a previous session
  /// that never got a clean shutdown (e.g. the process was killed outright).
  /// Each orphan is stamped with the timestamp of its last recorded point
  /// (or its own start time if it has no points at all).
  ///
  /// [exceptRideId] is left open — pass the ride that is *currently* recording
  /// so it keeps showing as in-progress; every other open ride is closed.
  Future<void> closeOrphanRides({int? exceptRideId}) async {
    final db = await _database;
    final orphans = await db.query('rides', where: 'end_time IS NULL');
    for (final row in orphans) {
      final rideId = row['id'] as int;
      if (rideId == exceptRideId) continue;
      final lastPoint = await db.query(
        'ride_points',
        where: 'ride_id = ?',
        whereArgs: [rideId],
        orderBy: 'timestamp DESC',
        limit: 1,
      );
      final endTime = lastPoint.isEmpty
          ? DateTime.parse(row['start_time'] as String)
          : DateTime.parse(lastPoint.first['timestamp'] as String);
      await db.update(
        'rides',
        {'end_time': endTime.toIso8601String()},
        where: 'id = ?',
        whereArgs: [rideId],
      );
    }
  }

  Future<void> close() async {
    final db = _db;
    if (db != null) {
      await db.close();
      _db = null;
    }
  }
}
