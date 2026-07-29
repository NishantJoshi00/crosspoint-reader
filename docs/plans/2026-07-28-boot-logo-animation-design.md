# Boot Logo Animation — Design

Date: 2026-07-28
Status: Implemented

## Visual

Two-phase boot mark replacing the Logo120 + wordmark splash. The boot screen
shows nothing but the logo, centered.

- **Phase 1 (splash):** white downward-pointing equilateral triangle with a
  2 px ink border, centered on an otherwise blank panel.
- **Phase 2 (handoff):** a black triangle — same size, initially hidden
  exactly beneath the white one — rotates 60° about the shared centroid,
  ending pointing upward. The final frame reads as a hexagram: the black
  triangle's points protrude around the bordered white one (mockup
  variant A).

SVG mockups reviewed and approved (rotation frames, e-ink stepped preview,
variants) before implementation.

## Geometry

- Circumradius R = 52 px (centroid → vertex); side ≈ 90 px; fits the former
  120×120 logo slot.
- Pivot: shared centroid at screen center.
- Border: outer fill at R, paper knockout at R − 2·border (border measured
  perpendicular to the edges).
- Rotation: 4 frames × 15° → 60°. A ▽ rotated 60° is a △, so no translation
  or scaling is involved.

## E-ink realization

Frames render as full-buffer fast refreshes; `displayBuffer(FAST_REFRESH)`
blocks on the panel BUSY line, which paces the animation naturally. Triangles
are scanline-filled via `fillRect` spans (no polygon primitive in
GfxRenderer). The animation runs synchronously at the end of `setup()` —
after the splash painted, before the first real activity replaces it — and
only on `BootResume::Splash` (never on silent reboots, quick-resume wakes,
recovery mode, or panic-report boots).
