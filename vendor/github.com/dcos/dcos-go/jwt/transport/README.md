# dcos-go/jwt/transport

#### Warning.
- package works only with `github.com/dgrijalva/jwt-go` `v2.6.0`. Please make sure you vendor the right version.
- package breaks `RoundTripper` interface spec defined in `https://golang.org/pkg/net/http/#RoundTripper`
  by mutating request instance.

jwt/transport is an http.RoundTripper implementation that automatically adds `Authorization` header to each request with
a signed token. If request returns 401 response code, the library will generate a new token, sign it with a bouncer and
retry the current request.
