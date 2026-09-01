#!/usr/bin/env bash
#
# virthub/install.sh - Unified dependency installer for Virthub & KLNK workspace.
#
# Usage:
#   ./install.sh [--with-integration] [--engine {vllm,sglang}] [--vllm-version <version>] [--python-version <version>] [--no-patch-vllm]
#   --with-integration  Install integration test packages (vLLM or SGLang, plus LMCache) inside the virtual environment.
#   --engine            Choose which engine to install: vllm or sglang (required when --with-integration is used).
#   --vllm-version      Specify the vLLM version. Accepts either "0.20.2" or "==0.20.2" (default: ">=0.26.0").
#   --python-version    Specify the Python version for the virtual environment (e.g., "3.10", "3.11").
#                       If not given, the default python3 is used. If the requested version is not installed,
#                       the script will try to use 'uv' to create the environment.
#   --no-patch-vllm     Do NOT automatically patch vLLM to register the Virthub connector.
#
# The Python bindings are installed as a pure Python package. The native Rust extension
# is optional and can be built later with maturin if needed.
#
# This installer prefers `uv pip` when `uv` is available, falling back to standard `pip`.
# When vLLM is selected via `--engine vllm`, the script automatically patches vLLM's
# KV connector factory to recognise the `virthub` connector (unless `--no-patch-vllm` is given).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

info()    { echo -e "${BLUE}[INFO]${NC} $*"; }
success() { echo -e "${GREEN}[SUCCESS]${NC} $*"; }
warn()    { echo -e "${YELLOW}[WARN]${NC} $*"; }
error()   { echo -e "${RED}[ERROR]${NC} $*"; exit 1; }

WITH_INTEGRATION=false
ENGINE_CHOICE=""
VLLM_VERSION=">=0.26.0"
PYTHON_VERSION=""
PATCH_VLLM=true
VENV_DIR="$SCRIPT_DIR/.venv"

export PIP_NO_WARN_SCRIPT_LOCATION=1
export PYTHONNOUSERSITE=1

pip_install() {
    if command -v uv &> /dev/null; then
        uv pip install "$@"
    else
        pip install "$@"
    fi
}

detect_os() {
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        OS=$ID
    else
        error "Unable to detect OS distribution via /etc/os-release."
    fi
}

install_system_deps() {
    info "Installing system build tools, RDMA libraries, and NUMA dependencies for $OS..."
    case "$OS" in
        ubuntu|debian)
            sudo apt-get update -y
            sudo apt-get install -y \
                build-essential pkg-config libssl-dev clang llvm libelf-dev \
                libibverbs-dev librdmacm-dev rdma-core iproute2 numactl libnuma-dev \
                protobuf-compiler git curl cmake python3 python3-pip python3-venv
            ;;
        rocky|rhel|fedora|almalinux)
            sudo dnf groupinstall -y "Development Tools"
            sudo dnf install -y \
                pkgconfig openssl-devel clang llvm elfutils-libelf-devel \
                rdma-core-devel libibverbs numactl numactl-devel protobuf-compiler \
                git curl cmake python3 python3-pip
            ;;
        *)
            warn "Unsupported OS distribution: $OS. Please ensure build-essential, clang, libelf, rdma-core, python3, and pip are installed manually."
            ;;
    esac
    success "System packages installed successfully."
}

install_rust() {
    if command -v cargo &> /dev/null; then
        info "Rust toolchain detected. Updating to latest stable..."
        rustup update stable
    else
        info "Installing Rust toolchain via rustup..."
        curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y --default-toolchain stable
        if [ -f "$HOME/.cargo/env" ]; then
            source "$HOME/.cargo/env"
        fi
    fi

    if ! command -v bpf-linker &> /dev/null; then
        info "Installing bpf-linker for klnk-ebpf..."
        cargo install bpf-linker || warn "bpf-linker install skipped or failed (only required for compiling eBPF programs)."
    fi

    success "Rust toolchain configured: $(cargo --version)"
}

