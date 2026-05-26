#!/usr/bin/env bash

set -u

PORT="${1:-18080}"
SERVER="./minihttpd"
LOG_FILE="/tmp/minihttpd-test.log"
PASS_COUNT=0
FAIL_COUNT=0
SERVER_PID=""

cleanup() {
    if [[ -n "${SERVER_PID}" ]]; then
        kill "${SERVER_PID}" 2>/dev/null || true
        wait "${SERVER_PID}" 2>/dev/null || true
    fi
}

trap cleanup EXIT

pass() {
    echo "[OK] $1"
    PASS_COUNT=$((PASS_COUNT + 1))
}

fail() {
    echo "[FAIL] $1"
    FAIL_COUNT=$((FAIL_COUNT + 1))
}

require_command() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "Falta el comando requerido: $1"
        exit 1
    fi
}

require_command curl
require_command nc
require_command make

echo "Compilando MiniHTTPd..."
if ! make clean >/dev/null 2>&1 || ! make >/dev/null 2>&1; then
    echo "Error al compilar."
    exit 1
fi

echo "Iniciando servidor en puerto ${PORT}..."
"${SERVER}" "${PORT}" > "${LOG_FILE}" 2>&1 &
SERVER_PID=$!

for _ in $(seq 1 20); do
    if nc -z 127.0.0.1 "${PORT}" >/dev/null 2>&1; then
        break
    fi
    sleep 0.1
done

if ! nc -z 127.0.0.1 "${PORT}" >/dev/null 2>&1; then
    echo "El servidor no inicio correctamente."
    echo "Log:"
    cat "${LOG_FILE}"
    exit 1
fi

assert_status() {
    local name="$1"
    local expected="$2"
    local url="$3"
    local method="${4:-GET}"

    local status

    status="$(curl -s -o /tmp/minihttpd-body.tmp -w "%{http_code}" -X "${method}" "${url}")"

    if [[ "${status}" == "${expected}" ]]; then
        pass "${name}"
    else
        fail "${name}: esperado ${expected}, recibido ${status}"
    fi
}

assert_content_type() {
    local name="$1"
    local expected="$2"
    local url="$3"

    local headers

    headers="$(curl -s -D - -o /tmp/minihttpd-body.tmp "${url}")"

    if echo "${headers}" | grep -qi "Content-Type: ${expected}"; then
        pass "${name}"
    else
        fail "${name}: no se encontro Content-Type: ${expected}"
        echo "${headers}"
    fi
}

BASE_URL="http://127.0.0.1:${PORT}"

assert_status "GET / devuelve 200" "200" "${BASE_URL}/"
assert_content_type "GET / usa text/html" "text/html" "${BASE_URL}/"

assert_status "GET /style.css devuelve 200" "200" "${BASE_URL}/style.css"
assert_content_type "GET /style.css usa text/css" "text/css" "${BASE_URL}/style.css"

assert_status "GET /image.png devuelve 200" "200" "${BASE_URL}/image.png"
assert_content_type "GET /image.png usa image/png" "image/png" "${BASE_URL}/image.png"

assert_status "GET /noexiste.html devuelve 404" "404" "${BASE_URL}/noexiste.html"

assert_status "POST / devuelve 405" "405" "${BASE_URL}/" "POST"

TRAVERSAL_STATUS="$(curl -s -o /tmp/minihttpd-body.tmp -w "%{http_code}" --path-as-is "${BASE_URL}/../../etc/passwd")"

if [[ "${TRAVERSAL_STATUS}" == "403" ]]; then
    pass "Directory Traversal devuelve 403"
else
    fail "Directory Traversal: esperado 403, recibido ${TRAVERSAL_STATUS}"
fi

BAD_RESPONSE="$(printf 'GET\r\n\r\n' | nc 127.0.0.1 "${PORT}")"

if echo "${BAD_RESPONSE}" | grep -q "HTTP/1.1 400 Bad Request"; then
    pass "Solicitud mal formada devuelve 400"
else
    fail "Solicitud mal formada no devolvio 400"
    echo "${BAD_RESPONSE}"
fi

OVERSIZED_RESPONSE="$(
python3 - <<'PY' | nc 127.0.0.1 "${PORT}"
uri = "/" + ("A" * 9000)
print(f"GET {uri} HTTP/1.1\r\nHost: localhost\r\n\r\n", end="")
PY
)"

if echo "${OVERSIZED_RESPONSE}" | grep -q "HTTP/1.1 400 Bad Request"; then
    pass "Solicitud demasiado grande devuelve 400"
else
    fail "Solicitud demasiado grande no devolvio 400"
    echo "${OVERSIZED_RESPONSE}"
fi

KEEP_ALIVE_RESPONSE="$(
{
    printf 'GET / HTTP/1.1\r\nHost: localhost\r\nConnection: keep-alive\r\n\r\n'
    sleep 0.2
    printf 'GET /style.css HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n'
} | nc 127.0.0.1 "${PORT}"
)"

KEEP_ALIVE_COUNT="$(echo "${KEEP_ALIVE_RESPONSE}" | grep -c "HTTP/1.1 200 OK" || true)"

if [[ "${KEEP_ALIVE_COUNT}" == "2" ]]; then
    pass "Keep-alive responde dos solicitudes en la misma conexion"
else
    fail "Keep-alive: esperado 2 respuestas 200, recibidas ${KEEP_ALIVE_COUNT}"
    echo "${KEEP_ALIVE_RESPONSE}"
fi

echo
echo "Resumen:"
echo "Aprobadas: ${PASS_COUNT}"
echo "Fallidas: ${FAIL_COUNT}"

if [[ "${FAIL_COUNT}" -ne 0 ]]; then
    echo
    echo "Log del servidor:"
    cat "${LOG_FILE}"
    exit 1
fi

exit 0
