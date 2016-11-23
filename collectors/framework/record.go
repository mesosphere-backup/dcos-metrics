package framework

import (
	"encoding/json"
	"fmt"
	"time"

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
func (ar avroRecord) extract(pmm *producers.MetricsMessage) error {
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
			} else if tagName == "framework_id" {
				pmm.Dimensions.FrameworkID = tagValue
			} else if tagName == "executor_id" {
				pmm.Dimensions.ExecutorID = tagValue
			} else {
				// Assumes Labels has been initialized already.
				pmm.Dimensions.Labels[tagName] = tagValue
			}
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
				name  = field.Fields[0].Datum
				value = field.Fields[1].Datum
				unit  = field.Fields[2].Datum
			)

			dp := producers.Datapoint{
				Name:      fmt.Sprintf("%v", name),
				Unit:      fmt.Sprintf("%v", unit),
				Value:     value,
				Timestamp: fmt.Sprintf("%v", time.Now()),
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
	jsonObj, err := json.MarshalIndent(record, "", "    ")
	if err != nil {
		return err
	}

	fwColLog.Debug("JSON Record:\n", string(jsonObj))
	return json.Unmarshal(jsonObj, &ar)
}
