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

package helpers

import (
	"crypto/tls"
	"crypto/x509"
	"errors"
	"io/ioutil"
	"net/http"
	"os"

	log "github.com/Sirupsen/logrus"
	"github.com/dcos/dcos-go/dcos/http/transport"
)

// loadCAPool will load a valid x509 cert.
func loadCAPool(path string) (*x509.CertPool, error) {
	caPool := x509.NewCertPool()
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()

	b, err := ioutil.ReadAll(f)
	if err != nil {
		return nil, err
	}

	if !caPool.AppendCertsFromPEM(b) {
		return nil, errors.New("CACertFile parsing failed")
	}

	return caPool, nil
}

// getTransport will return transport for http.Client
func getTransport(caCertificatePath string) (*http.Transport, error) {
	tr := &http.Transport{}
	// if user provided CA cert we must use it, otherwise use InsecureSkipVerify: true for all HTTPS requests.
	if caCertificatePath != "" {
		log.Infof("Loading CA cert: %s", caCertificatePath)
		caPool, err := loadCAPool(caCertificatePath)
		if err != nil {
			return tr, err
		}

		tr.TLSClientConfig = &tls.Config{
			RootCAs: caPool,
		}
	} else {
		tr.TLSClientConfig = &tls.Config{
			InsecureSkipVerify: true,
		}
	}
	return tr, nil
}

// NewMetricsClient returns a client with a transport using dcos-go/jwt/transport for
// secure communications if a IAM configuration is present. It uses a verified cert if
// present and skips verification if not present.
func NewMetricsClient(caCertificatePath string, iamConfigPath string) (*http.Client, error) {
	client := &http.Client{}
	tr, err := getTransport(caCertificatePath)
	if err != nil {
		return client, err
	}
	client.Transport = tr

	if len(iamConfigPath) != 0 {
		rt, err := transport.NewRoundTripper(
			client.Transport,
			transport.OptionReadIAMConfig(iamConfigPath))
		if err != nil {
			log.Fatal(err)
		}
		client.Transport = rt
	}

	return client, nil
}