install_python_bindings() {
    local PYBIND_DIR="$SCRIPT_DIR/bindings/python"
    if [ ! -d "$PYBIND_DIR" ]; then
        warn "Python bindings directory not found; skipping Python package installation."
        return 0
    fi

    if [ "$WITH_INTEGRATION" = true ] && [ -z "$ENGINE_CHOICE" ]; then
        error "When using --with-integration, you must specify --engine {vllm,sglang}."
    fi
    if [ -n "$ENGINE_CHOICE" ] && [[ "$ENGINE_CHOICE" != "vllm" && "$ENGINE_CHOICE" != "sglang" ]]; then
        error "Invalid engine: $ENGINE_CHOICE. Must be 'vllm' or 'sglang'."
    fi

    local PYTHON_BIN=""
    if [ -n "$PYTHON_VERSION" ]; then
        PYTHON_BIN="$(command -v python${PYTHON_VERSION} || true)"
        if [ -z "$PYTHON_BIN" ]; then
            if command -v uv &> /dev/null; then
                info "Using uv to create a virtual environment with Python ${PYTHON_VERSION}..."
                uv venv "$VENV_DIR" --python "$PYTHON_VERSION"
                PYTHON_BIN="$VENV_DIR/bin/python"
            else
                error "Python ${PYTHON_VERSION} not found and uv is not installed. Please install the required Python version or uv."
            fi
        fi
    else
        PYTHON_BIN="$(command -v python3 || true)"
        if [ -z "$PYTHON_BIN" ]; then
            error "python3 not found. Please install Python 3."
        fi
    fi

    if [ -d "$VENV_DIR" ]; then
        info "Virtual environment already exists at $VENV_DIR. Updating packages..."
    else
        info "Creating fresh virtual environment at $VENV_DIR using $PYTHON_BIN..."
        "$PYTHON_BIN" -m venv "$VENV_DIR" || error "Failed to create virtual environment."
    fi

    source "$VENV_DIR/bin/activate"

    info "Upgrading pip..."
    pip_install --upgrade pip

    info "Installing Python bindings (pure Python, using in‑memory stubs)..."
    if ! pip_install -e "$PYBIND_DIR[dev]"; then
        error "Failed to install Python bindings. Check the error above."
    fi

    info "Native extension (_virthub) is not built by default. To build it later, use:"
    info "  maturin develop --release -m bindings/rust/Cargo.toml"

    if [ "$WITH_INTEGRATION" = true ]; then
        info "Installing integration test dependencies: ${ENGINE_CHOICE} + LMCache..."
        pip_install --upgrade "lmcache>=0.5.0"

        case "$ENGINE_CHOICE" in
            vllm)
                local vllm_pkg="vllm"
                if [[ "$VLLM_VERSION" =~ ^(==|>=|<=|~=|!=|>|<) ]]; then
                    vllm_pkg="vllm${VLLM_VERSION}"
                else
                    vllm_pkg="vllm==${VLLM_VERSION}"
                fi
                info "Installing vLLM constraint: $vllm_pkg"
                pip_install --upgrade "$vllm_pkg"

                if [ "$PATCH_VLLM" = true ]; then
                    info "Patching vLLM to register the Virthub connector..."
                    if python "$SCRIPT_DIR/scripts/patch_vllm_connector.py"; then
                        success "vLLM patched successfully."
                    else
                        warn "Patch failed. You can apply it later with ./run.sh --patch-vllm"
                    fi
                else
                    info "Skipping vLLM patch (--no-patch-vllm was specified)."
                fi
                ;;
            sglang)
                info "Installing SGLang..."
                pip_install --upgrade "sglang>=0.5.13"
                ;;
        esac
        success "Integration dependencies installed for ${ENGINE_CHOICE}."
    else
        info "Skipping integration dependencies."
        info "To install them later, run: ./install.sh --with-integration --engine {vllm,sglang} [--vllm-version <version>] [--python-version <version>]"
    fi

    deactivate
    success "Virtual environment set up at $VENV_DIR"
}

