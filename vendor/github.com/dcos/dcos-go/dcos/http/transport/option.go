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

package transport

import (
	"crypto/rsa"
	"crypto/x509"
	"encoding/json"
	"encoding/pem"
	"errors"
	"io/ioutil"
	"os"
	"time"
)

var (
	// ErrInvalidCredentials is the error returned by NewRoundTripper if user used empty string for a credentials.
	ErrInvalidCredentials = errors.New("uid, secret and loginEndpoit cannot be empty")

	// ErrInvalidExpireDuration is the error returned by NewRoundTripper if the token expire duration is negative or
	// zero value.
	ErrInvalidExpireDuration = errors.New("token expire duration must be positive non zero value")
)

// OptionTransportFunc type sets optional configurations for the
// DC/OS HTTP transport
type OptionTransportFunc func(*dcosTransport) error

// OptionRoundtripperFunc type sets options configurations for the
// DC/OS HTTP roundtripper
type OptionRoundtripperFunc func(*dcosRoundtripper) error

func errorOnEmpty(arg string) error {
	if len(arg) == 0 {
		return errors.New("Must pass non-empty string to this option")
	}
	return nil
}

// OptionCaCertificatePath sets the CA certificate path option.
func OptionCaCertificatePath(caCertificatePath string) OptionTransportFunc {
	return func(o *dcosTransport) error {
		err := errorOnEmpty(caCertificatePath)
		if err == nil {
			o.CaCertificatePath = caCertificatePath
		}
		return err
	}
}

// OptionIAMConfigPath sets the IAM configuration path option.
func OptionIAMConfigPath(iamConfigPath string) OptionTransportFunc {
	return func(o *dcosTransport) error {
		err := errorOnEmpty(iamConfigPath)
		if err == nil {
			o.IAMConfigPath = iamConfigPath
		}
		return err
	}
}

// OptionTokenExpire is an option to set JWT expiration date. If not set 24h is ussed by default.
func OptionTokenExpire(t time.Duration) OptionRoundtripperFunc {
	return func(j *dcosRoundtripper) error {
		if t < 1 {
			return ErrInvalidExpireDuration
		}
		j.expire = t
		return nil
	}
}

// OptionCredentials is an option to set uid, secret and loginEndpoint.
func OptionCredentials(uid, secret, loginEndpoint string) OptionRoundtripperFunc {
	return func(j *dcosRoundtripper) error {
		if uid == "" || secret == "" || loginEndpoint == "" {
			return ErrInvalidCredentials
		}
		j.uid = uid
		j.loginEndpoint = loginEndpoint

		block, _ := pem.Decode([]byte(secret))
		if block == nil {
			return ErrInvalidCredentials
		}
		key, err := x509.ParsePKCS8PrivateKey(block.Bytes)
		if err != nil {
			return ErrInvalidCredentials
		}
		if key, ok := key.(*rsa.PrivateKey); ok {
			j.secret = key
			return nil
		}

		return ErrInvalidCredentials
	}
}

// OptionReadIAMConfig is an option to read the IAMConfig from file system and populate uid, secret and loginEndpoint.
func OptionReadIAMConfig(path string) OptionRoundtripperFunc {
	return func(j *dcosRoundtripper) error {
		f, err := os.Open(path)
		if err != nil {
			return err
		}
		defer f.Close()

		fileContent, err := ioutil.ReadAll(f)
		if err != nil {
			return err
		}

		var cfg = struct {
			UID           string `json:"uid"`
			Secret        string `json:"private_key"`
			LoginEndpoint string `json:"login_endpoint"`
		}{}

		if err := json.Unmarshal(fileContent, &cfg); err != nil {
			return err
		}

		return OptionCredentials(cfg.UID, cfg.Secret, cfg.LoginEndpoint)(j)
	}
}
