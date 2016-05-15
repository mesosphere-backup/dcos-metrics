package collector

import (
	"flag"
	"fmt"
	"os"
	"strconv"
	"strings"
)

func BoolEnvFlag(flagName string, defaultVal bool, usage string) *bool {
	envName := toEnvName(flagName)
	defaultValToUse, err := strconv.ParseBool(os.Getenv(envName))
	if err != nil {
		defaultValToUse = defaultVal
	}
	return flag.Bool(flagName, defaultValToUse, formatUsage(envName, usage))
}

func IntEnvFlag(flagName string, defaultVal int64, usage string) *int64 {
	envName := toEnvName(flagName)
	defaultValToUse, err := strconv.ParseInt(os.Getenv(envName), 10, 32)
	if err != nil {
		defaultValToUse = defaultVal
	}
	return flag.Int64(flagName, defaultValToUse, formatUsage(envName, usage))
}

func StringEnvFlag(flagName string, defaultVal string, usage string) *string {
	envName := toEnvName(flagName)
	defaultValToUse := os.Getenv(envName)
	if len(defaultValToUse) == 0 {
		defaultValToUse = defaultVal
	}
	return flag.String(flagName, defaultValToUse, formatUsage(envName, usage))
}

// ---

func toEnvName(flagName string) string {
	return strings.Replace(strings.ToUpper(flagName), "-", "_", -1)
}

func formatUsage(envName, usage string) string {
	// Results in eg:
	//  -kafka-framework string
	//        The Kafka framework to query for brokers. (overrides '-kafka-brokers')
	//        (env "KAFKA_FRAMEWORK") (default "kafka")
	return fmt.Sprintf("%s\n    \t(env \"%s\")", usage, envName)
}
