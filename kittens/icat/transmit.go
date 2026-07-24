// License: GPLv3 Copyright: 2022, Kovid Goyal, <kovid at kovidgoyal.net>

package icat

import (
	"bytes"
	"crypto/rand"
	"encoding/binary"
	"fmt"
	"github.com/kovidgoyal/kitty"
	"io"
	"math"
	not_rand "math/rand/v2"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"github.com/kovidgoyal/go-shm"
	"github.com/kovidgoyal/kitty/tools/tui"
	"github.com/kovidgoyal/kitty/tools/tui/graphics"
	"github.com/kovidgoyal/kitty/tools/tui/loop"
	"github.com/kovidgoyal/kitty/tools/utils"
	"github.com/kovidgoyal/kitty/tools/utils/images"
)

var _ = fmt.Print

// On Windows, conhost (ConPTY) re-renders the shell's output and drops the kitty
// graphics protocol APC, so it never reaches kitty. When windows_gfx_bypass is set
// (see main), the graphics commands are accumulated here and sent straight to kitty
// over a named pipe instead of stdout; the image is placed with unicode
// placeholders, which are plain text and pass through conhost untouched.
var windows_gfx_bypass bool
var gfx_buf bytes.Buffer

// graphics_writer returns where a graphics command should be written: the bypass
// buffer on Windows, otherwise stdout.
func graphics_writer() io.StringWriter {
	if windows_gfx_bypass {
		return &gfx_buf
	}
	return os.Stdout
}

// send_graphics_to_kitty delivers accumulated graphics command bytes to kitty over
// its per-process named pipe, prefixed with the target window id, bypassing conhost.
func send_graphics_to_kitty(data []byte) error {
	pid := os.Getenv("KITTY_PID")
	wid := os.Getenv("KITTY_WINDOW_ID")
	if pid == "" || wid == "" {
		return fmt.Errorf("KITTY_PID/KITTY_WINDOW_ID not set; cannot reach kitty graphics pipe")
	}
	window_id, err := strconv.ParseUint(wid, 10, 64)
	if err != nil {
		return fmt.Errorf("invalid KITTY_WINDOW_ID %q: %w", wid, err)
	}
	pipe_name := `\\.\pipe\kitty-gfx-` + pid
	var f *os.File
	for attempt := 0; attempt < 100; attempt++ {
		if f, err = os.OpenFile(pipe_name, os.O_WRONLY, 0); err == nil {
			break
		}
		time.Sleep(10 * time.Millisecond) // pipe momentarily has no free instance between server accepts
	}
	if err != nil {
		return fmt.Errorf("failed to connect to kitty graphics pipe: %w", err)
	}
	defer f.Close()
	var header [8]byte
	binary.LittleEndian.PutUint64(header[:], window_id)
	if _, err = f.Write(header[:]); err != nil {
		return err
	}
	_, err = f.Write(data)
	return err
}

type passthrough_type int

const (
	no_passthrough passthrough_type = iota
	tmux_passthrough
)

func new_graphics_command(imgd *image_data) *graphics.GraphicsCommand {
	gc := graphics.GraphicsCommand{}
	switch imgd.passthrough_mode {
	case tmux_passthrough:
		gc.WrapPrefix = "\033Ptmux;"
		gc.WrapSuffix = "\033\\"
		gc.EncodeSerializedDataFunc = func(x string) string { return strings.ReplaceAll(x, "\033", "\033\033") }
	}
	return &gc
}

func gc_for_image(imgd *image_data, frame_num int, frame *image_frame) *graphics.GraphicsCommand {
	gc := new_graphics_command(imgd)
	gc.SetDataWidth(uint64(frame.width)).SetDataHeight(uint64(frame.height))
	gc.SetQuiet(graphics.GRT_quiet_silent)
	gc.SetFormat(frame.transmission_format)
	if imgd.image_number != 0 {
		gc.SetImageNumber(imgd.image_number)
	}
	if imgd.image_id != 0 {
		gc.SetImageId(imgd.image_id)
	}
	if frame_num == 0 {
		gc.SetAction(graphics.GRT_action_transmit_and_display)
		if imgd.use_unicode_placeholder {
			gc.SetUnicodePlaceholder(graphics.GRT_create_unicode_placeholder)
			gc.SetColumns(uint64(imgd.width_cells))
			gc.SetRows(uint64(imgd.height_cells))
		}
		if imgd.cell_x_offset > 0 {
			gc.SetXOffset(uint64(imgd.cell_x_offset))
		}
		if z_index != 0 {
			gc.SetZIndex(z_index)
		}
		if place != nil {
			gc.SetCursorMovement(graphics.GRT_cursor_static)
		}
	} else {
		gc.SetAction(graphics.GRT_action_frame)
		gc.SetGap(int32(frame.delay_ms))
		gc.SetCompositionMode(utils.IfElse(frame.replace, graphics.Overwrite, graphics.AlphaBlend))
		if frame.compose_onto > 0 {
			gc.SetOverlaidFrame(uint64(frame.compose_onto))
		}
		gc.SetLeftEdge(uint64(frame.left)).SetTopEdge(uint64(frame.top))
	}
	return gc
}

