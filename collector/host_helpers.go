package collector

type HostCollectorConfig struct {
	MesosRole      string
	MesosPort      int
	MesosEndpoints map[string]string
	// Maybe testing config too? Unsure how those flags currently work.
}

type HostMetrics struct {
	Memory  string
	CPU     string
	Storage string
}

type Host struct {
	IPAddress string
	Hostname  string
	Metrics   HostMetrics
	Config    HostCollectorConfig
}

func (h *Host) SetIP() error { return nil }

func (h *Host) SetHostname() error { return nil }

func (h *Host) SetConfig(role string, port int, endpoints map[string]string) error { return nil }

func (h *Host) GetHostMetrics() error { return nil }
