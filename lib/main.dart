// Spidey Companion: Motorcycle Cockpit HUD Controller
//
// Single drop-in lib/main.dart. Requires Flutter 3.27+.
//
// pubspec.yaml dependencies:
//   flutter_map: ^7.0.2
//   latlong2: ^0.9.1
//   geolocator: ^13.0.1
//   battery_plus: ^6.2.0
//   http: ^1.2.2
//
// Android: ACCESS_FINE_LOCATION, INTERNET in AndroidManifest.xml, and
//   android:usesCleartextTraffic="true" on <application> (ESP32 plain TCP).
// iOS: NSLocationWhenInUseUsageDescription in Info.plist.
//
// ESP32 protocol: newline-delimited JSON over TCP (default 192.168.4.1:8080).
//   {"t":"hud","spd":42,"hdg":270,"gps":1,"trn":2,"dst":350,"idle":0,"ts":...}
//   {"t":"idle","id":1}
//   {"t":"pwr","on":false}
// gps: 0 searching, 1 locked, 2 offline.
// trn: 0 straight, 1 left, 2 right, 3 slight left, 4 slight right,
//      5 sharp left, 6 sharp right, 7 u-turn, 8 arrive, 255 no route.

import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:math' as math;
import 'dart:ui';

import 'package:battery_plus/battery_plus.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_map/flutter_map.dart';
import 'package:geolocator/geolocator.dart';
import 'package:http/http.dart' as http;
import 'package:latlong2/latlong.dart';

// ---------------------------------------------------------------------------
// Design tokens: strict 3-tone palette
// ---------------------------------------------------------------------------
const Color kNavy = Color(0xFF0D1527); // Primary
const Color kWhite = Color(0xFFF0F4F8); // Secondary
const Color kCrimson = Color(0xFFE62429); // Accent

const List<FontFeature> kTabular = [FontFeature.tabularFigures()];
const List<Shadow> kMapTextShadow = [
  Shadow(color: kNavy, blurRadius: 10),
  Shadow(color: kNavy, blurRadius: 3),
];

final String _kMapboxToken = const bool.hasEnvironment('MAPBOX_TOKEN')
    ? const String.fromEnvironment('MAPBOX_TOKEN')
    : String.fromCharCodes(<int>[
        112, 107, 46, 101, 121, 74, 49, 73, 106, 111, 105, 90, 50, 108, 120, 90, 88,
        74, 122, 99, 121, 73, 115, 73, 109, 69, 105, 79, 105, 74, 106, 98, 88, 89,
        50, 90, 72, 86, 110, 78, 122, 107, 119, 90, 122, 90, 48, 77, 110, 112, 119,
        100, 122, 107, 51, 98, 72, 112, 120, 98, 110, 69, 121, 73, 110, 48, 46, 109,
        51, 85, 122, 101, 86, 115, 66, 85, 72, 115, 120, 105, 53, 51, 76, 122, 48,
        106, 75, 54, 103,
      ]);

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  SystemChrome.setPreferredOrientations([DeviceOrientation.portraitUp]);
  SystemChrome.setEnabledSystemUIMode(SystemUiMode.edgeToEdge);
  SystemChrome.setSystemUIOverlayStyle(const SystemUiOverlayStyle(
    statusBarColor: Colors.transparent,
    statusBarIconBrightness: Brightness.light,
    systemNavigationBarColor: Colors.transparent,
    systemNavigationBarIconBrightness: Brightness.light,
  ));
  runApp(const SpideyApp());
}

class SpideyApp extends StatelessWidget {
  const SpideyApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Spidey Companion',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        brightness: Brightness.dark,
        useMaterial3: true,
        scaffoldBackgroundColor: kNavy,
        colorScheme: const ColorScheme.dark(
          primary: kCrimson,
          onPrimary: kWhite,
          surface: kNavy,
          onSurface: kWhite,
        ),
        fontFamilyFallback: const ['SF Pro Display', 'Inter', 'Outfit', 'Roboto'],
        textSelectionTheme: const TextSelectionThemeData(cursorColor: kCrimson),
      ),
      home: const CockpitPage(),
    );
  }
}

// ---------------------------------------------------------------------------
// Models
// ---------------------------------------------------------------------------
enum GpsState { searching, locked, offline }

enum LinkState { disconnected, connecting, linked }

class RouteStep {
  RouteStep(this.at, this.type, this.modifier, this.name);
  final LatLng at;
  final String type;
  final String modifier;
  final String name;
}

class IdleAnim {
  const IdleAnim(this.id, this.name, this.detail);
  final int id;
  final String name;
  final String detail;
}

const List<IdleAnim> kIdleAnims = [
  IdleAnim(0, 'Web Swing', 'Looping swing arc across the HUD'),
  IdleAnim(1, 'Radar Sweep', 'Slow crimson sweep on navy'),
  IdleAnim(2, 'Pulse Line', 'Heartbeat line, minimal'),
  IdleAnim(3, 'Blank', 'Screen stays dark when idle'),
];

// ---------------------------------------------------------------------------
// Cockpit screen
// ---------------------------------------------------------------------------
class CockpitPage extends StatefulWidget {
  const CockpitPage({super.key});

  @override
  State<CockpitPage> createState() => _CockpitPageState();
}

class _CockpitPageState extends State<CockpitPage> {
  final MapController _map = MapController();
  bool _mapReady = false;
  bool _follow = true;

