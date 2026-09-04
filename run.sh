#!/usr/bin/env bash
#
# virthub/run.sh - Unified launch, test, benchmark, and deployment automation
#
# Usage:
#   ./run.sh --test                    Run Rust unit & integration tests
#   ./run.sh --test <crate>            Run tests for a specific Rust crate
#   ./run.sh --test-precision          Run precision predictor tests
#   ./run.sh --test-pspkv              Run PSP-KV storage tests
#   ./run.sh --test-kernels            Build and run PSP-KV GPU kernel tests
#   ./run.sh --test-python             Run Python unit tests (skip integration)
#   ./run.sh --test-integration        Run Python integration tests
#   ./run.sh --test-all                Run both Rust and Python tests
#   ./run.sh --bench-predictor         Run precision predictor benchmarks
#   ./run.sh --bench-all               Run all Rust benchmarks
#   ./run.sh --bench                   Run performance benchmarks (cargo bench)
#   ./run.sh --bench-compare           Compare benchmarks against baseline
#   ./run.sh --benchmark-vllm          Run vLLM performance benchmark (baseline vs Virthub)
#   ./run.sh --build-kernels           Build PSP-KV GPU kernels (CMake)
#   ./run.sh --daemon                  Launch klnk-daemon in foreground
#   ./run.sh --daemon-start            Start daemon in background
#   ./run.sh --daemon-stop             Stop background daemon
#   ./run.sh --daemon-status           Check daemon status
#   ./run.sh --daemon-logs             View daemon logs
#   ./run.sh --cluster [--cluster-config <path>]  Start a local 3‑node cluster (optional base config)
#   ./run.sh --app <command...>        Launch app with LD_PRELOAD shim
#   ./run.sh --build                   Build workspace (debug)
#   ./run.sh --build-release           Build with release optimizations
#   ./run.sh --build-ext               Build native Python extension
#   ./run.sh --docker-build            Build Docker image
#   ./run.sh --docker-run              Run in Docker container
#   ./run.sh --deploy-k8s              Deploy to Kubernetes
#   ./run.sh --validate-config         Validate virthub.toml
#   ./run.sh --generate-docs           Generate API documentation
#   ./run.sh --coverage                Generate code coverage report
#   ./run.sh --lint                    Run clippy and rustfmt checks
#   ./run.sh --clean                   Clean build artifacts
#   ./run.sh --help                    Show this help message

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

VENV_DIR="$SCRIPT_DIR/.venv"
KLNK_SOCKET_PATH="${KLNK_SOCKET:-/tmp/klnk_daemon.sock}"
DAEMON_PID_FILE="/tmp/virthub_daemon.pid"
DAEMON_LOG_FILE="/tmp/virthub_daemon.log"
HUGEPAGES_COUNT="${HUGEPAGES_COUNT:-128}"
BENCHMARK_BASELINE="benchmarks/baseline.json"
TEST_REPORT_DIR="test-reports"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
MAGENTA='\033[0;35m'
NC='\033[0m'

info()    { echo -e "${BLUE}[INFO]${NC}    $(date '+%H:%M:%S') $*"; }
success() { echo -e "${GREEN}[SUCCESS]${NC} $(date '+%H:%M:%S') $*"; }
warn()    { echo -e "${YELLOW}[WARN]${NC}    $(date '+%H:%M:%S') $*"; }
error()   { echo -e "${RED}[ERROR]${NC}   $(date '+%H:%M:%S') $*"; exit 1; }
header()  { echo -e "\n${MAGENTA}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"; }

# Ensure cargo is available; try to source Rust environment if needed.
ensure_cargo() {
    if command -v cargo &> /dev/null; then
        return 0
    fi

    # Attempt to source the Rust cargo env if it exists
    local cargo_env="$HOME/.cargo/env"
    if [ -f "$cargo_env" ]; then
        source "$cargo_env" 2>/dev/null || true
        if command -v cargo &> /dev/null; then
            return 0
        fi
    fi

    error "cargo not found. Please install Rust via https://rustup.rs and ensure ~/.cargo/bin is in your PATH."
}

