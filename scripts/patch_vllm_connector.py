# virthub/scripts/patch_vllm_connector.py
#
# Patch vLLM's KV connector factory to recognize the `virthub` connector.
#
# Adds a new `elif` branch to vLLM's `factory.py` so that when the connector
# name is `virthub`, it imports and returns `VirthubKVConnectorV1` from the
# Virthub Python bindings.
#
# Usage:
#   python scripts/patch_vllm_connector.py
#
# The script is idempotent – running it multiple times has no effect after
# the first successful patch. It searches the active Python environment's
# site-packages for the vLLM installation.

import site
import sys
from pathlib import Path

_OLD_BLOCK = '''        else:
            raise ValueError(f"Unsupported connector type: {connector_name}")'''

_NEW_BLOCK = '''        elif connector_name == "virthub":
            from virthub.vllm import VirthubKVConnectorV1
            connector_cls = VirthubKVConnectorV1
            compat_sig = None
        else:
            raise ValueError(f"Unsupported connector type: {connector_name}")'''


def patch_factory(factory_path: Path) -> str:
    """
    Patch a single factory.py file.

    Returns:
        'patched'   – file was modified
        'already'   – file already contains the Virthub connector
        'not_found' – file does not exist
        'error'     – unexpected structure, could not patch
    """
    if not factory_path.exists():
        return 'not_found'

    content = factory_path.read_text(encoding="utf-8")

    # Idempotency check
    if "VirthubKVConnectorV1" in content:
        return 'already'

    if _OLD_BLOCK not in content:
        return 'error'

    new_content = content.replace(_OLD_BLOCK, _NEW_BLOCK, 1)
    factory_path.write_text(new_content, encoding="utf-8")
    return 'patched'


def main() -> int:
    found_vllm = False
    for site_dir in site.getsitepackages():
        candidate = (
            Path(site_dir)
            / "vllm"
            / "distributed"
            / "kv_transfer"
            / "kv_connector"
            / "factory.py"
        )
        status = patch_factory(candidate)

        if status == 'not_found':
            continue

        found_vllm = True

        if status == 'patched':
            print(f"Patched: {candidate}")
            return 0
        elif status == 'already':
            print(f"Already patched: {candidate}")
            return 0
        elif status == 'error':
            print(
                f"Failed to patch {candidate}: unexpected file structure.",
                file=sys.stderr,
            )
            return 1

    if not found_vllm:
        print(
            "Could not locate vLLM factory in the active environment. "
            "Ensure vLLM is installed and the correct virtual environment is activated.",
            file=sys.stderr,
        )
    else:
        print("No vLLM factory was patched.", file=sys.stderr)

    return 1


if __name__ == "__main__":
    sys.exit(main())
