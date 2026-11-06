#include "Python.h"
#include "pycore_ast.h"
#include "pycore_compile.h"
#include "pycore_symtable.h"

// these should be shared from codegen.c, but there is no internal header file
#define SUCCESS 0
#define ERROR -1
#define RETURN_IF_ERROR(X)  \
    do {                    \
        if ((X) == -1) {    \
            return ERROR;   \
        }                   \
    } while (0)
#define LOC(x) SRC_LOCATION_FROM_AST(x)

struct _PyCompiler;
typedef struct _PyCompiler compiler;
typedef _Py_SourceLocation location;

static int
codegen_validate_keywords(compiler *c, asdl_keyword_seq *keywords)
{
    Py_ssize_t nkeywords = asdl_seq_LEN(keywords);
    for (Py_ssize_t i = 0; i < nkeywords; i++) {
        keyword_ty key = ((keyword_ty)asdl_seq_GET(keywords, i));
        if (key->arg == NULL) {
            continue;
        }
        for (Py_ssize_t j = i + 1; j < nkeywords; j++) {
            keyword_ty other = ((keyword_ty)asdl_seq_GET(keywords, j));
            if (other->arg && !PyUnicode_Compare(key->arg, other->arg)) {
                return _PyCompile_Error(c, LOC(other), "keyword argument repeated: %U", key->arg);
            }
        }
    }
    return SUCCESS;
}

static int
validate_unpack(compiler *c, location loc, asdl_expr_seq *elts)
{
    Py_ssize_t n = asdl_seq_LEN(elts);
    int seen_star = 0;
    for (Py_ssize_t i = 0; i < n; i++) {
        expr_ty e = asdl_seq_GET(elts, i);
        if (e->kind == Starred_kind && !seen_star) {
            seen_star = 1;
            if ((i >= (1 << 8)) ||
                (n-i-1 >= (INT_MAX >> 8))) {
                return _PyCompile_Error(c, loc,
                    "too many expressions in "
                    "star-unpacking assignment");
            }
        } else if (e->kind == Starred_kind) {
            return _PyCompile_Error(c, loc,
                "multiple starred expressions in assignment");
        }
    }
    return SUCCESS;
}

typedef struct {
    compiler *c;
    PyUnicodeWriter *data;
    PyObject *const_cache;
    Py_ssize_t curr_lineno;
    Py_ssize_t curr_end_lineno;
} AnnotationEncoder;

static int
build_ast_size_t(AnnotationEncoder encoder, Py_ssize_t value) {
    do {
        unsigned char byte = value & 0x3F;
        value >>= 6;
        if (value) {
            byte |= 0x40;
        }
        RETURN_IF_ERROR(PyUnicodeWriter_WriteChar(encoder.data, byte));
    } while (value);
    return SUCCESS;
}

#define DEFINE_AST_SEQ_BUILDER(TYPE)                                             \
static int                                                                       \
build_ast_ ## TYPE ## _seq(AnnotationEncoder encoder, asdl_ ## TYPE ## _seq *seq) \
{                                                                                \
    if (!seq) {                                                                  \
        RETURN_IF_ERROR(build_ast_size_t(encoder, 0));                      \
        return SUCCESS;                                                          \
    }                                                                            \
    Py_ssize_t i, n = asdl_seq_LEN(seq);                                         \
    RETURN_IF_ERROR(build_ast_size_t(encoder, n));                          \
    for (i = 0; i < n; i++) {                                                    \
        RETURN_IF_ERROR(                                                         \
            build_ast_ ## TYPE (encoder, asdl_seq_GET(seq, i))              \
        );                                                                       \
    }                                                                            \
    return SUCCESS;                                                              \
}

static int
build_ast_const(AnnotationEncoder encoder, PyObject *value) {
    Py_ssize_t index;
    if (!value) {
        index = 0;
    } else if (value == Py_None) {
        index = 0;
    } else if (value == Py_False) {
        index = 1;
    } else if (value == Py_True) {
        index = 2;
    } else if (value == Py_Ellipsis) {
        index = 3;
    } else {
        index = 3 + _PyCompile_DictAddObj(encoder.const_cache, value);
    }
    RETURN_IF_ERROR(build_ast_size_t(encoder, index));
    return SUCCESS;
}

