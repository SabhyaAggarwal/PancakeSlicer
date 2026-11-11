# Ninja with cl and ccache. ccache cannot cache cl precompiled headers or /Zi objects, so both are off.
configure() {
  export PATH="$(dirname "$(command -v cl)"):$PATH"
  local wv2=engine/deps/WebView2/lib/win-x64/WebView2Loader.dll
  if [ ! -f "$wv2" ]; then
    local tmp; tmp=$(mktemp -d)
    curl -fsSL -o "$tmp/webview2.zip" https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/1.0.4314.35/microsoft.web.webview2.1.0.4314.35.nupkg
    echo "90778deb1b77e36292828b4b5833c9255c27533f40e0ddd55891ae718944edc4  $tmp/webview2.zip" | sha256sum -c -
    mkdir -p "$(dirname "$wv2")"
    python -c "import sys, zipfile; open(sys.argv[2], 'wb').write(zipfile.ZipFile(sys.argv[1]).read('build/native/x64/WebView2Loader.dll'))" "$tmp/webview2.zip" "$wv2"
  fi
  cmake -S engine -B build-native -G 'Ninja Multi-Config' -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl "${common[@]}" "${ccache[@]}" \
    -DSLIC3R_PCH=OFF -DSLIC3R_MSVC_PDB=OFF
}
