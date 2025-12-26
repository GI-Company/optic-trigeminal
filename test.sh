#!/bin/bash

set -e

API="http://localhost:8080"
TESTS_PASSED=0
TESTS_FAILED=0

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

log_test() {
    echo -e "${YELLOW}[TEST]${NC} $1"
}

log_pass() {
    echo -e "${GREEN}[PASS]${NC} $1"
    ((TESTS_PASSED++))
}

log_fail() {
    echo -e "${RED}[FAIL]${NC} $1"
    ((TESTS_FAILED++))
}

wait_for_server() {
    echo "Waiting for server to start..."
    for i in {1..30}; do
        if curl -s "$API/health" > /dev/null 2>&1; then
            echo -e "${GREEN}Server is ready${NC}"
            return 0
        fi
        sleep 1
    done
    echo -e "${RED}Server failed to start${NC}"
    exit 1
}

test_health() {
    log_test "Health check"
    RESPONSE=$(curl -s "$API/health" 2>/dev/null || echo "")
    if [ ! -z "$RESPONSE" ]; then
        log_pass "Health check"
    else
        log_fail "Health check"
    fi
}

test_math() {
    local prompt="$1"
    local expected="$2"
    log_test "Math: $prompt"
    
    RESPONSE=$(curl -s -X POST "$API/api/inference/native" \
        -H "Content-Type: application/json" \
        -d "{\"prompt\":\"$prompt\"}" 2>/dev/null || echo "")
    
    if [[ "$RESPONSE" == *"$expected"* ]]; then
        log_pass "Math: $prompt"
    else
        log_fail "Math: $prompt"
    fi
}

test_scenario_start() {
    local scenario=$1
    log_test "Start scenario: $scenario"
    
    RESPONSE=$(curl -s -X POST "$API/api/training/start" \
        -H "Content-Type: application/json" \
        -d "{\"scenario_id\":\"$scenario\",\"nurse_id\":\"TEST_NURSE\",\"nurse_role\":\"RN\"}" 2>/dev/null || echo "")
    
    if [[ "$RESPONSE" == *"training_session_started"* ]]; then
        log_pass "Start scenario: $scenario"
        return 0
    else
        log_fail "Start scenario: $scenario"
        return 1
    fi
}

test_training_status() {
    log_test "Get training status"
    RESPONSE=$(curl -s "$API/api/training/status" 2>/dev/null || echo "")
    
    if [[ "$RESPONSE" == *"vitals"* ]]; then
        log_pass "Get training status"
    else
        log_fail "Get training status"
    fi
}

test_training_tick() {
    log_test "Training tick (30 seconds)"
    RESPONSE=$(curl -s -X POST "$API/api/training/tick" \
        -H "Content-Type: application/json" \
        -d '{"delta_seconds":30}' 2>/dev/null || echo "")
    
    if [ ! -z "$RESPONSE" ]; then
        log_pass "Training tick"
    else
        log_fail "Training tick"
    fi
}

test_end_scenario() {
    log_test "End training scenario"
    RESPONSE=$(curl -s -X POST "$API/api/training/end" \
        -H "Content-Type: application/json" \
        -d '{"outcome":"completed"}' 2>/dev/null || echo "")
    
    if [ ! -z "$RESPONSE" ]; then
        log_pass "End training scenario"
    else
        log_fail "End training scenario"
    fi
}

test_patients() {
    log_test "Get patient vitals"
    RESPONSE=$(curl -s "$API/api/clinical/patients/all" 2>/dev/null || echo "")
    
    if [[ "$RESPONSE" == *"vitals"* ]]; then
        log_pass "Get patient vitals"
    else
        log_fail "Get patient vitals"
    fi
}

echo -e "${YELLOW}=======================================${NC}"
echo -e "${YELLOW}OpticTrigeminal System Test Suite${NC}"
echo -e "${YELLOW}=======================================${NC}\n"

# Start the server
echo "Starting OpticTrigeminal server..."
./build/optic-trigeminal > server.log 2>&1 &
SERVER_PID=$!
sleep 3

# Wait for server to be ready
wait_for_server

echo -e "\n${YELLOW}========== BASIC ENDPOINT TESTS ==========${NC}\n"

test_health
test_patients

echo -e "\n${YELLOW}========== INFERENCE TESTS ==========${NC}\n"

test_math "what is 2 + 2" "4"
test_math "calculate (10 + 5) * 2" "30"

echo -e "\n${YELLOW}========== TRAINING SCENARIO TESTS ==========${NC}\n"

scenarios=(
    "RESPIRATORY_001"
    "HYPOTENSION_001"
    "SEPSIS_EARLY_001"
    "CARDIAC_ARREST_001"
    "STROKE_ALERT_001"
    "DKA_CRISIS_001"
    "ANAPHYLAXIS_001"
    "SEVERE_BLEEDING_001"
)

for scenario in "${scenarios[@]}"; do
    if test_scenario_start "$scenario"; then
        sleep 1
        test_training_status
        test_training_tick
        test_training_tick
        sleep 1
        test_end_scenario
    fi
    echo ""
done

echo -e "${YELLOW}=======================================${NC}"
echo -e "${GREEN}Tests Passed: $TESTS_PASSED${NC}"
echo -e "${RED}Tests Failed: $TESTS_FAILED${NC}"
echo -e "${YELLOW}=======================================${NC}"

# Kill server
kill $SERVER_PID 2>/dev/null || true
wait $SERVER_PID 2>/dev/null || true

if [ $TESTS_FAILED -eq 0 ]; then
    echo -e "\n${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e "\n${RED}Some tests failed!${NC}"
    exit 1
fi
