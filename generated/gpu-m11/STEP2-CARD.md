# M11 step 2 - test card (compositor A1)

Image: `claude/m11-compositor`, QEMU window, copy A with
`gpu_compositor true` and `gpu_debug true` in the radeon_hd settings.

1. Sign in. Open Pulse, ActivityMonitor and Clock (Deskbar, Applications).
2. Put a Terminal (or any window) half over each of them. Wait 5 s.
3. Move that window away. The covered half must be correct at once, with
   no blank area and no delay.
4. Drag a window around and partly off the left edge, then back.
5. Resize a window from each edge, with and without the outline (a
   window with `B_OUTLINE_RESIZE`, for example the Tracker query window).
6. Switch workspace and back.
7. Say "klaar". I read the syslog from the image myself.

Pass lines (brief, step 2):

```
app_server: gpu compositor on lavapipe: no render node
app_server: compose compare ok
```

and never `compose compare MISMATCH`. Copy B (no setting): no line with
`compositor` at all.
