#!/bin/bash
killall optic-trigeminal 2>/dev/null || true
sleep 2

rm -f server.log
./build/optic-trigeminal > server.log 2>&1 &
SERVER_PID=$!
echo "Started server with PID $SERVER_PID. Waiting..."

while ! grep -q "Agentic HTTP Server listening on port 8080 Transparency Features:" server.log; do
  sleep 2
done

echo "Server ready. Testing Groq integration..."
curl -s -v -X POST http://localhost:8080/api/inference/enhanced -d '{"prompt": "Calculate 2+2 and explain it in detail."}' > curl_output.log 2>&1

echo "Curl exit code: $?"
cat curl_output.log

kill $SERVER_PID
