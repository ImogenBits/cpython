from collections.abc import Iterable, Mapping
from typing import get_type_annotations
from annotationlib import get_annotations, Format
from timeit import timeit
from dis import dis

def f(a: int): ...

dis(f.__annotate__)
print(f.__annotate__(Format.AST))


class Mixed:
    a: int
    b: str
    c: list[int]
    d: Iterable[Mapping[str, tuple[int, int, int, str]]]


class Small:
    a: int
    b: int
    c: int
    d: int
    e: int
    f: int


class Undefined:
    a: U[U[U[U[U[U[U[U[U[U[U[U]]]]]]]]]]]


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
