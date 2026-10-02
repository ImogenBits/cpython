#include "Python.h"
#include "pycore_ast.h"

#define SUCCESS 0
#define ERROR -1
#define RETURN_IF_ERROR(X)  \
    do {                    \
        if ((X) == -1) {    \
            return ERROR;   \
        }                   \
    } while (0)

static int
build_ast_size_t(PyUnicodeWriter *data, Py_ssize_t value) {
    do {
        unsigned char byte = value & 0x3F;
        value >>= 6;
        if (value) {
            byte |= 0x40;
        }
        RETURN_IF_ERROR(PyUnicodeWriter_WriteChar(data, byte));
    } while (value);
    return SUCCESS;
}

static int
build_ast_string(PyUnicodeWriter *data, PyObject *value) {
    Py_ssize_t len;
    const char *s = NULL;
    if (PyUnicode_CheckExact(value)) {
        s = PyUnicode_AsUTF8AndSize(value, &len);
    } else if (PyBytes_CheckExact(value)) {
        RETURN_IF_ERROR(PyBytes_AsStringAndSize(value, (char **) &s, &len));
    } else {
        PyErr_SetString(PyExc_TypeError, "value must be str or bytes");
        return ERROR;
    }
    if (!s) {
        return ERROR;
    }
    RETURN_IF_ERROR(build_ast_size_t(data, len));
    PyUnicodeWriter_WriteUTF8(data, s, len);
    return SUCCESS;
}

static int
build_ast_double(PyUnicodeWriter *data, double value) {
    if (value == -1.0 && PyErr_Occurred()) {
        return ERROR;
    }
    unsigned long long bytes = (unsigned long long) value;
    for (size_t i = 0; i < 10; i++) {
        RETURN_IF_ERROR(PyUnicodeWriter_WriteChar(data, bytes & 0x7F));
        bytes >>= 7;
    }
    return SUCCESS;
}

static int
build_ast_const(PyUnicodeWriter *data, PyObject *value) {
    if (!value) {
        RETURN_IF_ERROR(PyUnicodeWriter_WriteChar(data, Slice_kind + 2));
        return SUCCESS;
    } else if (PyUnicode_CheckExact(value)) {
        RETURN_IF_ERROR(PyUnicodeWriter_WriteChar(data, Slice_kind + 3));
        RETURN_IF_ERROR(build_ast_string(data, value));
    } else if (PyBytes_CheckExact(value)) {
        RETURN_IF_ERROR(PyUnicodeWriter_WriteChar(data, Slice_kind + 4));
        RETURN_IF_ERROR(build_ast_string(data, value));
    } else if (PyLong_CheckExact(value)) {
        RETURN_IF_ERROR(PyUnicodeWriter_WriteChar(data, Slice_kind + 5));
        Py_ssize_t val = PyLong_AsSsize_t(value);
        if (val == -1 && PyErr_Occurred()) {
            return ERROR;
        }
        RETURN_IF_ERROR(build_ast_size_t(data, val));
    } else if (PyFloat_CheckExact(value)) {
        RETURN_IF_ERROR(PyUnicodeWriter_WriteChar(data, Slice_kind + 6));
        double val = PyFloat_AsDouble(value);
        RETURN_IF_ERROR(build_ast_double(data, val));
    } else if (PyComplex_CheckExact(value)) {
        RETURN_IF_ERROR(PyUnicodeWriter_WriteChar(data, Slice_kind + 7));
        double re = PyComplex_RealAsDouble(value);
        RETURN_IF_ERROR(build_ast_double(data, re));
        double im = PyComplex_ImagAsDouble(value);
        RETURN_IF_ERROR(build_ast_double(data, im));
    } else if (value == Py_False) {
        RETURN_IF_ERROR(PyUnicodeWriter_WriteChar(data, Slice_kind + 8));
    } else if (value == Py_True) {
        RETURN_IF_ERROR(PyUnicodeWriter_WriteChar(data, Slice_kind + 9));
    } else if (value == Py_None) {
        RETURN_IF_ERROR(PyUnicodeWriter_WriteChar(data, Slice_kind + 10));
    } else if (value == Py_Ellipsis) {
        RETURN_IF_ERROR(PyUnicodeWriter_WriteChar(data, Slice_kind + 11));
    } else {
        PyErr_SetString(PyExc_ValueError, "malformed AST");
        return ERROR;
    }
    return SUCCESS;
}