static int
build_ast_int(AnnotationEncoder encoder, long op)
{
    PyObject *op_obj = PyLong_FromLong(op);
    if (!op_obj) {
        return ERROR;
    }
    return build_ast_const(encoder, op_obj);
}

// can't use the macro here because the ast enums store Compare
// types as ints, but we expect them to be decoded as (inlined) simple sums
static int
build_ast_int_seq(AnnotationEncoder encoder, asdl_int_seq *seq){
    if (!seq) {
        RETURN_IF_ERROR(build_ast_size_t(encoder, 0));
        return SUCCESS;
    }
    Py_ssize_t i, n = asdl_seq_LEN(seq);
    RETURN_IF_ERROR(build_ast_size_t(encoder, n));
    for (i = 0; i < n; i++) {
        RETURN_IF_ERROR(
            build_ast_size_t(encoder, asdl_seq_GET(seq, i))
        );
    }
    return SUCCESS;
}

static int
build_ast_loc(AnnotationEncoder encoder, int attrs[4], bool with_line_info) {
    RETURN_IF_ERROR(build_ast_int(encoder, attrs[1]));
    RETURN_IF_ERROR(build_ast_int(encoder, attrs[3]));
    if (with_line_info) {
        RETURN_IF_ERROR(build_ast_int(encoder, attrs[0]));
        RETURN_IF_ERROR(build_ast_int(encoder, attrs[2]));
    }
    return SUCCESS;
}

static int build_ast_expr(AnnotationEncoder encoder, expr_ty expr, bool allow_starred);
static int
build_ast_expr_seq(AnnotationEncoder encoder, asdl_expr_seq *seq, bool allow_starred);

static int
build_ast_arg(AnnotationEncoder encoder, arg_ty arg) {
    if (!arg) {
        RETURN_IF_ERROR(PyUnicodeWriter_WriteChar(encoder.data, 0));
        return SUCCESS;
    }
    RETURN_IF_ERROR(build_ast_const(encoder, arg->arg));
    RETURN_IF_ERROR(build_ast_expr(encoder, arg->annotation, false));
    RETURN_IF_ERROR(build_ast_const(encoder, arg->type_comment));
    RETURN_IF_ERROR(build_ast_loc(encoder, &arg->lineno, true));
    return SUCCESS;
}
DEFINE_AST_SEQ_BUILDER(arg);

static int
build_ast_args(AnnotationEncoder encoder, arguments_ty args) {
    RETURN_IF_ERROR(build_ast_arg_seq(encoder, args->posonlyargs));
    RETURN_IF_ERROR(build_ast_arg_seq(encoder, args->args));
    RETURN_IF_ERROR(build_ast_arg(encoder, args->vararg));
    RETURN_IF_ERROR(build_ast_arg_seq(encoder, args->kwonlyargs));
    RETURN_IF_ERROR(build_ast_expr_seq(encoder, args->kw_defaults, false));
    RETURN_IF_ERROR(build_ast_arg(encoder, args->kwarg));
    RETURN_IF_ERROR(build_ast_expr_seq(encoder, args->defaults, false));
    return SUCCESS;
}

static int
build_ast_comprehension(AnnotationEncoder encoder, comprehension_ty comp) {
    RETURN_IF_ERROR(build_ast_expr(encoder, comp->target, false));
    RETURN_IF_ERROR(build_ast_expr(encoder, comp->iter, false));
    RETURN_IF_ERROR(build_ast_expr_seq(encoder, comp->ifs, false));
    RETURN_IF_ERROR(build_ast_int(encoder, comp->is_async));
    return SUCCESS;
}
DEFINE_AST_SEQ_BUILDER(comprehension);

