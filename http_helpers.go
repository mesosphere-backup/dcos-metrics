package main

import (
	"crypto/tls"
	"crypto/x509"
	"errors"
	"io/ioutil"
	"net/http"
	"os"

	log "github.com/Sirupsen/logrus"
	"github.com/dcos/dcos-go/jwt/transport"
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

func getClient(caCertificatePath string, iamConfigPath string) (*http.Client, error) {
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
