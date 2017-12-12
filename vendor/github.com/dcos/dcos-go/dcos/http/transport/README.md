# dcos-go/jwt/transport

#### Warning.
- package breaks `RoundTripper` interface spec defined in `https://golang.org/pkg/net/http/#RoundTripper`
  by mutating request instance.

jwt/transport is an http.RoundTripper implementation that automatically adds `Authorization` header to each request with
a signed token. If request returns 401 response code, the library will generate a new token, sign it with a bouncer and
retry the current request.
