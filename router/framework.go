package main

import (
	"log"
	"strconv"
	"time"
)

// Retrieves the list of all frameworks which have been visible since the last refresh.
// This is a short-term snapshot of current state.
type FrameworkListRequest struct {
	response chan map[string]bool
}

type FrameworkState struct {
	framework_times map[string]time.Time
}
// Retrieves the list of all frameworks which haven't expired yet.
// This is a longer-term snapshot of current state.
type FrameworkStateRequest struct {
	response chan FrameworkState
}

// Framework_state keeps track of framework ids which have recently been found in the metrics data,
// automatically adding and pruning them as they appear and disappear.
// This function is intended to be run as a gofunc.
func framework_state(state_request <-chan FrameworkStateRequest, state_refresh <-chan time.Time, list_chan chan<- FrameworkListRequest, counters <-chan CountersRequest) {
	list_request := FrameworkListRequest{ response: make(chan map[string]bool) }
	framework_times := make(map[string]time.Time) // framework_id -> time last seen
	framework_prune_count := 0
	list_update_count := 0
	for {
		select {
		case <- state_refresh:
			list_update_count++
			list_chan <- list_request
			framework_ids := <- list_request.response
			log.Printf("%d new/existing frameworks: %s", len(framework_ids), framework_ids)
			now := time.Now()
			for fmwk, _ := range framework_ids {
				framework_times[fmwk] = now
			}
			missing_frameworks := make([]string, 0)
			for fmwk, _ := range framework_ids {
				if !framework_ids[fmwk] {
					missing_frameworks = append(missing_frameworks, fmwk)
				}
			}
			log.Printf("%d missing frameworks: %s", len(missing_frameworks), missing_frameworks)
			for _, fmwk := range missing_frameworks {
				last_seen := framework_times[fmwk]
				if now.Sub(last_seen) > missing_framework_prune_period {
					delete(framework_times, fmwk)
					framework_prune_count++
				}
			}
			log.Printf("%d tracked frameworks: %s", len(framework_times), framework_times)
		case request := <- state_request:
			request.response <- FrameworkState{ framework_times: framework_times }
		case request := <- counters:
			request.response <- CountersResponse{
				label: "framework_state",
				counters: []Counter{
					Counter{name: "framework_prunes", value: strconv.Itoa(framework_prune_count)},
					Counter{name: "list_updates", value: strconv.Itoa(list_update_count)},
				},
			}
			if request.exit {
				return
			}
		}
	}
}
