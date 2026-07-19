//go:build windows

// License: GPLv3 Copyright: 2024, Kovid Goyal, <kovid at kovidgoyal.net>

package utils

import (
	"context"
	"io"
	"os"
	"path/filepath"

	"golang.org/x/sys/windows"
)

// Windows has no openat/mkdirat family, so the *At operations are emulated with
// full paths built from the parent directory's name plus the child name. This
// loses the fd-relative atomicity of the Unix versions but is functionally
// equivalent for the uses in kitty (mainly the dnd kitten).

func join(dir *os.File, name string) string { return filepath.Join(dir.Name(), name) }

func MkdirAt(parentDir *os.File, name string, perm os.FileMode) error {
	if err := os.Mkdir(join(parentDir, name), perm); err != nil {
		return err
	}
	return nil
}

func OpenAt(dirFile *os.File, name string) (*os.File, error) {
	return os.Open(join(dirFile, name))
}

func OpenDirAt(dirFile *os.File, name string) (*os.File, error) {
	return os.Open(join(dirFile, name))
}

func SymlinkAt(dirFile *os.File, name, target string) error {
	return os.Symlink(target, join(dirFile, name))
}

func CreateAt(dirFile *os.File, name string, permissions os.FileMode) (*os.File, error) {
	return os.OpenFile(join(dirFile, name), os.O_RDWR|os.O_CREATE|os.O_TRUNC, permissions)
}

func CreateExclusiveAt(dirFile *os.File, name string, permissions os.FileMode) (*os.File, error) {
	return os.OpenFile(join(dirFile, name), os.O_RDWR|os.O_CREATE|os.O_EXCL, permissions)
}

func CreateDirAt(parent *os.File, name string, permissions os.FileMode) (*os.File, error) {
	if err := MkdirAt(parent, name, permissions); err != nil {
		if os.IsExist(err) {
			return OpenDirAt(parent, name)
		}
		return nil, err
	}
	return OpenDirAt(parent, name)
}

func StatAt(dirFile *os.File, name string) (os.FileInfo, error) {
	return os.Stat(join(dirFile, name))
}

func LstatAt(dirFile *os.File, name string) (os.FileInfo, error) {
	return os.Lstat(join(dirFile, name))
}

func UnlinkAt(parent *os.File, name string) error {
	return os.Remove(join(parent, name))
}

func RemoveDirAt(parent *os.File, name string) error {
	return os.Remove(join(parent, name))
}

// follow_symlinks is ignored: os.Link on Windows creates a hard link to the
// entry as named.
func LinkAt(oldparent *os.File, oldname string, newparent *os.File, newname string, follow_symlinks bool) error {
	return os.Link(join(oldparent, oldname), join(newparent, newname))
}

func ReadLinkAt(parent *os.File, name string) (string, error) {
	return os.Readlink(join(parent, name))
}

func RenameAt(old_parent *os.File, old_name string, new_parent *os.File, new_name string) error {
	return os.Rename(join(old_parent, old_name), join(new_parent, new_name))
}

// Device nodes do not exist on Windows.
func MknodAt(parent *os.File, name string, mode os.FileMode, dev uint64) error {
	return &os.PathError{Op: "mknodat", Path: join(parent, name), Err: os.ErrInvalid}
}

func DupFile(f *os.File) (*os.File, error) {
	var dup windows.Handle
	p := windows.CurrentProcess()
	if err := windows.DuplicateHandle(p, windows.Handle(f.Fd()), p, &dup, 0, true, windows.DUPLICATE_SAME_ACCESS); err != nil {
		return nil, &os.PathError{Op: "dup", Path: f.Name(), Err: err}
	}
	return os.NewFile(uintptr(dup), f.Name()), nil
}

// Remove every child of dirFile, keeping dirFile itself. Removes all it can and
// returns the first error, if any.
func RemoveChildren(dirFile *os.File) error {
	if _, err := dirFile.Seek(0, io.SeekStart); err != nil {
		return err
	}
	entries, err := dirFile.ReadDir(-1)
	if err != nil {
		return err
	}
	var firstErr error
	for _, e := range entries {
		if rerr := os.RemoveAll(join(dirFile, e.Name())); rerr != nil && firstErr == nil {
			firstErr = rerr
		}
	}
	return firstErr
}