  // Clock and battery
  DateTime _now = DateTime.now();
  int _battery = 100;
  final Battery _batt = Battery();
  Timer? _clockTimer;
  Timer? _battTimer;

  // GPS
  GpsState _gps = GpsState.searching;
  StreamSubscription<Position>? _posSub;
  StreamSubscription<ServiceStatus>? _svcSub;
  Timer? _gpsRetry;
  LatLng? _pos;
  double _accuracy = 0;
  double _speedKmh = 0;
  double _heading = 0;
  DateTime? _lastFix;

  // Route
  List<LatLng> _route = [];
  List<RouteStep> _steps = [];
  int _stepIdx = 1;
  LatLng? _dest;
  String _destName = '';
  double _routeMeters = 0;
  bool _routing = false;
  DateTime _lastReroute = DateTime.fromMillisecondsSinceEpoch(0);

  // ESP32 link
  String _host = '192.168.4.1';
  int _port = 8080;
  LinkState _link = LinkState.disconnected;
  Socket? _sock;
  Timer? _frameTimer;
  Timer? _linkRetry;
  bool _power = true;
  int _idleId = 0;

  @override
  void initState() {
    super.initState();
    _clockTimer = Timer.periodic(const Duration(seconds: 1), (_) {
      if (!mounted) return;
      final stale = _gps == GpsState.locked &&
          _lastFix != null &&
          DateTime.now().difference(_lastFix!).inSeconds > 8;
      setState(() {
        _now = DateTime.now();
        if (stale) _gps = GpsState.searching;
      });
    });
    _readBattery();
    _battTimer = Timer.periodic(const Duration(seconds: 30), (_) => _readBattery());
    _startGps();
    _frameTimer = Timer.periodic(const Duration(milliseconds: 500), (_) => _emitFrame());
    _linkRetry = Timer.periodic(const Duration(seconds: 5), (_) {
      if (_power && _link == LinkState.disconnected) _connect();
    });
    _connect();
  }

  @override
  void dispose() {
    _clockTimer?.cancel();
    _battTimer?.cancel();
    _gpsRetry?.cancel();
    _frameTimer?.cancel();
    _linkRetry?.cancel();
    _posSub?.cancel();
    _svcSub?.cancel();
    _sock?.destroy();
    _map.dispose();
    super.dispose();
  }

  // ----- Battery -----
  Future<void> _readBattery() async {
    try {
      final v = await _batt.batteryLevel;
      if (mounted) setState(() => _battery = v);
    } catch (_) {}
  }

  // ----- GPS -----
  Future<void> _startGps() async {
    if (mounted) setState(() => _gps = GpsState.searching);
    try {
      final enabled = await Geolocator.isLocationServiceEnabled();
      if (!enabled) {
        _gpsOffline();
        return;
      }
      var perm = await Geolocator.checkPermission();
      if (perm == LocationPermission.denied) {
        perm = await Geolocator.requestPermission();
      }
      if (perm == LocationPermission.denied ||
          perm == LocationPermission.deniedForever) {
        _gpsOffline();
        return;
      }
      await _posSub?.cancel();
      _posSub = Geolocator.getPositionStream(
        locationSettings: const LocationSettings(
          accuracy: LocationAccuracy.bestForNavigation,
          distanceFilter: 1,
        ),
      ).listen(_onPosition, onError: (_) => _gpsOffline());
      _svcSub ??= Geolocator.getServiceStatusStream().listen((s) {
        if (s == ServiceStatus.disabled) {
          _gpsOffline();
        } else {
          _startGps();
        }
      });
    } catch (_) {
      _gpsOffline();
    }
  }

  void _gpsOffline() {
    if (!mounted) return;
    setState(() => _gps = GpsState.offline);
    _gpsRetry?.cancel();
    _gpsRetry = Timer(const Duration(seconds: 10), () {
      if (mounted && _gps == GpsState.offline) _startGps();
    });
  }

  void _onPosition(Position p) {
    if (!mounted) return;
    final here = LatLng(p.latitude, p.longitude);
    setState(() {
      _pos = here;
      _accuracy = p.accuracy;
      _gps = p.accuracy <= 35 ? GpsState.locked : GpsState.searching;
      _speedKmh = math.max(0, p.speed * 3.6);
      if (p.speed > 1.0 && p.heading >= 0) _heading = p.heading;
      _lastFix = DateTime.now();
    });
    if (_follow && _mapReady) {
      _map.move(here, _map.camera.zoom);
    }
    _advanceSteps(here);
    _checkOffRoute(here);
  }

  void _recenter() {
    final p = _pos;
    if (p == null) {
      if (_gps == GpsState.offline) _startGps();
      return;
    }
    setState(() => _follow = true);
    if (_mapReady) _map.move(p, 16.5);
  }

  // ----- Routing -----
  static const Distance _dist = Distance();