#define DEFINE_AST_SEQ_BUILDER(TYPE)                                        \
static int                                                                  \
build_ast_ ## TYPE ## _seq(PyUnicodeWriter *data, asdl_ ## TYPE ## _seq *seq) \
{                                                                           \
    if (!seq) {                                                             \
        RETURN_IF_ERROR(build_ast_size_t(data, 0));                         \
        return SUCCESS;                                                     \
    }                                                                       \
    Py_ssize_t i, n = asdl_seq_LEN(seq);                                    \
    RETURN_IF_ERROR(build_ast_size_t(data, n));                             \
    for (i = 0; i < n; i++) {                                               \
        RETURN_IF_ERROR(                                                    \
            build_ast_ ## TYPE (data, asdl_seq_GET(seq, i))                 \
        );                                                                  \
    }                                                                       \
    return SUCCESS;                                                         \
}                                                                           \

static int build_ast_expr(PyUnicodeWriter *data, expr_ty expr);
DEFINE_AST_SEQ_BUILDER(expr);

static int
build_ast_arg(PyUnicodeWriter *data, arg_ty arg) {
    if (!arg) {
        RETURN_IF_ERROR(PyUnicodeWriter_WriteChar(data, 0));
        return SUCCESS;
    }
    RETURN_IF_ERROR(build_ast_string(data, arg->arg));
    RETURN_IF_ERROR(build_ast_expr(data, arg->annotation));
    RETURN_IF_ERROR(build_ast_string(data, arg->type_comment));
    return SUCCESS;
}
DEFINE_AST_SEQ_BUILDER(arg);

static int
build_ast_args(PyUnicodeWriter *data, arguments_ty args) {
    RETURN_IF_ERROR(build_ast_arg_seq(data, args->posonlyargs));
    RETURN_IF_ERROR(build_ast_arg_seq(data, args->args));
    RETURN_IF_ERROR(build_ast_arg(data, args->vararg));
    RETURN_IF_ERROR(build_ast_arg_seq(data, args->kwonlyargs));
    RETURN_IF_ERROR(build_ast_expr_seq(data, args->kw_defaults));
    RETURN_IF_ERROR(build_ast_arg(data, args->kwarg));
    RETURN_IF_ERROR(build_ast_expr_seq(data, args->defaults));
    return SUCCESS;
}

static int
build_ast_comprehension(PyUnicodeWriter *data, comprehension_ty comp) {
    RETURN_IF_ERROR(build_ast_expr(data, comp->target));
    RETURN_IF_ERROR(build_ast_expr(data, comp->iter));
    RETURN_IF_ERROR(build_ast_expr_seq(data, comp->ifs));
    RETURN_IF_ERROR(build_ast_size_t(data, comp->is_async));
    return SUCCESS;
}
DEFINE_AST_SEQ_BUILDER(comprehension);

static int
build_ast_int(PyUnicodeWriter *data, cmpop_ty op)
{
    return build_ast_size_t(data, op);
}
DEFINE_AST_SEQ_BUILDER(int);

static int
build_ast_keyword(PyUnicodeWriter *data, keyword_ty keyword)
{
    if (!keyword) {
        PyErr_SetString(PyExc_ValueError, "malformed AST");
        return ERROR;
    }
    RETURN_IF_ERROR(build_ast_string(data, keyword->arg));
    RETURN_IF_ERROR(build_ast_expr(data, keyword->value));
    return SUCCESS;
}
DEFINE_AST_SEQ_BUILDER(keyword);

