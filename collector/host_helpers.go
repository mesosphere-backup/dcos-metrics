package collector

// HostCollectorConfig ...
type HostCollectorConfig struct {
	MesosRole      string
	MesosPort      int
	MesosEndpoints map[string]string
	// Maybe testing config too? Unsure how those flags currently work.
}

// HostMetrics ...
type HostMetrics struct {
	Memory  string
	CPU     string
	Storage string
}

// Host ...
type Host struct {
	IPAddress string
	Hostname  string
	Metrics   HostMetrics
	Config    HostCollectorConfig
}

// SetIP ...
func (h *Host) SetIP() error { return nil }

// SetHostname ...
func (h *Host) SetHostname() error { return nil }

// SetConfig ...
func (h *Host) SetConfig(role string, port int, endpoints map[string]string) error { return nil }

// GetHostMetrics ...
func (h *Host) GetHostMetrics() error { return nil }
