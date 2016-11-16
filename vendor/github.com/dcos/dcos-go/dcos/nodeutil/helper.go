package nodeutil

import "regexp"

func validateUUID(text string) bool {
	r := regexp.MustCompile("[a-f0-9]{8}-[a-f0-9]{4}-4[a-f0-9]{3}-[8|9|aA|bB][a-f0-9]{3}-[a-f0-9]{12}")
	return r.MatchString(text)
}
