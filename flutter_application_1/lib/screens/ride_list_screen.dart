import 'package:flutter/material.dart';
import 'package:share_plus/share_plus.dart';

import '../models/ride.dart';
import '../models/ride_event.dart';
import '../services/ride_export_service.dart';
import '../services/ride_frame_store.dart';
import '../services/ride_recorder.dart';
import '../services/ride_repository.dart';
import '../services/video_encoder.dart';
import 'help_screen.dart';
import 'ride_playback_screen.dart';

/// Lists every recorded ride and lets the user start/stop the current
/// recording, replay a ride, or delete one (with its recorded footage).
/// Leaving this screen while recording is stopped prompts the user to confirm
/// whether they meant to start a new one.
class RideListScreen extends StatefulWidget {
  const RideListScreen({
    super.key,
    required this.repository,
    required this.frameStore,
    required this.recorder,
  });

  final RideRepository repository;
  final RideFrameStore frameStore;
  final RideRecorder recorder;

  @override
  State<RideListScreen> createState() => _RideListScreenState();
}

class _RideListScreenState extends State<RideListScreen> {
  /// null while the first load is in flight; the list is held in state (rather
  /// than read straight from a FutureBuilder) so a swiped ride can be removed
  /// synchronously in [Dismissible.onDismissed] — otherwise the framework
  /// asserts "A dismissed Dismissible widget is still part of the tree" and
  /// flashes a red error screen until the async reload lands.
  List<Ride>? _rides;

  /// Brake/collision totals per ride id (rides with none are absent).
  Map<int, RideEventCounts> _eventCounts = const {};

  late final RideExportService _exporter = RideExportService(
    repository: widget.repository,
    frameStore: widget.frameStore,
    videoEncoder: MethodChannelVideoEncoder(),
  );

  @override
  void initState() {
    super.initState();
    _load();
  }

  Future<void> _load() async {
    // Close any ride orphaned by a crash / force-kill so it never lingers as
    // "記錄中", but leave the one that's genuinely recording right now open.
    final activeId =
        widget.recorder.isRecording.value ? widget.recorder.currentRideId : null;
    await widget.repository.closeOrphanRides(exceptRideId: activeId);
    final rides = await widget.repository.listRides();
    final counts = await widget.repository.eventCounts();
    if (!mounted) return;
    setState(() {
      _rides = rides;
      _eventCounts = counts;
    });
  }

  Future<void> _toggleRecording() async {
    if (widget.recorder.isRecording.value) {
      await widget.recorder.stop();
    } else {
      await widget.recorder.start();
    }
    await _load();
  }

