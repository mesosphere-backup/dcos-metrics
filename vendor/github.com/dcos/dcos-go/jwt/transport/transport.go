package transport

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"net/http"
	"sync"
	"time"

	"github.com/dgrijalva/jwt-go"
)

// ErrTokenRefresh is an error type returned by `RoundTrip` if the bouncer response was not 200.
type ErrTokenRefresh struct {
	msg string
}

func (e ErrTokenRefresh) Error() string {
	return e.msg
}

var (
	// ErrEmptyToken returned by `GenerateToken` if signed string returned empty string.
	ErrEmptyToken = errors.New("Empty token")

	// ErrWrongRoundTripperImpl returned by `CurrentToken` if http.RoundTripper does not implement implWithJWT.
	ErrWrongRoundTripperImpl = errors.New("RoundTripper does not implement implWithJWT")
)

// Debug is an interface which defines methods to generate a token and get the latest generated token.
type Debug interface {
	GenerateToken() error
	CurrentToken() string
}

// implWithJWT is a wrapper over http.RoundTripper which adds a valid token to each request.
type implWithJWT struct {
	sync.Mutex
	token                      string
	expire                     time.Duration
	uid, secret, loginEndpoint string
	transport                  http.RoundTripper
}

// NewRoundTripper returns RoundTripper implementation with JWT handling.
func NewRoundTripper(rt http.RoundTripper, opts ...Option) (http.RoundTripper, error) {
	if rt == nil {
		rt = http.DefaultTransport
	}

	t := &implWithJWT{
		transport: rt,
	}

	for _, opt := range opts {
		if opt == nil {
			continue
		}

		if err := opt(t); err != nil {
			return nil, err
		}
	}

	// if expire is not set or negative value, default to 5 days.
	if t.expire < 1 {
		t.expire = time.Duration(time.Hour * 24 * 5)
	}

	if err := t.GenerateToken(); err != nil {
		return nil, err
	}

	return t, nil
}

// generateToken is a function that generates JWT and makes a POST request to bouncer to sign it.
func (t *implWithJWT) GenerateToken() error {
	t.Lock()
	defer t.Unlock()

	// TODO: this is very broken with the latest `jwt-go` lib version 3.0.0
	token := jwt.New(jwt.SigningMethodRS256)
	token.Claims["uid"] = t.uid
	token.Claims["exp"] = time.Now().Add(t.expire).Unix()

	tokenStr, err := token.SignedString([]byte(t.secret))
	if err != nil {
		return err
	}

	if tokenStr == "" {
		return ErrEmptyToken
	}

	authReq := struct {
		UID   string `json:"uid"`
		Token string `json:"token,omitempty"`
		Exp   int64  `json:"exp,omitempty"`
	}{
		UID:   t.uid,
		Token: tokenStr,
		Exp:   time.Now().Add(t.expire).Unix(),
	}

	b, err := json.Marshal(authReq)
	if err != nil {
		return err
	}

	authBody := bytes.NewBuffer(b)
	req, err := http.NewRequest("POST", t.loginEndpoint, authBody)
	if err != nil {
		return err
	}

	req.Header.Add("Content-type", "application/json")
	resp, err := t.transport.RoundTrip(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return ErrTokenRefresh{
			msg: fmt.Sprintf("POST %s failed, expect response code 200. Got %d", t.loginEndpoint, resp.StatusCode),
		}
	}

	var authResp struct {
		Token string `json:"token"`
	}

	if err = json.NewDecoder(resp.Body).Decode(&authResp); err != nil {
		return err
	}

	t.token = authResp.Token
	return nil
}

func (t *implWithJWT) CurrentToken() string {
	t.Lock()
	defer t.Unlock()
	return t.token
}

// RoundTrip is implementation of RoundTripper interface.
func (t *implWithJWT) RoundTrip(req *http.Request) (*http.Response, error) {
	// helper function to update `Authorization` header.
	addAuthToken := func() {
		if token := t.CurrentToken(); token != "" {
			req.Header.Set("Authorization", "token="+token)
		}
	}

	var (
		resp *http.Response
		err  error
	)

	// try first time
	addAuthToken()
	resp, err = t.transport.RoundTrip(req)
	if err != nil {
		return resp, err
	}

	// if request returned 401 retry one more time.
	if resp.StatusCode == http.StatusUnauthorized {
		if err := t.GenerateToken(); err != nil {
			return resp, err
		}

		addAuthToken()
		resp, err = t.transport.RoundTrip(req)
	}
	return resp, nil
}

// DebugTransport is a function user can use to get a token from decorated http.RoundTripper if it implements
// implWithJWT.
func DebugTransport(rt http.RoundTripper) (Debug, error) {
	d, ok := rt.(Debug)
	if !ok {
		return nil, ErrWrongRoundTripperImpl
	}
	return d, nil
}
