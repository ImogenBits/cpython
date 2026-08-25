from collections.abc import Iterable
import sys
from types import ModuleType
from importlib import import_module
import pkgutil

from dataclasses import dataclass, field

import tracemalloc

from pathlib import Path


sys.path.insert(0, "/workspaces/cpython/.venv/Lib/site-packages")
PACKAGES = [
    "urllib3",
    "certifi",
    "idna",
    "requests",
    "charset-normalizer",
    "setuptools",
    "cryptography",
    "pluggy",
    "pydantic",
    "pytest",
    "click",
    "iniconfig",
    "anyio",
    "attrs",
    "h11",
]

failed_modules = []
module_list_path = Path("/workspaces/cpython/module_list.txt")

def iter_modules(package: str) -> Iterable[ModuleType]:
    """Iterate over all modules in a package."""
    try:
        package_module = import_module(package)
    except Exception:
        failed_modules.append(package)
        print(f"Failed to import {package}")
        return
    yield package_module
    if hasattr(package_module, "__path__"):
        for _, name, _ in pkgutil.iter_modules(package_module.__path__, package + "."):
            yield from iter_modules(name)


tracemalloc.start()
start, _ = tracemalloc.get_traced_memory()
for package in PACKAGES:
    for mod in iter_modules(package):
        pass
end, _ = tracemalloc.get_traced_memory()

print(f"Total memory usage: {end - start}")
if failed_modules:
    module_list_path.write_text("\n".join(failed_modules))

raise SystemExit
