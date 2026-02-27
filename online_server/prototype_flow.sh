#!/usr/bin/env bash
set -euo pipefail

BASE_URL=${BASE_URL:-http://127.0.0.1:8787}

echo "== register two players =="
A_JSON=$(curl -sS -X POST "$BASE_URL/auth/register" -H 'content-type: application/json' -d '{"email":"a@example.com","password":"password123","display_name":"Alpha"}')
B_JSON=$(curl -sS -X POST "$BASE_URL/auth/register" -H 'content-type: application/json' -d '{"email":"b@example.com","password":"password123","display_name":"Bravo"}')

A_ACCESS=$(python - <<'PY' "$A_JSON"
import json,sys
print(json.loads(sys.argv[1])["access_token"])
PY
)
B_ACCESS=$(python - <<'PY' "$B_JSON"
import json,sys
print(json.loads(sys.argv[1])["access_token"])
PY
)

echo "== get profiles =="
A_ME=$(curl -sS "$BASE_URL/me" -H "authorization: Bearer $A_ACCESS")
B_ME=$(curl -sS "$BASE_URL/me" -H "authorization: Bearer $B_ACCESS")
echo "$A_ME"
echo "$B_ME"

A_ID=$(python - <<'PY' "$A_ME"
import json,sys
print(json.loads(sys.argv[1])["user_id"])
PY
)
B_ID=$(python - <<'PY' "$B_ME"
import json,sys
print(json.loads(sys.argv[1])["user_id"])
PY
)

echo "== friend request + accept =="
curl -sS -X POST "$BASE_URL/friends/request" -H "authorization: Bearer $A_ACCESS" -H 'content-type: application/json' -d "{\"target_user_id\":\"$B_ID\"}" | cat
curl -sS -X POST "$BASE_URL/friends/accept" -H "authorization: Bearer $B_ACCESS" -H 'content-type: application/json' -d "{\"target_user_id\":\"$A_ID\"}" | cat

echo "== queue ranked and get match =="
A_Q=$(curl -sS -X POST "$BASE_URL/queue/join" -H "authorization: Bearer $A_ACCESS" -H 'content-type: application/json' -d '{"mode":"ranked"}')
B_Q=$(curl -sS -X POST "$BASE_URL/queue/join" -H "authorization: Bearer $B_ACCESS" -H 'content-type: application/json' -d '{"mode":"ranked"}')
echo "$A_Q"
echo "$B_Q"

MATCH_ID=$(python - <<'PY' "$A_Q" "$B_Q"
import json,sys
for v in sys.argv[1:]:
    d=json.loads(v)
    if d.get("match_id"):
        print(d["match_id"])
        break
PY
)

echo "== both report same winner (Alpha) =="
REPORT_A=$(curl -sS -X POST "$BASE_URL/match/$MATCH_ID/report" -H "authorization: Bearer $A_ACCESS" -H 'content-type: application/json' -d "{\"winner_user_id\":\"$A_ID\"}")
REPORT_B=$(curl -sS -X POST "$BASE_URL/match/$MATCH_ID/report" -H "authorization: Bearer $B_ACCESS" -H 'content-type: application/json' -d "{\"winner_user_id\":\"$A_ID\"}")
echo "$REPORT_A"
echo "$REPORT_B"

echo "== final ratings =="
curl -sS "$BASE_URL/me" -H "authorization: Bearer $A_ACCESS" | cat
curl -sS "$BASE_URL/me" -H "authorization: Bearer $B_ACCESS" | cat
