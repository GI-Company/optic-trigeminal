#!/usr/bin/env python3

import subprocess
import time
import requests
import json
import sys

API_URL = "http://localhost:8080"
TESTS_PASSED = 0
TESTS_FAILED = 0

def log_test(msg):
    print(f"\033[93m[TEST]\033[0m {msg}")

def log_pass(msg):
    global TESTS_PASSED
    print(f"\033[92m[PASS]\033[0m {msg}")
    TESTS_PASSED += 1

def log_fail(msg):
    global TESTS_FAILED
    print(f"\033[91m[FAIL]\033[0m {msg}")
    TESTS_FAILED += 1

def wait_for_server(timeout=30):
    start = time.time()
    while time.time() - start < timeout:
        try:
            resp = requests.get(f"{API_URL}/health", timeout=2)
            if resp.status_code == 200:
                print(f"\033[92mServer is ready\033[0m")
                return True
        except:
            pass
        time.sleep(0.5)
    print(f"\033[91mServer failed to start\033[0m")
    return False

def test_health():
    log_test("Health check")
    try:
        resp = requests.get(f"{API_URL}/health")
        if resp.status_code == 200 and "healthy" in resp.text:
            log_pass("Health check")
        else:
            log_fail(f"Health check - Status: {resp.status_code}")
    except Exception as e:
        log_fail(f"Health check - {e}")

def test_math(prompt, expected):
    log_test(f"Math: {prompt}")
    try:
        resp = requests.post(f"{API_URL}/api/inference/native",
                            json={"prompt": prompt})
        if resp.status_code == 200:
            text = resp.text
            if expected in text:
                log_pass(f"Math: {prompt}")
            else:
                log_fail(f"Math: {prompt} - Got: {text[:100]}")
        else:
            log_fail(f"Math: {prompt} - Status: {resp.status_code}")
    except Exception as e:
        log_fail(f"Math: {prompt} - {e}")

def test_logic(prompt, expected):
    log_test(f"Logic: {prompt}")
    try:
        resp = requests.post(f"{API_URL}/api/inference/native",
                            json={"prompt": prompt})
        if resp.status_code == 200:
            text = resp.text
            if expected in text:
                log_pass(f"Logic: {prompt}")
            else:
                log_fail(f"Logic: {prompt} - Got: {text[:100]}")
        else:
            log_fail(f"Logic: {prompt} - Status: {resp.status_code}")
    except Exception as e:
        log_fail(f"Logic: {prompt} - {e}")

def test_scenario(scenario_id):
    log_test(f"Start scenario: {scenario_id}")
    try:
        resp = requests.post(f"{API_URL}/api/training/start",
                            json={
                                "scenario_id": scenario_id,
                                "nurse_id": "TEST_NURSE",
                                "nurse_role": "RN"
                            })
        if resp.status_code == 200 and "training_session_started" in resp.text:
            log_pass(f"Start scenario: {scenario_id}")
            return True
        else:
            log_fail(f"Start scenario: {scenario_id} - Status: {resp.status_code}")
            return False
    except Exception as e:
        log_fail(f"Start scenario: {scenario_id} - {e}")
        return False

def test_training_status():
    log_test("Get training status")
    try:
        resp = requests.get(f"{API_URL}/api/training/status")
        if resp.status_code == 200 and "vitals" in resp.text:
            log_pass("Get training status")
        else:
            log_fail(f"Get training status - Status: {resp.status_code}")
    except Exception as e:
        log_fail(f"Get training status - {e}")

def test_training_tick():
    log_test("Training tick (30 seconds)")
    try:
        resp = requests.post(f"{API_URL}/api/training/tick",
                            json={"delta_seconds": 30})
        if resp.status_code == 200:
            log_pass("Training tick")
        else:
            log_fail(f"Training tick - Status: {resp.status_code}")
    except Exception as e:
        log_fail(f"Training tick - {e}")

def test_end_scenario():
    log_test("End training scenario")
    try:
        resp = requests.post(f"{API_URL}/api/training/end",
                            json={"outcome": "completed"})
        if resp.status_code == 200:
            log_pass("End training scenario")
        else:
            log_fail(f"End training scenario - Status: {resp.status_code}")
    except Exception as e:
        log_fail(f"End training scenario - {e}")

def test_patients():
    log_test("Get patient vitals")
    try:
        resp = requests.get(f"{API_URL}/api/clinical/patients/all")
        if resp.status_code == 200 and "vitals" in resp.text:
            log_pass("Get patient vitals")
        else:
            log_fail(f"Get patient vitals - Status: {resp.status_code}")
    except Exception as e:
        log_fail(f"Get patient vitals - {e}")

def main():
    print("\033[93m" + "=" * 40)
    print("OpticTrigeminal System Test Suite")
    print("=" * 40 + "\033[0m\n")
    
    print("Starting OpticTrigeminal server...")
    proc = subprocess.Popen(["./build/optic-trigeminal"],
                           stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL)
    time.sleep(2)
    
    if not wait_for_server():
        proc.terminate()
        sys.exit(1)
    
    print("\n\033[93m========== BASIC ENDPOINT TESTS =========\033[0m\n")
    test_health()
    test_patients()
    
    print("\n\033[93m========== INFERENCE TESTS =========\033[0m\n")
    test_math("what is 2 + 2", "4")
    test_math("calculate (10 + 5) * 2", "30")
    test_logic("if true then false", "false")
    
    print("\n\033[93m========== TRAINING SCENARIO TESTS =========\033[0m\n")
    
    scenarios = [
        "RESPIRATORY_001",
        "HYPOTENSION_001",
        "SEPSIS_EARLY_001",
        "CARDIAC_ARREST_001",
        "STROKE_ALERT_001",
        "DKA_CRISIS_001",
        "ANAPHYLAXIS_001",
        "SEVERE_BLEEDING_001"
    ]
    
    for scenario_id in scenarios:
        if test_scenario(scenario_id):
            time.sleep(0.5)
            test_training_status()
            test_training_tick()
            test_training_tick()
            time.sleep(0.5)
            test_end_scenario()
            time.sleep(0.5)
        print()
    
    print("\033[93m" + "=" * 40)
    print(f"\033[92mTests Passed: {TESTS_PASSED}\033[0m")
    print(f"\033[91mTests Failed: {TESTS_FAILED}\033[0m")
    print("\033[93m" + "=" * 40 + "\033[0m")
    
    proc.terminate()
    proc.wait()
    
    if TESTS_FAILED == 0:
        print(f"\n\033[92mAll tests passed!\033[0m")
        sys.exit(0)
    else:
        print(f"\n\033[91mSome tests failed!\033[0m")
        sys.exit(1)

if __name__ == "__main__":
    main()
