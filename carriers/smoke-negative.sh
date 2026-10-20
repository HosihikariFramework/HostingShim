#!/usr/bin/env bash
# A fake server that prints the expected lines but exits nonzero must make the
# Linux smoke scripts fail. This guards the "PASS despite exit 42" regression.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

mkdir -p "$tmp/endstone/server" "$tmp/selfhosted"

cat > "$tmp/endstone/fake-endstone" <<'EOF'
#!/usr/bin/env bash
echo "正在把 CLR 托管进 BDS 进程"
echo "Hosihikari: 已就绪"
echo "已接管"
echo "Enabling hosihikari"
echo "Server stop requested"
exit 42
EOF
chmod +x "$tmp/endstone/fake-endstone"

if ENDSTONE_EXE="$tmp/endstone/fake-endstone" \
    HOSIHIKARI_SMOKE_DELAY=0 HOSIHIKARI_SMOKE_SETTLE=0 \
    "$here/endstone/smoke.sh" "$tmp/endstone/server" > "$tmp/endstone.out" 2>&1; then
    echo "FAIL  Endstone smoke accepted a fake server exit 42"
    cat "$tmp/endstone.out"
    exit 1
fi

cat > "$tmp/selfhosted/hosihikari-server" <<'EOF'
#!/usr/bin/env bash
echo "Server started"
echo "已加载 0 个插件：[]"
echo "Server stop requested"
exit 42
EOF
chmod +x "$tmp/selfhosted/hosihikari-server"

if HOSIHIKARI_SMOKE_DELAY=0 \
    "$here/selfhosted-linux/smoke.sh" "$tmp/selfhosted" > "$tmp/selfhosted.out" 2>&1; then
    echo "FAIL  self-hosted smoke accepted a fake server exit 42"
    cat "$tmp/selfhosted.out"
    exit 1
fi

echo "PASS  smoke scripts reject a nonzero server exit"
