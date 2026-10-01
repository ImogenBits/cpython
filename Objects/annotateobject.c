// Compiler-generated __annotate__ and evaluate functions.

#include "Python.h"
#include "pycore_annotateobject.h"
#include "pycore_long.h"          // _PyLong_GetOne()
#include "pycore_modsupport.h"    // _PyArg_NoKeywords()
#include "pycore_object.h"        // _Py_ANNOTATE_FORMAT_VALUE
#include "pycore_runtime.h"       // _Py_ID()
#include "pycore_ast.h"            // _PyAST_FromAnnotationData()

#include <stddef.h>               // offsetof()

#define PyAnnotateObject_CAST(op)  ((PyAnnotateObject *)(op))

// unpack the static metadata for an annotation
static int
unpack_data(PyObject *data, PyAnnotateObject *self)
{
    if (PyUnicode_Check(data)) {
        self->ann_qualname = Py_NewRef(data);
        self->ann_closure = NULL;
        self->ann_freevars = NULL;
        self->ann_explicit_globals = NULL;
        self->ann_private_name = NULL;
        self->ann_mangled_names = NULL;
        return 0;
    }
    if (!PyTuple_Check(data) || PyTuple_GET_SIZE(data) != 6) {
        PyErr_Format(PyExc_SystemError,
                     "malformed annotation function data: %R", data);
        return -1;
    }
    PyObject *cells = PyTuple_GET_ITEM(data, 0);
    PyObject *freevars = PyTuple_GET_ITEM(data, 1);
    PyObject *explicit_globals = PyTuple_GET_ITEM(data, 2);
    PyObject *private_name = PyTuple_GET_ITEM(data, 3);
    PyObject *mangled_names = PyTuple_GET_ITEM(data, 4);
    PyObject *qualname = PyTuple_GET_ITEM(data, 5);

    if (   !(cells == Py_None || PyTuple_Check(cells))
        || !(freevars == Py_None || PyTuple_Check(freevars))
        || !(explicit_globals == Py_None || PyTuple_Check(explicit_globals))
        || !(private_name == Py_None || PyUnicode_Check(private_name))
        || !(mangled_names == Py_None || PyFrozenSet_Check(mangled_names))
        || !(PyUnicode_Check(qualname)))
    {
        PyErr_Format(PyExc_SystemError,
                     "malformed annotation function data: %R", data);
        return -1;
    }
    assert(PyTuple_GET_SIZE(cells) == PyTuple_GET_SIZE(freevars));
    self->ann_closure = Py_NewRef(cells);
    self->ann_freevars = Py_NewRef(freevars);
    self->ann_explicit_globals = Py_NewRef(explicit_globals);
    self->ann_private_name = Py_NewRef(private_name);
    self->ann_mangled_names = Py_NewRef(mangled_names);
    self->ann_qualname = Py_NewRef(qualname);
    return 0;
}

// annotate arguments are:
// * asts: a single AST string or a dict mapping variable names to them
// * globals: globals dict
// * data: either the qualname or a tuple of (cells, freevars, explicit_globals, private_name, qualname)
PyObject *
_PyAnnotate_New(PyObject *asts, PyObject *globals, PyObject *data)
{
    PyAnnotateObject *self = PyObject_GC_New(PyAnnotateObject, &PyAnnotate_Type);
    if (self == NULL) {
        return NULL;
    }
    if (unpack_data(data, self) < 0) {
        PyObject_GC_Del(self);
        return NULL;
    }
    self->ann_asts = Py_NewRef(asts);
    self->ann_globals = Py_XNewRef(globals);
    _PyObject_GC_TRACK(self);
    return (PyObject *)self;
}

static int
annotate_traverse(PyObject *op, visitproc visit, void *arg)
{
    PyAnnotateObject *self = PyAnnotateObject_CAST(op);
    Py_VISIT(self->ann_qualname);
    Py_VISIT(self->ann_freevars);
    Py_VISIT(self->ann_closure);
    Py_VISIT(self->ann_globals);
    Py_VISIT(self->ann_asts);
    Py_VISIT(self->ann_explicit_globals);
    Py_VISIT(self->ann_private_name);
    Py_VISIT(self->ann_mangled_names);
    return 0;
}

static int
annotate_clear(PyObject *op)
{
    PyAnnotateObject *self = PyAnnotateObject_CAST(op);
    Py_CLEAR(self->ann_qualname);
    Py_CLEAR(self->ann_freevars);
    Py_CLEAR(self->ann_closure);
    Py_CLEAR(self->ann_globals);
    Py_CLEAR(self->ann_asts);
    Py_CLEAR(self->ann_explicit_globals);
    Py_CLEAR(self->ann_private_name);
    Py_CLEAR(self->ann_mangled_names);
    return 0;
}

