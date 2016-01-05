package main

import (
	"bytes"
	"net"
)

var framework_id_prefix = []byte("framework_id:")

// A superset of Packet which also includes information extracted from the raw data.
type Record struct {
	src_addr *net.UDPAddr
	data []byte

	name string
	framework_id string
}

func newRecord(src_addr *net.UDPAddr, data []byte) Record {
	pp := Record{ src_addr: src_addr, data: data }
	// expected format for each (\n-separated) record:
	// "stat_name|val|@ignoredSection|#tag1:val1,tag2:val2|$ignoredSection"
	// -> get "stat_name" (required) and value for any "framework_id" tag (optional)
	name_end := bytes.Index(pp.data, []byte("\n"))
	if name_end == -1 {
		// packet is invalid. just give up and omit metadata
		return pp
	}
	pp.name = string(pp.data[0:name_end])

	// search for tags following the record name
	tags_start := name_end
	for {
		// seek forward to the next tag section prefix
		tags_start = bytes.Index(pp.data[tags_start:], []byte("|#"))
		if tags_start < 0 {
			// no tag section found
			break
		}
		// seek to the end of the tag section (or to the end of the string)
		tags_end := bytes.Index(pp.data[tags_start:], []byte("|"))
		if tags_end < 0 {
			// tags run to end of data
			tags_end = len(pp.data)
		}
		// split comma-separated tags, then find any "framework_id" tag
		tags := bytes.Split(pp.data[tags_start:tags_end], []byte(","))
		for _, tag := range tags {
			if bytes.HasPrefix(tag, framework_id_prefix) {
				pp.framework_id = string(tag[len(framework_id_prefix):])
				return pp
			}
		}

		// check for any additional tag sections beyond this one
		tags_start = tags_end
	}
	return pp
}

// Extracts router-related statsd data from the provided raw packet data and returns a new
// one or more Records with any metadata broken out. One Record will be produced per
// \n-separated row in the input. If any given row is of invalid format, the corresponding
// Record for that row will lack any metadata.
func parse(pkt Packet) []Record {
	if !bytes.Contains(pkt.data, []byte("\n")) {
		// no newline handling, just reference original data
		return []Record{newRecord(pkt.src_addr, pkt.data)}
	}

	lines := bytes.Split(pkt.data, []byte("\n"))
	pps := make([]Record, len(lines))
	for i, line := range lines {
		pps[i] = newRecord(pkt.src_addr, line)
	}
	return pps
}
