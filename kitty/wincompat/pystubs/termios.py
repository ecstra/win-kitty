# Minimal 'termios' module for Windows. The real module is Unix only. On Windows
# the pseudoconsole owns line discipline, so tc* are no-ops and tcgetattr returns
# a neutral attribute list. Constants are conventional values, unused here.


class error(Exception):
    pass


# tcsetattr actions
TCSANOW = 0
TCSADRAIN = 1
TCSAFLUSH = 2

# tcflush queues
TCIFLUSH = 0
TCOFLUSH = 1
TCIOFLUSH = 2

# indices into the attribute list returned by tcgetattr
IFLAG, OFLAG, CFLAG, LFLAG, ISPEED, OSPEED, CC = 0, 1, 2, 3, 4, 5, 6

# c_iflag bits
IGNBRK, BRKINT, IGNPAR, PARMRK, INPCK, ISTRIP = 0x1, 0x2, 0x4, 0x8, 0x10, 0x20
INLCR, IGNCR, ICRNL, IXON, IXANY, IXOFF, IUTF8 = 0x40, 0x80, 0x100, 0x400, 0x800, 0x1000, 0x4000
# c_oflag bits
OPOST, ONLCR = 0x1, 0x4
# c_cflag bits
CSIZE, CS8, CREAD, PARENB, CLOCAL, HUPCL = 0x30, 0x30, 0x80, 0x100, 0x800, 0x400
# c_lflag bits
ISIG, ICANON, ECHO, ECHOE, ECHOK, ECHONL, NOFLSH, IEXTEN = 0x1, 0x2, 0x8, 0x10, 0x20, 0x40, 0x80, 0x8000

# c_cc indices
VINTR, VQUIT, VERASE, VKILL, VEOF, VTIME, VMIN = 0, 1, 2, 3, 4, 5, 6
VSTART, VSTOP, VSUSP, VREPRINT, VWERASE, VLNEXT = 8, 9, 10, 12, 14, 15

# baud rates
B0, B9600, B38400, B115200 = 0, 9600, 38400, 115200


def tcgetattr(fd: int) -> list:
    return [0, 0, 0, 0, 0, 0, [b'\x00'] * 32]


def tcsetattr(fd: int, when: int, attributes: list) -> None:
    return None


def tcflush(fd: int, queue: int) -> None:
    return None


def tcdrain(fd: int) -> None:
    return None


def tcsendbreak(fd: int, duration: int) -> None:
    return None
