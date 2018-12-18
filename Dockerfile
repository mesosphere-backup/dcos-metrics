FROM golang:1.9.3

RUN go get -u github.com/jstemmer/go-junit-report
RUN go get -u github.com/smartystreets/goconvey
RUN go get -u golang.org/x/tools/cmd/cover
RUN go get -u github.com/axw/gocov/...
RUN go get -u github.com/AlekSi/gocov-xml
RUN go get -u golang.org/x/tools/cmd/goimports
RUN go get -u golang.org/x/lint/golint
