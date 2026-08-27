from collections.abc import Callable, Iterable
import sys
from types import ModuleType
from importlib import import_module
import pkgutil
import tracemalloc
from annotationlib import Format, call_annotate_function
import timeit


sys.path.insert(0, "/workspaces/cpython/.venv/Lib/site-packages")
PACKAGES = [
    "urllib3",
    "certifi",
    "idna",
    "requests",
    "charset_normalizer",
    "cryptography",
    "pluggy",
    "pytest",
    "click",
    "iniconfig",
    "anyio",
    "attrs",
    "h11",
]


def iter_modules(package: str) -> Iterable[ModuleType]:
    """Iterate over all modules in a package."""
    try:
        package_module = import_module(package)
    except Exception as e:
        if "." not in package:
            raise
        return
    yield package_module
    if hasattr(package_module, "__path__"):
        for _, name, _ in pkgutil.iter_modules(package_module.__path__, package + "."):
            yield from iter_modules(name)


obj_cache = set()

def iter_annotates(obj: object) -> Iterable[Callable[[int], object]]:
    obj_cache.add(id(obj))
    if getattr(obj, "__annotate__", None) is not None and callable(obj.__annotate__):
        yield obj.__annotate__
    if isinstance(obj, (type, ModuleType)) and hasattr(obj, "__dict__"):
        for attr_value in obj.__dict__.values():
            if id(attr_value) not in obj_cache:
                yield from iter_annotates(attr_value)


tracemalloc.start()
start, _ = tracemalloc.get_traced_memory()
for package in PACKAGES:
    for mod in iter_modules(package):
        pass
end, _ = tracemalloc.get_traced_memory()
print(f"Total memory usage: {end - start}")


time = 0
for package in PACKAGES:
    for mod in iter_modules(package):
        for annotate in iter_annotates(mod):
            time_start = timeit.default_timer()
            call_annotate_function(annotate, format=Format.VALUE)
            time += timeit.default_timer() - time_start
print(f"Total time taken: {time}")


raise SystemExit
