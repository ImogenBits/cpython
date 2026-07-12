from collections.abc import Callable, Iterable, Mapping
from sys import _is_interned, getsizeof
from types import EllipsisType, ModuleType
from typing import Self, TypeAliasType, get_type_annotations
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
    interned: set[str] = field(default_factory=set)
    num_annotates: int = 0
    const_size: int = 0

    @property
    def bytecode_size(self) -> int:
        return self.num_annotates * 22

    def interned_size(self) -> int:
        return sum(getsizeof(val) for val in self.interned)

    @classmethod
    def measure(cls, packages: Iterable[str]) -> Self:
        self = cls()
        annotates: dict[int, Callable] = {}
        for package in packages:
            for module in iter_modules(package):
                annotates |= {id(annotate): annotate for annotate in iter_annotates(module)}
        for annotate in annotates.values():
            self.num_annotates += 1
            self.process_const(get_ast_consts(annotate))
        return self

    def process_const(self, value: Constant) -> None:
        match value:
            case float() | complex() | bytes():
                self.const_size += getsizeof(value)
            case int() if value < -5 or value > 256:
                self.const_size += getsizeof(value)
            case str():
                if _is_interned(value):
                    self.interned.add(value)
                else:
                    self.const_size += getsizeof(value)
            case tuple():
                self.const_size += getsizeof(value)
                for elem in value:
                    self.process_const(elem)
            case int() | None | EllipsisType():
                return
            case _:
                raise ValueError


measurement = MeasureAnnotates.measure([
    "dummy_module",
    #"packaging",
    #"urllib3",
])
print(
    f"""Number of Annotates: {measurement.num_annotates: }
Bytecode: {measurement.bytecode_size: }
Consts: {measurement.const_size: }
Interned: {measurement.interned_size(): }"""
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