static int
build_ast_keyword(AnnotationEncoder encoder, keyword_ty keyword)
{
    if (!keyword) {
        PyErr_SetString(PyExc_ValueError, "malformed AST");
        return ERROR;
    }
    RETURN_IF_ERROR(build_ast_const(encoder, keyword->arg));
    RETURN_IF_ERROR(build_ast_expr(encoder, keyword->value, false));
    RETURN_IF_ERROR(build_ast_loc(encoder, &keyword->lineno, true));
    return SUCCESS;
}
DEFINE_AST_SEQ_BUILDER(keyword);

static int
build_ast_expr(AnnotationEncoder encoder, expr_ty expr, bool allow_starred)
{
    if (!expr) {
        RETURN_IF_ERROR(PyUnicodeWriter_WriteChar(encoder.data, 0));
        return SUCCESS;
    }
    if (Py_EnterRecursiveCall(" during compilation")) {
        return ERROR;
    }
    Py_UCS1 kind;
    // use Slice_kind + 1 to indicate that the constant is prefixed with u
    if (expr->kind == Constant_kind && expr->v.Constant.kind) {
        kind = Slice_kind + 1;
    } else {
        kind = expr->kind;
    }
    // highest bit can't be used because of UTF-8, so we use the 7th bit
    bool with_line_info = encoder.curr_lineno != expr->lineno || encoder.curr_end_lineno != expr->end_lineno;
    kind = kind | (with_line_info << 6);
    RETURN_IF_ERROR(PyUnicodeWriter_WriteChar(encoder.data, kind));
    switch (expr->kind) {
    case BoolOp_kind:
        RETURN_IF_ERROR(PyUnicodeWriter_WriteChar(encoder.data, expr->v.BoolOp.op));
        if (build_ast_expr_seq(encoder, expr->v.BoolOp.values, false)) {
            goto failed;
        }
        break;
    case BinOp_kind:
        if (build_ast_expr(encoder, expr->v.BinOp.left, false)) {
            goto failed;
        }
        RETURN_IF_ERROR(PyUnicodeWriter_WriteChar(encoder.data, expr->v.BinOp.op));
        if (build_ast_expr(encoder, expr->v.BinOp.right, false)) {
            goto failed;
        }
        break;
    case UnaryOp_kind:
        RETURN_IF_ERROR(PyUnicodeWriter_WriteChar(encoder.data, expr->v.UnaryOp.op));
        if (build_ast_expr(encoder, expr->v.UnaryOp.operand, false)) {
            goto failed;
        }
        break;
    case Lambda_kind:
        if (build_ast_args(encoder, expr->v.Lambda.args)) {
            goto failed;
        }
        if (build_ast_expr(encoder, expr->v.Lambda.body, false)) {
            goto failed;
        }
        break;
    case IfExp_kind:
        if (build_ast_expr(encoder, expr->v.IfExp.test, false)) {
            goto failed;
        }
        if (build_ast_expr(encoder, expr->v.IfExp.body, false)) {
            goto failed;
        }
        if (build_ast_expr(encoder, expr->v.IfExp.orelse, false)) {
            goto failed;
        }
        break;
    case Dict_kind:
        if (build_ast_expr_seq(encoder, expr->v.Dict.keys, false)) {
            goto failed;
        }
        if (build_ast_expr_seq(encoder, expr->v.Dict.values, false)) {
            goto failed;
        }
        break;
    case Set_kind:
        if (build_ast_expr_seq(encoder, expr->v.Set.elts, true)) {
            goto failed;
        }
        break;
    case ListComp_kind:
        if (build_ast_expr(encoder, expr->v.ListComp.elt, true)) {
            goto failed;
        }
        if (build_ast_comprehension_seq(encoder, expr->v.ListComp.generators)) {
            goto failed;
        }
        break;
    case SetComp_kind:
        if (build_ast_expr(encoder, expr->v.SetComp.elt, true)) {
            goto failed;
        }
        if (build_ast_comprehension_seq(encoder, expr->v.SetComp.generators)) {
            goto failed;
        }
        break;
    case DictComp_kind:
        if (build_ast_expr(encoder, expr->v.DictComp.key, false)) {
            goto failed;
        }
        if (build_ast_expr(encoder, expr->v.DictComp.value, false)) {
            goto failed;
        }
        if (build_ast_comprehension_seq(encoder, expr->v.DictComp.generators)) {
            goto failed;
        }
        break;
    case GeneratorExp_kind:
        if (build_ast_expr(encoder, expr->v.GeneratorExp.elt, true)) {
            goto failed;
        }
        if (build_ast_comprehension_seq(encoder, expr->v.GeneratorExp.generators)) {
            goto failed;
        }
        break;
    case Compare_kind:
        if (build_ast_expr(encoder, expr->v.Compare.left, false)) {
            goto failed;
        }
        if (build_ast_int_seq(encoder, expr->v.Compare.ops)) {
            goto failed;
        }
        if (build_ast_expr_seq(encoder, expr->v.Compare.comparators, false)) {
            goto failed;
        }
        break;
    case Call_kind:
        if (codegen_validate_keywords(encoder.c, expr->v.Call.keywords) < 0) {
            goto failed;
        }
        if (build_ast_expr(encoder, expr->v.Call.func, false)) {
            goto failed;
        }
        if (build_ast_expr_seq(encoder, expr->v.Call.args, true)) {
            goto failed;
        }
        if (build_ast_keyword_seq(encoder, expr->v.Call.keywords)) {
            goto failed;
        }
        break;
    case FormattedValue_kind:
        if (build_ast_expr(encoder, expr->v.FormattedValue.value, false)) {
            goto failed;
        }
        if (build_ast_int(encoder, expr->v.FormattedValue.conversion)) {
            goto failed;
        }
        if (build_ast_expr(encoder, expr->v.FormattedValue.format_spec, false)) {
            goto failed;
        }
        break;
    case Interpolation_kind:
        if (build_ast_expr(encoder, expr->v.Interpolation.value, false)) {
            goto failed;
        }
        if (build_ast_const(encoder, expr->v.Interpolation.str)) {
            goto failed;
        }
        if (build_ast_int(encoder, expr->v.Interpolation.conversion)) {
            goto failed;
        }
        if (build_ast_expr(encoder, expr->v.Interpolation.format_spec, false)) {
            goto failed;
        }
        break;
    case JoinedStr_kind:
        if (build_ast_expr_seq(encoder, expr->v.JoinedStr.values, false)) {
            goto failed;
        }
        break;
    case TemplateStr_kind:
        if (build_ast_expr_seq(encoder, expr->v.TemplateStr.values, false)) {
            goto failed;
        }
        break;
    case Constant_kind:
        if (build_ast_const(encoder, expr->v.Constant.value)) {
            goto failed;
        }
        break;
    case Attribute_kind:
        if (build_ast_expr(encoder, expr->v.Attribute.value, false)) {
            goto failed;
        }
        if (build_ast_const(encoder, expr->v.Attribute.attr)) {
            goto failed;
        }
        break;
    case Subscript_kind:
        if (build_ast_expr(encoder, expr->v.Subscript.value, false)) {
            goto failed;
        }
        if (build_ast_expr(encoder, expr->v.Subscript.slice, false)) {
            goto failed;
        }
        break;
    case Starred_kind:
        if (!allow_starred) {
            if (expr->v.Starred.ctx == Store) {
                _PyCompile_Error(encoder.c, LOC(expr),
                "starred assignment target must be in a list or tuple");
                goto failed;
            }
            _PyCompile_Error(encoder.c, LOC(expr), "can't use starred expression here");
            goto failed;
        }
        if (build_ast_expr(encoder, expr->v.Starred.value, false)) {
            goto failed;
        }
        break;
    case Name_kind:
        if (build_ast_const(encoder, expr->v.Name.id)) {
            goto failed;
        }
        break;
    case List_kind:
        if (validate_unpack(encoder.c, LOC(expr), expr->v.List.elts) < 0) {
            goto failed;
        }
        if (build_ast_expr_seq(encoder, expr->v.List.elts, true)) {
            goto failed;
        }
        break;
    case Tuple_kind:
        if (validate_unpack(encoder.c, LOC(expr), expr->v.Tuple.elts) < 0) {
            goto failed;
        }
        if (build_ast_expr_seq(encoder, expr->v.Tuple.elts, true)) {
            goto failed;
        }
        break;
    case Slice_kind:
        if (build_ast_expr(encoder, expr->v.Slice.lower, false)) {
            goto failed;
        }
        if (build_ast_expr(encoder, expr->v.Slice.upper, false)) {
            goto failed;
        }
        if (build_ast_expr(encoder, expr->v.Slice.step, false)) {
            goto failed;
        }
        break;
    default:
        goto failed;
    }
    if (build_ast_loc(encoder, &expr->lineno, with_line_info)) {
        goto failed;
    }
    Py_LeaveRecursiveCall();
    return SUCCESS;
failed:
    Py_LeaveRecursiveCall();
    return ERROR;
}