// Unix file mode bits, for the exported ConvertFileModeToUnix helper. Windows
// does not use these, but the value is occasionally serialized (for example over
// the file transfer protocol).
const (
	s_IFDIR  = 0o040000
	s_IFLNK  = 0o120000
	s_IFIFO  = 0o010000
	s_IFSOCK = 0o140000
	s_IFCHR  = 0o020000
	s_IFBLK  = 0o060000
	s_IFREG  = 0o100000
	s_ISUID  = 0o4000
	s_ISGID  = 0o2000
	s_ISVTX  = 0o1000
)

func ConvertFileModeToUnix(goMode os.FileMode) uint32 {
	unixMode := uint32(goMode.Perm())
	switch {
	case goMode.IsDir():
		unixMode |= s_IFDIR
	case goMode&os.ModeSymlink != 0:
		unixMode |= s_IFLNK
	case goMode&os.ModeNamedPipe != 0:
		unixMode |= s_IFIFO
	case goMode&os.ModeSocket != 0:
		unixMode |= s_IFSOCK
	case goMode&os.ModeDevice != 0:
		if goMode&os.ModeCharDevice != 0 {
			unixMode |= s_IFCHR
		} else {
			unixMode |= s_IFBLK
		}
	default:
		unixMode |= s_IFREG
	}
	if goMode&os.ModeSetuid != 0 {
		unixMode |= s_ISUID
	}
	if goMode&os.ModeSetgid != 0 {
		unixMode |= s_ISGID
	}
	if goMode&os.ModeSticky != 0 {
		unixMode |= s_ISVTX
	}
	return unixMode
}

// Copy the contents of src_folder into dest_folder recursively, preserving
// permissions. This is a simpler version than the Unix one: it does not do the
// inode based symlink de-duplication, which is not reachable on Windows since
// drag and drop (the only caller) is not implemented there.
func CopyFolderContents(ctx context.Context, src_folder *os.File, dest_folder *os.File, opts CopyFolderOptions) error {
	is_ok := opts.Filter_files
	if is_ok == nil {
		is_ok = func(*os.File, os.FileInfo) bool { return true }
	}

	var recurse func(srcDir, destDir *os.File) error
	recurse = func(srcDir, destDir *os.File) error {
		if _, err := srcDir.Seek(0, io.SeekStart); err != nil {
			return err
		}
		entries, err := srcDir.ReadDir(-1)
		if err != nil {
			return err
		}
		for _, entry := range entries {
			select {
			case <-ctx.Done():
				return ctx.Err()
			default:
			}
			child, err := entry.Info()
			if err != nil {
				return err
			}
			if !is_ok(srcDir, child) {
				continue
			}
			srcPath := join(srcDir, child.Name())
			destPath := join(destDir, child.Name())
			mode := child.Mode()
			switch {
			case mode.IsDir():
				sf, err := OpenDirAt(srcDir, child.Name())
				if err != nil {
					return err
				}
				df, err := CreateDirAt(destDir, child.Name(), mode.Perm())
				if err != nil {
					sf.Close()
					return err
				}
				rerr := recurse(sf, df)
				sf.Close()
				df.Close()
				if rerr != nil {
					return rerr
				}
			case mode&os.ModeSymlink != 0:
				target, err := os.Readlink(srcPath)
				if err != nil {
					return err
				}
				_ = os.Remove(destPath)
				if err := os.Symlink(target, destPath); err != nil {
					return err
				}
			case mode.IsRegular():
				if !opts.Disallow_hardlinks && os.Link(srcPath, destPath) == nil {
					continue
				}
				sf, err := os.Open(srcPath)
				if err != nil {
					return err
				}
				df, err := os.OpenFile(destPath, os.O_RDWR|os.O_CREATE|os.O_TRUNC, mode.Perm())
				if err != nil {
					sf.Close()
					return err
				}
				if err := CopyFileAndClose(ctx, sf, df); err != nil {
					_ = os.Remove(destPath)
					return err
				}
			}
		}
		return nil
	}
	return recurse(src_folder, dest_folder)
}