static int
build_ast_expr(PyUnicodeWriter *data, expr_ty expr)
{
    if (!expr) {
        RETURN_IF_ERROR(PyUnicodeWriter_WriteChar(data, 0));
        return SUCCESS;
    }
    if (Py_EnterRecursiveCall(" during compilation")) {
        return ERROR;
    }
    if (expr->kind != Constant_kind) {
        RETURN_IF_ERROR(PyUnicodeWriter_WriteChar(data, expr->kind));
    }
    switch (expr->kind) {
    case BoolOp_kind:
        RETURN_IF_ERROR(PyUnicodeWriter_WriteChar(data, expr->v.BoolOp.op));
        if (build_ast_expr_seq(data, expr->v.BoolOp.values)) {
            goto failed;
        }
        break;
    case BinOp_kind:
        if (build_ast_expr(data, expr->v.BinOp.left)) {
            goto failed;
        }
        RETURN_IF_ERROR(PyUnicodeWriter_WriteChar(data, expr->v.BinOp.op));
        if (build_ast_expr(data, expr->v.BinOp.right)) {
            goto failed;
        }
        break;
    case UnaryOp_kind:
        RETURN_IF_ERROR(PyUnicodeWriter_WriteChar(data, expr->v.UnaryOp.op));
        if (build_ast_expr(data, expr->v.UnaryOp.operand)) {
            goto failed;
        }
        break;
    case Lambda_kind:
        if (build_ast_args(data, expr->v.Lambda.args)) {
            goto failed;
        }
        if (build_ast_expr(data, expr->v.Lambda.body)) {
            goto failed;
        }
        break;
    case IfExp_kind:
        if (build_ast_expr(data, expr->v.IfExp.test)) {
            goto failed;
        }
        if (build_ast_expr(data, expr->v.IfExp.body)) {
            goto failed;
        }
        if (build_ast_expr(data, expr->v.IfExp.orelse)) {
            goto failed;
        }
        break;
    case Dict_kind:
        if (build_ast_expr_seq(data, expr->v.Dict.keys)) {
            goto failed;
        }
        if (build_ast_expr_seq(data, expr->v.Dict.values)) {
            goto failed;
        }
        break;
    case Set_kind:
        if (build_ast_expr_seq(data, expr->v.Set.elts)) {
            goto failed;
        }
        break;
    case ListComp_kind:
        if (build_ast_expr(data, expr->v.ListComp.elt)) {
            goto failed;
        }
        if (build_ast_comprehension_seq(data, expr->v.ListComp.generators)) {
            goto failed;
        }
        break;
    case SetComp_kind:
        if (build_ast_expr(data, expr->v.SetComp.elt)) {
            goto failed;
        }
        if (build_ast_comprehension_seq(data, expr->v.SetComp.generators)) {
            goto failed;
        }
        break;
    case DictComp_kind:
        if (build_ast_expr(data, expr->v.DictComp.key)) {
            goto failed;
        }
        if (build_ast_expr(data, expr->v.DictComp.value)) {
            goto failed;
        }
        if (build_ast_comprehension_seq(data, expr->v.DictComp.generators)) {
            goto failed;
        }
        break;
    case GeneratorExp_kind:
        if (build_ast_expr(data, expr->v.GeneratorExp.elt)) {
            goto failed;
        }
        if (build_ast_comprehension_seq(data, expr->v.GeneratorExp.generators)) {
            goto failed;
        }
        break;
    case Compare_kind:
        if (build_ast_expr(data, expr->v.Compare.left)) {
            goto failed;
        }
        if (build_ast_int_seq(data, expr->v.Compare.ops)) {
            goto failed;
        }
        if (build_ast_expr_seq(data, expr->v.Compare.comparators)) {
            goto failed;
        }
        break;
    case Call_kind:
        if (build_ast_expr(data, expr->v.Call.func)) {
            goto failed;
        }
        if (build_ast_expr_seq(data, expr->v.Call.args)) {
            goto failed;
        }
        if (build_ast_keyword_seq(data, expr->v.Call.keywords)) {
            goto failed;
        }
        break;
    case FormattedValue_kind:
        if (build_ast_expr(data, expr->v.FormattedValue.value)) {
            goto failed;
        }
        if (build_ast_size_t(data, expr->v.FormattedValue.conversion)) {
            goto failed;
        }
        if (build_ast_expr(data, expr->v.FormattedValue.format_spec)) {
            goto failed;
        }
        break;
    case Interpolation_kind:
        if (build_ast_expr(data, expr->v.Interpolation.value)) {
            goto failed;
        }
        if (build_ast_const(data, expr->v.Interpolation.str)) {
            goto failed;
        }
        if (build_ast_size_t(data, expr->v.Interpolation.conversion)) {
            goto failed;
        }
        if (build_ast_expr(data, expr->v.Interpolation.format_spec)) {
            goto failed;
        }
        break;
    case JoinedStr_kind:
        if (build_ast_expr_seq(data, expr->v.JoinedStr.values)) {
            goto failed;
        }
        break;
    case TemplateStr_kind:
        if (build_ast_expr_seq(data, expr->v.TemplateStr.values)) {
            goto failed;
        }
        break;
    case Constant_kind:
        if (build_ast_const(data, expr->v.Constant.value)) {
            goto failed;
        }
        break;
    case Attribute_kind:
        if (build_ast_expr(data, expr->v.Attribute.value)) {
            goto failed;
        }
        if (build_ast_string(data, expr->v.Attribute.attr)) {
            goto failed;
        }
        break;
    case Subscript_kind:
        if (build_ast_expr(data, expr->v.Subscript.value)) {
            goto failed;
        }
        if (build_ast_expr(data, expr->v.Subscript.slice)) {
            goto failed;
        }
        break;
    case Starred_kind:
        if (build_ast_expr(data, expr->v.Starred.value)) {
            goto failed;
        }
        break;
    case Name_kind:
        if (build_ast_string(data, expr->v.Name.id)) {
            goto failed;
        }
        break;
    case List_kind:
        if (build_ast_expr_seq(data, expr->v.List.elts)) {
            goto failed;
        }
        break;
    case Tuple_kind:
        if (build_ast_expr_seq(data, expr->v.Tuple.elts)) {
            goto failed;
        }
        break;
    case Slice_kind:
        if (build_ast_expr(data, expr->v.Slice.lower)) {
            goto failed;
        }
        if (build_ast_expr(data, expr->v.Slice.upper)) {
            goto failed;
        }
        if (build_ast_expr(data, expr->v.Slice.step)) {
            goto failed;
        }
        break;
    default:
        goto failed;
    }
    Py_LeaveRecursiveCall();
    return SUCCESS;
failed:
    Py_LeaveRecursiveCall();
    return ERROR;
}

PyObject *
_PyAST_GetAnnotationAST(expr_ty annotation, int stringify)
{
    PyUnicodeWriter *data = PyUnicodeWriter_Create(0);
    if (data == NULL) {
        return NULL;
    }
    int err = 0;
    if (stringify) {
        PyObject *annotation_str = _PyAST_ExprAsUnicode(annotation);
        if (annotation_str == NULL) {
            PyUnicodeWriter_Discard(data);
            return NULL;
        }
        err = build_ast_const(data, annotation_str);
        Py_DECREF(annotation_str);
    } else {
        err = build_ast_expr(data, annotation);
    }
    if (err == ERROR) {
        PyUnicodeWriter_Discard(data);
        return NULL;
    }
    PyObject *result = PyUnicodeWriter_Finish(data);
    if (!result) {
        return NULL;
    }
    PyUnicode_InternInPlace(&result);
    return result;
}
