# Handover: cursor jumping and flicker under pwsh

Under pwsh the cursor blinks, shifts and jumps while the shell redraws its prompt
line. The same shell under msys does not, because msys goes through the Cygwin
pty bridge. This is the original problem the bridge was built for, showing up on
the one path the bridge cannot cover.

## What is established

- conhost re-renders the child's output on its own frame cadence, consumes the
  synchronized update markers an application sends, and can split one logical
  redraw across frames. That is recorded at the top of
  `shell-integration/msys2/pty-bridge.py` and is the reason the bridge exists.
- pwsh is a native Windows program, so kitty runs it on a ConPTY. Every byte
  passes through conhost, the markers are gone before kitty sees them, and kitty
  can paint a frame partway through a redraw.
- Compared side by side: msys shows the artifact rarely, pwsh shows it plus
  flashing. That ordering is what the explanation above predicts.
- Setting `input_delay` to 10 does not fix it, so simply waiting longer for the
  rest of a redraw is not the answer on its own.

## What is ruled out

- Not the acrylic work. It reproduces with `background_blur 0`.
- Not the pty bridge. pwsh never touches it.
- Not `cursor_trail`. That option explains a different symptom, the two cursors
  seen while holding backspace across a line wrap, and is working as designed.

## Where the code is

- `kitty/child.py`, `cygwin_pty_bridge_cmd` and the branch near line 511. This is
  where kitty picks between the bridge and a ConPTY. The choice is made once, at
  spawn, from the exe about to run.
- `fast_data_types.open_pty` is the ConPTY creation itself.

## Leads worth checking first

- Whether the ConPTY can be created in a mode that passes VT through rather than
  re-rendering it. Windows has added flags in this area since ConPTY first
  shipped, so check what the current SDK exposes and what the installed Windows
  build actually honours.
- Whether kitty can hold a frame back while a control sequence is only partly
  parsed, so a split redraw is never painted half applied. This is a renderer
  change and needs scoping before anyone starts on it.

## One caution

The request that opened this work recalled an earlier assessment that the pwsh
case is easier and better scoped than the msys one. That assessment is not in the
session that produced this note and has not been verified. Re-derive the scope
rather than relying on it.
