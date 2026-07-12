//go:build !linux

package pier

import "os"

// CurrentCaps on a non-Linux host reports Supported=false with empty
// sets. Capability bits are a Linux kernel concept; pretending a macOS
// dev machine holds or lacks CAP_NET_RAW would make preflight reports
// lie, so the whole struct stays zero and readers branch on Supported.
func CurrentCaps() StatusCaps {
	return StatusCaps{}
}

// CurrentRlimits on a non-Linux host reports Supported=false. The values
// exist on Darwin too, but the tiers that consume them are Linux-only, so
// reading them here would only manufacture false confidence.
func CurrentRlimits() Rlimits {
	return Rlimits{}
}

// CurrentEUID works everywhere and is reported even on non-Linux hosts,
// since "you are not root on your laptop either" is still useful context
// in a preflight report.
func CurrentEUID() int {
	return os.Geteuid()
}
