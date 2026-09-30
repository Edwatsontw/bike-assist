/// A brake or collision the firmware detected while a ride was recording.
class RideEvent {
  const RideEvent({
    required this.type,
    required this.timestamp,
    this.lat,
    this.lng,
    this.magnitude,
  });

  static const brake = 'BRAKE';
  static const collision = 'COLLISION';

  /// [brake] or [collision] (the firmware's `accel.event`).
  final String type;
  final DateTime timestamp;

  /// Position at the time, when the GPS had a fix.
  final double? lat;
  final double? lng;

  /// Peak G reported with the event.
  final double? magnitude;

  bool get isCollision => type == collision;
  bool get hasPosition => lat != null && lng != null;

  String get label => isCollision ? '碰撞' : '急煞';
}

/// Per-ride event totals, for the history list.
class RideEventCounts {
  const RideEventCounts({this.brakes = 0, this.collisions = 0});
  final int brakes;
  final int collisions;
  bool get isEmpty => brakes == 0 && collisions == 0;
}
