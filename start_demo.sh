#!/bin/bash
# Starts the backend with fixed, predictable demo credentials instead of the
# random one-time passwords AuthManager normally generates and prints once
# to the server log (see src/server/auth_manager.cpp: seed_staff_password).
# Same auth flow, same server-side checks -- this only fixes what the
# passwords are, so a demo doesn't depend on scraping server.log first.
# The sign-in screen's "Quick Demo Sign-In" buttons assume this exact
# password (web/src/components/SignInScreen.ts: DEMO_PASSWORD).
set -e
cd "$(dirname "$0")"

DEMO_PASSWORD="demo1234"

echo "Stopping any server already running..."
pkill -f "build/optic-trigeminal" 2>/dev/null || true
sleep 1

if [ ! -f ./build/optic-trigeminal ]; then
  echo "No build found -- building first (./build.sh)..."
  ./build.sh
fi

export ACMK_ADMIN_PASSWORD="$DEMO_PASSWORD"
export ACMK_RN001_PASSWORD="$DEMO_PASSWORD"
export ACMK_CHARGE001_PASSWORD="$DEMO_PASSWORD"
export ACMK_PROVIDER001_PASSWORD="$DEMO_PASSWORD"
export ACMK_IT001_PASSWORD="$DEMO_PASSWORD"
export ACMK_INSTRUCTOR001_PASSWORD="$DEMO_PASSWORD"

nohup ./build/optic-trigeminal > server.log 2>&1 &
SERVER_PID=$!
echo "Server starting (PID $SERVER_PID)..."

READY=""
for i in $(seq 1 60); do
  if curl -s http://localhost:8080/health 2>/dev/null | grep -q healthy; then
    READY="1"
    break
  fi
  sleep 1
done

if [ -z "$READY" ]; then
  echo "Server did not come up within 60s -- check server.log"
  exit 1
fi

cat <<EOF

Server is up: http://localhost:8080

Demo credentials (same password for every account): $DEMO_PASSWORD

  Staff ID        Role
  --------------  -------------------
  RN_001          Nurse (RN/LPN)
  CHARGE_001      Charge Nurse
  PROVIDER_001    Provider (MD/DO)
  ADMIN_001       Admin / Audit
  IT_001          IT / System
  INSTRUCTOR_001  Instructor (class cohorts)

The sign-in screen's "Quick Demo Sign-In" buttons use these automatically.

The frontend build is embedded directly in this binary (see
include/embedded_web_assets.h) -- open http://localhost:8080 directly,
no separate frontend dev server needed. (For active frontend development
with hot-reload instead, cd web && npm run dev and use :5173 as before --
that still works too.)
EOF
