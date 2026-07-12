package probe

import (
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
)

// RdmaLink is one RDMA port as reported by `rdma link show -j`.
type RdmaLink struct {
	Ifname        string `json:"ifname"`
	Port          int    `json:"port"`
	State         string `json:"state"`
	PhysicalState string `json:"physical_state,omitempty"`
	Netdev        string `json:"netdev,omitempty"`
}

// IbvDevice is one device stanza from ibv_devinfo, reduced to the fields
// that decide tier eligibility.
type IbvDevice struct {
	HCA       string `json:"hca"`
	Transport string `json:"transport,omitempty"`
	PortState string `json:"port_state,omitempty"`
	LinkLayer string `json:"link_layer,omitempty"`
}

// RDMAReport describes the RDMA fabric visible from this host: what the
// kernel RDMA subsystem lists in sysfs, what the rdma tool reports, and
// the verbs view when rdma-core is installed. IB and RoCE are told apart
// by LinkLayer.
type RDMAReport struct {
	SysfsDevices []string    `json:"sysfs_devices,omitempty"`
	Links        []RdmaLink  `json:"links,omitempty"`
	Devices      []IbvDevice `json:"devices,omitempty"`
	Notes        []string    `json:"notes,omitempty"`
}

// ParseRdmaLinkJSON parses `rdma link show -j` output.
func ParseRdmaLinkJSON(out []byte) ([]RdmaLink, error) {
	var links []RdmaLink
	if err := json.Unmarshal(out, &links); err != nil {
		return nil, err
	}
	return links, nil
}

// ParseIbvDevinfo parses ibv_devinfo text output. Stanzas open with an
// hca_id line; the port block contributes state and link layer:
//
//	hca_id:	mlx5_0
//		transport:			InfiniBand (0)
//			port:	1
//				state:			PORT_ACTIVE (4)
//				link_layer:		Ethernet
func ParseIbvDevinfo(out []byte) []IbvDevice {
	var devices []IbvDevice
	var cur *IbvDevice

	for _, line := range strings.Split(string(out), "\n") {
		key, value, ok := strings.Cut(line, ":")
		if !ok {
			continue
		}
		key = strings.TrimSpace(key)
		value = strings.TrimSpace(value)

		switch key {
		case "hca_id":
			devices = append(devices, IbvDevice{HCA: value})
			cur = &devices[len(devices)-1]
		case "transport":
			if cur != nil {
				cur.Transport = trimParen(value)
			}
		case "state":
			if cur != nil {
				cur.PortState = trimParen(value)
			}
		case "link_layer":
			if cur != nil {
				cur.LinkLayer = value
			}
		}
	}
	return devices
}

// trimParen turns "PORT_ACTIVE (4)" into "PORT_ACTIVE".
func trimParen(v string) string {
	if idx := strings.Index(v, "("); idx >= 0 {
		return strings.TrimSpace(v[:idx])
	}
	return v
}

// RDMAProbe assembles the RDMA report. root is prepended to sysfs paths
// for tests; tools run only when installed, exactly like the base audit.
func RDMAProbe(root string) RDMAReport {
	r := RDMAReport{}

	if entries, err := os.ReadDir(filepath.Join(root, "sys/class/infiniband")); err == nil {
		for _, e := range entries {
			r.SysfsDevices = append(r.SysfsDevices, e.Name())
		}
	} else {
		r.Notes = append(r.Notes, "sys/class/infiniband unreadable: "+err.Error())
	}

	if toolAvailable("rdma") {
		res := runTool("rdma", "link", "show", "-j")
		if res.Error == "" {
			links, err := ParseRdmaLinkJSON([]byte(res.Output))
			if err != nil {
				r.Notes = append(r.Notes, "rdma link JSON: "+err.Error())
			} else {
				r.Links = links
			}
		} else {
			r.Notes = append(r.Notes, "rdma link show failed: "+res.Error)
		}
	} else {
		r.Notes = append(r.Notes, "rdma tool not on PATH")
	}

	if toolAvailable("ibv_devinfo") {
		res := runTool("ibv_devinfo")
		if res.Error == "" {
			r.Devices = ParseIbvDevinfo([]byte(res.Output))
		} else {
			r.Notes = append(r.Notes, "ibv_devinfo failed: "+res.Error)
		}
	} else {
		r.Notes = append(r.Notes, "ibv_devinfo not on PATH")
	}

	return r
}

// HasActiveRDMA reports whether any port is ACTIVE, the go/no-go summary
// a tier check wants from this report.
func (r RDMAReport) HasActiveRDMA() bool {
	for _, l := range r.Links {
		if strings.EqualFold(l.State, "ACTIVE") {
			return true
		}
	}
	for _, d := range r.Devices {
		if strings.EqualFold(strings.TrimPrefix(d.PortState, "PORT_"), "ACTIVE") {
			return true
		}
	}
	return false
}
