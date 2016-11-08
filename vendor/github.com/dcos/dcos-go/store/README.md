# dcos-go/store
A simple, local, and goroutine-safe in-memory key-value store.

## Overview
dcos-go/store is a simple, local, in-memory key-value store for caching objects
for short periods of time. The motivation for this project was the HTTP producer
in [dcos-metrics][dcos-metrics-github] where we have a need to cache a
"snapshot" of a given agent's metrics until the next polling interval.

## Usage

```go
import "github.com/dcos/dcos-go/store"

// Basic usage
s := store.New()
s.Set("foo", "fooval")
s.Set("bar", "barval")

s.Get("foo") // fooval
s.Objects()  // map[foo:{fooval} bar:{barval}]
s.Size()     // 1
s.Delete("foo")
s.Purge()

// Advanced usage
newMap := make(map[string]interface{})
newMap["foo2"] = "fooval2"
newMap["bar2"] = "barval2"

// Replace (supplant) all objects in the cache with thsoe in newMap
s.Supplant(newMap) // map[foo2:{fooval2} bar2:{barval2}]
```

[dcos-metrics-github]: https://github.com/dcos/dcos-metrics
