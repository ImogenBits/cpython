// Compiler-generated __annotate__ and evaluate functions.

#include "Python.h"
#include "pycore_annotateobject.h"
#include "pycore_long.h"          // _PyLong_GetOne()
#include "pycore_modsupport.h"    // _PyArg_NoKeywords()
#include "pycore_object.h"        // _Py_ANNOTATE_FORMAT_VALUE
#include "pycore_runtime.h"       // _Py_ID()

#include <stddef.h>               // offsetof()

#define PyAnnotateObject_CAST(op)  ((PyAnnotateObject *)(op))

// unpack the static metadata for an annotation. it is either just the
// qualname a tuple of (qualname, freevars, flags, maybe scope metadata).
static int
unpack_payload(PyObject *payload, PyAnnotateObject *self)
{
    if (PyUnicode_Check(payload)) {
        PyObject *empty = PyTuple_New(0);
        if (empty == NULL) {
            return -1;
        }
        self->ann_qualname = Py_NewRef(payload);
        self->ann_freevars = empty;
        self->ann_flags = 0;
        self->ann_metadata = NULL;
        return 0;
    }
    if (!PyTuple_Check(payload)
        || PyTuple_GET_SIZE(payload) < 3
        || PyTuple_GET_SIZE(payload) > 4)
    {
        PyErr_Format(PyExc_SystemError,
                     "malformed annotation function payload: %R", payload);
        return -1;
    }
    PyObject *qualname = PyTuple_GET_ITEM(payload, 0);
    PyObject *freevars = PyTuple_GET_ITEM(payload, 1);
    PyObject *flags = PyTuple_GET_ITEM(payload, 2);
    if (!PyUnicode_Check(qualname) || !PyTuple_Check(freevars)
        || !PyLong_Check(flags))
    {
        PyErr_Format(PyExc_SystemError,
                     "malformed annotation function payload: %R", payload);
        return -1;
    }
    int f = PyLong_AsInt(flags);
    if (f == -1 && PyErr_Occurred()) {
        return -1;
    }
    self->ann_qualname = Py_NewRef(qualname);
    self->ann_freevars = Py_NewRef(freevars);
    self->ann_flags = f;
    self->ann_metadata = PyTuple_GET_SIZE(payload) == 4
                         ? Py_NewRef(PyTuple_GET_ITEM(payload, 3))
                         : NULL;
    return 0;
}

// annotate arguments are:
// * payload: static metadata; see unpack_payload above
// * closure: tuple of cells for freevars
// * globals: globals dict
PyObject *
_PyAnnotate_New(PyObject *payload, PyObject *closure, PyObject *globals)
{
    PyAnnotateObject *self = PyObject_GC_New(PyAnnotateObject, &PyAnnotate_Type);
    if (self == NULL) {
        return NULL;
    }
    if (unpack_payload(payload, self) < 0) {
        PyObject_GC_Del(self);
        return NULL;
    }
    assert(closure == NULL
           || PyTuple_GET_SIZE(closure) == PyTuple_GET_SIZE(self->ann_freevars));
    self->ann_closure = Py_XNewRef(closure);
    self->ann_globals = Py_XNewRef(globals);
    self->ann_strings = NULL;
    _PyObject_GC_TRACK(self);
    return (PyObject *)self;
}

// setting the strings is its own operation because currently we only
// support up to two argument intrinsics
int
_PyAnnotate_SetStrings(PyObject *op, PyObject *strings)
{
    assert(PyAnnotate_CheckExact(op));
    PyAnnotateObject *self = PyAnnotateObject_CAST(op);
    assert(self->ann_strings == NULL);
    self->ann_strings = Py_NewRef(strings);
    return 0;
}

static int
annotate_traverse(PyObject *op, visitproc visit, void *arg)
{
    PyAnnotateObject *self = PyAnnotateObject_CAST(op);
    Py_VISIT(self->ann_qualname);
    Py_VISIT(self->ann_freevars);
    Py_VISIT(self->ann_closure);
    Py_VISIT(self->ann_globals);
    Py_VISIT(self->ann_strings);
    Py_VISIT(self->ann_metadata);
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
    Py_CLEAR(self->ann_strings);
    Py_CLEAR(self->ann_metadata);
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
        (self->ann_flags & ANNOTATE_EVALUATE) ? "evaluate function"
                                              : "annotate function",
        self->ann_qualname, op);
}

static int
format_equals(PyObject *format, long expected)
{
    // Do a rich comparision so that behavior matches even when format
    // is an enum or something else weird.
    PyObject *o = PyLong_FromLong(expected);
    if (o == NULL) {
        return -1;
    }
    int res = PyObject_RichCompareBool(format, o, Py_EQ);
    Py_DECREF(o);
    return res;
}

// XXX: TODO: DESLOP
// The whole PEP 649 protocol for a compiler-generated annotation function.
// Both kinds produce annotation source strings, which the enclosing scope
// handed over at definition time:
//
//     if format == VALUE and not PEP 563: return <the strings, evaluated>
//     if format == VALUE or format == STRING: return the strings
//     raise NotImplementedError
//
// Evaluating means handing this object to annotationlib, which uses its
// globals and closure as the environment. PEP 563 only concerns __annotate__;
// a type alias value or type param bound is evaluated either way.
static PyObject *
annotate_call(PyObject *op, PyObject *args, PyObject *kwargs)
{
    PyAnnotateObject *self = PyAnnotateObject_CAST(op);
    int is_evaluate = (self->ann_flags & ANNOTATE_EVALUATE) != 0;

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
        // XXX: this message is wrong:
        PyErr_Format(PyExc_TypeError,
                     "%U() takes exactly one argument (%zd given)",
                     self->ann_qualname, nargs);
        return NULL;
    }

    int is_value = format_equals(format, _Py_ANNOTATE_FORMAT_VALUE);
    if (is_value < 0) {
        return NULL;
    }
    // evaluate_FOO() do not get stringified when ANNOTATE_FUTURE is set, but
    // __annotate__ functions do
    if (is_value && (is_evaluate || !(self->ann_flags & ANNOTATE_FUTURE))) {
        PyObject *impl = PyImport_ImportModuleAttrString("annotationlib",
                                                         "_annotate_value");
        if (impl == NULL) {
            return NULL;
        }
        PyObject *res = PyObject_CallFunctionObjArgs(
            impl, op, is_evaluate ? Py_True : Py_False, NULL);
        Py_DECREF(impl);
        return res;
    }
    if (!is_value) {
        int is_string = format_equals(format, _Py_ANNOTATE_FORMAT_STRING);
        if (is_string < 0) {
            return NULL;
        }
        if (!is_string) {
            PyErr_SetNone(PyExc_NotImplementedError);
            return NULL;
        }
    }
    if (self->ann_strings == NULL) {
        PyErr_SetString(PyExc_SystemError,
                        "annotation function has no annotation strings");
        return NULL;
    }
    return Py_NewRef(self->ann_strings);
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
        impl, (self->ann_flags & ANNOTATE_EVALUATE) ? Py_True : Py_False);
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
    {"_string_annotations", _Py_T_OBJECT, ANN_OFF(ann_strings), Py_READONLY},
    {"_annotate_metadata", _Py_T_OBJECT, ANN_OFF(ann_metadata), Py_READONLY},
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
