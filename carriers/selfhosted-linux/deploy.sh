#!/usr/bin/env bash
# Copy the self-hosted shim, nethost, launcher and, for source deployments, the managed core.
#
#   ./deploy.sh <bedrock server folder> [shim-only]
#
# The managed core is taken from a sibling checkout of Hosihikari.PluginManager,
# or from HOSIHIKARI_PLUGINMANAGER. Release packaging passes shim-only: the core
# then comes from its own release archive.
set -euo pipefail

if [ $# -lt 1 ]; then
    echo "usage: $0 <bedrock server folder> [shim-only]" >&2
    exit 1
fi

shim_only=${2:-}

here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$1"
server=$(cd "$1" && pwd)
managed_root=${HOSIHIKARI_PLUGINMANAGER:-"$here/../../../PluginManager"}
managed="$managed_root/src/bin/Release/net10.0"
plugin="$here/build/libhosihikari.so"
nethost="$here/build/nethost/runtimes/linux-x64/native/libnethost.so"
dst="$server/plugins/hosihikari"

if [ ! -f "$plugin" ]; then
    echo "run build.sh first" >&2
    exit 1
fi

rm -rf "$dst"
mkdir -p "$dst"

cp -f "$plugin" "$dst/libhosihikari.so"
cp -f "$nethost" "$dst/libnethost.so"

cp -f "$here/hosihikari-server" "$server/hosihikari-server"
chmod +x "$server/hosihikari-server"

# 载体清单（ADR-0065）：核心据此认载体包，组件与服务配置器都在这里声明。
carrier_version=${3:-0.0.0-local}
cat > "$dst/hosihikari.json" <<EOF
{
  "schemaVersion": 1,
  "name": "hosihikari",
  "version": "$carrier_version",
  "entry": "hosihikari.dll",
  "loader": "assembly",
  "kind": "carrier",
  "apiVersion": 3,
  "dependencies": [],
  "carrier": { "sharedAssemblies": [], "components": [] }
}
EOF

if [ "$shim_only" = "shim-only" ]; then
    echo "deployed shim-only to $dst"
    exit 0
fi

if [ ! -d "$managed" ]; then
    echo "build Hosihikari.PluginManager in Release first: $managed" >&2
    exit 1
fi

cp -f "$managed"/*.dll "$managed"/*.json "$dst/"
if compgen -G "$managed/*.pdb" > /dev/null; then
    cp -f "$managed"/*.pdb "$dst/"
fi
# The localized resources live in culture subdirectories (zh/).
for dir in "$managed"/*/; do
    [ -d "$dir" ] && cp -R "${dir%/}" "$dst/"
done

echo "deployed to $dst"
