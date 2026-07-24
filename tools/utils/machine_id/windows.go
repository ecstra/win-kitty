//go:build windows

package machine_id

import (
	"fmt"
	"strings"

	"golang.org/x/sys/windows/registry"
)

var _ = fmt.Print

// Windows has no /etc/machine-id. MachineGuid is the direct equivalent: the OS
// generates it during installation and keeps it for the life of that
// installation, which is the same promise /etc/machine-id makes. Read it from
// the 64 bit view explicitly, so a 32 bit build does not get redirected into
// Wow6432Node and see a different value than the rest of the system.
func read_machine_id() (string, error) {
	k, err := registry.OpenKey(
		registry.LOCAL_MACHINE, `SOFTWARE\Microsoft\Cryptography`,
		registry.QUERY_VALUE|registry.WOW64_64KEY)
	if err != nil {
		return "", err
	}
	defer k.Close()
	guid, _, err := k.GetStringValue("MachineGuid")
	if err != nil {
		return "", err
	}
	guid = strings.TrimSpace(guid)
	if guid == "" {
		return "", fmt.Errorf("MachineGuid is empty in the registry")
	}
	return guid, nil
}
