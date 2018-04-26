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

// 'go generate' must be run for the 'metrics_schema' package to be present:
//go:generate go run generator.go -infile metrics.avsc -outfile metrics_schema/schema.go

// Generate Go code from an Avro schema.
package main

import (
	"flag"
	"fmt"
	"io/ioutil"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"time"

	log "github.com/Sirupsen/logrus"
	"github.com/antonholmquist/jason"
)

var (
	infileFlag  = flag.String("infile", "", "Path to input JSON schema")
	outfileFlag = flag.String("outfile", "", "Path to output go file")
)

// We must break out all nested Avro Records into separate variables. This is required by the goavro
// library requires to support generating nested records.
// For example: A{B{C}} => A{B{C}}, B{C}, C
func writeNestedRecords(outfile *os.File, record *jason.Object) {
	// get info about Record
	name, err := record.GetString("name")
	if err != nil || len(name) == 0 {
		log.Fatal("Record lacks a name: ", record)
	}
	namespace, err := record.GetString("namespace")
	if err != nil || len(namespace) == 0 {
		log.Fatal("Record lacks a namespace: ", record)
	}

	// write Record contents. separate vars by two newlines to avoid triggering gofmt formatting
	if _, err = fmt.Fprintf(outfile, "	%sNamespace = `%s`\n\n", name, namespace); err != nil {
		log.Fatalf("Couldn't write namespace %s to %s: %s", namespace, *outfileFlag, err)
	}
	if _, err = fmt.Fprintf(outfile, "	%sSchema = `%s`\n", name, record.String()); err != nil {
		log.Fatalf("Couldn't write var %s to %s: %s", name, *outfileFlag, err)
	}

	// search for nested Records. let's assume that Records are only nested in Arrays.
	recordFields, err := record.GetObjectArray("fields")
	if err != nil {
		log.Fatalf("Expected fields in record %s", name)
	}
	for i, field := range recordFields {
		log.Printf("Scanning %s['fields'][%d]", name, i)
		fieldType, err := field.GetObject("type")
		if err != nil {
			continue // 'type' is not an object
		}
		fieldTypeType, err := fieldType.GetString("type")
		if err != nil {
			log.Fatalf("Expected %s['fields'][%d]['type']['type'] == str", name, i)
		}
		if fieldTypeType != "array" {
			log.Fatalf("Expected %s['fields'][%d]['type']['type'] == 'array'", name, i)
		}
		fieldTypeItems, err := fieldType.GetObject("items")
		if err != nil {
			log.Fatalf("Expected %s['fields'][%d]['type']['items'] == object", name, i)
		}
		fmt.Fprint(outfile, "\n")                   // only add additional newline if more vars will be printed
		writeNestedRecords(outfile, fieldTypeItems) // recurse
	}
}

func gitRev() string {
	rev, err := exec.Command("git", "rev-parse", "HEAD").Output()
	if err != nil {
		log.Warn("Failed to get Git revision: ", err, os.Stderr)
		return "UNKNOWN"
	}
	sanitizedRev := strings.TrimSpace(string(rev))
	log.Print("Git revision: ", sanitizedRev)
	return sanitizedRev
}

func main() {
	flag.Usage = func() {
		fmt.Fprint(os.Stderr,
			"Generates Go source file with a named var containing an Avro JSON schema\n")
		fmt.Fprintf(os.Stderr, "Usage of %s:\n", os.Args[0])
		flag.PrintDefaults()
	}
	flag.Parse()

	if *infileFlag == "" {
		flag.Usage()
		log.Fatal("Missing argument: -infile")
	}
	log.Info("Opening JSON ", *infileFlag)
	infile, err := os.Open(*infileFlag)
	if err != nil {
		log.Fatalf("Couldn't open input %s: %s", *infileFlag, err)
	}

	data, err := ioutil.ReadAll(infile)
	if err != nil {
		log.Fatalf("Failed to read from %s: %s", *infileFlag, err)
	}
	// prune "doc" entries from the schema by hacking them out of the file directly
	// (we do this because 'jason' lib doesn't support editing JSON properly
	prunedData := make([]byte, 0)
	for i, row := range strings.Split(string(data), "\n") {
		if strings.Contains(row, "\"doc\"") {
			log.Infof("Skipping doc (row %d): %s", i, strings.TrimSpace(row))
		} else {
			prunedData = append(prunedData, []byte(row+"\n")...)
		}
	}

	rootObject, err := jason.NewObjectFromBytes(prunedData)
	if err != nil {
		log.Fatalf("Failed to parse JSON from %s: %s", *infileFlag, err)
	}

	// check output flags before creating an output file:
	if *outfileFlag == "" {
		flag.Usage()
		log.Fatal("Missing argument: -outfile")
	}

	if err = os.MkdirAll(filepath.Dir(*outfileFlag), 0777); err != nil {
		log.Fatalf("Couldn't create output directory %s: %s", filepath.Dir(*outfileFlag), err)
	}
	outfile, err := os.Create(*outfileFlag)
	if err != nil {
		log.Fatalf("Couldn't open output %s: %s", *outfileFlag, err)
	}
	if _, err = fmt.Fprint(outfile, `package metricsSchema

// THIS FILE IS AUTOGENERATED BY 'go generate'. DO NOT EDIT.
// goavro requires that we extract each nested message in our schema. so here we are.

`); err != nil {
		log.Fatalf("Couldn't write header to %s: %s", *outfileFlag, err)
	}
	if _, err = fmt.Fprintf(outfile, "// Generated at: %s\n", time.Now().String()); err != nil {
		log.Fatalf("Couldn't write timestamp to %s: %s", *outfileFlag, err)
	}
	if _, err = fmt.Fprintf(outfile, "// Command: %+v\n", strings.Join(os.Args, " ")); err != nil {
		log.Fatalf("Couldn't write git rev to %s: %s", *outfileFlag, err)
	}
	if _, err = fmt.Fprintf(outfile, "// Git revision: %s\n\n", gitRev()); err != nil {
		log.Fatalf("Couldn't write git rev to %s: %s", *outfileFlag, err)
	}
	if _, err = fmt.Fprint(outfile, "const (\n"); err != nil {
		log.Fatalf("Couldn't write varintro to %s: %s", *outfileFlag, err)
	}

	writeNestedRecords(outfile, rootObject)

	if _, err = fmt.Fprint(outfile, `)

// AGAIN, THIS FILE IS AUTOGENERATED BY 'go generate'. DO NOT EDIT.
`); err != nil {
		log.Fatalf("Couldn't write footer to %s: %s", *outfileFlag, err)
	}
	log.Info("SUCCESS: Wrote preprocessed schema to ", *outfileFlag)
}