static void
annotate_dealloc(PyObject *op)
{
    _PyObject_GC_UNTRACK(op);
    (void)annotate_clear(op);
    Py_TYPE(op)->tp_free(op);
}

static PyObject *
annotate_repr(PyObject *op)
{
    PyAnnotateObject *self = PyAnnotateObject_CAST(op);
    return PyUnicode_FromFormat(
        "<%s %U at %p>",
        (PyUnicode_Check(self->ann_asts)) ? "evaluate function"
                                          : "annotate function",
        self->ann_qualname, op);
}

PyObject *
_eval_ast(PyObject *ann_ast_class, PyObject *format, PyObject *ast, PyObject *namespace,
    PyObject *explicit_globals, PyObject *private_name, PyObject *mangled_names)
{
    PyObject *ann_ast = PyObject_CallFunctionObjArgs(
        ann_ast_class, ast, namespace, explicit_globals, private_name,
        mangled_names, NULL);
    if (!ann_ast) {
        return NULL;
    }
    PyObject *result = PyObject_CallMethodObjArgs(ann_ast, &_Py_ID(evaluate), format);
    Py_DECREF(ann_ast);
    if (!result) {
        return NULL;
    }
    return result;
}

// The whole PEP 649 protocol for a compiler-generated annotation function.
// Supports VALUE and AST formats by constructing AnnotationAST objects
// and calling .evaluate(format) on them.
static PyObject *
annotate_call(PyObject *op, PyObject *args, PyObject *kwargs)
{
    PyAnnotateObject *self = PyAnnotateObject_CAST(op);
    int is_evaluate = PyUnicode_Check(self->ann_asts);

    if (kwargs != NULL && PyDict_GET_SIZE(kwargs) != 0) {
        PyErr_Format(PyExc_TypeError, "%U() takes no keyword arguments",
                     self->ann_qualname);
        return NULL;
    }
    PyObject *format;
    Py_ssize_t nargs = PyTuple_GET_SIZE(args);
    if (nargs == 1) {
        format = PyTuple_GET_ITEM(args, 0);
    }
    else if (nargs == 0 && is_evaluate) {
        // An evaluate function can be called with no arguments at all.
        format = _PyLong_GetOne();
    }
    else {
        if (is_evaluate) {
            PyErr_Format(PyExc_TypeError,
                "%U() takes from 0 to 1 positional arguments but %zd were given",
                self->ann_qualname, nargs);
        } else if (nargs == 0) {
            PyErr_Format(PyExc_TypeError,
                "%U() missing 1 required positional argument: 'format'",
                self->ann_qualname);
        } else {
            PyErr_Format(PyExc_TypeError,
                "%U() takes 1 positional argument but %zd were given",
                self->ann_qualname, nargs);
        }
        return NULL;
    }

    PyObject *parsed_asts = _PyAST_FromAnnotationData(self->ann_asts);
    if (!parsed_asts) {
        return NULL;
    }
    PyObject *make_namespace = PyImport_ImportModuleAttrString(
        "annotationlib", "_namespace_from_annotate"
    );
    if (!make_namespace) {
        return NULL;
    }
    PyObject *namespace = PyObject_CallFunctionObjArgs(make_namespace, self, NULL);
    Py_DECREF(make_namespace);
    if (!namespace) {
        return NULL;
    }
    PyObject *annASTClass = PyImport_ImportModuleAttrString(
        "annotationlib", "AnnotationAST"
    );
    if (!annASTClass) {
        Py_DECREF(namespace);
        return NULL;
    }
    PyObject *explicit_globals = self->ann_explicit_globals;
    if (!explicit_globals) {
        explicit_globals = Py_None;
    }
    PyObject *private_name = self->ann_private_name;
    if (!private_name) {
        private_name = Py_None;
    }
    PyObject *mangled_names = self->ann_mangled_names;
    if (!mangled_names) {
        mangled_names = Py_None;
    }
    PyObject *result = NULL;
    if (is_evaluate) {
        assert(PyUnicode_Check(parsed_asts));
        result = _eval_ast(annASTClass, format, parsed_asts, namespace,
            explicit_globals, private_name, mangled_names);
    } else {
        assert(PyDict_Check(parsed_asts));
        result = PyDict_New();
        if (!result) {
            goto failed;
        }
        PyObject *key, *value;
        Py_ssize_t pos = 0;
        while (PyDict_Next(parsed_asts, &pos, &key, &value)) {
            PyObject *evaluated = _eval_ast(annASTClass, format, value, namespace,
                explicit_globals, private_name, mangled_names);
            if (!evaluated) {
                goto failed;
            }
            if (PyDict_SetItem(result, key, evaluated) < 0) {
                Py_DECREF(evaluated);
                goto failed;
            }
            Py_DECREF(evaluated);
        }
    }
failed:
    Py_DECREF(annASTClass);
    Py_DECREF(namespace);
    Py_DECREF(parsed_asts);
    return result;
}

