#include "datatable.h"
#include "py_datatable.h"
#include "py_datawindow.h"
#include "py_rowindex.h"
#include "dtutils.h"

// Forward declarations
void dt_DataTable_dealloc_objcol(void *data, int64_t nrows);

static PyObject *strRowIndexTypeArray, *strRowIndexTypeSlice;


void init_py_datatable() {
    py_string_coltypes = malloc(sizeof(PyObject*) * DT_COUNT);
    py_string_coltypes[DT_AUTO]   = PyUnicode_FromString("auto");
    py_string_coltypes[DT_DOUBLE] = PyUnicode_FromString("real");
    py_string_coltypes[DT_LONG]   = PyUnicode_FromString("int");
    py_string_coltypes[DT_BOOL]   = PyUnicode_FromString("bool");
    py_string_coltypes[DT_STRING] = PyUnicode_FromString("str");
    py_string_coltypes[DT_OBJECT] = PyUnicode_FromString("obj");
    strRowIndexTypeArray = PyUnicode_FromString("array");
    strRowIndexTypeSlice = PyUnicode_FromString("slice");
}


/**
 * "Main" function that drives transformation of datatables.
 *
 * :param rows:
 *     A row selector (a `RowIndex_PyObject` object). This cannot be None --
 *     instead supply row index spanning all rows in the datatable.
 *
 * ... more to be added ...
 */
static DataTable_PyObject*
__call__(DataTable_PyObject *self, PyObject *args, PyObject *kwds)
{
    RowIndex_PyObject *rows = NULL;
    DataTable *dtres = NULL;
    DataTable_PyObject *pyres = NULL;

    static char *kwlist[] = {"rows", NULL};
    int ret = PyArg_ParseTupleAndKeywords(args, kwds, "O!:DataTable.__call__",
                                          kwlist, &RowIndex_PyType, &rows);
    if (!ret || rows->ref == NULL) return NULL;

    dtres = dt_DataTable_call(self->ref, rows->ref);
    if (dtres == NULL) goto fail;
    rows->ref = NULL; // The reference ownership is transferred to `dtres`

    pyres = DataTable_PyNew();
    if (pyres == NULL) goto fail;
    pyres->ref = dtres;
    if (dtres->source == NULL)
        pyres->source = NULL;
    else {
        if (dtres->source == self->ref)
            pyres->source = self;
        else if (dtres->source == self->ref->source)
            pyres->source = self->source;
        else {
            PyErr_SetString(PyExc_RuntimeError, "Unknown source dataframe");
            goto fail;
        }
        Py_XINCREF(pyres->source);
    }

    return pyres;

  fail:
    dt_DataTable_dealloc(dtres, &dt_DataTable_dealloc_objcol);
    Py_XDECREF(pyres);
    return NULL;
}


static PyObject* get_nrows(DataTable_PyObject *self) {
    return PyLong_FromLong(self->ref->nrows);
}

static PyObject* get_ncols(DataTable_PyObject *self) {
    return PyLong_FromLong(self->ref->ncols);
}

static PyObject* get_types(DataTable_PyObject *self) {
    int64_t i = self->ref->ncols;
    PyObject *list = PyTuple_New((Py_ssize_t) i);
    if (list == NULL) return NULL;
    while (--i >= 0) {
        ColType ct = self->ref->columns[i].type;
        PyTuple_SET_ITEM(list, i, py_string_coltypes[ct]);
        Py_INCREF(py_string_coltypes[ct]);
    }
    return list;
}

static PyObject* get_rowindex_type(DataTable_PyObject *self) {
    if (self->ref->rowindex == NULL)
        return none();
    RowIndexType rit = self->ref->rowindex->type;
    return rit == RI_SLICE? incref(strRowIndexTypeSlice) :
           rit == RI_ARRAY? incref(strRowIndexTypeArray) : none();
}


static DataWindow_PyObject* window(DataTable_PyObject *self, PyObject *args)
{
    int64_t row0, row1, col0, col1;
    if (!PyArg_ParseTuple(args, "llll", &row0, &row1, &col0, &col1))
        return NULL;

    PyObject *nargs = Py_BuildValue("Ollll", self, row0, row1, col0, col1);
    PyObject *res = PyObject_CallObject((PyObject*) &DataWindow_PyType, nargs);
    Py_XDECREF(nargs);

    return (DataWindow_PyObject*) res;
}