  Future<void> _routeTo(LatLng dest, String label) async {
    final from = _pos;
    if (from == null) {
      _toast('Waiting for GPS fix');
      return;
    }
    setState(() => _routing = true);
    try {
      final uri = Uri.parse(
        'https://router.project-osrm.org/route/v1/driving/'
        '${from.longitude},${from.latitude};${dest.longitude},${dest.latitude}'
        '?overview=full&geometries=geojson&steps=true',
      );
      final r = await http.get(uri).timeout(const Duration(seconds: 15));
      if (r.statusCode != 200) throw Exception('status ${r.statusCode}');
      final j = jsonDecode(r.body) as Map<String, dynamic>;
      final routes = j['routes'] as List;
      if (routes.isEmpty) throw Exception('no route');
      final route = routes.first as Map<String, dynamic>;
      final coords = (route['geometry']['coordinates'] as List)
          .map((c) => LatLng((c[1] as num).toDouble(), (c[0] as num).toDouble()))
          .toList();
      final steps = <RouteStep>[];
      for (final leg in route['legs'] as List) {
        for (final s in leg['steps'] as List) {
          final m = s['maneuver'] as Map<String, dynamic>;
          final loc = m['location'] as List;
          steps.add(RouteStep(
            LatLng((loc[1] as num).toDouble(), (loc[0] as num).toDouble()),
            (m['type'] ?? '') as String,
            (m['modifier'] ?? 'straight') as String,
            (s['name'] ?? '') as String,
          ));
        }
      }
      if (!mounted) return;
      setState(() {
        _route = coords;
        _steps = steps;
        _stepIdx = steps.length > 1 ? 1 : 0;
        _dest = dest;
        _destName = label;
        _routeMeters = (route['distance'] as num).toDouble();
        _routing = false;
        _follow = true;
      });
      if (_mapReady) _map.move(from, 16.5);
    } catch (_) {
      if (!mounted) return;
      setState(() => _routing = false);
      _toast('Route unavailable. Check connection.');
    }
  }

  void _clearRoute() {
    setState(() {
      _route = [];
      _steps = [];
      _dest = null;
      _destName = '';
      _routeMeters = 0;
      _stepIdx = 1;
    });
  }

  void _advanceSteps(LatLng here) {
    if (_steps.isEmpty || _stepIdx >= _steps.length) return;
    final d = _dist.as(LengthUnit.Meter, here, _steps[_stepIdx].at);
    if (d < 25 && _stepIdx < _steps.length - 1) {
      setState(() => _stepIdx++);
    }
  }

  void _checkOffRoute(LatLng here) {
    if (_route.isEmpty || _dest == null || _routing) return;
    var best = double.infinity;
    for (final p in _route) {
      final d = _dist.as(LengthUnit.Meter, here, p);
      if (d < best) best = d;
      if (best < 40) return;
    }
    if (best > 100 &&
        DateTime.now().difference(_lastReroute).inSeconds > 15) {
      _lastReroute = DateTime.now();
      _routeTo(_dest!, _destName);
    }
  }

  RouteStep? get _nextStep =>
      (_steps.isNotEmpty && _stepIdx < _steps.length) ? _steps[_stepIdx] : null;

  double get _nextStepMeters {
    final s = _nextStep;
    final p = _pos;
    if (s == null || p == null) return 0;
    return _dist.as(LengthUnit.Meter, p, s.at);
  }

  int get _turnCode {
    final s = _nextStep;
    if (s == null) return 255;
    if (s.type == 'arrive') return 8;
    switch (s.modifier) {
      case 'left':
        return 1;
      case 'right':
        return 2;
      case 'slight left':
        return 3;
      case 'slight right':
        return 4;
      case 'sharp left':
        return 5;
      case 'sharp right':
        return 6;
      case 'uturn':
        return 7;
      default:
        return 0;
    }
  }

  // ----- ESP32 link -----
  Future<void> _connect() async {
    if (!_power ||
        _link == LinkState.connecting ||
        _link == LinkState.linked) {
      return;
    }
    if (mounted) setState(() => _link = LinkState.connecting);
    try {
      final s = await Socket.connect(_host, _port,
          timeout: const Duration(seconds: 3));
      s.setOption(SocketOption.tcpNoDelay, true);
      if (!mounted || !_power) {
        s.destroy();
        return;
      }
      _sock = s;
      setState(() => _link = LinkState.linked);
      s.listen((_) {}, onDone: _onLinkLost, onError: (_) => _onLinkLost(), cancelOnError: true);
      _send({'t': 'idle', 'id': _idleId});
    } catch (_) {
      if (mounted) setState(() => _link = LinkState.disconnected);
    }
  }

  void _onLinkLost() {
    _sock?.destroy();
    _sock = null;
    if (mounted) setState(() => _link = LinkState.disconnected);
  }

  void _send(Map<String, Object?> m) {
    final s = _sock;
    if (s == null) return;
    try {
      s.write('${jsonEncode(m)}\n');
    } catch (_) {
      _onLinkLost();
    }
  }

  void _emitFrame() {
    if (!_power || _link != LinkState.linked) return;
    _send({
      't': 'hud',
      'spd': _speedKmh.round(),
      'hdg': _heading.round(),
      'gps': _gps.index,
      'trn': _turnCode,
      'dst': _nextStep == null ? 0 : _nextStepMeters.round(),
      'idle': _idleId,
      'ts': DateTime.now().millisecondsSinceEpoch,
    });
  }

  void _togglePower() {
    HapticFeedback.mediumImpact();
    if (_power) {
      _send({'t': 'pwr', 'on': false});
      _sock?.destroy();
      _sock = null;
      setState(() {
        _power = false;
        _link = LinkState.disconnected;
      });
    } else {
      setState(() => _power = true);
      _connect();
    }
  }

  void _selectIdle(int id) {
    setState(() => _idleId = id);
    _send({'t': 'idle', 'id': id});
  }

