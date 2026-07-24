<p align="center">
  <img
    width="700"
    alt="kitty for Windows"
    src="logo/header.png" />
</p>

<p align="center">
  <br>
  <a href="https://github.com/ecstra/win-kitty/actions/workflows/ci.yml"><img alt="CI" src="https://github.com/ecstra/win-kitty/actions/workflows/ci.yml/badge.svg"></a>
  <a href="LICENSE"><img alt="GPLv3 licence" src="https://img.shields.io/badge/licence-GPLv3-yellow.svg"></a>
  <a href="https://github.com/ecstra/win-kitty/releases"><img alt="Windows 11" src="https://img.shields.io/badge/windows-11-0078D6.svg"></a>
  <a href="https://sw.kovidgoyal.net/kitty/"><img alt="kitty 0.48.0" src="https://img.shields.io/badge/kitty-0.48.0-brightgreen.svg"></a>
  <br><br>
</p>

## Overview

kitty is a fast, GPU based terminal. This fork runs it natively on Windows, with no WSL, no X server, and no Cygwin layer under the terminal itself.

Windows console programs run through the pseudoconsole API. MSYS2 and Cygwin shells run on a real Cygwin pty instead, which is what keeps zsh from bouncing and flickering the way it does through conhost. Everything is drawn with OpenGL, the same rendering path kitty uses on every other platform.

Built for Windows 11, which is where all of it is developed and tested. The acrylic and the rounded title bar lean on composition features Windows 10 does not have, so those will not look right there, but nothing blocks you from installing it and the terminal itself should still run. Try it if you like. There is no support for Windows 10 and no plan to add any.

