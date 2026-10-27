#include "Python.h"
#include "pycore_ast.h"
#include "pycore_compile.h"

#define SUCCESS 0
#define ERROR -1
#define RETURN_IF_ERROR(X)  \
    do {                    \
        if ((X) == -1) {    \
            return ERROR;   \
        }                   \
    } while (0)

typedef struct {
    PyUnicodeWriter *data;
    PyObject *const_cache;
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
}                                                                                \

static int build_ast_expr(AnnotationEncoder encoder, expr_ty expr);
DEFINE_AST_SEQ_BUILDER(expr);

static int
build_ast_arg(AnnotationEncoder encoder, arg_ty arg) {
    if (!arg) {
        RETURN_IF_ERROR(PyUnicodeWriter_WriteChar(encoder.data, 0));
        return SUCCESS;
    }
    RETURN_IF_ERROR(build_ast_const(encoder, arg->arg));
    RETURN_IF_ERROR(build_ast_expr(encoder, arg->annotation));
    RETURN_IF_ERROR(build_ast_const(encoder, arg->type_comment));
    return SUCCESS;
}
DEFINE_AST_SEQ_BUILDER(arg);

static int
build_ast_args(AnnotationEncoder encoder, arguments_ty args) {
    RETURN_IF_ERROR(build_ast_arg_seq(encoder, args->posonlyargs));
    RETURN_IF_ERROR(build_ast_arg_seq(encoder, args->args));
    RETURN_IF_ERROR(build_ast_arg(encoder, args->vararg));
    RETURN_IF_ERROR(build_ast_arg_seq(encoder, args->kwonlyargs));
    RETURN_IF_ERROR(build_ast_expr_seq(encoder, args->kw_defaults));
    RETURN_IF_ERROR(build_ast_arg(encoder, args->kwarg));
    RETURN_IF_ERROR(build_ast_expr_seq(encoder, args->defaults));
    return SUCCESS;
}

static int
build_ast_comprehension(AnnotationEncoder encoder, comprehension_ty comp) {
    RETURN_IF_ERROR(build_ast_expr(encoder, comp->target));
    RETURN_IF_ERROR(build_ast_expr(encoder, comp->iter));
    RETURN_IF_ERROR(build_ast_expr_seq(encoder, comp->ifs));
    RETURN_IF_ERROR(build_ast_size_t(encoder, comp->is_async));
    return SUCCESS;
}
DEFINE_AST_SEQ_BUILDER(comprehension);

static int
build_ast_int(AnnotationEncoder encoder, long op)
{
    PyObject *op_obj = PyLong_FromLong(op);
    if (!op_obj) {
        return ERROR;
    }
    return build_ast_const(encoder, op_obj);
}
DEFINE_AST_SEQ_BUILDER(int);

static int
build_ast_keyword(AnnotationEncoder encoder, keyword_ty keyword)
{
    if (!keyword) {
        PyErr_SetString(PyExc_ValueError, "malformed AST");
        return ERROR;
    }
    RETURN_IF_ERROR(build_ast_const(encoder, keyword->arg));
    RETURN_IF_ERROR(build_ast_expr(encoder, keyword->value));
    return SUCCESS;
}
DEFINE_AST_SEQ_BUILDER(keyword);

