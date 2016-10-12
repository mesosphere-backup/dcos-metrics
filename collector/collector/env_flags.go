// Copyright 2016 Mesosphere, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package collector

import (
	"flag"
	"fmt"
	"os"
	"strconv"
	"strings"
)

// BoolEnvFlag ...
func BoolEnvFlag(flagName string, defaultVal bool, usage string) *bool {
	envName := toEnvName(flagName)
	defaultValToUse, err := strconv.ParseBool(os.Getenv(envName))
	if err != nil {
		defaultValToUse = defaultVal
	}
	return flag.Bool(flagName, defaultValToUse, formatUsage(envName, usage))
}

// IntEnvFlag ...
func IntEnvFlag(flagName string, defaultVal int64, usage string) *int64 {
	envName := toEnvName(flagName)
	defaultValToUse, err := strconv.ParseInt(os.Getenv(envName), 10, 32)
	if err != nil {
		defaultValToUse = defaultVal
	}
	return flag.Int64(flagName, defaultValToUse, formatUsage(envName, usage))
}

// StringEnvFlag ...
func StringEnvFlag(flagName string, defaultVal string, usage string) *string {
	envName := toEnvName(flagName)
	defaultValToUse := os.Getenv(envName)
	if len(defaultValToUse) == 0 {
		defaultValToUse = defaultVal
	}
	return flag.String(flagName, defaultValToUse, formatUsage(envName, usage))
}

// PrintFlagEnv ...
func PrintFlagEnv(flag *flag.Flag) {
	if len(flag.Value.String()) == 0 {
		fmt.Fprintf(os.Stderr, "%s=%s\n", toEnvName(flag.Name), flag.DefValue)
	} else {
		fmt.Fprintf(os.Stderr, "%s=%s\n", toEnvName(flag.Name), flag.Value)
	}
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