static int
build_ast_expr_seq(AnnotationEncoder encoder, asdl_expr_seq *seq, bool allow_starred)
{
    if (!seq) {
        RETURN_IF_ERROR(build_ast_size_t(encoder, 0));
        return SUCCESS;
    }
    Py_ssize_t i, n = asdl_seq_LEN(seq);
    RETURN_IF_ERROR(build_ast_size_t(encoder, n));
    for (i = 0; i < n; i++) {
        RETURN_IF_ERROR(
            build_ast_expr(encoder, asdl_seq_GET(seq, i), allow_starred)
        );
    }
    return SUCCESS;
}

PyObject *
_PyCompile_GetAnnotationAST(compiler *compiler, expr_ty annotation, int stringify)
{
    PyUnicodeWriter *data = PyUnicodeWriter_Create(0);
    if (!data) {
        return NULL;
    }
    PyObject *const_cache = PyDict_New();
    if (!const_cache) {
        PyUnicodeWriter_Discard(data);
        return NULL;
    }
    if (_PyCompile_DictAddObj(const_cache, Py_None) == -1) {
        PyUnicodeWriter_Discard(data);
        Py_DECREF(const_cache);
        return NULL;
    }
    AnnotationEncoder encoder = {
        .c=compiler,
        .data=data,
        .const_cache=const_cache,
        .curr_lineno=1,
        .curr_end_lineno=1,
    };
    int err = 0;
    if (stringify) {
        PyObject *annotation_str = _PyAST_ExprAsUnicode(annotation);
        if (annotation_str == NULL) {
            PyUnicodeWriter_Discard(data);
            Py_DECREF(const_cache);
            return NULL;
        }
        err = PyUnicodeWriter_WriteChar(encoder.data, Constant_kind | (1 << 6));
        if (err == SUCCESS) {
            err = build_ast_const(encoder, annotation_str);
        }
        if (err == SUCCESS) {
            err = build_ast_loc(encoder, &annotation->lineno, true);
        }
        Py_DECREF(annotation_str);
    } else {
        err = build_ast_expr(encoder, annotation, true);
    }
    if (err == ERROR) {
        PyUnicodeWriter_Discard(data);
        Py_DECREF(const_cache);
        return NULL;
    }

    PyObject *result = PyUnicodeWriter_Finish(data);
    if (!result) {
        Py_DECREF(const_cache);
        return NULL;
    }
    PyUnicode_InternInPlace(&result);
    PyObject *consts, *k, *v;
    Py_ssize_t i, pos = 0, size = PyDict_GET_SIZE(const_cache);
    consts = PyTuple_New(size);
    if (!consts) {
        Py_DECREF(result);
        Py_DECREF(const_cache);
        return NULL;
    }
    while (PyDict_Next(const_cache, &pos, &k, &v)) {
        assert(PyLong_CheckExact(v));
        i = PyLong_AsLong(v);
        assert(i < size);
        assert(i >= 0);
        PyTuple_SET_ITEM(consts, i, Py_NewRef(k));
    }
    Py_DECREF(const_cache);
    PyTuple_SET_ITEM(consts, 0, result);
    return consts;
}
