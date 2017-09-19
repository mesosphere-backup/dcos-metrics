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

package framework

import (
	"encoding/json"
	"fmt"
	"time"

	mesosAgent "github.com/dcos/dcos-metrics/collectors/mesos/agent"
	"github.com/dcos/dcos-metrics/producers"
)

// avroRecord{} conveys field for goavro.Record
// schema set by the schema package
type field struct {
	Name  interface{} `json:"name"`
	Datum interface{} `json:"datum"`
}

type record struct {
	Name   string  `json:"name"`
	Fields []field `json:"fields"`
}

type avroRecord []record

// avroRecord.extract() gets tags and datapoints from avro formatted data
// and creates a MetricsMessage{}
func (ar avroRecord) extract(pmm *producers.MetricsMessage, ctr *mesosAgent.ContainerTaskRels) error {
	var fieldType string
	if len(ar) > 0 {
		fieldType = ar[0].Name
	} else {
		return fmt.Errorf("no records found for extract")
	}

	switch fieldType {
	case "dcos.metrics.Tag": // Extract tags
		for _, field := range ar {
			if len(field.Fields) != 2 {
				return fmt.Errorf("tags must have 2 fields, got %d", len(field.Fields))
			}
			fwColLog.Debugf("Adding tag %s", field)
			tagName := fmt.Sprintf("%v", field.Fields[0].Datum)
			tagValue := fmt.Sprintf("%v", field.Fields[1].Datum)

			if tagName == "container_id" {
				pmm.Dimensions.ContainerID = tagValue

				info := ctr.Get(tagValue)
				if info != nil {
					pmm.Dimensions.TaskID = info.ID
					pmm.Dimensions.TaskName = info.Name
				} else {
					fwColLog.Debugf("Container ID %s had no associated task", tagValue)
				}

			} else if tagName == "framework_id" {
				pmm.Dimensions.FrameworkID = tagValue
			} else if tagName == "executor_id" {
				pmm.Dimensions.ExecutorID = tagValue
			} else {
				// Assumes Labels has been initialized already.
				pmm.Dimensions.Labels[tagName] = tagValue
			}
			pmm.Timestamp = time.Now().Unix()
		}
		return nil

	case "dcos.metrics.Datapoint": // Extract datapoints
		datapoints := []producers.Datapoint{}
		for _, field := range ar {
			if len(field.Fields) != 3 {
				return fmt.Errorf("datapoints must have 3 fields, got %d", len(field.Fields))
			}
			fwColLog.Debugf("Adding datapoint %s", field)

			var (
				name = field.Fields[0].Datum
				//time_ms = field.Fields[1].Datum
				value = field.Fields[2].Datum
			)

			// TODO(roger): the datapoint schema does not contain any fields
			// allowing for the sender to specify units. Therefore we default
			// to the zero value, an empty string.
			dp := producers.Datapoint{
				Name: fmt.Sprintf("%v", name),
				//Unit:      fmt.Sprintf("%v", unit),
				Value:     value,
				Timestamp: time.Now().UTC().Format(time.RFC3339),
			}

			datapoints = append(datapoints, dp)
		}
		pmm.Datapoints = datapoints
		return nil
	}

	return fmt.Errorf("must have dcos.metrics.Tags or dcos.metrics.Datapoint in avro record to use .extract()")
}

// *avroRecord.createObjectFromRecord creates a JSON implementation of the avro
// record, then serializes it to our known avroRecord type.
func (ar *avroRecord) createObjectFromRecord(record interface{}) error {
	jsonObj, err := json.Marshal(record)
	if err != nil {
		fwColLog.Debugf("Bad record:\n%s", record)
		return err
	}

	fwColLog.Debugf("JSON Record:\n%s", string(jsonObj))
	return json.Unmarshal(jsonObj, &ar)
}