func transmit_shm(imgd *image_data, frame_num int, frame *image_frame) (err error) {
	var mmap shm.MMap
	var data_size int64
	if frame.in_memory_bytes == nil {
		f, err := os.Open(frame.filename)
		if err != nil {
			return fmt.Errorf("Failed to open image data output file: %s with error: %w", frame.filename, err)
		}
		defer f.Close()
		if data_size, err = f.Seek(0, io.SeekEnd); err != nil {
			return fmt.Errorf("Failed to seek in image data output file: %s with error: %w", frame.filename, err)
		}
		if _, err = f.Seek(0, io.SeekStart); err != nil {
			return fmt.Errorf("Failed to seek in image data output file: %s with error: %w", frame.filename, err)
		}
		if mmap, err = shm.CreateTemp("icat-*", uint64(data_size)); err != nil {
			return fmt.Errorf("Failed to create a SHM file for transmission: %w", err)
		}
		if _, err = io.ReadFull(f, mmap.Slice()); err != nil {
			mmap.Close()
			mmap.Unlink()
			return fmt.Errorf("Failed to read data from image output data file: %w", err)
		}
	} else {
		data_size = int64(len(frame.in_memory_bytes))
		if mmap, err = shm.CreateTemp("icat-*", uint64(data_size)); err != nil {
			return fmt.Errorf("Failed to create a SHM file for transmission: %w", err)
		}
		copy(mmap.Slice(), frame.in_memory_bytes)
	}
	defer mmap.Close() // terminal is responsible for unlink
	gc := gc_for_image(imgd, frame_num, frame)
	gc.SetTransmission(graphics.GRT_transmission_sharedmem)
	gc.SetDataSize(uint64(data_size))
	err = gc.WriteWithPayloadTo(graphics_writer(), utils.UnsafeStringToBytes(mmap.Name()))
	return
}

func transmit_file(imgd *image_data, frame_num int, frame *image_frame) (err error) {
	is_temp := false
	fname := ""
	var data_size int
	if frame.in_memory_bytes == nil {
		fname, err = filepath.Abs(frame.filename)
		if err != nil {
			return fmt.Errorf("Failed to convert image data output file: %s to absolute path with error: %w", frame.filename, err)
		}
		frame.filename = "" // so it isn't deleted in cleanup
	} else {
		is_temp = true
		f, err := images.CreateTempInRAM()
		if err != nil {
			return fmt.Errorf("Failed to create a temp file for image data transmission: %w", err)
		}
		data_size = len(frame.in_memory_bytes)
		_, err = bytes.NewBuffer(frame.in_memory_bytes).WriteTo(f)
		f.Close()
		if err != nil {
			os.Remove(f.Name())
			return fmt.Errorf("Failed to write image data to temp file for transmission: %w", err)
		}
		fname = f.Name()
	}
	gc := gc_for_image(imgd, frame_num, frame)
	gc.SetTransmission(utils.IfElse(is_temp, graphics.GRT_transmission_tempfile, graphics.GRT_transmission_file))
	if data_size > 0 {
		gc.SetDataSize(uint64(data_size))
	}
	return gc.WriteWithPayloadTo(graphics_writer(), utils.UnsafeStringToBytes(fname))
}

func transmit_stream(imgd *image_data, frame_num int, frame *image_frame) (err error) {
	data := frame.in_memory_bytes
	if data == nil {
		data, err = os.ReadFile(frame.filename)
		if err != nil {
			return fmt.Errorf("Failed to read image data output file: %s with error: %w", frame.filename, err)
		}
	}
	gc := gc_for_image(imgd, frame_num, frame)
	return gc.WriteWithPayloadTo(graphics_writer(), data)
}

