#!/usr/bin/env bash
#
# EasyScript & EPM Automated Installer for Linux / macOS
#

set -e

INSTALL_DIR="${EASYSCRIPT_INSTALL_DIR:-$HOME/.easyscript}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

GREEN='\033[0;32m'
CYAN='\033[0;36m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

echo -e "${CYAN}============================================================="
echo "   EasyScript Installer for Linux / macOS"
echo -e "=============================================================${NC}"

# 1. 檢查先決條件
if ! command -v cmake &> /dev/null; then
    echo -e "${RED}[ERR] CMake is required. Please install cmake 3.20+ via your package manager.${NC}"
    exit 1
fi

# 2. 編譯
echo -e "\n${CYAN}==> Building EasyScript & EPM (${BUILD_TYPE})...${NC}"
cmake -B "${SCRIPT_DIR}/build" -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
cmake --build "${SCRIPT_DIR}/build" --config "${BUILD_TYPE}" --target easyscript_test

# 3. 安裝
echo -e "\n${CYAN}==> Installing to '${INSTALL_DIR}'...${NC}"
mkdir -p "${INSTALL_DIR}"
cmake --install "${SCRIPT_DIR}/build" --config "${BUILD_TYPE}" --prefix "${INSTALL_DIR}"

BIN_DIR="${INSTALL_DIR}/bin"
PLUGINS_DIR="${INSTALL_DIR}/plugins"

# 4. 配置環境變數提示
echo -e "\n${CYAN}==> Configuring Environment Variables...${NC}"

SHELL_RC=""
if [ -n "$ZSH_VERSION" ] || [ "$SHELL" = "*/zsh" ]; then
    SHELL_RC="$HOME/.zshrc"
elif [ -n "$BASH_VERSION" ] || [ "$SHELL" = "*/bash" ]; then
    SHELL_RC="$HOME/.bashrc"
else
    SHELL_RC="$HOME/.profile"
fi

CONFIG_LINES="
# EasyScript Environment
export PATH=\"${BIN_DIR}:\$PATH\"
export EASYSCRIPT_PATH=\"${PLUGINS_DIR}\"
"

if [ -f "$SHELL_RC" ]; then
    if ! grep -q "EASYSCRIPT_PATH" "$SHELL_RC"; then
        echo "$CONFIG_LINES" >> "$SHELL_RC"
        echo -e "${GREEN}[OK] Added EasyScript to ${SHELL_RC}${NC}"
    else
        echo -e "${GREEN}[OK] EasyScript environment already present in ${SHELL_RC}${NC}"
    fi
fi

echo -e "\n${GREEN}============================================================="
echo "             Installation Completed Successfully!"
echo "============================================================="
echo "EasyScript has been installed to: ${INSTALL_DIR}"
echo ""
echo "Please reload your shell profile or restart your terminal:"
echo "  source ${SHELL_RC}"
echo ""
echo "Then test by running:"
echo "  easyscript"
echo "  epm --help"
echo -e "=============================================================${NC}"
