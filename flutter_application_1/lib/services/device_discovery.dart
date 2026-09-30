import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:http/http.dart' as http;
import 'package:shared_preferences/shared_preferences.dart';

/// Finds the bike device on the local network without the user typing an IP.
///
/// Order tried (first answer wins):
///  1. the host that worked last time (remembered),
///  2. `bike-assist.local` (the firmware registers this mDNS name),
///  3. `192.168.4.1` (the firmware's own setup hotspot),
///  4. a /24 sweep of each private IPv4 network the phone is on — covers the
///     usual setup where the phone *is* the hotspot and the ESP32 got a random
///     DHCP address from it.
///
/// A host counts as the device only if `GET /api/status` returns JSON with an
/// `imu` or `gps` object, so a random web server on the LAN isn't mistaken
/// for the bike.
class DeviceDiscovery {
  DeviceDiscovery({
    http.Client? client,
    this.probeTimeout = const Duration(milliseconds: 1500),
    this.sweepTimeout = const Duration(milliseconds: 900),
    this.sweepConcurrency = 32,
    Future<List<NetworkInterface>> Function()? listInterfaces,
  })  : _client = client ?? http.Client(),
        _listInterfaces = listInterfaces ??
            (() => NetworkInterface.list(
                  type: InternetAddressType.IPv4,
                  includeLoopback: false,
                ));

  static const kMdnsHost = 'bike-assist.local';
  static const kSetupApHost = '192.168.4.1';
  static const _kLastHost = 'last_device_host';

  final http.Client _client;
  final Duration probeTimeout;
  final Duration sweepTimeout;
  final int sweepConcurrency;
  final Future<List<NetworkInterface>> Function() _listInterfaces;

  bool _cancelled = false;

  /// Stops an in-flight [find] at the next checkpoint.
  void cancel() => _cancelled = true;

  static Future<String?> loadLastHost() async {
    final prefs = await SharedPreferences.getInstance();
    return prefs.getString(_kLastHost);
  }

  static Future<void> saveLastHost(String host) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(_kLastHost, host);
  }

  /// Returns the host (IP or name, optionally `:port`) of a responding device,
  /// or null if none was found. [onProgress] receives short status lines for
  /// the UI.
  Future<String?> find({
    String? preferred,
    bool sweep = true,
    void Function(String message)? onProgress,
  }) async {
    _cancelled = false;
    final direct = <String>{
      if (preferred != null && preferred.trim().isNotEmpty) preferred.trim(),
      kMdnsHost,
      kSetupApHost,
    };
    for (final host in direct) {
      if (_cancelled) return null;
      onProgress?.call('嘗試 $host');
      if (await probe(host, probeTimeout)) return host;
    }
    if (!sweep) return null;

    final List<NetworkInterface> interfaces;
    try {
      interfaces = await _listInterfaces();
    } catch (_) {
      return null;
    }
    for (final iface in interfaces) {
      if (_isCellular(iface.name)) continue;
      for (final addr in iface.addresses) {
        final prefix = privatePrefix24(addr.address);
        if (prefix == null) continue;
        if (_cancelled) return null;
        onProgress?.call('掃描 $prefix.x');
        final hit = await _sweep(prefix, exclude: addr.address);
        if (hit != null) return hit;
      }
    }
    return null;
  }

  Future<String?> _sweep(String prefix, {required String exclude}) async {
    final hosts = [
      for (var i = 1; i <= 254; i++)
        if ('$prefix.$i' != exclude) '$prefix.$i',
    ];
    for (var start = 0; start < hosts.length; start += sweepConcurrency) {
      if (_cancelled) return null;
      final end = (start + sweepConcurrency).clamp(0, hosts.length);
      final batch = hosts.sublist(start, end);
      final results = await Future.wait(
        batch.map((h) async => await probe(h, sweepTimeout) ? h : null),
      );
      for (final r in results) {
        if (r != null) return r;
      }
    }
    return null;
  }

  /// True if [host] answers `/api/status` like the bike firmware does.
  Future<bool> probe(String host, Duration timeout) async {
    try {
      final uri = Uri.parse('http://$host/api/status');
      final response = await _client.get(uri).timeout(timeout);
      if (response.statusCode != 200) return false;
      final json = jsonDecode(utf8.decode(response.bodyBytes));
      return json is Map && (json.containsKey('imu') || json.containsKey('gps'));
    } catch (_) {
      return false;
    }
  }

  /// `a.b.c` for a private-range IPv4 address, else null.
  static String? privatePrefix24(String ip) {
    final parts = ip.split('.');
    if (parts.length != 4) return null;
    final o = parts.map(int.tryParse).toList();
    if (o.any((v) => v == null || v < 0 || v > 255)) return null;
    final a = o[0]!, b = o[1]!;
    final isPrivate = a == 10 ||
        (a == 172 && b >= 16 && b <= 31) ||
        (a == 192 && b == 168);
    if (!isPrivate) return null;
    return '${o[0]}.${o[1]}.${o[2]}';
  }

  /// Mobile-data interfaces often carry 10.x carrier addresses; sweeping them
  /// is pointless and slow.
  static bool _isCellular(String name) {
    final n = name.toLowerCase();
    return n.startsWith('rmnet') ||
        n.startsWith('ccmni') ||
        n.startsWith('pdp') ||
        n.startsWith('v4-') ||
        n.startsWith('clat');
  }

  void dispose() => _client.close();
}