/**
 * Deallocator function, called when the object is being garbage-collected.
 */
static void __dealloc__(DataTable_PyObject *self)
{
    dt_DataTable_dealloc(self->ref, &dt_DataTable_dealloc_objcol);
    Py_XDECREF(self->source);
    Py_TYPE(self)->tp_free((PyObject*)self);
}

void dt_DataTable_dealloc_objcol(void *data, int64_t nrows) {
    PyObject **coldata = data;
    int64_t j = nrows;
    while (--j >= 0)
        Py_XDECREF(coldata[j]);
}


static PyObject* test(DataTable_PyObject *self, PyObject *args)
{
    void *ptr;
    if (!PyArg_ParseTuple(args, "l", &ptr))
        return NULL;

    DataTable *dt = self->ref;
    int64_t *buf = calloc(sizeof(int64_t), dt->nrows);

    int64_t (*func)(DataTable*, int64_t*) = ptr;
    int64_t res = func(dt, buf);

    PyObject *list = PyList_New(res);
    for (int64_t i = 0; i < res; i++) {
        PyList_SET_ITEM(list, i, PyLong_FromLong(buf[i]));
    }
    free(buf);
    return list;
}


//======================================================================================================================
// DataTable type definition
//======================================================================================================================

PyDoc_STRVAR(dtdoc_window, "Retrieve datatable's data within a window");
PyDoc_STRVAR(dtdoc_nrows, "Number of rows in the datatable");
PyDoc_STRVAR(dtdoc_ncols, "Number of columns in the datatable");
PyDoc_STRVAR(dtdoc_types, "List of column types");
PyDoc_STRVAR(dtdoc_rowindex_type, "Type of the row numbers: 'slice' or 'array'");
PyDoc_STRVAR(dtdoc_test, "");

#define METHOD1(name) {#name, (PyCFunction)name, METH_VARARGS, dtdoc_##name}

static PyMethodDef datatable_methods[] = {
    METHOD1(window),
    METHOD1(test),
    {NULL, NULL}           /* sentinel */
};

#define GETSET1(name) {#name, (getter)get_##name, NULL, dtdoc_##name, NULL}

static PyGetSetDef datatable_getseters[] = {
    GETSET1(nrows),
    GETSET1(ncols),
    GETSET1(types),
    GETSET1(rowindex_type),
    {NULL}  /* sentinel */
};

PyTypeObject DataTable_PyType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "_datatable.DataTable",             /* tp_name */
    sizeof(DataTable_PyObject),         /* tp_basicsize */
    0,                                  /* tp_itemsize */
    (destructor)__dealloc__,            /* tp_dealloc */
    0,                                  /* tp_print */
    0,                                  /* tp_getattr */
    0,                                  /* tp_setattr */
    0,                                  /* tp_compare */
    0,                                  /* tp_repr */
    0,                                  /* tp_as_number */
    0,                                  /* tp_as_sequence */
    0,                                  /* tp_as_mapping */
    0,                                  /* tp_hash  */
    (ternaryfunc)__call__,              /* tp_call */
    0,                                  /* tp_str */
    0,                                  /* tp_getattro */
    0,                                  /* tp_setattro */
    0,                                  /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,                 /* tp_flags */
    "DataTable object",                 /* tp_doc */
    0,                                  /* tp_traverse */
    0,                                  /* tp_clear */
    0,                                  /* tp_richcompare */
    0,                                  /* tp_weaklistoffset */
    0,                                  /* tp_iter */
    0,                                  /* tp_iternext */
    datatable_methods,                  /* tp_methods */
    0,                                  /* tp_members */
    datatable_getseters,                /* tp_getset */
    0,                                  /* tp_base */
    0,                                  /* tp_dict */
    0,                                  /* tp_descr_get */
    0,                                  /* tp_descr_set */
    0,                                  /* tp_dictoffset */
    0,                                  /* tp_init */
    0,                                  /* tp_alloc */
    0,                                  /* tp_new */
    0,                                  /* tp_free */
    0,                                  /* tp_is_gc */
    0,                                  /* tp_bases */
    0,                                  /* tp_mro */
    0,                                  /* tp_cache */
    0,                                  /* tp_subclasses */
    0,                                  /* tp_weaklist */
    0,                                  /* tp_del */
    0,                                  /* tp_version_tag */
};