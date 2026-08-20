from collections.abc import Callable, Iterable, Mapping
from sys import _is_interned, getsizeof
import sys
from types import EllipsisType, ModuleType
from typing import ClassVar, Self, TypeAliasType, get_type_annotations
from annotationlib import get_annotations, Format
from timeit import timeit
from dis import dis, Bytecode, opmap
from importlib import import_module
import pkgutil

from dataclasses import dataclass, field


def iter_modules(package: str) -> Iterable[ModuleType]:
    """Iterate over all modules in a package."""
    package_module = import_module(package)
    yield package_module
    if hasattr(package_module, "__path__"):
        for _, name, _ in pkgutil.iter_modules(package_module.__path__, package + "."):
            yield from iter_modules(name)


type Constant = bool | int | str | bytes | float | complex | None | EllipsisType | tuple[Constant, ...]


def iter_annotates(obj: object) -> Iterable[Callable]:
    if isinstance(obj, (type, ModuleType)) or callable(obj):
        annotate = getattr(obj, "__annotate__", None)
    elif isinstance(obj, TypeAliasType):
        annotate = obj.evaluate_value
    else:
        annotate = None
    if callable(annotate):
        yield annotate

    if isinstance(obj, (type, TypeAliasType)) or callable(obj):
        for type_var in obj.__type_params__:
            yield type_var.evaluate_bound
            yield type_var.evaluate_contraints
            yield type_var.evaluate_default

    if isinstance(obj, (type, ModuleType)):
        for attr in obj.__dict__.values():
            if not isinstance(attr, ModuleType):
                yield from iter_annotates(attr)


def get_ast_consts(annotate: Callable[..., object]) -> Constant:
    bytecode = Bytecode(annotate)
    instructions = list(bytecode)
    for i, instr in enumerate(instructions):
        if instr.opname == "CALL_INTRINSIC_2" and instr.arg == 6:
            return instructions[i - 2].argval
    raise RuntimeError


@dataclass
class MeasureAnnotates:
    interned: dict[str, bool] = field(default_factory=dict)
    num_annotates: int = 0
    ast_consts: int = 0
    base_consts: int = 0
    base_bytecode: int = 0

    AST_BYTECODE_LEN: ClassVar[int] = 22

    @property
    def ast_bytecode_size(self) -> int:
        return self.num_annotates * self.AST_BYTECODE_LEN

    def interned_size(self, *, is_base: bool) -> int:
        return sum(getsizeof(val) for val, base in self.interned.items() if base == is_base)

    @classmethod
    def measure(cls, packages: Iterable[str]) -> Self:
        self = cls()
        annotates: dict[int, Callable] = {}
        for package in packages:
            for module in iter_modules(package):
                annotates |= {id(annotate): annotate for annotate in iter_annotates(module)}
        for annotate in annotates.values():
            self.num_annotates += 1
            ast_data = get_ast_consts(annotate)
            self.ast_consts += self.const_size(ast_data, is_base=False)
            self.base_consts += sum(self.const_size(const, is_base=True) for const in annotate.__code__.co_consts if const is not ast_data)
            self.base_bytecode += self.base_bytecode_size(annotate)
        return self

    def const_size(self, value: Constant, *, is_base: bool) -> int:
        match value:
            case float() | complex() | bytes():
                return getsizeof(value)
            case int() if value < -5 or value > 256:
                return getsizeof(value)
            case str():
                if _is_interned(value):
                    self.interned[value] = self.interned.get(value, False) | is_base
                    return 0
                else:
                    return getsizeof(value)
            case tuple():
                return getsizeof(value) + sum(self.const_size(elem, is_base=is_base) for elem in value)
            case int() | None | EllipsisType():
                return 0
            case _:
                raise ValueError

    def base_bytecode_size(self, annotate: Callable) -> int:
        return len(annotate.__code__.co_code) - self.AST_BYTECODE_LEN

sys.path.append("./venv/Lib/site-packages")

measurement = MeasureAnnotates.measure([
    "urllib",
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
])
print(
    f"""Number of Annotates: {measurement.num_annotates: }
Base Bytecode: {measurement.base_bytecode: }
Base Consts: {measurement.base_consts: }
Base Interned: {measurement.interned_size(is_base=True): }
AST Bytecode: {measurement.ast_bytecode_size: }
AST Consts: {measurement.ast_consts: }
AST Interned: {measurement.interned_size(is_base=False): }
"""
)


"""
def test_annotations(name: str):
    formats = ["VALUE", "FORWARDREF", "STRING"]
    tests = [
        (f"anno {format}:", f"get_annotations({name}, {' eval_str=True,' if format == 'VALUE' else ''} format=Format.{format})")
        for format in formats
    ]
    formats = ["AST", *formats]
    tests += [
        (f"ast  {format}:", f"get_type_annotations({name}, eval_str=True, format=Format.{format})")
        for format in formats
    ]
    print(f"{name} tests:")
    for test_name, expr in tests:
        try:
            time = timeit(expr, globals=globals(), number=10_000)
        except (NameError, KeyError) as e:
            time = str(e)
        print(f"{test_name: <16} {time}")


test_annotations("Small")
test_annotations("Mixed")
test_annotations("Undefined")
"""
