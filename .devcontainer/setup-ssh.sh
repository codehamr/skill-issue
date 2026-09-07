#!/bin/sh
# Refresh the read-only host snapshot with permissions OpenSSH accepts.
# Optional source/target arguments let fixtures use isolated Unix directories.
set -eu

ssh_host_dir=${1:-/root/.ssh-host}
ssh_local_dir=${2:-/root/.ssh}
if [ ! -d "$ssh_host_dir" ]; then
    printf '%s\n' "SSH host mount missing: $ssh_host_dir" >&2
    exit 1
fi
mkdir -p "$ssh_local_dir"
chmod 700 "$ssh_local_dir"
cp -rL "$ssh_host_dir/." "$ssh_local_dir/"
find "$ssh_local_dir" -type d -exec chmod 700 {} +
find "$ssh_local_dir" -type f -exec chmod 600 {} +

# Git configuration is read only. A .pub signing path can use the adjacent
# private key, but OpenSSH needs the public file to exist first.
ssh_signing_key=$(git config --path --get user.signingkey || true)
case "$ssh_signing_key" in
    "$ssh_local_dir"/*.pub)
        ssh_private_key=${ssh_signing_key%.pub}
        ssh_host_public="$ssh_host_dir/${ssh_signing_key#"$ssh_local_dir"/}"
        if [ -f "$ssh_private_key" ] && [ ! -f "$ssh_host_public" ]; then
            # Re-derive when the host supplies only a private key, including
            # after key rotation. Never prompt for a passphrase in a hook.
            ssh_public_tmp=$(mktemp "$ssh_local_dir/.public.XXXXXX")
            trap 'rm -f "$ssh_public_tmp"' EXIT
            if ! ssh-keygen -y -P '' -f "$ssh_private_key" >"$ssh_public_tmp" 2>/dev/null; then
                printf '%s\n' "Cannot derive $ssh_signing_key noninteractively; provide its .pub file on the host." >&2
                exit 1
            fi
            chmod 600 "$ssh_public_tmp"
            mv "$ssh_public_tmp" "$ssh_signing_key"
            trap - EXIT
        fi
        ;;
esac
printf '%s\n' 'SSH host files refreshed; local permissions ready.'