  Future<bool> _confirmDelete(Ride ride) async {
    final choice = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('刪除這筆記錄?'),
        content: Text(
          '${_formatDateTime(ride.startTime)} 的路線與該趟錄下的影像都會被刪除,無法復原。',
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.of(context).pop(false),
            child: const Text('取消'),
          ),
          FilledButton(
            style: FilledButton.styleFrom(
              backgroundColor: Theme.of(context).colorScheme.error,
            ),
            onPressed: () => Navigator.of(context).pop(true),
            child: const Text('刪除'),
          ),
        ],
      ),
    );
    return choice ?? false;
  }

  /// Removes the ride from the visible list *synchronously* (so the Dismissible
  /// is satisfied), then deletes its rows and recorded frame files in the
  /// background. Frames are deleted after the database rows so a failure mid-way
  /// leaves orphaned files (harmless) rather than rows pointing at missing
  /// images. If the delete fails, the list is reloaded to resync.
  Future<void> _deleteRide(int rideId) async {
    setState(() => _rides?.removeWhere((ride) => ride.id == rideId));
    try {
      await widget.repository.deleteRide(rideId);
      await widget.frameStore.deleteRideFrames(rideId);
    } catch (_) {
      await _load();
    }
  }

  /// Exports a ride into a video (MP4, built from its camera frames) and a
  /// coordinate CSV, then hands both to the system share sheet so the user can
  /// save them to the gallery/Files or send them on. A ride with no recorded
  /// footage exports the CSV alone.
  Future<void> _exportRide(Ride ride) async {
    _showProgress('正在整理影片與座標…');
    RideExport export;
    try {
      export = await _exporter.exportRide(ride.id);
    } catch (error) {
      if (!mounted) return;
      Navigator.of(context).pop(); // dismiss progress
      _showMessage('匯出失敗:$error');
      return;
    }
    if (!mounted) return;
    Navigator.of(context).pop(); // dismiss progress before the share sheet
    await SharePlus.instance.share(
      ShareParams(
        files: [
          for (final file in export.files)
            XFile(
              file.path,
              mimeType: file.path.endsWith('.mp4') ? 'video/mp4' : 'text/csv',
            ),
        ],
        subject: '${_formatDateTime(ride.startTime)} 的騎乘記錄',
      ),
    );
    if (mounted && export.video == null) {
      _showMessage('這趟沒有錄到影像,只匯出座標 CSV');
    }
  }

  void _showProgress(String message) {
    showDialog<void>(
      context: context,
      barrierDismissible: false,
      builder: (_) => PopScope(
        canPop: false,
        child: AlertDialog(
          content: Row(
            children: [
              const CircularProgressIndicator(),
              const SizedBox(width: 20),
              Expanded(child: Text(message)),
            ],
          ),
        ),
      ),
    );
  }

  void _showMessage(String message) {
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(message)));
  }

  Future<bool> _confirmLeaveWithoutRestarting() async {
    final choice = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('尚未開始新的記錄'),
        content: const Text('目前記錄已停止,要重新開始記錄嗎?'),
        actions: [
          TextButton(
            onPressed: () => Navigator.of(context).pop(false),
            child: const Text('先不要'),
          ),
          FilledButton(
            onPressed: () => Navigator.of(context).pop(true),
            child: const Text('重新開始'),
          ),
        ],
      ),
    );
    return choice ?? false;
  }

  @override
  Widget build(BuildContext context) {
    return PopScope(
      canPop: widget.recorder.isRecording.value,
      onPopInvokedWithResult: (didPop, result) async {
        if (didPop) return;
        final shouldRestart = await _confirmLeaveWithoutRestarting();
        if (shouldRestart) {
          await widget.recorder.start();
          await _load();
        }
        if (context.mounted) {
          Navigator.of(context).pop();
        }
      },
      child: Scaffold(
        appBar: AppBar(
          title: const Text('歷史記錄'),
          actions: [
            IconButton(
              tooltip: '使用說明',
              icon: const Icon(Icons.help_outline),
              onPressed: () => Navigator.of(context).push(
                MaterialPageRoute(builder: (_) => const HelpScreen()),
              ),
            ),
          ],
        ),
        body: Column(
          children: [
            _RecordingStatusCard(
              recorder: widget.recorder,
              onToggle: _toggleRecording,
            ),
            Expanded(
              child: Builder(
                builder: (context) {
                  final rides = _rides;
                  if (rides == null) {
                    return const Center(child: CircularProgressIndicator());
                  }
                  if (rides.isEmpty) {
                    return const Center(child: Text('還沒有任何記錄'));
                  }
                  return ListView.builder(
                    itemCount: rides.length,
                    itemBuilder: (context, index) {
                      final ride = rides[index];
                      final tile = ListTile(
                        leading: Icon(
                          ride.isActive ? Icons.fiber_manual_record : Icons.route,
                          color: ride.isActive
                              ? Theme.of(context).colorScheme.error
                              : null,
                        ),
                        title: Text(_formatDateTime(ride.startTime)),
                        subtitle: Text(
                          ride.isActive
                              ? '記錄中'
                              : _subtitle(ride, _eventCounts[ride.id]),
                        ),
                        // The in-progress ride can't be exported (it has no end
                        // time yet); every finished ride gets a share action.
                        trailing: ride.isActive
                            ? null
                            : IconButton(
                                tooltip: '匯出',
                                icon: const Icon(Icons.ios_share),
                                onPressed: () => _exportRide(ride),
                              ),
                        onTap: () async {
                          await Navigator.of(context).push(
                            MaterialPageRoute(
                              builder: (_) => RidePlaybackScreen(
                                rideId: ride.id,
                                repository: widget.repository,
                                frameStore: widget.frameStore,
                              ),
                            ),
                          );
                          await _load();
                        },
                      );

                      // The in-progress ride can't be deleted out from under
                      // the recorder — stop it first.
                      if (ride.isActive) return tile;

                      return Dismissible(
                        key: ValueKey(ride.id),
                        direction: DismissDirection.endToStart,
                        background: Container(
                          alignment: Alignment.centerRight,
                          padding: const EdgeInsets.only(right: 24),
                          color: Theme.of(context).colorScheme.error,
                          child: Icon(
                            Icons.delete,
                            color: Theme.of(context).colorScheme.onError,
                          ),
                        ),
                        confirmDismiss: (_) => _confirmDelete(ride),
                        onDismissed: (_) => _deleteRide(ride.id),
                        child: tile,
                      );
                    },
                  );
                },
              ),
            ),
          ],
        ),
      ),
    );
  }

  String _formatDateTime(DateTime time) =>
      '${time.year}/${time.month.toString().padLeft(2, '0')}/${time.day.toString().padLeft(2, '0')} '
      '${time.hour.toString().padLeft(2, '0')}:${time.minute.toString().padLeft(2, '0')}';

  String _subtitle(Ride ride, RideEventCounts? counts) {
    final parts = <String>[_formatDuration(ride.duration)];
    if (counts != null && counts.brakes > 0) parts.add('急煞 ${counts.brakes}');
    if (counts != null && counts.collisions > 0) parts.add('碰撞 ${counts.collisions}');
    return parts.join(' · ');
  }

  String _formatDuration(Duration duration) {
    final minutes = duration.inMinutes.remainder(60);
    final hours = duration.inHours;
    final seconds = duration.inSeconds.remainder(60);
    if (hours > 0) {
      return '$hours 小時 $minutes 分';
    }
    if (minutes > 0) {
      return '$minutes 分 $seconds 秒';
    }
    return '$seconds 秒';
  }
}

class _RecordingStatusCard extends StatelessWidget {
  const _RecordingStatusCard({
    required this.recorder,
    required this.onToggle,
  });

  final RideRecorder recorder;
  final Future<void> Function() onToggle;

  @override
  Widget build(BuildContext context) {
    return ValueListenableBuilder<bool>(
      valueListenable: recorder.isRecording,
      builder: (context, isRecording, _) {
        return Card(
          margin: const EdgeInsets.all(16),
          child: ListTile(
            leading: Icon(
              isRecording ? Icons.fiber_manual_record : Icons.stop_circle_outlined,
              color: isRecording ? Colors.red : null,
            ),
            title: Text(isRecording ? '記錄中' : '已停止'),
            trailing: FilledButton(
              onPressed: onToggle,
              child: Text(isRecording ? '結束記錄' : '開始記錄'),
            ),
          ),
        );
      },
    );
  }
}
