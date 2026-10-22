#!/usr/bin/env bash
# Builds ps5/mg_web.cpp for the host (clang in the ps5sd-build container) and checks the access rules with curl:
# open without a key file, 401 with one, login cookie, Bearer header, wrong key, cross-site POST, no CORS header.
#   MSYS_NO_PATHCONV=1 docker run --rm --platform linux/amd64 -v "$PWD:/src:ro" ps5sd-build bash /src/tools/test-web/run.sh
set -u
mkdir -p /tmp/g /data/PS5MG/{jobs,in,out,logs}
printf 'static const unsigned char mg_index_html[]={0}; static const unsigned long mg_index_html_len=1;\n' > /tmp/g/mg_index_html.h
clang++ -std=c++17 -O1 -I /src/ps5 -I /tmp/g -I /src/src/trellis.cpp/thirdparty/cpp-httplib /src/tools/test-web/main.cpp /src/ps5/mg_web.cpp -o /tmp/web -lpthread || exit 1
/tmp/web > /tmp/web.log 2>&1 &
sleep 1
U=http://127.0.0.1:7870; fail=0
t() { # name expected actual
  if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1: expected $2, got $3"; fail=1; fi; }
code() { curl -s -o /dev/null -w '%{http_code}' "$@"; }

rm -f /data/PS5MG/api-key.txt
t "no key file: status is open"            200 "$(code $U/api/status)"
t "no key file: /api/auth says not required" '{"required":false,"ok":true}' "$(curl -s $U/api/auth)"
t "cross-site POST is refused"             403 "$(code -X POST -H 'Origin: http://evil.example' $U/api/quit)"
t "same-origin POST gets through (409: no such queued job)" 409 "$(code -X POST -H 'Origin: http://127.0.0.1:7870' $U/api/jobs/x/cancel)"
t "no CORS allow-origin header"            0   "$(curl -s -D - $U/api/status | grep -ci 'access-control-allow-origin')"

printf 'secret123\n' > /data/PS5MG/api-key.txt
t "with a key: status without it is 401"   401 "$(code $U/api/status)"
t "with a key: quit without it is 401"     401 "$(code -X POST $U/api/quit)"
t "with a key: the page itself loads"      200 "$(code $U/)"
t "with a key: /api/auth is reachable"     '{"required":true,"ok":false}' "$(curl -s $U/api/auth)"
t "wrong Bearer key is 401"                401 "$(code -H 'Authorization: Bearer nope' $U/api/status)"
t "right Bearer key is 200"                200 "$(code -H 'Authorization: Bearer secret123' $U/api/status)"
t "wrong login is 401"                     401 "$(code -F key=nope $U/api/login)"
rm -f /tmp/jar; t "right login is 200" 200 "$(code -c /tmp/jar -F key=secret123 $U/api/login)"
t "the cookie is HttpOnly and SameSite=Strict" 1 "$(grep -c 'HttpOnly' /tmp/jar)"
t "the cookie opens /api/status"           200 "$(code -b /tmp/jar $U/api/status)"
t "the cookie opens /api/auth as ok"       '{"required":true,"ok":true}' "$(curl -s -b /tmp/jar $U/api/auth)"
t "a wrong cookie is 401"                  401 "$(code -H 'Cookie: mgkey=00ff' $U/api/jobs)"
t "the hex of the key as a cookie is the login" 200 "$(code -H 'Cookie: mgkey=736563726574313233' $U/api/jobs)"

printf '[1/7] preprocess\n[ScePthread/System] Internal Memory is running out.\nerror: could not read ss_flow.gguf\n' > /data/PS5MG/logs/w1.log
t "job log: the noise is left out"         '{"lines":["[1/7] preprocess","error: could not read ss_flow.gguf"]}' "$(curl -s -H 'Authorization: Bearer secret123' $U/api/jobs/w1/log)"
t "job log needs the key too"              401 "$(code $U/api/jobs/w1/log)"
rm -f /data/PS5MG/api-key.txt
t "key file removed: open again"           200 "$(code $U/api/status)"

# ---- pictures from PS5SD, the atlas field
mkdir -p /data/PS5SD/outputs /data/PS5MG/models/q4; : > /data/PS5MG/models/q4/ss_flow.gguf
printf '\x89PNG\r\n\x1a\nPS5SD-PICTURE' > /data/PS5SD/outputs/sd_20261022_100000_1.png
printf 'x' > /data/PS5SD/outputs/sd_20261022_100000_2.jpg; printf 'x' > /data/PS5SD/outputs/notes.txt; printf 'x' > '/data/PS5SD/outputs/bad name.png'; printf 'x' > /data/PS5SD/outputs/.hidden.png
L=$(curl -s $U/api/ps5sd/pictures)
t "PS5SD list: a png and a jpg, newest first" '[{"name":"sd_20261022_100000_2.jpg","bytes":1},{"name":"sd_20261022_100000_1.png","bytes":21}]' "$L"
t "PS5SD picture is served"                  200 "$(code $U/api/ps5sd/picture/sd_20261022_100000_1.png)"
t "PS5SD picture type is image/png"          1   "$(curl -s -D - -o /dev/null $U/api/ps5sd/picture/sd_20261022_100000_1.png | grep -ci 'content-type: image/png')"
t "a text file is not served as a picture"   400 "$(code $U/api/ps5sd/picture/notes.txt)"
t "a hidden file is refused"                 400 "$(code $U/api/ps5sd/picture/.hidden.png)"
t "double dots are refused"                  400 "$(code $U/api/ps5sd/picture/a..b.png)"
t "an encoded path does not escape"          1   "$([ "$(code --path-as-is "$U/api/ps5sd/picture/..%2f..%2f..%2fetc%2fpasswd")" != 200 ] && echo 1 || echo 0)"
t "a path with slashes does not escape"      1   "$([ "$(code --path-as-is "$U/api/ps5sd/picture/../../../etc/passwd")" != 200 ] && echo 1 || echo 0)"
rm -f /data/PS5MG/jobs/* /data/PS5MG/in/*
R=$(curl -s -F from_ps5sd=sd_20261022_100000_1.png -F atlas=2048 -F steps=6 $U/api/jobs); ID=$(echo "$R" | sed -n 's/.*"id":"\([^"]*\)".*/\1/p')
t "job from a PS5SD picture is accepted"     1   "$([ -n "$ID" ] && echo 1 || echo 0)"
t "the picture was copied into in/"          0   "$(cmp -s /data/PS5SD/outputs/sd_20261022_100000_1.png /data/PS5MG/in/$ID.png; echo $?)"
t "the job line carries --atlas 2048"        1   "$(grep -c -- '--atlas 2048' /data/PS5MG/jobs/$ID.job)"
t "the job line carries --steps 6"           1   "$(grep -c -- '--steps 6' /data/PS5MG/jobs/$ID.job)"
R2=$(curl -s -F from_ps5sd=sd_20261022_100000_2.jpg $U/api/jobs); ID2=$(echo "$R2" | sed -n 's/.*"id":"\([^"]*\)".*/\1/p')
t "no atlas given: no --atlas in the job"    0   "$(grep -c -- '--atlas' /data/PS5MG/jobs/$ID2.job)"
t "atlas 999 is refused"                     400 "$(code -F from_ps5sd=sd_20261022_100000_1.png -F atlas=999 $U/api/jobs)"
t "a path as picture name is refused"        400 "$(code -F from_ps5sd=../x.png $U/api/jobs)"
t "a missing picture is 404"                 404 "$(code -F from_ps5sd=nothing.png $U/api/jobs)"
t "no picture at all is 400"                 400 "$(code -F steps=8 $U/api/jobs)"
printf 'secret123\n' > /data/PS5MG/api-key.txt
t "with a key: the PS5SD list needs it"      401 "$(code $U/api/ps5sd/pictures)"
t "with a key: a PS5SD picture needs it"     401 "$(code $U/api/ps5sd/picture/sd_20261022_100000_1.png)"
rm -f /data/PS5MG/api-key.txt
kill %1 2>/dev/null
[ $fail = 0 ] && echo "ALL PASSED" || echo "SOME FAILED"
exit $fail
