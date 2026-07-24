### The native Windows port

This fork carries the native Windows port, which lives at
https://github.com/ecstra/win-kitty and is not part of upstream kitty.

Anything about the Windows build belongs here, not on the upstream tracker.
That means bug reports, feature requests, and pull requests that touch the
Windows code, the installer, the pty bridge, or the Windows documentation.
Upstream cannot act on any of it, because none of this code is upstream, so
filing it there only costs the maintainer time.

Open a pull request against this fork. The port is young and has plenty of
bugs, so reports are useful even when they are rough. Say which Windows
version you are on, which shell you were running, and whether the shell was a
Windows one such as pwsh or cmd or an MSYS2 or Cygwin one, since those take
completely different paths through the code. See
[docs/windows-port.md](docs/windows-port.md) for what is known to be missing
before reporting, and [docs/windows-internals.md](docs/windows-internals.md)
if you want to work on it.

Bugs that reproduce on Linux or macOS as well are upstream bugs, and those do
belong on the upstream tracker below.

### Reporting bugs

Please first search existing bug reports (especially closed ones) for a report
that matches your issue.

When reporting a bug, provide full details of your environment, that means, at
a minimum, kitty version, OS and OS version, kitty config (ideally a minimal
config to reproduce the issue with).

Note that bugs and feature requests are often closed quickly as they are either
fixed or deemed wontfix/invalid. In my experience, this is the only scalable way to
manage a bug tracker. Feel free to continue to post to a closed bug report
if you would like to discuss the issue further. Being closed does not mean you
will not get any more responses.

### Contributing code

Install [the dependencies](https://sw.kovidgoyal.net/kitty/build/#dependencies)
using your favorite package manager. Build and run kitty [from
source](https://sw.kovidgoyal.net/kitty/build/#install-and-run-from-source).

Make a fork, submit your Pull Request. If it's a large/controversial change, open an issue
beforehand to discuss it, so that you don't waste your time making a pull
request that gets rejected.

If the code you are submitting is reasonably easily testable, please contribute
tests as well (see the `kitty_tests/` sub-directory for existing tests, which
can be run with `./test.py`).

That's it.
