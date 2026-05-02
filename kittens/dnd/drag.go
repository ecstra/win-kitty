package dnd

import (
	"fmt"
	"maps"
	"slices"
	"strings"

	"github.com/kovidgoyal/kitty/tools/utils"
)

var _ = fmt.Print

type drag_status struct {
	active                 bool
	terminal_accepted_drag bool
	offered_mimes          []string
	accepted_mime          int
	accepted_operation     int
	dropped                bool
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
	dnd.drag_status = drag_status{}
}

func (dnd *dnd) on_drag_event(x, y, operation int) (err error) {
	switch x {
	case 1:
		dnd.drag_status.accepted_mime = y
	case 2:
		dnd.drag_status.accepted_operation = operation
	case 3:
		dnd.drag_status.dropped = true
	case 4:
		dnd.reset_drag()
	}
	return dnd.render_screen()
}
