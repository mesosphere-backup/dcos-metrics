// +build unit

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
	"io/ioutil"
	"os"
	"testing"

	. "github.com/smartystreets/goconvey/convey"
)

var mockCaCertificate = []byte(`-----BEGIN CERTIFICATE-----
MIIEETCCAvmgAwIBAgIJAIOSO2cJ/bBAMA0GCSqGSIb3DQEBCwUAMIGdMQswCQYD
VQQGEwJTQzETMBEGA1UECAwKU29tZS1TdGF0ZTESMBAGA1UEBwwJU29tZS1DaXR5
MREwDwYDVQQKDAhTb21lLU9yZzEQMA4GA1UECwwHU29tZS1PVTEQMA4GA1UEAwwH
U29tZS1DTjEuMCwGCSqGSIb3DQEJARYfU29tZS1lbWFpbEBzb21lLWRvbWFpbi5z
b21lLXRsZDAgFw0xNjExMzAyMDA5MzRaGA8yMTE2MTEwNjIwMDkzNFowgZ0xCzAJ
BgNVBAYTAlNDMRMwEQYDVQQIDApTb21lLVN0YXRlMRIwEAYDVQQHDAlTb21lLUNp
dHkxETAPBgNVBAoMCFNvbWUtT3JnMRAwDgYDVQQLDAdTb21lLU9VMRAwDgYDVQQD
DAdTb21lLUNOMS4wLAYJKoZIhvcNAQkBFh9Tb21lLWVtYWlsQHNvbWUtZG9tYWlu
LnNvbWUtdGxkMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEArRh0U2qz
I5e/opcjph+yTm9VQEcKHvTyQh658r7nksczX6HsqpoEhrHcu0MKC8hIKG2GbKg8
a2PSq/lf7N8spcfWcjv8D5YkT9Z44YNF0HhVgIB7MJChLpTKL1g1l+SpgLm4LqIr
Xc+d7Kfaf7Qw/7+Kc4NA3On3RLzzSYFSObT3TEKUfDaDnKN2h0rvs2lDf+RDERZY
XLUfKXHoNuF0DYSr+CNfKG+jFFsCG+femU85PGVlt3bzgLnNdyXuOAbzuVaMB+7L
kwNeMoxAjM8GCE3PhzwCMOI0seRPKacm0gI3/2qGZknoukYOhIKsMafC1aEprfFx
0sQPCnFR0tTw6wIDAQABo1AwTjAdBgNVHQ4EFgQUYzgKIbv2Ap1p90Dnj6+zN5SF
eo0wHwYDVR0jBBgwFoAUYzgKIbv2Ap1p90Dnj6+zN5SFeo0wDAYDVR0TBAUwAwEB
/zANBgkqhkiG9w0BAQsFAAOCAQEAmv8r/Xo5+Ehja0fU4JPBGjD9Ee0RfEqtu2l1
K/cfxxR43KHfSTsrT6fFk2+U8CRoVktCMh/5spCIh4PSlQ4jwLZu1A2Nas3cl2HE
GgWYrPOhSkfNq/ihJ1fVuY3hBGxTyoOG5H3CVc54E3v8WpeQxnZvvTUVuzyF6Ag2
W8QE1cu3/HVjHGPxSu42Yf8ZY4rGpMT9AK1JdX2empr46liD+A87kcYWodWxCFA7
syFB14IaSy600tgcYU9iX2cbd6IlvLaZnD/opft7pR/X74FVIjR4q61kyD8IWlKG
0pG7vhk+Rj8HSim8oAvfX/wrgZ5KgHDD2sNkq0olUVs//kOhXQ==
-----END CERTIFICATE-----
`)

func setup() string {
	tmpfile, err := ioutil.TempFile(os.TempDir(), "dcos-metrics-mock-ca-cert")
	if err != nil {
		panic(err)
	}
	defer tmpfile.Close()
	tmpfile.Write(mockCaCertificate)
	return tmpfile.Name()
}

func TestLoadCAPool(t *testing.T) {
	Convey("When loading a CA pool", t, func() {
		Convey("Should return an x509.CertPool if the CA was valid", func() {
			cp, err := loadCAPool(setup())
			So(err, ShouldBeNil)
			So(len(cp.Subjects()), ShouldEqual, 1)
		})

		Convey("Should return an error if the CA certificate wasn't valid or couldn't be loaded", func() {
			_, err := loadCAPool("/dev/null")
			So(err, ShouldNotBeNil)
		})
	})
}

func TestGetTransport(t *testing.T) {
	Convey("When getting a transport for a HTTP client", t, func() {
		Convey("Should not skip cert verification if a caCertificatePath was provided", func() {
			t, err := getTransport(setup())
			So(err, ShouldBeNil)
			So(t.TLSClientConfig.InsecureSkipVerify, ShouldBeFalse)
		})

		Convey("Should skip insecure cert verification if an empty string was provided for caCertificatePath", func() {
			t, err := getTransport("")
			So(err, ShouldBeNil)
			So(t.TLSClientConfig.InsecureSkipVerify, ShouldBeTrue)
		})
	})
}