check_kernel_version() {
    info "Checking Linux kernel compatibility..."
    KERNEL_MAJOR=$(uname -r | cut -d. -f1)
    KERNEL_MINOR=$(uname -r | cut -d. -f2)
    info "Detected Kernel: $(uname -r)"
    if [ "$KERNEL_MAJOR" -lt 6 ] || { [ "$KERNEL_MAJOR" -eq 6 ] && [ "$KERNEL_MINOR" -lt 8 ]; }; then
        warn "Linux kernel $(uname -r) detected. Linux 6.8+ is recommended for optimal zero-copy UFFDIO_MOVE performance."
    else
        success "Kernel version supports Linux 6.8+ zero-copy UFFDIO_MOVE ioctls."
    fi
}

verify_workspace() {
    info "Checking Virthub workspace compilation..."
    cargo check --workspace
    success "All workspace crate dependencies verified!"
}

show_help() {
    cat << EOF
Usage:
  ./install.sh [OPTIONS]

Options:
  --with-integration          Install integration test dependencies (vLLM or SGLang + LMCache) inside the virtual environment.
  --engine {vllm,sglang}      Choose which engine to install (required when --with-integration is used).
  --vllm-version <version>    Specify the vLLM version. Accepts either "0.20.2" or "==0.20.2" directly. Default: ">=0.26.0".
  --python-version <version>  Specify the Python version for the virtual environment (e.g., "3.10", "3.11").
  --no-patch-vllm             Do NOT automatically patch vLLM to register the Virthub connector.
  --help                      Show this help message.

Examples:
  ./install.sh                                  # Install base dependencies + dev extras
  ./install.sh --with-integration --engine vllm --vllm-version "0.20.2" --python-version "3.10"
  ./install.sh --with-integration --engine vllm --vllm-version "==0.20.2" --python-version "3.10" --no-patch-vllm
  ./install.sh --with-integration --engine sglang

Note: vLLM and SGLang have conflicting dependencies. You must choose only one engine.
      The virtual environment is created / updated at .venv/ in the project root.
      The Python bindings are installed as a pure Python package (using in‑memory stubs).
      The native Rust extension can be built later with maturin.
      When vLLM is selected, the script automatically patches vLLM's KV connector factory
      to recognise the `virthub` connector unless `--no-patch-vllm` is given.
      To run Python tests, use ./run.sh which automatically activates the venv.
EOF
}

main() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --with-integration)
                WITH_INTEGRATION=true
                shift
                ;;
            --engine)
                if [[ -z "${2:-}" ]]; then
                    error "--engine requires an argument: vllm or sglang"
                fi
                ENGINE_CHOICE="$2"
                shift 2
                ;;
            --vllm-version)
                if [[ -z "${2:-}" ]]; then
                    error "--vllm-version requires an argument, e.g., '0.20.2' or '==0.20.2'"
                fi
                VLLM_VERSION="$2"
                shift 2
                ;;
            --python-version)
                if [[ -z "${2:-}" ]]; then
                    error "--python-version requires an argument, e.g., '3.10'"
                fi
                PYTHON_VERSION="$2"
                shift 2
                ;;
            --no-patch-vllm)
                PATCH_VLLM=false
                shift
                ;;
            --help|-h)
                show_help
                exit 0
                ;;
            *)
                error "Unknown argument: $1. Use --help for usage."
                ;;
        esac
    done

    info "=== Virthub & KLNK Dependency Installer ==="
    detect_os
    install_system_deps
    install_rust
    install_python_bindings
    check_kernel_version
    verify_workspace
    echo ""
    success "Installation complete! You can now run:"
    success "  ./run.sh --test              (Rust tests)"
    success "  ./run.sh --test-python       (Python unit tests, using venv)"
    success "  ./run.sh --test-integration  (Python integration tests, using venv)"
}

main "$@"
