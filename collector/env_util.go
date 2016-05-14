package collector

import (
	"os"
	"strconv"
)

func EnvBool(envName string, defaultVal bool) bool {
	envVal := os.Getenv(envName)
	if len(envVal) == 0 {
		return defaultVal
	}
	result, err := strconv.ParseBool(envVal)
	if err != nil {
		return defaultVal
	}
	return result
}

func EnvInt(envName string, defaultVal int) int {
	envVal := os.Getenv(envName)
	if len(envVal) == 0 {
		return defaultVal
	}
	result, err := strconv.ParseInt(envVal, 10, 32)
	if err != nil {
		return defaultVal
	}
	return int(result)
}

func EnvString(envName string, defaultVal string) string {
	envVal := os.Getenv(envName)
	if len(envVal) == 0 {
		return defaultVal
	}
	return envVal
}
