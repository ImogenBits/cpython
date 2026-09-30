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

typedef struct {
    PyObject_HEAD
    PyObject *ann_qualname;     // str; __name__ is its last dotted component
    PyObject *ann_freevars;     // tuple of str, parallel to ann_closure
    PyObject *ann_closure;      // tuple of cells, or NULL
    PyObject *ann_globals;      // dict of the defining frame
    PyObject *ann_asts;         // the annotation AST data: a dict for an __annotate__,
                                // a single str for an evaluate function.
    PyObject *ann_explicit_globals; // names that aren't looked up in the containing
                                // class, but always the global namespce.
    PyObject *ann_private_name;     // the private name of the containing class.
    PyObject *ann_mangled_names;    // frozenset of names that should be mangled.
    int ann_flags;
} PyAnnotateObject;

// Steals nothing
extern PyObject *_PyAnnotate_New(PyObject *qualname, PyObject *asts, PyObject *globals, PyObject *data);

#ifdef __cplusplus
}
#endif
#endif // !Py_INTERNAL_ANNOTATEOBJECT_H
