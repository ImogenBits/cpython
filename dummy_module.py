class Mixed:
    a: int
    b: str
    c: list[int]
    d: Iterable[Mapping[str, tuple[int, int, int, str]]]


class Small:
    pass
    a: int
    b: int
    c: int
    d: int
    e: int
    f: int


class Undefined:
    a: U[U[U[U[U[U[U[U[U[U[U[U]]]]]]]]]]]