static int
build_ast_expr(AnnotationEncoder encoder, expr_ty expr)
{
    if (!expr) {
        RETURN_IF_ERROR(PyUnicodeWriter_WriteChar(encoder.data, 0));
        return SUCCESS;
    }
    if (Py_EnterRecursiveCall(" during compilation")) {
        return ERROR;
    }
    RETURN_IF_ERROR(PyUnicodeWriter_WriteChar(encoder.data, expr->kind));
    switch (expr->kind) {
    case BoolOp_kind:
        RETURN_IF_ERROR(PyUnicodeWriter_WriteChar(encoder.data, expr->v.BoolOp.op));
        if (build_ast_expr_seq(encoder, expr->v.BoolOp.values)) {
            goto failed;
        }
        break;
    case BinOp_kind:
        if (build_ast_expr(encoder, expr->v.BinOp.left)) {
            goto failed;
        }
        RETURN_IF_ERROR(PyUnicodeWriter_WriteChar(encoder.data, expr->v.BinOp.op));
        if (build_ast_expr(encoder, expr->v.BinOp.right)) {
            goto failed;
        }
        break;
    case UnaryOp_kind:
        RETURN_IF_ERROR(PyUnicodeWriter_WriteChar(encoder.data, expr->v.UnaryOp.op));
        if (build_ast_expr(encoder, expr->v.UnaryOp.operand)) {
            goto failed;
        }
        break;
    case Lambda_kind:
        if (build_ast_args(encoder, expr->v.Lambda.args)) {
            goto failed;
        }
        if (build_ast_expr(encoder, expr->v.Lambda.body)) {
            goto failed;
        }
        break;
    case IfExp_kind:
        if (build_ast_expr(encoder, expr->v.IfExp.test)) {
            goto failed;
        }
        if (build_ast_expr(encoder, expr->v.IfExp.body)) {
            goto failed;
        }
        if (build_ast_expr(encoder, expr->v.IfExp.orelse)) {
            goto failed;
        }
        break;
    case Dict_kind:
        if (build_ast_expr_seq(encoder, expr->v.Dict.keys)) {
            goto failed;
        }
        if (build_ast_expr_seq(encoder, expr->v.Dict.values)) {
            goto failed;
        }
        break;
    case Set_kind:
        if (build_ast_expr_seq(encoder, expr->v.Set.elts)) {
            goto failed;
        }
        break;
    case ListComp_kind:
        if (build_ast_expr(encoder, expr->v.ListComp.elt)) {
            goto failed;
        }
        if (build_ast_comprehension_seq(encoder, expr->v.ListComp.generators)) {
            goto failed;
        }
        break;
    case SetComp_kind:
        if (build_ast_expr(encoder, expr->v.SetComp.elt)) {
            goto failed;
        }
        if (build_ast_comprehension_seq(encoder, expr->v.SetComp.generators)) {
            goto failed;
        }
        break;
    case DictComp_kind:
        if (build_ast_expr(encoder, expr->v.DictComp.key)) {
            goto failed;
        }
        if (build_ast_expr(encoder, expr->v.DictComp.value)) {
            goto failed;
        }
        if (build_ast_comprehension_seq(encoder, expr->v.DictComp.generators)) {
            goto failed;
        }
        break;
    case GeneratorExp_kind:
        if (build_ast_expr(encoder, expr->v.GeneratorExp.elt)) {
            goto failed;
        }
        if (build_ast_comprehension_seq(encoder, expr->v.GeneratorExp.generators)) {
            goto failed;
        }
        break;
    case Compare_kind:
        if (build_ast_expr(encoder, expr->v.Compare.left)) {
            goto failed;
        }
        if (build_ast_int_seq(encoder, expr->v.Compare.ops)) {
            goto failed;
        }
        if (build_ast_expr_seq(encoder, expr->v.Compare.comparators)) {
            goto failed;
        }
        break;
    case Call_kind:
        if (build_ast_expr(encoder, expr->v.Call.func)) {
            goto failed;
        }
        if (build_ast_expr_seq(encoder, expr->v.Call.args)) {
            goto failed;
        }
        if (build_ast_keyword_seq(encoder, expr->v.Call.keywords)) {
            goto failed;
        }
        break;
    case FormattedValue_kind:
        if (build_ast_expr(encoder, expr->v.FormattedValue.value)) {
            goto failed;
        }
        if (build_ast_int(encoder, expr->v.FormattedValue.conversion)) {
            goto failed;
        }
        if (build_ast_expr(encoder, expr->v.FormattedValue.format_spec)) {
            goto failed;
        }
        break;
    case Interpolation_kind:
        if (build_ast_expr(encoder, expr->v.Interpolation.value)) {
            goto failed;
        }
        if (build_ast_const(encoder, expr->v.Interpolation.str)) {
            goto failed;
        }
        if (build_ast_int(encoder, expr->v.Interpolation.conversion)) {
            goto failed;
        }
        if (build_ast_expr(encoder, expr->v.Interpolation.format_spec)) {
            goto failed;
        }
        break;
    case JoinedStr_kind:
        if (build_ast_expr_seq(encoder, expr->v.JoinedStr.values)) {
            goto failed;
        }
        break;
    case TemplateStr_kind:
        if (build_ast_expr_seq(encoder, expr->v.TemplateStr.values)) {
            goto failed;
        }
        break;
    case Constant_kind:
        if (build_ast_const(encoder, expr->v.Constant.value)) {
            goto failed;
        }
        break;
    case Attribute_kind:
        if (build_ast_expr(encoder, expr->v.Attribute.value)) {
            goto failed;
        }
        if (build_ast_const(encoder, expr->v.Attribute.attr)) {
            goto failed;
        }
        break;
    case Subscript_kind:
        if (build_ast_expr(encoder, expr->v.Subscript.value)) {
            goto failed;
        }
        if (build_ast_expr(encoder, expr->v.Subscript.slice)) {
            goto failed;
        }
        break;
    case Starred_kind:
        if (build_ast_expr(encoder, expr->v.Starred.value)) {
            goto failed;
        }
        break;
    case Name_kind:
        if (build_ast_const(encoder, expr->v.Name.id)) {
            goto failed;
        }
        break;
    case List_kind:
        if (build_ast_expr_seq(encoder, expr->v.List.elts)) {
            goto failed;
        }
        break;
    case Tuple_kind:
        if (build_ast_expr_seq(encoder, expr->v.Tuple.elts)) {
            goto failed;
        }
        break;
    case Slice_kind:
        if (build_ast_expr(encoder, expr->v.Slice.lower)) {
            goto failed;
        }
        if (build_ast_expr(encoder, expr->v.Slice.upper)) {
            goto failed;
        }
        if (build_ast_expr(encoder, expr->v.Slice.step)) {
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
    AnnotationEncoder encoder = {data, const_cache};
    int err = 0;
    if (stringify) {
        PyObject *annotation_str = _PyAST_ExprAsUnicode(annotation);
        if (annotation_str == NULL) {
            PyUnicodeWriter_Discard(data);
            Py_DECREF(const_cache);
            return NULL;
        }
        err = PyUnicodeWriter_WriteChar(encoder.data, Constant_kind);
        if (err == SUCCESS) {
            err = build_ast_const(encoder, annotation_str);
        }
        Py_DECREF(annotation_str);
    } else {
        err = build_ast_expr(encoder, annotation);
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
