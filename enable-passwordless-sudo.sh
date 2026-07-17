#!/usr/bin/env bash
# Enable passwordless sudo for the current user.
# Run this once (it will prompt for your password). After that, sudo will not
# prompt for a password, which lets automated tooling run privileged commands.
#
# SECURITY NOTE: passwordless sudo means anything running as your user can gain
# root without a password. Remove it later with:
#   sudo rm /etc/sudoers.d/99-${USER}-nopasswd
set -euo pipefail

TARGET_USER="${SUDO_USER:-$(id -un)}"
SUDOERS_FILE="/etc/sudoers.d/99-${TARGET_USER}-nopasswd"
RULE="${TARGET_USER} ALL=(ALL) NOPASSWD: ALL"

TMP="$(mktemp)"
printf '%s\n' "$RULE" > "$TMP"

# Validate syntax before installing to avoid breaking sudo.
if ! sudo visudo -cf "$TMP"; then
    echo "Refusing to install: sudoers syntax check failed." >&2
    rm -f "$TMP"
    exit 1
fi

sudo install -m 0440 -o root -g root "$TMP" "$SUDOERS_FILE"
rm -f "$TMP"

echo "Installed ${SUDOERS_FILE}:"
echo "  ${RULE}"
echo "Passwordless sudo is now enabled for ${TARGET_USER}."
