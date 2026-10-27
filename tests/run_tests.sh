#!/bin/sh
# Runs every test that does not need Qt. From the project root:  sh tests/run_tests.sh
set -e
cd "$(dirname "$0")/.."
echo "== C++ core (original flood logic) =="
g++ -std=c++11 -Wall -Wextra -Ibackend tests/test_core.cpp backend/FloodCore.cpp -o /tmp/flood_test_core
/tmp/flood_test_core
echo "== JavaScript syntax =="
for f in frontend/js/*.js; do node --check "$f"; done; echo ok
echo "== Weather logic =="
node tests/weather_test.js
if NODE_PATH="$(npm root -g)" node -e "require('playwright')" 2>/dev/null; then
  echo "== UI smoke test (mock backend) =="
  NODE_PATH="$(npm root -g)" node tests/ui_smoke.js
else
  echo "(UI smoke test skipped: playwright not installed)"
fi
