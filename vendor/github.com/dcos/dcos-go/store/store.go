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

// Package store is a simple, local, goroutine-safe in-memory key-value store.
package store

import (
	"regexp"
	"sync"
)

// Store represents the interface and available methods of the dcos-go/store package.
type Store interface {
	Delete(string)
	Get(string) (interface{}, bool)
	GetByRegex(string) (map[string]interface{}, error)
	Objects() map[string]interface{}
	Purge()
	Set(string, interface{})
	Size() int
	Supplant(map[string]interface{})
}

// storeImpl represents the structure of the store, including the store objects
// and a single locking mechanism that is shared across a given instance, ensuring
// some level of goroutine-safety.
type storeImpl struct {
	objects map[string]object
	mutex   sync.RWMutex
}

// object represents a single object in the store. Although this could be represented
// as a bare interface{}, we chose to create a new object type in case we wish to add
// additional functionality or metadata in the future.
type object struct {
	contents interface{}
}

// New creates a new, basic, in-memory store. The caller must handle
// all Set() and Delete() operations by itself; that is to say, there is no
// concept of a maximum size or expiration on stored objects.
func New() Store {
	return &storeImpl{objects: make(map[string]object)}
}

// Delete removes a single object from the store.
func (s *storeImpl) Delete(key string) {
	s.mutex.Lock()
	defer s.mutex.Unlock()
	delete(s.objects, key)
}

// Get returns a single key-value pair from the store based on its name.
func (s *storeImpl) Get(key string) (interface{}, bool) {
	s.mutex.RLock()
	defer s.mutex.RUnlock()

	object, ok := s.objects[key]
	if !ok {
		return nil, false
	}

	return object.contents, true
}

// GetByRegex returns a map of key-value pairs from the store based on a regexp search.
func (s *storeImpl) GetByRegex(expr string) (map[string]interface{}, error) {
	s.mutex.RLock()
	defer s.mutex.RUnlock()

	m := make(map[string]interface{})
	for k, v := range s.objects {
		matched, err := regexp.MatchString(expr, k)
		if err != nil {
			return m, err
		}
		if matched {
			m[k] = v.contents
		}
	}
	return m, nil
}

// Objects returns all objects in the store.
func (s *storeImpl) Objects() (m map[string]interface{}) {
	s.mutex.RLock()
	defer s.mutex.RUnlock()

	sz := len(s.objects)
	if sz == 0 {
		return
	}

	m = make(map[string]interface{}, sz)
	for k, v := range s.objects {
		m[k] = v.contents
	}
	return
}

// Purge removes ALL objects from the store.
func (s *storeImpl) Purge() {
	s.mutex.Lock()
	defer s.mutex.Unlock()
	s.objects = map[string]object{}
}

// Set creates an object in the store. If the object already exists, it is overwritten.
func (s *storeImpl) Set(key string, val interface{}) {
	s.mutex.Lock()
	defer s.mutex.Unlock()
	s.objects[key] = object{contents: val}
}

// Size returns the number of objects in the store as an integer.
func (s *storeImpl) Size() int {
	s.mutex.RLock()
	defer s.mutex.RUnlock()
	return len(s.objects)
}

// Supplant replaces all objects in the store based on a given map.
func (s *storeImpl) Supplant(m map[string]interface{}) {
	s.mutex.Lock()
	defer s.mutex.Unlock()

	n := make(map[string]object, len(m))
	for k, v := range m {
		n[k] = object{contents: v}
	}

	s.objects = n
}
