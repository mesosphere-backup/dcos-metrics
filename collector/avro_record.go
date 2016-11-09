package collector

import (
	"encoding/json"
	"errors"
	"fmt"

	"github.com/dcos/dcos-metrics/producers"
)

// avroRecord{} conveys field for goavro.Record
// schema set by the schema package
type avroRecord []struct {
	Name   string `json:"name"`
	Fields []struct {
		Name  interface{} `json:"name"`
		Datum interface{} `json:"datum"`
	} `json:"fields"`
}

// avroRecord.extract() gets tags and datapoints from avro formatted data
// and creates a MetricsMessage{}
func (ar avroRecord) extract(pmm *producers.MetricsMessage) error {
	fieldType := ""
	if len(ar) > 0 {
		fieldType = ar[0].Name
	} else {
		return errors.New("No records found for extract.")
	}

	// Extract tags
	if fieldType == "dcos.metrics.Tag" {
		for _, field := range ar {
			fwColLog.Debugf("Adding tag %s", field)
			tagName := fmt.Sprintf("%s", field.Fields[0].Datum)
			tagValue := fmt.Sprintf("%s", field.Fields[1].Datum)

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
	}

	// Extract datapoints
	if fieldType == "dcos.metrics.Datapoint" {
		datapoints := []producers.Datapoint{}
		for _, field := range ar {
			fwColLog.Debugf("Adding datapoint %s", field)
			dp := producers.Datapoint{
				Name:  fmt.Sprintf("%s", field.Fields[0].Datum),
				Value: fmt.Sprintf("%s", field.Fields[1].Datum),
				Unit:  fmt.Sprintf("%s", field.Fields[2].Datum),
			}
			datapoints = append(datapoints, dp)
		}
		pmm.Datapoints = datapoints
	}

	if fieldType == "" {
		return errors.New("Must have dcos.metrics.Tags or dcos.metrics.Datapoint in avro record to use .extract()")
	}

	return nil
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
