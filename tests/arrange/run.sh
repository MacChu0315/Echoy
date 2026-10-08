#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
node tests/arrange/oracle.mjs
ECHOY_ARRANGE_SANITIZE=1 node tests/arrange/oracle.mjs
