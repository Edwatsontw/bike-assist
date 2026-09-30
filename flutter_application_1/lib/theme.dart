import 'package:flutter/material.dart';

/// SafeWay's minimal palette: near-black ink on a cool grey page, one accent
/// per meaning (amber = caution, red = danger, green = OK).
class SwColors {
  static const ink = Color(0xFF0F1722);
  static const amber = Color(0xFFF5A300);
  static const red = Color(0xFFE5484D);
  static const green = Color(0xFF1F9D63);
}

ThemeData buildSafeWayTheme(Brightness brightness) {
  final dark = brightness == Brightness.dark;
  final page = dark ? const Color(0xFF0D131A) : const Color(0xFFF4F6F9);
  final surface = dark ? const Color(0xFF151D26) : Colors.white;
  final line = dark ? const Color(0xFF26313D) : const Color(0xFFE3E8EE);
  final ink = dark ? const Color(0xFFE8EDF3) : SwColors.ink;
  final muted = dark ? const Color(0xFF8B98A7) : const Color(0xFF5B6B7C);

  final scheme = ColorScheme.fromSeed(
    seedColor: SwColors.ink,
    brightness: brightness,
  ).copyWith(
    primary: ink,
    onPrimary: page,
    secondary: ink,
    onSecondary: page,
    surface: surface,
    onSurface: ink,
    onSurfaceVariant: muted,
    outline: muted,
    outlineVariant: line,
    surfaceContainerLowest: surface,
    surfaceContainerLow: surface,
    surfaceContainer: surface,
    surfaceContainerHigh: page,
    surfaceContainerHighest: page,
    surfaceTint: Colors.transparent,
    error: SwColors.red,
    onError: Colors.white,
  );

  final shape = RoundedRectangleBorder(borderRadius: BorderRadius.circular(10));

  return ThemeData(
    useMaterial3: true,
    brightness: brightness,
    colorScheme: scheme,
    scaffoldBackgroundColor: page,
    cardTheme: CardThemeData(
      color: surface,
      elevation: 0,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(14),
        side: BorderSide(color: line),
      ),
    ),
    dividerTheme: DividerThemeData(color: line, space: 1, thickness: 1),
    filledButtonTheme: FilledButtonThemeData(
      style: FilledButton.styleFrom(
        backgroundColor: ink,
        foregroundColor: page,
        shape: shape,
        textStyle: const TextStyle(fontWeight: FontWeight.w700),
      ),
    ),
    outlinedButtonTheme: OutlinedButtonThemeData(
      style: OutlinedButton.styleFrom(
        foregroundColor: ink,
        side: BorderSide(color: line, width: 1.5),
        shape: shape,
        textStyle: const TextStyle(fontWeight: FontWeight.w700),
      ),
    ),
    textButtonTheme: TextButtonThemeData(
      style: TextButton.styleFrom(foregroundColor: ink),
    ),
    snackBarTheme: const SnackBarThemeData(behavior: SnackBarBehavior.floating),
  );
}

/// Section label used above cards: small, bold, muted, letter-spaced.
class SectionLabel extends StatelessWidget {
  const SectionLabel(this.text, {super.key, this.trailing});

  final String text;
  final Widget? trailing;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Padding(
      padding: const EdgeInsets.only(bottom: 10),
      child: Row(
        children: [
          Expanded(
            child: Text(
              text,
              style: theme.textTheme.labelMedium?.copyWith(
                fontWeight: FontWeight.w700,
                letterSpacing: 0.6,
                color: theme.colorScheme.onSurfaceVariant,
              ),
            ),
          ),
          ?trailing,
        ],
      ),
    );
  }
}

/// A flat bordered panel — the one container style used across the app.
class Panel extends StatelessWidget {
  const Panel({super.key, required this.child, this.padding = const EdgeInsets.all(16)});

  final Widget child;
  final EdgeInsetsGeometry padding;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return Container(
      padding: padding,
      decoration: BoxDecoration(
        color: scheme.surface,
        borderRadius: BorderRadius.circular(14),
        border: Border.all(color: scheme.outlineVariant),
      ),
      child: child,
    );
  }
}
