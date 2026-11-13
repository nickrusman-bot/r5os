# M11 step 1 - test card (R5 decorator)

Image: test build of `claude/m11-decorator` (`R5_DECORATOR_ABI_TEST=1`), QEMU window.
Paste back the lines marked **paste**.

1. **Try R5**, Sign in, open Terminal (Deskbar, Applications).
2. **Pick R5:** Settings > Personalization > Appearance, tab Look and feel,
   Decorator: **R5**.
   - The windows get a 1-pixel line, Haiku's yellow tab, no resize knob.
3. **Resize:** on the Terminal window, drag each edge and each corner from just
   outside the line (about 4 px out). The resize cursor shows before you click.
4. **Scroll bar:** drag the Terminal scroll bar at the right edge. It scrolls, it
   does not resize.
5. **Not resizable:** Settings > System > Information... or About R5: no resize
   cursor outside its line.
6. **Move:** drag the tab: the window moves.
7. **Old add-on:** Decorator: **R5DecoratorAbiTest**. The look does not change
   (Default stays). Then pick **R5** again.
8. **Default:** Decorator: **Default**; dragging its border also resizes now.
9. Terminal:

```
grep -E "decorator: r5 loaded|decorator .* refused" /var/log/syslog
```

**paste** the output. Expected:

```
decorator: r5 loaded
app_server: decorator R5DecoratorAbiTest refused: abi 0, need 1
```

**paste** also: what did not work in steps 2-8 (or "all ok"), and a screenshot of
a Terminal window with the R5 frame.
