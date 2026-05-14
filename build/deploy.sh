#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LANDING="${ROOT}/examples/bluejs-landing"
ENV_FILE="${LANDING}/.env"

if [[ ! -f "${ENV_FILE}" ]]; then
    echo "deploy: missing ${ENV_FILE}" >&2
    exit 1
fi

set -a
source "${ENV_FILE}"
set +a

: "${SSH_USER:?deploy: SSH_USER is required in ${ENV_FILE}}"
: "${SSH_HOST:?deploy: SSH_HOST is required in ${ENV_FILE}}"
: "${SSH_PASSWORD:?deploy: SSH_PASSWORD is required in ${ENV_FILE}}"

REMOTE="${SSH_USER}@${SSH_HOST}"
REMOTE_APP_DIR="${REMOTE_APP_DIR:-/var/www/bluejs-landing}"
SSH_OPTS="${SSH_OPTS:--o StrictHostKeyChecking=no}"

ssh_run() {
    sshpass -p "${SSH_PASSWORD}" ssh ${SSH_OPTS} "${REMOTE}" "$@"
}

echo "==> [1/6] Building Blue compiler"
cd "${ROOT}"
make all

echo "==> [2/6] Building landing CSS"
cd "${LANDING}"
chmod +x node_modules/.bin/tailwindcss
npm run build:css

echo "==> [3/6] Packaging releases"
cd "${ROOT}"
bash "${LANDING}/scripts/package-linux-release.sh"
bash "${LANDING}/scripts/package-windows-release.sh"
bash "${ROOT}/build/make-windows-installer.sh"
bash "${LANDING}/scripts/package-debian.sh"

echo "==> [3b] Updating playground package"
cp "${LANDING}/public/downloads/blue-linux-amd64.deb" \
   "${ROOT}/bluejs-playground/.devcontainer/blue.deb"

echo "==> [4/6] Rebuilding landing binary"
cd "${ROOT}"
./blue -build examples/bluejs-landing -o examples/bluejs-landing/bluejs_landing_v1

echo "==> [5/6] Uploading to server"
sshpass -p "${SSH_PASSWORD}" rsync -az --progress \
    -e "ssh ${SSH_OPTS}" \
    "${LANDING}/bluejs_landing_v1" \
    "${REMOTE}:${REMOTE_APP_DIR}/"

sshpass -p "${SSH_PASSWORD}" rsync -az --progress \
    -e "ssh ${SSH_OPTS}" \
    "${LANDING}/public/" \
    "${REMOTE}:${REMOTE_APP_DIR}/public/"

echo "==> [6/6] Restarting service"
ssh_run "
    systemctl stop bluejs-landing
    cp ${REMOTE_APP_DIR}/bluejs_landing_v1 ${REMOTE_APP_DIR}/bluejs_landing
    chmod +x ${REMOTE_APP_DIR}/bluejs_landing
    systemctl start bluejs-landing
    systemctl status bluejs-landing --no-pager
"

echo ""
echo "Deploy complete."