check_prerequisites() {
    local mode="${1:-full}"
    header
    info "Checking system prerequisites..."

    if command -v cargo &> /dev/null; then
        success "Rust $(rustc --version | awk '{print $2}') found"
    else
        ensure_cargo > /dev/null 2>&1 || true
        if command -v cargo &> /dev/null; then
            success "Rust $(rustc --version | awk '{print $2}') found (after sourcing env)"
        else
            error "Rust not found. Install from https://rustup.rs"
        fi
    fi

    if command -v python3 &> /dev/null; then
        success "$(python3 --version) found"
    else
        warn "Python3 not found (required for Python tests)"
    fi

    local kernel_ver=$(uname -r | cut -d. -f1-2)
    if [ "$(printf '%s\n' "6.8" "$kernel_ver" | sort -V | head -n1)" = "6.8" ]; then
        success "Kernel $kernel_ver supports UFFDIO_MOVE"
    else
        warn "Kernel $kernel_ver < 6.8 (UFFDIO_MOVE not available)"
    fi

    if [ -d "/sys/class/infiniband" ] && ls /sys/class/infiniband/*/ &>/dev/null; then
        success "RDMA devices detected"
    else
        warn "No RDMA devices found (will use TCP fallback)"
    fi

    if [ -f "/sys/kernel/debug/tracing/trace" ]; then
        success "eBPF tracing available"
    else
        warn "eBPF tracing not available"
    fi

    local available_hp=$(cat /proc/sys/vm/nr_hugepages 2>/dev/null || echo 0)
    if [ "$available_hp" -ge "$HUGEPAGES_COUNT" ]; then
        success "Huge pages available ($available_hp)"
    else
        warn "Huge pages: $available_hp/$HUGEPAGES_COUNT (run --setup-env)"
    fi

    local memlock=$(ulimit -l 2>/dev/null || echo "unknown")
    if [ "$memlock" = "unlimited" ]; then
        success "memlock: unlimited"
    else
        warn "memlock: $memlock (RDMA registration may fail)"
    fi

    if [ "$mode" = "full" ]; then
        if command -v docker &> /dev/null; then success "Docker available"; else warn "Docker not found (optional)"; fi
        if command -v kubectl &> /dev/null; then success "kubectl available"; else warn "kubectl not found (optional)"; fi
    fi
}

setup_env() {
    header
    info "Configuring Linux environment for zero-copy DSM..."
    if [ "$(id -u)" -ne 0 ]; then
        warn "Not running as root. Skipping system configuration."
        return 0
    fi
    if ulimit -l unlimited 2>/dev/null; then
        success "Set memlock to unlimited"
    else
        warn "Failed to set memlock"
    fi
    sysctl -w vm.nr_hugepages="$HUGEPAGES_COUNT" > /dev/null || warn "Failed to set huge pages"
    mkdir -p /dev/hugepages && mount -t hugetlbfs none /dev/hugepages 2>/dev/null || true
    success "Environment configured."
}

activate_venv() {
    if [ -d "$VENV_DIR" ]; then
        source "$VENV_DIR/bin/activate"
        info "Activated virtual environment from $VENV_DIR"
    else
        error "Virtual environment not found at $VENV_DIR. Run ./install.sh first."
    fi
}

has_gpu() {
    "$VENV_DIR/bin/python" -c "import torch; print(torch.cuda.is_available())" 2>/dev/null | grep -q True
}

launch_daemon() {
    ensure_cargo
    setup_env
    info "Building klnk-daemon..."
    cargo build -p klnk-daemon --release
    info "Starting klnk-daemon (foreground)..."
    rm -f "$KLNK_SOCKET_PATH"
    exec cargo run -p klnk-daemon --release
}

start_daemon_background() {
    ensure_cargo
    info "Starting klnk-daemon in background..."
    stop_daemon 2>/dev/null || true
    cargo build -p klnk-daemon --release
    rm -f "$KLNK_SOCKET_PATH"
    RUST_LOG="${RUST_LOG:-info}" \
    VIRTHUB_CONFIG="$VIRTHUB_CONFIG" \
    nohup cargo run -p klnk-daemon --release > "$DAEMON_LOG_FILE" 2>&1 &
    echo $! > "$DAEMON_PID_FILE"
    for i in $(seq 1 30); do
        if [ -S "$KLNK_SOCKET_PATH" ]; then
            success "Daemon started (PID: $(cat $DAEMON_PID_FILE))"
            return 0
        fi
        sleep 0.1
    done
    error "Daemon failed to start. Check $DAEMON_LOG_FILE"
}

stop_daemon() {
    if [ ! -f "$DAEMON_PID_FILE" ]; then
        warn "No daemon PID file found"
        return 1
    fi
    local pid=$(cat "$DAEMON_PID_FILE")
    if ! kill -0 "$pid" 2>/dev/null; then
        warn "Daemon not running (PID: $pid)"
        rm -f "$DAEMON_PID_FILE"
        return 1
    fi
    info "Stopping daemon (PID: $pid)..."
    kill "$pid"
    for i in $(seq 1 10); do
        if ! kill -0 "$pid" 2>/dev/null; then break; fi
        sleep 0.1
    done
    if kill -0 "$pid" 2>/dev/null; then
        warn "Force killing daemon..."
        kill -9 "$pid"
    fi
    rm -f "$DAEMON_PID_FILE" "$KLNK_SOCKET_PATH"
    success "Daemon stopped"
}

daemon_status() {
    if [ -f "$DAEMON_PID_FILE" ]; then
        local pid=$(cat "$DAEMON_PID_FILE")
        if kill -0 "$pid" 2>/dev/null; then
            info "Daemon is running (PID: $pid)"
            return 0
        fi
    fi
    warn "Daemon is not running"
    return 1
}

view_daemon_logs() { [ -f "$DAEMON_LOG_FILE" ] && tail -f "$DAEMON_LOG_FILE" || error "No daemon log file found"; }

launch_cluster() {
    ensure_cargo
    header
    info "Starting local 3‑node Virthub cluster..."
    setup_env
    cargo build -p klnk-daemon --release

    local cluster_config="${1:-}"
    local ports=(19001 19002 19003)
    local sockets=("/tmp/virthub_node1.sock" "/tmp/virthub_node2.sock" "/tmp/virthub_node3.sock")
    local pids=()

    for i in "${!ports[@]}"; do
        local node_id="node-$((i+1))"
        local cfg="/tmp/virthub_cluster_${node_id}.toml"

        if [ -n "$cluster_config" ] && [ -f "$cluster_config" ]; then
            info "Using base configuration: $cluster_config"
            cp "$cluster_config" "$cfg"
            python3 -c "
import toml
with open('$cfg') as f:
    c = toml.load(f)
c.setdefault('general', {})['node_id'] = '$node_id'
c['general']['control_socket'] = '${sockets[$i]}'
c['general']['data_bind_addr'] = '0.0.0.0:${ports[$i]}'
c.setdefault('transport', {}).setdefault('tcp', {})['tcp_port'] = ${ports[$i]}
with open('$cfg', 'w') as f:
    toml.dump(c, f)
"
        else
            info "Using built‑in minimal cluster configuration."
            cat > "$cfg" <<EOF
[general]
control_socket = "${sockets[$i]}"
data_bind_addr = "0.0.0.0:${ports[$i]}"
node_id = "$node_id"
log_level = "debug"

[klnk]
enable_uffd_move = false
fallback_copy = true
staging_num_pages = 4
huge_page_size = 2097152

[store]
block_size = 4096
[store.tier]
l0_enabled = false
l1_enabled = true
l2_enabled = false
l2_path = "/tmp/virthub_cache"

[master.raft]
embedded = true
initial_peers = ["node-1","node-2","node-3"]
etcd_endpoints = []

[master.scheduler]
prefetch_window = 8
l0_promote_threshold = 100
l1_demote_idle_secs = 60
lru_decay = 0.8

[master.sharding]
shard_count = 64

[transport]
default_protocol = "tcp"
[transport.rdma]
device_name = ""
enable_gdr = false
rq_prepost_count = 512
control_immediate = true
[transport.tcp]
io_uring_enabled = false
tcp_port = ${ports[$i]}

[ebpf]
enabled = false
program_path = "/dev/null"
report_interval_ms = 100

[tuning]
numa_node = -1
operation_timeout_ms = 500
memlock_limit = 0

[precision]
format_generation = 1
sink_window = 16
local_window = 64
critical_layer_count = 2
elevated_pressure_threshold = 0.78
nominal_pressure_threshold = 0.70
critical_pressure_threshold = 0.88
critical_relax_threshold = 0.82
EOF
        fi

        rm -f "${sockets[$i]}"
        RUST_LOG=debug VIRTHUB_CONFIG="$cfg" \
            nohup cargo run -p klnk-daemon --release > "/tmp/virthub_cluster_${node_id}.log" 2>&1 &
        pids+=($!)
    done

    for i in "${!sockets[@]}"; do
        for _ in $(seq 1 30); do
            if [ -S "${sockets[$i]}" ]; then break; fi
            sleep 0.1
        done
    done

    success "Cluster started with ${cluster_config:-default} settings."
    echo "Sockets: ${sockets[@]}"
    echo "PIDs: ${pids[@]}"
    echo "Run './run.sh --daemon-stop-all' to stop all nodes."
}

stop_all_daemons() {
    info "Stopping all local daemons..."
    pkill -f "klnk-daemon" || true
    rm -f /tmp/virthub_node*.sock /tmp/virthub_cluster_*.toml /tmp/virthub_cluster_*.log
    success "All daemons stopped."
}

build_workspace() {
    ensure_cargo
    header
    info "Building workspace (debug)..."
    cargo build --workspace
    success "Build complete"
}

build_release() {
    ensure_cargo
    header
    info "Building workspace (release)..."
    cargo build --workspace --release
    success "Release build complete"
}

build_ext() {
    ensure_cargo
    header
    info "Building native Python extension (_virthub)..."
    activate_venv
    pip install -e "$SCRIPT_DIR/bindings/python[dev]"
    success "Native extension built and installed."
}

build_docker() {
    ensure_cargo
    header
    info "Building Docker image..."
    docker build -t virthub:latest --build-arg RUST_VERSION=stable -f Dockerfile .
    success "Docker image built: virthub:latest"
}

build_kernels() {
    header
    if ! command -v nvcc &> /dev/null; then
        error "CUDA compiler (nvcc) not found. Install CUDA toolkit or set CUDACXX."
    fi
    info "Building PSP-KV GPU kernels..."
    local kernels_dir="$SCRIPT_DIR/kernels"
    if [ ! -f "$kernels_dir/CMakeLists.txt" ]; then
        error "Kernels CMakeLists.txt not found at $kernels_dir"
    fi
    mkdir -p "$kernels_dir/build"
    cd "$kernels_dir/build"
    cmake .. || error "CMake configuration failed"
    make -j || error "Kernel build failed"
    cd "$SCRIPT_DIR"
    success "PSP-KV kernels built successfully."
}

test_kernels() {
    header
    info "Testing PSP-KV GPU kernels..."
    build_kernels

    local kernels_dir="$SCRIPT_DIR/kernels"
    local test_dir="$kernels_dir/tests"

    if [ -f "$test_dir/test_header_consistency.py" ]; then
        info "Running header consistency test..."
        python3 "$test_dir/test_header_consistency.py" || error "Header consistency test failed"
    else
        warn "Header consistency test not found; skipping."
    fi

    local cpu_test="$test_dir/cpu_reference_dequant.cu"
    if [ -f "$cpu_test" ]; then
        info "Compiling CPU reference dequantization test..."
        local cpu_bin="$kernels_dir/build/cpu_reference_dequant"
        nvcc -I"$kernels_dir/common" -o "$cpu_bin" "$cpu_test" || error "CPU reference compile failed"
        info "Running CPU reference dequantization test..."
        "$cpu_bin" || error "CPU reference test failed"
    else
        warn "CPU reference test not found; skipping."
    fi

    success "Kernel tests completed."
}

run_tests() {
    ensure_cargo
    local target_crate="${1:-}"
    header
    if [ -z "$target_crate" ]; then
        info "Running ALL Rust tests..."
        cargo test --workspace --all-targets -- --nocapture
        success "All Rust tests passed!"
    else
        info "Running tests for crate: $target_crate"
        cargo test -p "$target_crate" --all-targets -- --nocapture
        success "Tests for $target_crate passed!"
    fi
}

run_precision_tests() {
    ensure_cargo
    header
    info "Running precision predictor tests..."
    cargo test -p precision --all-targets -- --nocapture
    success "Precision tests passed!"
}

run_pspkv_tests() {
    ensure_cargo
    header
    info "Running PSP‑KV storage tests..."
    cargo test -p store --test psp_kv_tests --test quantization_tests -- --nocapture
    success "PSP-KV tests passed!"
}

run_python_tests() {
    header
    info "Running Python unit tests..."
    activate_venv
    export PYTHONPATH="$SCRIPT_DIR/bindings/python:$PYTHONPATH"
    "$VENV_DIR/bin/python" -m pytest tests/python -v --tb=short -m "not integration"
    success "Python unit tests passed!"
}

run_python_integration() {
    header
    info "Running Python integration tests..."
    activate_venv
    export PYTHONPATH="$SCRIPT_DIR/bindings/python:$PYTHONPATH"

    if has_gpu; then
        info "GPU detected. Configuring CUDA environment..."
        export CUDA_VISIBLE_DEVICES=0
        export VLLM_WORKER_MULTIPROC_METHOD=spawn
        export VLLM_USE_FLASH_ATTN=0

        local py_ver
        py_ver=$("$VENV_DIR/bin/python" -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')")

        export LD_LIBRARY_PATH="$VENV_DIR/lib/python${py_ver}/site-packages/nvidia/cu13/lib:$VENV_DIR/lib/python${py_ver}/site-packages/torch/lib:$VENV_DIR/lib/python${py_ver}/site-packages/nvidia/nccl/lib:${LD_LIBRARY_PATH:-}"
    else
        info "No GPU detected. Tests that require GPU will skip."
    fi

    "$VENV_DIR/bin/python" -m pytest tests/python -v --tb=short -m integration
    success "Python integration tests passed!"
}

run_all_tests() {
    ensure_cargo
    run_tests
    run_precision_tests
    run_pspkv_tests
    run_python_tests
}

run_predictor_bench() {
    ensure_cargo
    header
    info "Running precision predictor benchmarks..."
    cargo bench -p precision
    success "Predictor benchmarks completed"
}

run_all_benches() {
    ensure_cargo
    header
    info "Running all Rust benchmarks..."
    cargo bench --workspace
    success "All benchmarks completed"
}

run_benchmarks() {
    ensure_cargo
    header
    info "Running performance benchmarks..."
    cargo bench --workspace -- --output-format bencher
    success "Benchmarks completed"
}

compare_benchmarks() {
    ensure_cargo
    header
    info "Comparing benchmarks against baseline..."
    cargo bench --workspace -- --baseline main
    success "Benchmark comparison complete"
}

run_vllm_benchmark() {
    ensure_cargo
    header
    info "Running vLLM performance benchmark (baseline vs Virthub)..."
    activate_venv

    if has_gpu; then
        export CUDA_VISIBLE_DEVICES=0
        export VLLM_WORKER_MULTIPROC_METHOD=spawn
        export VLLM_USE_FLASH_ATTN=0

        local py_ver
        py_ver=$("$VENV_DIR/bin/python" -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')")

        export LD_LIBRARY_PATH="$VENV_DIR/lib/python${py_ver}/site-packages/nvidia/cu13/lib:$VENV_DIR/lib/python${py_ver}/site-packages/torch/lib:$VENV_DIR/lib/python${py_ver}/site-packages/nvidia/nccl/lib:${LD_LIBRARY_PATH:-}"
    else
        warn "No GPU detected. Benchmark may not run correctly."
    fi

    "$VENV_DIR/bin/python" scripts/benchmark_vllm_virthub.py "$@"
    success "vLLM benchmark completed."
}

launch_app() {
    ensure_cargo
    if [ "$#" -eq 0 ]; then error "No application command provided!"; fi
    setup_env
    cargo build -p klnk-shim --release
    local shim_lib="$SCRIPT_DIR/target/release/libklnk_shim.so"
    [ ! -f "$shim_lib" ] && shim_lib="$SCRIPT_DIR/target/debug/libklnk_shim.so"
    [ ! -f "$shim_lib" ] && error "Could not find libklnk_shim.so"
    info "Injecting klnk-shim via LD_PRELOAD..."
    export LD_PRELOAD="$shim_lib"
    export KLNK_SOCKET="$KLNK_SOCKET_PATH"
    exec "$@"
}

validate_config() {
    header
    info "Validating configuration: $VIRTHUB_CONFIG"
    activate_venv
    python3 -c "
import sys
try:
    import tomllib
except ImportError:
    import tomli as tomllib
with open('$VIRTHUB_CONFIG','rb') as f:
    config = tomllib.load(f)
required = ['general','klnk','store','master','transport','precision']
for s in required:
    if s not in config:
        print(f'Missing section [{s}]')
        sys.exit(1)
print('Configuration valid')
" || error "Configuration validation failed"
    success "Configuration is valid"
}

generate_docs() {
    ensure_cargo
    header
    info "Generating documentation..."
    cargo doc --no-deps --workspace --document-private-items
    success "Rust docs: target/doc/index.html"
}

run_lint() {
    ensure_cargo
    header
    info "Running linters..."
    cargo fmt --all -- --check
    cargo clippy --workspace -- -D warnings
    success "Linting passed"
}

clean() {
    header
    info "Cleaning build artifacts..."
    if command -v cargo &> /dev/null; then
        cargo clean
    else
        warn "cargo not found; skipping cargo clean."
    fi
    rm -rf "$TEST_REPORT_DIR" /tmp/virthub_*.sock /tmp/virthub_*.pid /tmp/virthub_*.log
    success "Cleanup complete"
}

run_stress_tests() {
    warn "Stress tests are not implemented yet. Skipping."
}

run_chaos_tests() {
    warn "Chaos tests are not implemented yet. Skipping."
}

run_docker() {
    warn "Docker run not implemented in run.sh. Please use docker run manually."
}

deploy_kubernetes() {
    warn "Kubernetes deployment not implemented in run.sh. Please use kubectl manually."
}

run_tests_with_coverage() {
    ensure_cargo
    warn "Coverage tests are not implemented yet. Skipping."
}

show_help() {
    cat << EOF
Virthub Launch & Testing Toolchain

Usage:
  ./run.sh [OPTION]

Options:
  --test [crate]             Run Rust tests (all or specific crate)
  --test-precision           Run precision predictor tests
  --test-pspkv               Run PSP-KV storage tests
  --test-kernels             Run PSP-KV GPU kernel tests
  --test-python              Run Python unit tests
  --test-integration         Run Python integration tests
  --test-all                 Run complete test suite
  --test-stress              Run concurrent stress tests
  --test-chaos               Run chaos engineering tests
  --bench                    Run benchmarks (cargo bench)
  --bench-compare            Compare against baseline
  --bench-predictor          Run precision predictor benchmarks
  --bench-all                Run all Rust benchmarks
  --benchmark-vllm [args]    Run vLLM performance benchmark (baseline vs Virthub)
  --build-kernels            Build PSP-KV GPU kernels (CMake)
  --daemon                   Start daemon in foreground
  --daemon-start             Start daemon in background
  --daemon-stop              Stop background daemon
  --daemon-status            Check daemon status
  --daemon-logs              View daemon logs
  --cluster [--cluster-config <path>]  Start a local 3‑node cluster (optional base config)
  --daemon-stop-all          Stop all local daemons
  --build                    Build workspace (debug)
  --build-release            Build with release optimizations
  --build-ext                Build native Python extension
  --docker-build             Build Docker image
  --docker-run [args]        Run in Docker container
  --deploy-k8s               Deploy to Kubernetes
  --lint                     Run linters (fmt, clippy)
  --coverage                 Generate code coverage report
  --generate-docs            Generate API documentation
  --validate-config          Validate virthub.toml
  --setup-env                Configure HugePages and limits (root)
  --setup-env-check          Check environment prerequisites
  --clean                    Clean build artifacts
  --help, -h                 Show this help

Examples:
  ./run.sh --build && ./run.sh --test
  ./run.sh --build-ext && ./run.sh --test-python
  ./run.sh --cluster --cluster-config conf/cluster.toml
  ./run.sh --cluster && ./run.sh --test-integration
  ./run.sh --benchmark-vllm --prompts 8 --max-tokens 32
  ./run.sh --test-precision
  ./run.sh --test-pspkv
  ./run.sh --build-kernels
  ./run.sh --test-kernels
EOF
}

main() {
    if [ "$#" -eq 0 ]; then
        show_help
        exit 0
    fi

    case "$1" in
        --test)            shift; run_tests "${1:-}" ;;
        --test-precision)  shift; run_precision_tests ;;
        --test-pspkv)      shift; run_pspkv_tests ;;
        --test-kernels)    shift; test_kernels ;;
        --test-python)     shift; run_python_tests ;;
        --test-integration) shift; run_python_integration ;;
        --test-all)        shift; run_all_tests ;;
        --test-stress)     shift; run_stress_tests ;;
        --test-chaos)      shift; run_chaos_tests ;;
        --bench)           shift; run_benchmarks ;;
        --bench-compare)   shift; compare_benchmarks ;;
        --bench-predictor) shift; run_predictor_bench ;;
        --bench-all)       shift; run_all_benches ;;
        --benchmark-vllm)  shift; run_vllm_benchmark "$@" ;;
        --build-kernels)   shift; build_kernels ;;
        --daemon)          launch_daemon ;;
        --daemon-start)    start_daemon_background ;;
        --daemon-stop)     stop_daemon ;;
        --daemon-status)   daemon_status ;;
        --daemon-logs)     view_daemon_logs ;;
        --cluster)
            shift
            local cluster_cfg=""
            if [[ "${1:-}" == "--cluster-config" ]]; then
                cluster_cfg="$2"
                shift 2
            fi
            launch_cluster "$cluster_cfg"
            ;;
        --daemon-stop-all) stop_all_daemons ;;
        --build)           build_workspace ;;
        --build-release)   build_release ;;
        --build-ext)       build_ext ;;
        --docker-build)    build_docker ;;
        --docker-run)      shift; run_docker "$@" ;;
        --deploy-k8s)      deploy_kubernetes ;;
        --lint)            run_lint ;;
        --coverage)        run_tests_with_coverage ;;
        --generate-docs)   generate_docs ;;
        --validate-config) validate_config ;;
        --setup-env)       setup_env ;;
        --setup-env-check) check_prerequisites full ;;
        --app)             shift; launch_app "$@" ;;
        --clean)           clean ;;
        --help|-h)         show_help ;;
        *)                 error "Unknown argument: $1. Use --help for usage." ;;
    esac
}

cleanup_on_exit() {
    [ -f "$DAEMON_PID_FILE" ] && stop_daemon 2>/dev/null || true
}
trap cleanup_on_exit EXIT INT TERM

main "$@"
