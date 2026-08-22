#!/bin/bash
set -x

# Kill any existing server
killall optic-trigeminal 2>/dev/null || true
rm -f server.log curl_output.log

# Start server in background
./build/optic-trigeminal > server.log 2>&1 &
SERVER_PID=$!
echo "Server started with PID $SERVER_PID"

# Wait for it to bind to the port
echo "Waiting for HTTP Server to listen on 8080..."
for i in {1..120}; do
    if grep -q "HTTP Server listening on port 8080" server.log; then
        echo "Server is fully booted!"
        break
    fi
    sleep 1
done

echo "Sending curl request to Groq integration endpoint..."
curl -v -X POST http://localhost:8080/api/inference/enhanced \
     -H "Content-Type: application/json" \
     -d '{"prompt": "Calculate 2+2 and explain it in detail."}' > curl_output.log 2>&1

CURL_EXIT=$?
echo "Curl finished with exit code $CURL_EXIT"

# Cleanup
kill $SERVER_PID || true
