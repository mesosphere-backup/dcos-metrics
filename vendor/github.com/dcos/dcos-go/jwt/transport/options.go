package transport

import (
	"encoding/json"
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

// Option is a functional option used to build a custom JWTTransport.
type Option func(*implWithJWT) error

// OptionTokenExpire is an option to set JWT expiration date. If not set 24h is ussed by default.
func OptionTokenExpire(t time.Duration) Option {
	return func(j *implWithJWT) error {
		if t < 1 {
			return ErrInvalidExpireDuration
		}
		j.expire = t
		return nil
	}
}

// OptionCredentials is an option to set uid, secret and loginEndpoint.
func OptionCredentials(uid, secret, loginEndpoint string) Option {
	return func(j *implWithJWT) error {
		if uid == "" || secret == "" || loginEndpoint == "" {
			return ErrInvalidCredentials
		}
		j.uid = uid
		j.secret = secret
		j.loginEndpoint = loginEndpoint
		return nil
	}
}

// OptionReadIAMConfig is an option to read the IAMConfig from file system and populate uid, secret and loginEndpoint.
func OptionReadIAMConfig(path string) Option {
	return func(j *implWithJWT) error {
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
