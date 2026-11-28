# Code signing policy

This page says how I mean to build and sign the releases of win-kitty, the native Windows port of kitty, so that Windows and security software can tell they come from this repository. Releases are not signed yet. Once they are, every signed release is made the way this page describes.

## Team roles

* Committers and reviewers: [ecstra](https://github.com/ecstra). I write the changes, and a change from anyone else comes as a pull request that I review before it lands.
* Approvers: [ecstra](https://github.com/ecstra). I approve each release's signing myself.

## How a signed release is made

1. The release's changes land on `master` through reviewed branches.
2. GitHub Actions builds the programs and the installer from that commit, on runners GitHub hosts.
3. The build goes to the signing service, and I approve it there.
4. The signed installer is attached to the release on GitHub.

## What gets signed

* `kitty.exe`, `kitty-console.exe` and `kitten.exe`.
* The two small programs in `bin` that put `kitty` and `kitten` on your PATH.
* The installer, `kitty-setup.exe`.
* The package that adds Open in kitty to the Windows 11 right-click menu.

kitty also ships libraries from MSYS2 and the Slang shader compiler, which come from their own projects, and I do not sign them.

Every signed file names its product, win-kitty, and its version in its file details.

## Privacy

This program will not transfer any information to other networked systems unless specifically requested by the user or the person installing or operating it.
