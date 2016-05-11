#!/bin/sh

echo "$1 => $2"

cat > $2 <<EOF
#pragma once

namespace metrics_schema {
  const std::string SCHEMA_JSON(R"JSON(
EOF

cat $1 >> $2

cat >> $2 <<EOF
)JSON");
}
EOF