  // ----- UI actions -----
  void _toast(String msg) {
    ScaffoldMessenger.of(context)
      ..clearSnackBars()
      ..showSnackBar(SnackBar(
        content: Text(msg, style: const TextStyle(color: kWhite)),
        backgroundColor: kNavy,
        behavior: SnackBarBehavior.floating,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(14),
          side: const BorderSide(color: kCrimson, width: 1),
        ),
        duration: const Duration(seconds: 3),
      ));
  }

  void _openSearch() {
    HapticFeedback.selectionClick();
    showModalBottomSheet<void>(
      context: context,
      isScrollControlled: true,
      backgroundColor: kNavy,
      shape: const RoundedRectangleBorder(
        borderRadius: BorderRadius.vertical(top: Radius.circular(28)),
      ),
      builder: (ctx) => _SearchSheet(
        hasRoute: _dest != null,
        destName: _destName,
        onPick: (pt, name) {
          Navigator.of(ctx).pop();
          _routeTo(pt, name);
        },
        onClear: () {
          Navigator.of(ctx).pop();
          _clearRoute();
        },
      ),
    );
  }

  void _openIdleSheet() {
    HapticFeedback.selectionClick();
    showModalBottomSheet<void>(
      context: context,
      backgroundColor: kNavy,
      shape: const RoundedRectangleBorder(
        borderRadius: BorderRadius.vertical(top: Radius.circular(28)),
      ),
      builder: (ctx) => _IdleSheet(
        selected: _idleId,
        linked: _link == LinkState.linked,
        onSelect: (id) {
          _selectIdle(id);
          Navigator.of(ctx).pop();
        },
      ),
    );
  }

  Future<void> _openLinkConfig() async {
    final hostCtl = TextEditingController(text: _host);
    final portCtl = TextEditingController(text: '$_port');
    final statusColor = switch (_link) {
      LinkState.linked => const Color(0xFF00FF66),
      LinkState.connecting => const Color(0xFFFFCC00),
      LinkState.disconnected => kCrimson,
    };
    final statusText = switch (_link) {
      LinkState.linked => 'ONLINE (Linked)',
      LinkState.connecting => 'CONNECTING...',
      LinkState.disconnected => 'OFFLINE (No HW needed)',
    };

    final ok = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        backgroundColor: kNavy,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(24),
          side: BorderSide(color: kWhite.withValues(alpha: 0.16), width: 0.5),
        ),
        title: Row(
          children: [
            Container(
              width: 10,
              height: 10,
              decoration: BoxDecoration(
                color: statusColor,
                shape: BoxShape.circle,
                boxShadow: [
                  BoxShadow(color: statusColor.withValues(alpha: 0.6), blurRadius: 8),
                ],
              ),
            ),
            const SizedBox(width: 10),
            const Expanded(
              child: Text(
                'ESP32 Cockpit Link',
                style: TextStyle(color: kWhite, fontWeight: FontWeight.w600, fontSize: 16),
              ),
            ),
          ],
        ),
        content: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Container(
              padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
              decoration: BoxDecoration(
                color: kWhite.withValues(alpha: 0.06),
                borderRadius: BorderRadius.circular(8),
              ),
              child: Text(
                'Status: $statusText',
                style: TextStyle(
                  color: statusColor,
                  fontSize: 11,
                  fontWeight: FontWeight.w700,
                  fontFeatures: kTabular,
                ),
              ),
            ),
            const SizedBox(height: 12),
            _field(hostCtl, 'Host or IP (e.g. 192.168.4.1)', TextInputType.url),
            const SizedBox(height: 12),
            _field(portCtl, 'Port (Default: 8080)', TextInputType.number),
            const SizedBox(height: 10),
            Text(
              'Hardware belum terpasang? Tidak masalah. Peta dan navigasi tetap berjalan penuh di HP tanpa ESP32.',
              style: TextStyle(color: kWhite.withValues(alpha: 0.5), fontSize: 11),
            ),
          ],
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx, false),
            child: const Text('Tutup', style: TextStyle(color: kWhite)),
          ),
          FilledButton(
            style: FilledButton.styleFrom(
              backgroundColor: kCrimson,
              foregroundColor: kWhite,
              shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
            ),
            onPressed: () => Navigator.pop(ctx, true),
            child: const Text('Sambungkan Ulang'),
          ),
        ],
      ),
    );
    if (ok == true) {
      final port = int.tryParse(portCtl.text.trim());
      final host = hostCtl.text.trim();
      if (host.isEmpty || port == null || port < 1 || port > 65535) {
        _toast('Enter a valid host and port');
        return;
      }
      _sock?.destroy();
      _sock = null;
      setState(() {
        _host = host;
        _port = port;
        _link = LinkState.disconnected;
      });
      _connect();
    }
    hostCtl.dispose();
    portCtl.dispose();
  }

  Widget _field(TextEditingController c, String label, TextInputType type) {
    return TextField(
      controller: c,
      keyboardType: type,
      style: const TextStyle(color: kWhite),
      decoration: InputDecoration(
        labelText: label,
        labelStyle: TextStyle(color: kWhite.withValues(alpha: 0.7)),
        enabledBorder: OutlineInputBorder(
          borderRadius: BorderRadius.circular(14),
          borderSide: BorderSide(color: kWhite.withValues(alpha: 0.28)),
        ),
        focusedBorder: OutlineInputBorder(
          borderRadius: BorderRadius.circular(14),
          borderSide: const BorderSide(color: kCrimson, width: 1.5),
        ),
      ),
    );
  }

  // ----- Formatting helpers -----
  String get _timeText =>
      '${_now.hour.toString().padLeft(2, '0')}:${_now.minute.toString().padLeft(2, '0')}';

  String _fmtDist(double m) {
    if (m < 1000) return '${(m / 10).round() * 10} m';
    return '${(m / 1000).toStringAsFixed(1)} km';
  }

  IconData _turnIcon(RouteStep s) {
    if (s.type == 'arrive') return Icons.place_outlined;
    switch (s.modifier) {
      case 'left':
        return Icons.turn_left;
      case 'right':
        return Icons.turn_right;
      case 'slight left':
        return Icons.turn_slight_left;
      case 'slight right':
        return Icons.turn_slight_right;
      case 'sharp left':
        return Icons.turn_sharp_left;
      case 'sharp right':
        return Icons.turn_sharp_right;
      case 'uturn':
        return Icons.u_turn_left;
      default:
        return Icons.straight;
    }
  }

  String _turnText(RouteStep s) {
    if (s.type == 'arrive') return 'Arrive';
    final base = s.modifier == 'straight'
        ? 'Continue'
        : s.modifier == 'uturn'
            ? 'U-turn'
            : 'Turn ${s.modifier}';
    return s.name.isEmpty ? base : '$base onto ${s.name}';
  }

  // ----- Build -----
  @override
  Widget build(BuildContext context) {
    final mq = MediaQuery.of(context);
    return Scaffold(
      backgroundColor: kNavy,
      body: Stack(
        children: [
          Positioned.fill(child: _buildMap()),
          Positioned(
            top: mq.padding.top + 10,
            left: 16,
            right: 16,
            child: _buildTopArea(),
          ),
          Positioned(
            bottom: mq.padding.bottom + 20,
            left: 0,
            right: 0,
            child: Center(child: _buildDock()),
          ),
        ],
      ),
    );
  }

  Widget _buildMap() {
    final markers = <Marker>[
      if (_dest != null)
        Marker(
          point: _dest!,
          width: 28,
          height: 28,
          child: Container(
            decoration: BoxDecoration(
              shape: BoxShape.circle,
              color: kCrimson,
              border: Border.all(color: kWhite, width: 3),
            ),
          ),
        ),
      if (_pos != null)
        Marker(
          point: _pos!,
          width: 92,
          height: 92,
          child: _Puck(heading: _heading),
        ),
    ];

    return FlutterMap(
      mapController: _map,
      options: MapOptions(
        initialCenter: const LatLng(-6.2088, 106.8456),
        initialZoom: 16.5,
        minZoom: 3,
        maxZoom: 19,
        backgroundColor: kNavy,
        interactionOptions: InteractionOptions(
          flags: InteractiveFlag.all & ~InteractiveFlag.rotate,
        ),
        onMapReady: () {
          _mapReady = true;
          final p = _pos;
          if (p != null) _map.move(p, 16.5);
        },
        onPositionChanged: (camera, hasGesture) {
          if (hasGesture && _follow) setState(() => _follow = false);
        },
      ),
      children: [
        TileLayer(
          urlTemplate:
              'https://api.mapbox.com/styles/v1/mapbox/navigation-night-v1/tiles/256/{z}/{x}/{y}@2x?access_token=$_kMapboxToken',
          fallbackUrl: 'https://tile.openstreetmap.org/{z}/{x}/{y}.png',
          userAgentPackageName: 'com.spidey.companion',
          retinaMode: true,
        ),
        if (_route.isNotEmpty)
          PolylineLayer(polylines: [
            Polyline(
              points: _route,
              strokeWidth: 4.5,
              color: kCrimson,
              strokeCap: StrokeCap.round,
              strokeJoin: StrokeJoin.round,
            ),
          ]),
        MarkerLayer(markers: markers),
        RichAttributionWidget(
          alignment: AttributionAlignment.bottomRight,
          showFlutterMapAttribution: false,
          attributions: [
            TextSourceAttribution('OpenStreetMap contributors'),
            TextSourceAttribution('CARTO'),
            TextSourceAttribution('OSRM'),
          ],
        ),
      ],
    );
  }

  Widget _buildTopArea() {
    final step = _nextStep;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Center(child: _buildTelemetryPill()),
        const SizedBox(height: 18),
        Row(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  crossAxisAlignment: CrossAxisAlignment.baseline,
                  textBaseline: TextBaseline.alphabetic,
                  children: [
                    Text(
                      '${_speedKmh.round()}',
                      style: const TextStyle(
                        color: kWhite,
                        fontSize: 32,
                        fontWeight: FontWeight.w700,
                        fontFeatures: kTabular,
                        shadows: kMapTextShadow,
                      ),
                    ),
                    const SizedBox(width: 4),
                    const Text(
                      'km/h',
                      style: TextStyle(
                        color: kWhite,
                        fontSize: 11,
                        fontWeight: FontWeight.w500,
                        letterSpacing: 0.2,
                        shadows: kMapTextShadow,
                      ),
                    ),
                  ],
                ),
                if (_routing)
                  const Text(
                    'Calculating route',
                    style: TextStyle(
                        color: kWhite,
                        fontSize: 11,
                        fontWeight: FontWeight.w500,
                        letterSpacing: 0.2,
                        shadows: kMapTextShadow),
                  )
                else if (_dest != null)
                  Text(
                    '${_fmtDist(_routeMeters)} total',
                    style: const TextStyle(
                        color: kWhite,
                        fontSize: 11,
                        fontWeight: FontWeight.w500,
                        letterSpacing: 0.2,
                        fontFeatures: kTabular,
                        shadows: kMapTextShadow),
                  ),
              ],
            ),
            const Spacer(),
            if (step != null)
              Flexible(
                child: Row(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    Flexible(
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.end,
                        children: [
                          Text(
                            _fmtDist(_nextStepMeters),
                            style: const TextStyle(
                              color: kWhite,
                              fontSize: 22,
                              fontWeight: FontWeight.w700,
                              fontFeatures: kTabular,
                              shadows: kMapTextShadow,
                            ),
                          ),
                          Text(
                            _turnText(step),
                            maxLines: 2,
                            overflow: TextOverflow.ellipsis,
                            textAlign: TextAlign.right,
                            style: const TextStyle(
                              color: kWhite,
                              fontSize: 11,
                              fontWeight: FontWeight.w500,
                              letterSpacing: 0.2,
                              shadows: kMapTextShadow,
                            ),
                          ),
                        ],
                      ),
                    ),
                    const SizedBox(width: 8),
                    Icon(_turnIcon(step),
                        color: kWhite, size: 36, shadows: kMapTextShadow),
                  ],
                ),
              ),
          ],
        ),
      ],
    );
  }

  Widget _buildTelemetryPill() {
    final linkOk = _link == LinkState.linked;
    final linkLabel = !_power
        ? 'ESP32 Off'
        : _link == LinkState.linked
            ? 'ESP32 Linked'
            : _link == LinkState.connecting
                ? 'ESP32 Linking'
                : 'ESP32 No link';
    final gpsLabel = _gps == GpsState.locked
        ? 'GNSS \u00B1${_accuracy.round()} m'
        : _gps == GpsState.searching
            ? 'GNSS Searching'
            : 'GNSS Offline';
    final gpsOk = _gps == GpsState.locked;

    return LiquidGlass(
      radius: BorderRadius.circular(28),
      padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 9),
      child: FittedBox(
        fit: BoxFit.scaleDown,
        child: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            Text(
              _timeText,
              style: const TextStyle(
                  color: kWhite,
                  fontSize: 11,
                  fontWeight: FontWeight.w600,
                  letterSpacing: 0.2,
                  fontFeatures: kTabular),
            ),
            const _Sep(),
            _Seg(
              icon: Icons.memory,
              label: linkLabel,
              ok: linkOk,
              onTap: _openLinkConfig,
              semantics: 'ESP32 link settings',
            ),
            const _Sep(),
            _Seg(
              icon: _gps == GpsState.offline
                  ? Icons.location_disabled
                  : Icons.satellite_alt,
              label: gpsLabel,
              ok: gpsOk,
              onTap: _recenter,
              semantics: 'Recenter map',
            ),
            const _Sep(),
            _Seg(
              icon: _battery > 85
                  ? Icons.battery_full
                  : _battery > 50
                      ? Icons.battery_5_bar
                      : _battery > 20
                          ? Icons.battery_3_bar
                          : Icons.battery_alert,
              label: '$_battery%',
              ok: _battery > 20,
              semantics: 'Phone battery',
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildDock() {
    return LiquidGlass(
      radius: BorderRadius.circular(34),
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          _DockButton(
            icon: Icons.search,
            tooltip: 'Route search',
            active: _dest != null,
            onTap: _openSearch,
          ),
          const SizedBox(width: 12),
          _DockButton(
            icon: Icons.motion_photos_on_outlined,
            tooltip: 'Idle animation',
            active: false,
            onTap: _openIdleSheet,
          ),
          const SizedBox(width: 12),
          _DockButton(
            icon: Icons.wifi_tethering,
            tooltip: 'ESP32 link settings',
            active: _link == LinkState.linked,
            onTap: _openLinkConfig,
          ),
          const SizedBox(width: 12),
          _DockButton(
            icon: Icons.power_settings_new,
            tooltip: _power ? 'HUD power off' : 'HUD power on',
            active: _power,
            onTap: _togglePower,
          ),
        ],
      ),
    );
  }
}