func calculate_in_cell_x_offset(width, cell_width int) int {
	extra_pixels := width % cell_width
	if extra_pixels == 0 {
		return 0
	}
	switch opts.Align {
	case "left":
		return 0
	case "right":
		return cell_width - extra_pixels
	default:
		return (cell_width - extra_pixels) / 2
	}
}

func place_cursor(imgd *image_data) {
	cw := max(int(screen_size.Xpixel)/int(screen_size.Col), 1)
	ch := max(int(screen_size.Ypixel)/int(screen_size.Row), 1)
	imgd.cell_x_offset = calculate_in_cell_x_offset(imgd.canvas_width, cw)
	imgd.width_cells = int(math.Ceil(float64(imgd.canvas_width) / float64(cw)))
	imgd.height_cells = int(math.Ceil(float64(imgd.canvas_height) / float64(ch)))
	if place == nil {
		switch opts.Align {
		case "center":
			imgd.move_x_by = (int(screen_size.Col) - imgd.width_cells) / 2
		case "right":
			imgd.move_x_by = (int(screen_size.Col) - imgd.width_cells)
		}
	} else {
		imgd.move_to.x = place.left + 1
		imgd.move_to.y = place.top + 1
		switch opts.Align {
		case "center":
			imgd.move_to.x += (place.width - imgd.width_cells) / 2
		case "right":
			imgd.move_to.x += (place.width - imgd.width_cells)
		}
	}
}

func next_random() (ans uint32) {
	for ans == 0 {
		b := make([]byte, 4)
		_, err := rand.Read(b)
		if err == nil {
			ans = binary.LittleEndian.Uint32(b[:])
		} else {
			ans = not_rand.Uint32()
		}
	}
	return ans
}

func write_unicode_placeholder(imgd *image_data) {
	prefix := ""
	r, g, b := (imgd.image_id>>16)&255, (imgd.image_id>>8)&255, imgd.image_id&255
	// The lower 24 bits of the image id are carried in this foreground colour.
	// conhost forwards only the legacy semicolon-form truecolor SGR, not the colon
	// (ITU) form kitty normally uses, so fall back to semicolons on Windows.
	foreground := fmt.Sprintf("\033[38:2:%d:%d:%dm", r, g, b)
	if windows_gfx_bypass {
		foreground = fmt.Sprintf("\033[38;2;%d;%d;%dm", r, g, b)
	}
	os.Stdout.WriteString(foreground)
	restore := "\033[39m"
	if imgd.move_to.y > 0 {
		os.Stdout.WriteString(loop.SAVE_CURSOR)
		restore += loop.RESTORE_CURSOR
	} else if imgd.move_x_by > 0 {
		prefix = strings.Repeat(" ", imgd.move_x_by)
	}
	defer func() { os.Stdout.WriteString(restore) }()
	if imgd.move_to.y > 0 {
		fmt.Printf(loop.MoveCursorToTemplate, imgd.move_to.y, 0)
	}
	id_char := string(images.NumberToDiacritic[(imgd.image_id>>24)&255])
	for r := 0; r < imgd.height_cells; r++ {
		if imgd.move_to.x > 0 {
			fmt.Printf("\x1b[%dC", imgd.move_to.x-1)
		} else {
			os.Stdout.WriteString(prefix)
		}
		for c := 0; c < imgd.width_cells; c++ {
			if windows_gfx_bypass && c > 0 {
				// Only the first cell of each row carries the row/column/id diacritics;
				// kitty infers the rest from the run. Bare cells are a single column, so
				// conhost (which counts each combining diacritic as an extra column) no
				// longer over-counts the row width and wraps it, which was desyncing its
				// cursor from kitty's and making the prompt jump.
				os.Stdout.WriteString(string(kitty.ImagePlaceholderChar))
			} else {
				os.Stdout.WriteString(string(kitty.ImagePlaceholderChar) + string(images.NumberToDiacritic[r]) + string(images.NumberToDiacritic[c]) + id_char)
			}
		}
		if r < imgd.height_cells-1 {
			os.Stdout.WriteString("\n\r")
		}
	}
}

var seen_image_ids *utils.Set[uint32]

