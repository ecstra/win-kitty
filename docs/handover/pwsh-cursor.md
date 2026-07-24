# Handover: cursor jumping and flicker under pwsh

Under pwsh the cursor blinks, shifts and jumps while the shell redraws its prompt
line. It happens while typing, while holding backspace, and while moving along an
existing line. Confirmed pwsh only. cmd does not do it, and msys has not done it
since kitty stopped putting msys shells on a ConPTY at all.

Two rounds of investigation have gone into this. The second closed off both of
the leads the first one left open, and did not produce a fix, so what follows is
mostly a record of what the answer is not.

## What is established

- conhost re-renders the child's output on its own frame cadence, consumes the
  synchronized update markers an application sends, and can split one logical
  redraw across frames. That is recorded at the top of
  `shell-integration/msys2/pty-bridge.py` and is the reason the bridge exists.
- pwsh is a native Windows program, so kitty runs it on a ConPTY. Every byte
  passes through conhost and the markers are gone before kitty sees them.
- conhost does in fact split every PSReadLine repaint, and the split is
  measurable. Measured by spawning a shell on a pseudoconsole, typing a scripted
  sequence into it, and timestamping every read from the output pipe;
  `kitty/wincompat/conpty_poc.c` is the same skeleton and is the quickest thing
  to copy if this needs redoing. pwsh emits the cursor hide alone, then the text
  and the cursor show about ten milliseconds later:

      [4] 2655.34ms   9 bytes: \e[m\e[?25l
      [5] 2664.40ms  25 bytes: \e[93m\x08ab    \e[2;41H\e[?25h

  Over 80 repaints at held-key rate: min 8.27ms, mean 9.70ms, max 12.85ms.
- cmd never splits. One byte per keystroke, `\x08 \x08` per backspace, cursor
  visibility untouched. This matches the symptom being pwsh only, and is the one
  place where the split theory and the observed behaviour agree.

## What is ruled out

- **ConPTY passthrough mode.** `PSEUDOCONSOLE_PASSTHROUGH_MODE`, which is 0x8 and
  needs build 22621 or later, is accepted on build 26200 and changes nothing. The
  probe's output at flags=8 is byte for byte identical to flags=0 across 55
  writes with the timestamps stripped. This was the first lead in the previous
  version of this note. It is dead.
- **`input_delay`.** kitty holds unparsed input for `input_delay` and only then
  hands it to the parser (`vt-parser.c:1528`), so a large enough value should
  rejoin anything conhost split. 25ms, which is double the largest gap measured
  above, does not change the symptom at all. 10ms did not either.
- Not the acrylic work. It reproduces with `background_blur 0`.
- Not the pty bridge. pwsh never touches it.
- Not `cursor_trail`. That option explains a different symptom, the two cursors
  seen while holding backspace across a line wrap, and is working as designed.

## The contradiction to resolve first

The split repaint is real and it is pwsh only, which is exactly the shape of the
bug. But covering it with `input_delay` does nothing, and that value is large
enough that it should have. So one of these is true, and finding out which is the
first job for whoever picks this up:

1. The split is not what the user is seeing, and it is a coincidence that both
   are pwsh only. Something else in the pwsh path produces the flicker.
2. `input_delay` does not coalesce the way `run_worker` reads, on this platform
   or on this path, so the split has never actually been tested against.

Two is cheap to check and worth doing before anything else: instrument
`run_worker` to log how many writes each parse consumed and how far apart they
arrived, then hold backspace under pwsh and read it back. If parses are already
consuming both halves together at the default `input_delay`, the split is
innocent and one is the answer.

## A constraint on any fix

Added input latency is not acceptable, so a fix that works by waiting longer is
out even if waiting longer turns out to work. That rules out shipping a larger
`input_delay` default on Windows.

## Where the code is

- `kitty/child.c`, `open_pty` and `spawn`. `CreatePseudoConsole` is called at
  `child.c:401` with a flags argument of 0, which is where a passthrough
  experiment would go if anyone wants to repeat it.
- `kitty/child.py`, the branch near line 511, picks between the bridge and a
  ConPTY. The choice is made once, at spawn, from the exe about to run.
- `kitty/vt-parser.c`, `run_worker` and `vt_parser_commit_write`, is the
  deferral. `new_input_at` is stamped on the first unconsumed byte and cleared
  after a parse.
