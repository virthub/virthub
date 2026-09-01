# tests/python/integration/conftest.py

import os
import sys
import pytest
import warnings
from pathlib import Path
from typing import Generator


def has_gpu() -> bool:
    """Return True if a CUDA‑capable GPU is available."""
    try:
        import torch
        return torch.cuda.is_available()
    except Exception:
        return False


def pytest_sessionstart(session: pytest.Session) -> None:
    """
    Runs before any test module is imported.

    - Forces vLLM to use the `spawn` multiprocessing start method, which is
      required to avoid "Cannot re-initialize CUDA in forked subprocess".
    - If a GPU is present, ensures only the first GPU is visible during tests.
    - Suppresses harmless third‑party deprecation warnings.
    """
    # Required for vLLM on GPU – avoids CUDA re‑initialization after fork.
    os.environ.setdefault("VLLM_WORKER_MULTIPROC_METHOD", "spawn")

    # If GPU is available, set the default visible device to "0".
    # Users can override by setting CUDA_VISIBLE_DEVICES themselves.
    if has_gpu():
        os.environ.setdefault("CUDA_VISIBLE_DEVICES", "0")

    # Suppress known, harmless warnings from torch/transformers/multiprocessing.
    warnings.filterwarnings(
        "ignore",
        category=DeprecationWarning,
        module=r"torch\.jit\._script",
    )
    warnings.filterwarnings(
        "ignore",
        category=DeprecationWarning,
        module=r"transformers\.models\.gpt2\.tokenization_gpt2",
    )
    warnings.filterwarnings(
        "ignore",
        category=DeprecationWarning,
        message=r".*multi-threaded.*fork.*",
    )


@pytest.fixture(scope="session")
def project_root() -> Path:
    """Return the absolute path to the project root (contains ``Cargo.toml``)."""
    current = Path(__file__).resolve()
    while current.parent != current:
        if (current / "Cargo.toml").exists():
            return current
        current = current.parent
    raise RuntimeError("Could not find project root (Cargo.toml)")


@pytest.fixture(scope="session", autouse=True)
def setup_test_environment(project_root: Path) -> Generator[None, None, None]:
    """Add Python bindings to path and set test mode."""
    bindings_path = project_root / "bindings" / "python"
    if bindings_path.exists():
        sys.path.insert(0, str(bindings_path))

    os.environ["VIRTHUB_TEST_MODE"] = "1"

    yield

    os.environ.pop("VIRTHUB_TEST_MODE", None)


def pytest_configure(config: pytest.Config) -> None:
    """Register custom markers."""
    config.addinivalue_line(
        "markers", "integration: mark test as integration test (may be slow)"
    )
    config.addinivalue_line(
        "markers", "slow: mark test as slow-running"
    )
    config.addinivalue_line(
        "markers", "gpu: mark test as requiring GPU"
    )
    config.addinivalue_line(
        "markers", "performance: mark test as a performance/benchmark test"
    )


def pytest_collection_modifyitems(
    config: pytest.Config, items: list[pytest.Item]
) -> None:
    """Skip GPU‑only tests when no GPU is available."""
    if not has_gpu():
        skip_gpu = pytest.mark.skip(reason="No GPU available")
        for item in items:
            if "gpu" in item.keywords:
                item.add_marker(skip_gpu)