func transmit_image(imgd *image_data, no_trailing_newline bool) {
	if seen_image_ids == nil {
		seen_image_ids = utils.NewSet[uint32](32)
	}
	if windows_gfx_bypass {
		gfx_buf.Reset()
	}
	var f func(*image_data, int, *image_frame) error
	if opts.TransferMode != "detect" {
		switch opts.TransferMode {
		case "file":
			f = transmit_file
		case "memory":
			f = transmit_shm
		case "stream":
			f = transmit_stream
		}
	}
	if f == nil && transfer_by_memory == supported && imgd.frames[0].in_memory_bytes != nil {
		f = transmit_shm
	}
	if f == nil && transfer_by_file == supported {
		f = transmit_file
	}
	if f == nil {
		f = transmit_stream
	}
	if imgd.image_id == 0 {
		if imgd.use_unicode_placeholder {
			for imgd.image_id&0xFF000000 == 0 || imgd.image_id&0x00FFFF00 == 0 || seen_image_ids.Has(imgd.image_id) {
				// Generate a 32-bit image id using rejection sampling such that the most
				// significant byte and the two bytes in the middle are non-zero to avoid
				// collisions with applications that cannot represent non-zero most
				// significant bytes (which is represented by the third combining character)
				// or two non-zero bytes in the middle (which requires 24-bit color mode).
				imgd.image_id = next_random()
			}
			seen_image_ids.Add(imgd.image_id)
		} else {
			if len(imgd.frames) > 1 {
				for imgd.image_number == 0 {
					imgd.image_number = next_random()
				}
			}
		}
	}
	place_cursor(imgd)
	if imgd.use_unicode_placeholder && utils.Max(imgd.width_cells, imgd.height_cells) >= len(images.NumberToDiacritic) {
		imgd.err = fmt.Errorf("Image too large to be displayed using Unicode placeholders. Maximum size is %dx%d cells", len(images.NumberToDiacritic), len(images.NumberToDiacritic))
		return
	}
	switch imgd.passthrough_mode {
	case tmux_passthrough:
		imgd.err = tui.TmuxAllowPassthrough()
		if imgd.err != nil {
			return
		}
	}
	fmt.Print("\r")
	if !imgd.use_unicode_placeholder {
		if imgd.move_x_by > 0 {
			fmt.Printf("\x1b[%dC", imgd.move_x_by)
		}
		if imgd.move_to.x > 0 {
			fmt.Printf(loop.MoveCursorToTemplate, imgd.move_to.y, imgd.move_to.x)
		}
	}
	frame_control_cmd := new_graphics_command(imgd)
	frame_control_cmd.SetAction(graphics.GRT_action_animate)
	if imgd.image_id != 0 {
		frame_control_cmd.SetImageId(imgd.image_id)
	} else {
		frame_control_cmd.SetImageNumber(imgd.image_number)
	}
	is_animated := len(imgd.frames) > 1

	for frame_num, frame := range imgd.frames {
		err := f(imgd, frame_num, frame)
		if err != nil {
			imgd.err = err
			return
		}
		if is_animated {
			switch frame_num {
			case 0:
				// set gap for the first frame and number of loops for the animation
				c := frame_control_cmd
				c.SetTargetFrame(uint64(frame.number))
				c.SetGap(int32(frame.delay_ms))
				switch {
				case opts.Loop < 0:
					c.SetNumberOfLoops(1)
				case opts.Loop > 0:
					c.SetNumberOfLoops(uint64(opts.Loop) + 1)
				}
				if imgd.err = c.WriteWithPayloadTo(graphics_writer(), nil); imgd.err != nil {
					return
				}
			case 1:
				c := frame_control_cmd
				c.SetAnimationControl(2) // set animation to loading mode
				if imgd.err = c.WriteWithPayloadTo(graphics_writer(), nil); imgd.err != nil {
					return
				}
			}
		}
	}
	if imgd.use_unicode_placeholder {
		write_unicode_placeholder(imgd)
	}
	if is_animated {
		c := frame_control_cmd
		c.SetAnimationControl(3) // set animation to normal mode
		if imgd.err = c.WriteWithPayloadTo(graphics_writer(), nil); imgd.err != nil {
			return
		}
	}
	if imgd.move_to.x == 0 && !no_trailing_newline {
		fmt.Println() // ensure cursor is on new line
	}
	if windows_gfx_bypass && gfx_buf.Len() > 0 {
		// Flush the accumulated graphics commands to kitty over the side channel.
		// The placeholders above are already on stdout; kitty resolves them to the
		// image the next time it renders after this transmit loads it.
		if err := send_graphics_to_kitty(gfx_buf.Bytes()); err != nil {
			imgd.err = err
		}
	}
}
