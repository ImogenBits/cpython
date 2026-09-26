// Compiler-generated __annotate__ and evaluate functions.

#ifndef Py_INTERNAL_ANNOTATEOBJECT_H
#define Py_INTERNAL_ANNOTATEOBJECT_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef Py_BUILD_CORE
#  error "this header requires Py_BUILD_CORE define"
#endif

PyAPI_DATA(PyTypeObject) PyAnnotate_Type;
#define PyAnnotate_CheckExact(op) Py_IS_TYPE((op), &PyAnnotate_Type)

// XXX: TODO: DESLOP
// An __annotate__ function, or an evaluate function for a type alias value or
// a type parameter bound or default. Everything one of these needs in order to
// serve the PEP 649 protocol, without a code object: the compiler emits the
// static part as a single constant and the closure cells alongside it. See
// codegen_annotate_func() in Python/codegen.c.
#define ANNOTATE_EVALUATE  0x1  // an evaluate function, not an __annotate__
#define ANNOTATE_FUTURE    0x2  // "from __future__ import annotations" (PEP 563)

typedef struct {
    PyObject_HEAD
    PyObject *ann_qualname;   // str; __name__ is its last dotted component
    PyObject *ann_freevars;   // tuple of str, parallel to ann_closure
    PyObject *ann_closure;    // tuple of cells, or NULL
    PyObject *ann_globals;    // dict of the defining frame
    // XXX: TODO: DESLOP
    PyObject *ann_strings;    // the annotation source strings: a dict for an
                              // __annotate__, a single str for an evaluate
                              // function. Set right after construction.
    PyObject *ann_metadata;   // tuple for annotationlib._AnnotateMetadata, or NULL
    int ann_flags;
} PyAnnotateObject;

// Steals nothing; closure may be NULL.
extern PyObject *_PyAnnotate_New(PyObject *payload, PyObject *closure,
                                 PyObject *globals);
extern int _PyAnnotate_SetStrings(PyObject *annotate, PyObject *strings);

#ifdef __cplusplus
}
#endif
#endif // !Py_INTERNAL_ANNOTATEOBJECT_H
