#!/usr/bin/env bash
set -euo pipefail

VERSION="2.0.0"
INSTALL_DIR="/usr/local/bin"
DATA_DIR="/var/lib/clove"
RUN_DIR="/var/run/clove"

echo "╔══════════════════════════════════════╗"
echo "║  CLOVE Installer v${VERSION}             ║"
echo "╚══════════════════════════════════════╝"
echo ""

# Check root
if [[ $EUID -ne 0 ]]; then
    echo "Error: Please run as root (sudo ./install.sh)"
    exit 1
fi

# Detect OS
if [[ "$(uname)" == "Darwin" ]]; then
    OS="macos"
    echo "Detected: macOS"
elif [[ "$(uname)" == "Linux" ]]; then
    OS="linux"
    echo "Detected: Linux"
else
    echo "Unsupported OS: $(uname)"
    exit 1
fi

# Check if built
if [[ ! -f "build/kernel/clove_kernel" ]]; then
    echo "Building CLOVE..."
    mkdir -p build && cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release
    cmake --build . --target clove_kernel clove_cli -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)
    cd ..
fi

# Install binaries
echo "Installing binaries to ${INSTALL_DIR}..."
cp build/kernel/clove_kernel "${INSTALL_DIR}/clove_kernel"
if [[ -f "build/cli/clove_cli" ]]; then
    cp build/cli/clove_cli "${INSTALL_DIR}/clove"
fi
chmod +x "${INSTALL_DIR}/clove_kernel"
[[ -f "${INSTALL_DIR}/clove" ]] && chmod +x "${INSTALL_DIR}/clove"

# Create directories
echo "Creating data directories..."
mkdir -p "${DATA_DIR}" "${RUN_DIR}"

# Create user (Linux only)
if [[ "$OS" == "linux" ]]; then
    if ! id -u clove &>/dev/null; then
        useradd --system --no-create-home --shell /usr/sbin/nologin clove
        echo "Created system user: clove"
    fi
    chown clove:clove "${DATA_DIR}" "${RUN_DIR}"

    # Install systemd service
    if command -v systemctl &>/dev/null; then
        cp deploy/systemd/clove.service /etc/systemd/system/
        systemctl daemon-reload
        echo "Installed systemd service"
        echo ""
        echo "To start:  systemctl start clove"
        echo "To enable: systemctl enable clove"
    fi
fi

echo ""
echo "Installation complete!"
echo ""
echo "  Kernel:  ${INSTALL_DIR}/clove_kernel"
[[ -f "${INSTALL_DIR}/clove" ]] && echo "  CLI:     ${INSTALL_DIR}/clove"
echo "  Data:    ${DATA_DIR}/"
echo ""
echo "Quick start:"
echo "  clove_kernel --api --api-port 8080"
echo ""
