#!/usr/bin/env bash
# Copy the built Endstone plugin and, for source deployments, the managed core.
#
#   ./deploy.sh <bedrock server folder> [shim-only]
#
# The managed core is taken from a sibling checkout of Hosihikari.PluginManager,
# or from HOSIHIKARI_PLUGINMANAGER. Release packaging passes shim-only: the core
# then comes from its own lip package. libnethost.so goes next to the plugin:
# the plugin is linked with $ORIGIN so the loader looks there, unlike Windows
# where nethost.dll sits next to bedrock_server.exe.
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
plugin="$here/build/hosihikari.so"
nethost="$here/build/nethost/runtimes/linux-x64/native/libnethost.so"
dst="$server/plugins/hosihikari"

if [ ! -f "$plugin" ]; then
    echo "run build.sh first" >&2
    exit 1
fi

mkdir -p "$server/plugins"
rm -rf "$dst"
mkdir -p "$dst"

# The plugin itself has to sit flat in plugins/: that is where Endstone looks.
cp -f "$plugin" "$server/plugins/hosihikari.so"

# libnethost.so is resolved through the plugin's $ORIGIN, so it lives beside it.
cp -f "$nethost" "$server/plugins/libnethost.so"

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

echo "deployed to $dst"