> [!IMPORTANT]
> This is a fork of [kitty](https://github.com/kovidgoyal/kitty), and none of the Windows work is upstream. Report anything about this build [here](https://github.com/ecstra/win-kitty/issues) rather than on the upstream tracker, since upstream cannot act on it.
>
> This was built so I could use kitty as my daily terminal on Windows, and in this state it does that for me. That is the whole promise. It is young and it has bugs, the features below are the ones used enough to trust, and anything off that path is likely to be rough. I may add features, port more of upstream, or fix more bugs in my free time. I am not committing to any of it, and there is no roadmap.

## Features

* **Genuine Windows acrylic, taken straight from how Windows Terminal does it:** not a blur approximation. It follows the same WinUI `AcrylicBrush` recipe, the real noise texture, the blurred backdrop, the luminosity and tint blends, assembled here as a composition effect graph. Because kitty owns that graph instead of asking DWM for a backdrop, the material keeps its blur and transparency when the window loses focus, which is exactly where DWM's own version gives up and goes flat. `background_opacity` is scaled to match, so a number means the same thing it means in Windows Terminal. Measured against Terminal on the same wallpaper it lands within half a percent.
* **A title bar kitty draws itself:** the caption shares the terminal background, with rounded corners and its own minimize, maximize and close buttons, so the whole window is one surface instead of kitty wearing a grey Windows hat.
* **Any shell, each on the right plumbing:** pwsh, Windows PowerShell and cmd run on a pseudoconsole. MSYS2 and Cygwin shells run on a real Cygwin pty through a bridge written for this port, which keeps conhost out of the data path entirely. That is what stops zsh bouncing and flickering the way it does in every terminal that pipes it through ConPTY. Shell integration for each.
* **Images in the terminal:** icat and the graphics protocol work. conhost eats the escape sequences that carry image data, so the port routes them around it through a private named pipe and places the image with unicode placeholders.
* **Kittens:** icat, hints, unicode_input, diff, themes, choose-files, ask and the confirmation prompts, all running as the real Go kittens inside kitty.
* **Remote control:** `kitty @` works over `listen_on tcp:`, so scripting kitty from outside works the same as it does elsewhere.
* **Native toasts:** desktop notifications are real Windows notifications, attributed to kitty.
* **Explorer integration:** an **Open in kitty** entry in the main Windows 11 menu, which needs a signed MSIX package rather than a registry key, and in the classic menu too.
* **Snappy input:** the port asks Windows for a 1 ms timer resolution, which is where most of the typing latency was hiding, and renders at your monitor's real refresh rate rather than an assumed 60 Hz.
* **The rest of kitty:** tabs and OS windows, scrollback, mouse selection, the Windows clipboard, ligatures and fonts, per monitor DPI scaling, config reloading, and dropping files onto a window.

## Install

Download `kitty-setup.exe` from the [latest release](https://github.com/ecstra/win-kitty/releases/latest) and run it. It asks for administrator rights, installs to `C:\Program Files\kitty`, and offers to put kitty on your PATH, add the Explorer menu entry, and create a desktop shortcut.

Nothing else is needed. Python, every MinGW DLL and the terminfo are bundled, and each build is smoke tested against a cleaned PATH so a missing dependency fails the build rather than you.

To install without prompts:

```
kitty-setup.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART
```

## Configuration

kitty reads its config from `%USERPROFILE%\.config\kitty\kitty.conf`, which
expands to:

```
C:\Users\<user>\.config\kitty\kitty.conf
```

Note that this is `.config` in your home folder, the same place kitty uses on
Linux, and not `%APPDATA%`. The folder is not created for you. Make it yourself,
or set `KITTY_CONFIG_DIRECTORY` to keep the config somewhere else.

A working config is in [example/.config/kitty](example/.config/kitty). Copy
`kitty.conf` and the `kitty-themes` folder beside it into your own
`.config\kitty` and you have the setup in the screenshots, MSYS2 zsh included.
The comments in it cover the options where Windows behaves differently from
Linux or macOS, so you can tell which ones are safe to change.

Config reloading works, so you can edit the file and see the result without
restarting.

## What is missing

* **Remote control over a Unix socket:** `listen_on unix:...`. Windows has carried AF_UNIX since build 17063, but CPython does not build it ([python/cpython#77589](https://github.com/python/cpython/issues/77589)), so the socket cannot be created. `kitty @` itself works over `listen_on tcp:...`.
* **Four kittens:** dnd, panel, quick-access-terminal and desktop-ui. They are marked in the `kitten` command list and refuse cleanly.
* **Dragging content out** of a window. Dropping onto one works.
* **Some console invocations:** the `kitty +something` entry points and the full `kitty --help` list when run from another terminal.
* **Emoji at an MSYS2 zsh prompt:** typing or pasting one shows its pieces instead, as `<d83d>` or similar. Cygwin stores a wide character in 16 bits, so anything above U+FFFF needs two of them and zsh's line editor assumes one. This is zsh on Cygwin rather than anything kitty does, and it reproduces in Windows Terminal with the same shell. Only the prompt is affected. Emoji in program output, in file names and in kitten interfaces are all fine, and bash on the same install handles them correctly.

Kittens deliberately refuse to start outside kitty on Windows, because they depend on kitty's own input and output path. Inside kitty they work. [docs/windows-port.md](docs/windows-port.md) has the full list, including what is simply untested.

## Build

You need the MSYS2 mingw64 toolchain, its build of Python 3, and Go. From git-bash or an MSYS2 shell:

```bash
./packaging/windows/make-installer.sh
```

That one command builds everything and produces `dist/kitty-setup.exe`. CI runs the same build on every push, so a green run always has an installer attached.

## Documentation

* [example/.config/kitty](example/.config/kitty): a working config, commented for Windows.
* [docs/windows-port.md](docs/windows-port.md): installing, building, the Windows defaults, and what is not there yet.
* [docs/windows-internals.md](docs/windows-internals.md): how each Windows piece works, from the pty bridge to the acrylic.
* [docs/decisions.md](docs/decisions.md): why the choices were made, including the ones that were reversed.
* [CONTRIBUTING.md](CONTRIBUTING.md): where to report things, and what to include.

## Credits

kitty is created and maintained by [Kovid Goyal](https://github.com/kovidgoyal). This fork only adds the Windows port, by **ecstra**. Everything that makes kitty good is his work, and the upstream documentation at [sw.kovidgoyal.net/kitty](https://sw.kovidgoyal.net/kitty/) applies here too.

Licensed under GPLv3, the same as upstream.
