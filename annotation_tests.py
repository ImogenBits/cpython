from collections.abc import Callable, Iterable
import sys
from types import ModuleType
from importlib import import_module
import pkgutil
import tracemalloc
from annotationlib import Format, call_annotate_function
import timeit
from pathlib import Path
from dataclasses import InitVar, dataclass
from typing import Annotated, ClassVar
from contextlib import redirect_stdout
import subprocess


class _Address:
    pass


def _measure_dataclass_creation(class_factory, iterations=1000):
    elapsed = 0.0
    for _ in range(iterations):
        cls = class_factory()
        start = timeit.default_timer()
        dataclass(cls)
        elapsed += timeit.default_timer() - start
    return elapsed / iterations


def make_simple_class():
    class Simple:
        id: int
        name: str
        active: bool
        score: float

    return Simple


def make_complex_class():
    class Complex:
        id: int
        name: str
        address: _Address
        addresses: list[_Address]
        metadata: dict[str, str]
        nickname: str | None
        score: Annotated[int, "display score"]
        _cache: ClassVar[dict[str, object]]
        source: InitVar[str]

    return Complex


def make_forwardref_class():
    class ForwardRef:
        id: int
        name: str
        address: MissingAddress
        addresses: list[MissingAddress]
        metadata: dict[str, str]
        nickname: str | None
        score: Annotated[MissingScore, "display score"]
        _cache: ClassVar[dict[str, object]]
        source: InitVar[str]

    return ForwardRef


def run_test():
    VENV_PATH = Path() / ".venv/Lib/site-packages"
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
    with open("/dev/null", "w") as dev_null, redirect_stdout(dev_null):
        start, _ = tracemalloc.get_traced_memory()
        time = timeit.default_timer()
        for package in PACKAGES:
            for mod in iter_modules(package):
                pass
        import_time = timeit.default_timer() - time
    end, _ = tracemalloc.get_traced_memory()
    memory = end - start

    pyc_size = 0
    for path in VENV_PATH.glob("**/*.pyc"):
        pyc_size += path.stat().st_size

    time = 0
    for package in PACKAGES:
        for mod in iter_modules(package):
            for annotate in iter_annotates(mod):
                time_start = timeit.default_timer()
                call_annotate_function(annotate, format=Format.VALUE)
                time += timeit.default_timer() - time_start

    tracemalloc.stop()
    simple = _measure_dataclass_creation(make_simple_class)
    complex = _measure_dataclass_creation(make_complex_class)
    forwardref = _measure_dataclass_creation(make_forwardref_class)

    return (
        import_time,
        memory,
        pyc_size,
        time,
        simple,
        complex,
        forwardref,
    )


code = """
import annotation_tests
print(*annotation_tests.run_test())
"""

if __name__ == "__main__":
    count = 10
    if len(sys.argv) >= 2:
        count = int(sys.argv[1])

    res = (0.0,) * 7
    if count > 1:
        procs = [subprocess.run([sys.executable, "-c", code], capture_output=True, text=True) for _ in range(count)]
        for proc in procs:
            if proc.stderr:
                print(proc.stderr)
                raise SystemExit
            res = tuple(
                curr + float(x) / len(procs)
                for curr, x in zip(res, proc.stdout.split())
            )
    else:
        sys.argv = ["annotation_tests.py"]
        res = run_test()

    print(
        f"Import time: {res[0]:.2f} s\n"
        f"Memory: {res[1] / 1_000_000:.3f} MB\n"
        f"Pyc size: {res[2] / 1_000_000:.3f} MB\n"
        f"Exec time: {res[3]:.4f} s\n"
        f"Dataclass creation (simple): {res[4] * 1_000_000:.2f} us\n"
        f"Dataclass creation (complex): {res[5] * 1_000_000:.2f} us\n"
        f"Dataclass creation (forward refs): {res[6] * 1_000_000:.2f} us"
    )