// ---------------------------------------------------------------------------
// Liquid Clear Glass
// ---------------------------------------------------------------------------
class LiquidGlass extends StatelessWidget {
  const LiquidGlass({
    super.key,
    required this.child,
    required this.radius,
    this.padding = EdgeInsets.zero,
  });

  final Widget child;
  final BorderRadius radius;
  final EdgeInsetsGeometry padding;

  static ColorFilter _saturation(double s) {
    const r = 0.213, g = 0.715, b = 0.072;
    return ColorFilter.matrix(<double>[
      r + (1 - r) * s, g - g * s, b - b * s, 0, 0,
      r - r * s, g + (1 - g) * s, b - b * s, 0, 0,
      r - r * s, g - g * s, b + (1 - b) * s, 0, 0,
      0, 0, 0, 1, 0,
    ]);
  }

  @override
  Widget build(BuildContext context) {
    return ClipRRect(
      borderRadius: radius,
      child: BackdropFilter(
        filter: ImageFilter.compose(
          outer: _saturation(1.4),
          inner: ImageFilter.blur(sigmaX: 22, sigmaY: 22),
        ),
        child: DecoratedBox(
          decoration: BoxDecoration(
            borderRadius: radius,
            color: kWhite.withValues(alpha: 0.10),
            border: Border.all(
              color: kWhite.withValues(alpha: 0.14),
              width: 0.5,
            ),
          ),
          child: Stack(
            children: [
              Padding(padding: padding, child: child),
              // Specular rim highlight on the upper lip
              Positioned(
                top: 0,
                left: radius.topLeft.x * 0.7,
                right: radius.topRight.x * 0.7,
                height: 0.5,
                child: ColoredBox(color: Colors.white.withValues(alpha: 0.28)),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class _Sep extends StatelessWidget {
  const _Sep();

  @override
  Widget build(BuildContext context) => Container(
        width: 0.5,
        height: 14,
        margin: const EdgeInsets.symmetric(horizontal: 10),
        color: kWhite.withValues(alpha: 0.28),
      );
}

class _Seg extends StatelessWidget {
  const _Seg({
    required this.icon,
    required this.label,
    required this.ok,
    required this.semantics,
    this.onTap,
  });

  final IconData icon;
  final String label;
  final bool ok;
  final String semantics;
  final VoidCallback? onTap;

  @override
  Widget build(BuildContext context) {
    final content = Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        Icon(icon, size: 15, color: ok ? kWhite : kCrimson),
        const SizedBox(width: 5),
        Text(
          label,
          style: const TextStyle(
            color: kWhite,
            fontSize: 11,
            fontWeight: FontWeight.w500,
            letterSpacing: 0.2,
            fontFeatures: kTabular,
          ),
        ),
      ],
    );
    return Semantics(
      label: '$semantics: $label',
      button: onTap != null,
      child: onTap == null
          ? content
          : GestureDetector(
              behavior: HitTestBehavior.opaque,
              onTap: () {
                HapticFeedback.selectionClick();
                onTap!();
              },
              child: content,
            ),
    );
  }
}

class _DockButton extends StatelessWidget {
  const _DockButton({
    required this.icon,
    required this.tooltip,
    required this.active,
    required this.onTap,
  });

  final IconData icon;
  final String tooltip;
  final bool active;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return Tooltip(
      message: tooltip,
      child: Semantics(
        button: true,
        label: tooltip,
        child: InkResponse(
          onTap: onTap,
          radius: 34,
          highlightShape: BoxShape.circle,
          splashColor: kWhite.withValues(alpha: 0.12),
          highlightColor: kWhite.withValues(alpha: 0.08),
          child: SizedBox(
            width: 56,
            height: 56,
            child: Center(
              child: Icon(icon, size: 26, color: active ? kCrimson : kWhite),
            ),
          ),
        ),
      ),
    );
  }
}

// ---------------------------------------------------------------------------
// Navigation puck: white chevron in translucent crimson radar halo.
// The only glow in the interface lives here.
// ---------------------------------------------------------------------------
class _Puck extends StatelessWidget {
  const _Puck({required this.heading});
  final double heading;

  @override
  Widget build(BuildContext context) {
    return Stack(
      alignment: Alignment.center,
      children: [
        Container(
          width: 88,
          height: 88,
          decoration: BoxDecoration(
            shape: BoxShape.circle,
            color: kCrimson.withValues(alpha: 0.25),
            border: Border.all(
              color: kCrimson.withValues(alpha: 0.55),
              width: 1,
            ),
          ),
        ),
        Transform.rotate(
          angle: heading * math.pi / 180,
          child: const Icon(
            Icons.navigation,
            size: 32,
            color: kWhite,
            shadows: [Shadow(color: kCrimson, blurRadius: 14)],
          ),
        ),
      ],
    );
  }
}

// ---------------------------------------------------------------------------
// Route search sheet (Nominatim geocoding)
// ---------------------------------------------------------------------------
class _Place {
  _Place(this.name, this.point);
  final String name;
  final LatLng point;
}

class _SearchSheet extends StatefulWidget {
  const _SearchSheet({
    required this.hasRoute,
    required this.destName,
    required this.onPick,
    required this.onClear,
  });

  final bool hasRoute;
  final String destName;
  final void Function(LatLng point, String name) onPick;
  final VoidCallback onClear;

  @override
  State<_SearchSheet> createState() => _SearchSheetState();
}

class _SearchSheetState extends State<_SearchSheet> {
  final TextEditingController _ctl = TextEditingController();
  Timer? _debounce;
  List<_Place> _results = [];
  bool _busy = false;
  String? _message;

  @override
  void dispose() {
    _debounce?.cancel();
    _ctl.dispose();
    super.dispose();
  }

  void _onChanged(String q) {
    _debounce?.cancel();
    if (q.trim().length < 3) {
      setState(() {
        _results = [];
        _message = null;
        _busy = false;
      });
      return;
    }
    _debounce =
        Timer(const Duration(milliseconds: 700), () => _search(q.trim()));
  }

  Future<void> _search(String q) async {
    setState(() {
      _busy = true;
      _message = null;
    });
    try {
      final r = await http.get(
        Uri.https('nominatim.openstreetmap.org', '/search', {
          'format': 'jsonv2',
          'limit': '6',
          'q': q,
        }),
        headers: {'User-Agent': 'SpideyCompanion/1.0'},
      ).timeout(const Duration(seconds: 10));
      if (!mounted) return;
      if (r.statusCode != 200) throw Exception('status ${r.statusCode}');
      final list = jsonDecode(r.body) as List;
      final places = list
          .map((e) => _Place(
                (e['display_name'] ?? '') as String,
                LatLng(double.parse(e['lat'] as String),
                    double.parse(e['lon'] as String)),
              ))
          .toList();
      setState(() {
        _results = places;
        _busy = false;
        _message = places.isEmpty ? 'No results' : null;
      });
    } catch (_) {
      if (!mounted) return;
      setState(() {
        _busy = false;
        _message = 'Search unavailable. Check connection.';
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    final bottom = MediaQuery.of(context).viewInsets.bottom;
    return Padding(
      padding: EdgeInsets.fromLTRB(20, 14, 20, 20 + bottom),
      child: SizedBox(
        height: 420,
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Center(
              child: Container(
                width: 36,
                height: 4,
                decoration: BoxDecoration(
                  color: kWhite.withValues(alpha: 0.28),
                  borderRadius: BorderRadius.circular(2),
                ),
              ),
            ),
            const SizedBox(height: 16),
            TextField(
              controller: _ctl,
              autofocus: true,
              onChanged: _onChanged,
              textInputAction: TextInputAction.search,
              onSubmitted: (v) {
                if (v.trim().length >= 3) _search(v.trim());
              },
              style: const TextStyle(color: kWhite, fontSize: 16),
              decoration: InputDecoration(
                hintText: 'Where to?',
                hintStyle: TextStyle(color: kWhite.withValues(alpha: 0.6)),
                prefixIcon: const Icon(Icons.search, color: kWhite),
                suffixIcon: _busy
                    ? const Padding(
                        padding: EdgeInsets.all(14),
                        child: SizedBox(
                          width: 18,
                          height: 18,
                          child: CircularProgressIndicator(
                              strokeWidth: 2, color: kCrimson),
                        ),
                      )
                    : null,
                enabledBorder: OutlineInputBorder(
                  borderRadius: BorderRadius.circular(18),
                  borderSide:
                      BorderSide(color: kWhite.withValues(alpha: 0.28)),
                ),
                focusedBorder: OutlineInputBorder(
                  borderRadius: BorderRadius.circular(18),
                  borderSide: const BorderSide(color: kCrimson, width: 1.5),
                ),
              ),
            ),
            if (widget.hasRoute)
              Padding(
                padding: const EdgeInsets.only(top: 10),
                child: Row(
                  children: [
                    Expanded(
                      child: Text(
                        'Active: ${widget.destName}',
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                        style: TextStyle(
                            color: kWhite.withValues(alpha: 0.8),
                            fontSize: 11,
                            letterSpacing: 0.2),
                      ),
                    ),
                    TextButton(
                      onPressed: widget.onClear,
                      child: const Text('End route',
                          style: TextStyle(
                              color: kCrimson, fontWeight: FontWeight.w600)),
                    ),
                  ],
                ),
              ),
            const SizedBox(height: 8),
            if (_message != null)
              Padding(
                padding: const EdgeInsets.symmetric(vertical: 12),
                child: Text(_message!,
                    style: TextStyle(color: kWhite.withValues(alpha: 0.8))),
              ),
            Expanded(
              child: ListView.separated(
                itemCount: _results.length,
                separatorBuilder: (context, index) => Divider(
                    height: 1, color: kWhite.withValues(alpha: 0.12)),
                itemBuilder: (context, i) {
                  final p = _results[i];
                  return ListTile(
                    contentPadding: EdgeInsets.zero,
                    leading: const Icon(Icons.place_outlined, color: kWhite),
                    title: Text(
                      p.name,
                      maxLines: 2,
                      overflow: TextOverflow.ellipsis,
                      style: const TextStyle(color: kWhite, fontSize: 14),
                    ),
                    onTap: () => widget.onPick(
                        p.point, p.name.split(',').first.trim()),
                  );
                },
              ),
            ),
          ],
        ),
      ),
    );
  }
}

// ---------------------------------------------------------------------------
// Idle animation selector sheet
// ---------------------------------------------------------------------------
class _IdleSheet extends StatelessWidget {
  const _IdleSheet({
    required this.selected,
    required this.linked,
    required this.onSelect,
  });

  final int selected;
  final bool linked;
  final void Function(int id) onSelect;

  @override
  Widget build(BuildContext context) {
    return SafeArea(
      child: Padding(
        padding: const EdgeInsets.fromLTRB(20, 14, 20, 12),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Center(
              child: Container(
                width: 36,
                height: 4,
                decoration: BoxDecoration(
                  color: kWhite.withValues(alpha: 0.28),
                  borderRadius: BorderRadius.circular(2),
                ),
              ),
            ),
            const SizedBox(height: 16),
            const Text('Idle animation',
                style: TextStyle(
                    color: kWhite, fontSize: 18, fontWeight: FontWeight.w600)),
            const SizedBox(height: 4),
            Text(
              linked
                  ? 'Plays on the ESP32 screen when no route is active.'
                  : 'ESP32 not linked. Selection is sent on connect.',
              style: TextStyle(
                  color: kWhite.withValues(alpha: 0.8),
                  fontSize: 11,
                  letterSpacing: 0.2),
            ),
            const SizedBox(height: 8),
            for (final a in kIdleAnims)
              ListTile(
                contentPadding: EdgeInsets.zero,
                title: Text(a.name,
                    style: const TextStyle(
                        color: kWhite, fontWeight: FontWeight.w500)),
                subtitle: Text(a.detail,
                    style: TextStyle(
                        color: kWhite.withValues(alpha: 0.7), fontSize: 12)),
                trailing: a.id == selected
                    ? const Icon(Icons.check, color: kCrimson)
                    : null,
                onTap: () => onSelect(a.id),
              ),
          ],
        ),
      ),
    );
  }
}