static PyObject *
annotate_get_name(PyObject *op, void *Py_UNUSED(closure))
{
    PyObject *qualname = PyAnnotateObject_CAST(op)->ann_qualname;
    // The name is the last component of the potentially-dotted qualname.
    Py_ssize_t len = PyUnicode_GET_LENGTH(qualname);
    Py_ssize_t dot = PyUnicode_FindChar(qualname, '.', 0, len, -1);
    if (dot < 0) {
        if (dot < -1) {
            return NULL;
        }
        return Py_NewRef(qualname);
    }
    return PyUnicode_Substring(qualname, dot + 1, len);
}

static PyObject *
annotate_get_module(PyObject *op, void *Py_UNUSED(closure))
{
    PyObject *globals = PyAnnotateObject_CAST(op)->ann_globals;
    if (globals == NULL) {
        Py_RETURN_NONE;
    }
    PyObject *module;
    if (PyDict_GetItemRef(globals, &_Py_ID(__name__), &module) < 0) {
        return NULL;
    }
    if (module == NULL) {
        Py_RETURN_NONE;
    }
    return module;
}

static PyObject *
annotate_get_signature(PyObject *op, void *Py_UNUSED(closure))
{
    // XXX: TODO: DESLOP
    // inspect.signature() cannot work this out for itself: an instance of a C
    // type takes neither the __text_signature__ path nor the functionlike one.
    PyAnnotateObject *self = PyAnnotateObject_CAST(op);
    PyObject *impl = PyImport_ImportModuleAttrString("annotationlib",
                                                     "_annotate_signature");
    if (impl == NULL) {
        return NULL;
    }
    PyObject *res = PyObject_CallOneArg(
        impl, PyUnicode_Check(self->ann_asts) ? Py_True : Py_False);
    Py_DECREF(impl);
    return res;
}

static PyGetSetDef annotate_getsetlist[] = {
    {"__name__", annotate_get_name, NULL, NULL},
    {"__module__", annotate_get_module, NULL, NULL},
    {"__signature__", annotate_get_signature, NULL, NULL},
    {NULL}
};

#define ANN_OFF(x) offsetof(PyAnnotateObject, x)

static PyMemberDef annotate_memberlist[] = {
    {"__qualname__", _Py_T_OBJECT, ANN_OFF(ann_qualname), Py_READONLY},
    {"__globals__", _Py_T_OBJECT, ANN_OFF(ann_globals), Py_READONLY},
    {"__closure__", _Py_T_OBJECT, ANN_OFF(ann_closure), Py_READONLY},
    {"__freevars__", _Py_T_OBJECT, ANN_OFF(ann_freevars), Py_READONLY},
    {"_asts", _Py_T_OBJECT, ANN_OFF(ann_asts), Py_READONLY},
    {"_explicit_globals", _Py_T_OBJECT, ANN_OFF(ann_explicit_globals), Py_READONLY},
    {"_private_name", _Py_T_OBJECT, ANN_OFF(ann_private_name), Py_READONLY},
    {"_mangled_names", _Py_T_OBJECT, ANN_OFF(ann_mangled_names), Py_READONLY},
    {NULL}
};

#undef ANN_OFF

PyDoc_STRVAR(annotate_doc,
"An __annotate__ function, or a function that evaluates a type alias\n\
value or a type parameter bound or default.\n\
\n\
Takes a single argument, a member of the annotationlib.Format enum, and\n\
returns the annotations in that format.");

PyTypeObject PyAnnotate_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    .tp_name = "annotate",
    .tp_basicsize = sizeof(PyAnnotateObject),
    .tp_dealloc = annotate_dealloc,
    .tp_repr = annotate_repr,
    .tp_call = annotate_call,
    .tp_getattro = PyObject_GenericGetAttr,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_doc = annotate_doc,
    .tp_traverse = annotate_traverse,
    .tp_clear = annotate_clear,
    .tp_members = annotate_memberlist,
    .tp_getset = annotate_getsetlist,
    .tp_alloc = PyType_GenericAlloc,
    .tp_free = PyObject_GC_Del,
};
