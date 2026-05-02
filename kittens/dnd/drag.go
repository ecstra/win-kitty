package dnd

import (
	"errors"
	"fmt"
	"io"
	"maps"
	"os"
	"slices"
	"strings"

	"github.com/emmansun/base64"
	"github.com/kovidgoyal/kitty/tools/tui/loop"
	"github.com/kovidgoyal/kitty/tools/utils"
	"github.com/kovidgoyal/kitty/tools/utils/streaming_base64"
)

var _ = fmt.Print

type data_request struct {
	drag_source      *drag_source
	send_remote_data bool
	index            int
	write_id         loop.IdType
	base64           streaming_base64.StreamingBase64Encoder
}

type drag_status struct {
	active                 bool
	terminal_accepted_drag bool
	offered_mimes          []string
	accepted_mime          int
	accepted_operation     int
	dropped                bool
	data_requests          []*data_request
}

func (dnd *dnd) on_potential_drag_start(cell_x, cell_y int) (err error) {
	if !dnd.allow_drags || dnd.drag_status.active {
		return
	}
	mimes := slices.Collect(maps.Keys(dnd.drag_sources))
	actions := 3
	if dnd.copy_button_region.has(cell_x, cell_y) {
		actions = 1
	} else if dnd.move_button_region.has(cell_x, cell_y) {
		actions = 2
	}
	dnd.lp.QueueDnDData(DC{Type: 'o', Operation: actions, Payload: utils.UnsafeStringToBytes(strings.Join(mimes, " "))})
	total_preloaded_data_sz := 0
	for i, mt := range mimes {
		s := dnd.drag_sources[mt]
		if len(s.data) > 0 && len(s.data)+total_preloaded_data_sz < 64*1024*1024 {
			total_preloaded_data_sz += len(s.data)
			dnd.lp.QueueDnDData(DC{Type: 'p', X: i, Operation: actions, Payload: s.data})
			dnd.lp.QueueDnDData(DC{Type: 'p', X: i, Operation: actions})
		}
	}
	dnd.drag_status.offered_mimes = mimes
	// TODO: set the drag image
	dnd.lp.QueueDnDData(DC{Type: 'P', X: -1}) // start drag
	dnd.drag_status.active = true

	return dnd.render_screen()
}

func (dnd *dnd) on_drag_error(cmd DC) (err error) {
	payload := string(cmd.Payload)
	switch payload {
	case "OK":
		if dnd.drag_status.active && !dnd.drag_status.terminal_accepted_drag {
			dnd.drag_status.terminal_accepted_drag = true
			err = dnd.render_screen()
		}
	default:
		err = fmt.Errorf("terminal responded with drag source error: %s", payload)
	}
	return
}

func (dnd *dnd) reset_drag() {
	for _, dr := range dnd.drag_status.data_requests {
		if dr.drag_source.file != nil {
			dr.drag_source.file.Close()
			dr.drag_source.file = nil
		}
	}
	dnd.drag_status = drag_status{}
}

func (dnd *dnd) on_drag_event(x, y, operation, Y int) (err error) {
	switch x {
	case 1:
		dnd.drag_status.accepted_mime = y
	case 2:
		dnd.drag_status.accepted_operation = operation
	case 3:
		dnd.drag_status.dropped = true
	case 4:
		dnd.reset_drag()
	case 5:
		if err = dnd.handle_data_request(y, Y == 1); err != nil {
			return err
		}
	}
	return dnd.render_screen()
}

func (dnd *dnd) finish_drag(errname string) {
	if errname == "" { // cancel drag
		dnd.lp.QueueDnDData(DC{Type: 'E', Y: -1})
	} else {
		dnd.lp.QueueDnDData(DC{Type: 'E', Payload: []byte(errname)})
	}
	dnd.reset_drag()
}

func (dnd *dnd) handle_data_request(idx int, send_remote_data bool) (err error) {
	if idx < 0 || idx >= len(dnd.drag_status.offered_mimes) {
		dnd.finish_drag("EINVAL")
		return fmt.Errorf("terminal asked for drag data from MIME list with out of bounds index: %d", idx)
	}
	mime := dnd.drag_status.offered_mimes[idx]
	ds := dnd.drag_sources[mime]
	send_remote_data = send_remote_data && mime == "text/uri-list" && len(ds.uri_list) > 0
	for _, dr := range dnd.drag_status.data_requests {
		if dr.index == idx {
			dnd.finish_drag("EINVAL")
			return fmt.Errorf("terminal sent a duplicate drag data request")
		}
	}
	dr := &data_request{drag_source: ds, send_remote_data: send_remote_data, index: idx}
	if ds.path == "" {
		dnd.lp.QueueDnDData(DC{Type: 'e', Y: idx, Payload: utils.UnsafeStringToBytes(base64.RawStdEncoding.EncodeToString(ds.data))})
		dnd.lp.QueueDnDData(DC{Type: 'e', Y: idx}) // EOF
		if !dr.send_remote_data {
			return
		}
		return dnd.start_remote_data_send(ds)
	} else {
		if ds.file != nil {
			ds.file.Close()
		}
		if ds.file, err = os.Open(ds.path); err != nil {
			dnd.finish_drag("EIO")
			return err
		}
	}
	dnd.drag_status.data_requests = append(dnd.drag_status.data_requests, dr)
	return dnd.send_data_for_data_request(len(dnd.drag_status.data_requests) - 1)
}

var read_buf [64 * 1024]byte
var encode_buf [128 * 1024]byte

func (dnd *dnd) send_data_for_data_request(i int) (err error) {
	dr := dnd.drag_status.data_requests[i]
	n, err := dr.drag_source.file.Read(read_buf[:])
	if n > 0 {
		for chunk := range dr.base64.Encode(read_buf[:n], encode_buf[:]) {
			dr.write_id = dnd.lp.QueueDnDData(DC{Type: 'e', Y: dr.index, Payload: chunk})
		}
	}
	if err == nil {
		return nil
	}
	if errors.Is(err, io.EOF) {
		chunk := dr.base64.Finish()
		if len(chunk) > 0 {
			dr.write_id = dnd.lp.QueueDnDData(DC{Type: 'e', Y: dr.index, Payload: chunk})
		}
		dr.write_id = dnd.lp.QueueDnDData(DC{Type: 'e', Y: dr.index}) // EOF
		return dnd.on_data_request_finished(i)
	}
	dnd.finish_drag("EIO")
	return err
}

func (dnd *dnd) on_send_done(id loop.IdType) (err error) {
	for i, dr := range dnd.drag_status.data_requests {
		if dr.write_id == id {
			return dnd.send_data_for_data_request(i)
		}
	}
	return
}

func (dnd *dnd) on_data_request_finished(i int) (err error) {
	dr := dnd.drag_status.data_requests[i]
	if dr.drag_source.file != nil {
		dr.drag_source.file.Close()
		dr.drag_source.file = nil
	}
	dnd.drag_status.data_requests = slices.Delete(dnd.drag_status.data_requests, i, i+1)
	if dr.send_remote_data {
		err = dnd.start_remote_data_send(dr.drag_source)
	} else if len(dnd.drag_status.data_requests) > 0 {
		err = dnd.send_data_for_data_request(0)
	}
	return
}

func (dnd *dnd) start_remote_data_send(ds *drag_source) (err error) {
	// TODO: Implement this
	return
}
