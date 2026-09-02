from collections.abc import Callable, Iterable
import sys
from types import ModuleType
from importlib import import_module
import pkgutil
import tracemalloc
from annotationlib import Format
import timeit
from pathlib import Path
from typing import eval_annotate_as_types

import subprocess


code = """
from collections.abc import Callable, Iterable
import sys
from types import ModuleType
from importlib import import_module
import pkgutil
import tracemalloc
from annotationlib import Format
import timeit
from typing import eval_annotate_as_types
from pathlib import Path
from contextlib import redirect_stdout

VENV_PATH = Path("/workspaces/cpython/.venv/Lib/site-packages")
sys.path.insert(0, str(VENV_PATH))
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


def iter_annotates(obj: object) -> Iterable[Callable[[int], object]]:
    obj_cache.add(id(obj))
    if getattr(obj, "__annotate__", None) is not None and callable(obj.__annotate__):
        yield obj.__annotate__
    if isinstance(obj, (type, ModuleType)) and hasattr(obj, "__dict__"):
        for attr_value in obj.__dict__.values():
            if id(attr_value) not in obj_cache:
                yield from iter_annotates(attr_value)

obj_cache = set()

tracemalloc.start()
with redirect_stdout(open("/dev/null", "w")):
    start, _ = tracemalloc.get_traced_memory()
    time = timeit.default_timer()
    for package in PACKAGES:
        for mod in iter_modules(package):
            pass
print(timeit.default_timer() - time)
end, _ = tracemalloc.get_traced_memory()
print(end - start)

pyc_size = 0
for path in VENV_PATH.glob("**/*.pyc"):
    pyc_size += path.stat().st_size
print(pyc_size)

time = 0
for package in PACKAGES:
    for mod in iter_modules(package):
        for annotate in iter_annotates(mod):
            time_start = timeit.default_timer()
            try:
                eval_annotate_as_types(annotate, format=Format.VALUE)
            except KeyError:
                pass
            time += timeit.default_timer() - time_start
print(time)
"""


results = [subprocess.run([sys.executable, "-c", code], capture_output=True, text=True).stdout.split() for _ in range(5)]
import_time, memory, pyc, exec_time = 0, 0, 0, 0
for result in results:
    import_time += float(result[0])
    memory += int(result[1])
    pyc += int(result[2])
    exec_time += float(result[3])
import_time /= len(results)
memory /= len(results)
pyc /= len(results)
exec_time /= len(results)


print(f"Import time: {import_time}\nMemory: {memory}\nPyc size: {pyc}\nExec time: {exec_time}")
