/*
    Authors:
        Jakub Hrozek <jhrozek@redhat.com>

    Copyright (C) 2009 Red Hat

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "config.h"

#include <Python.h>
#include <structmember.h>
#include <talloc.h>
#include <pwd.h>
#include <grp.h>

#include "util/util.h"
#include "util/sss_python.h"
#include "db/sysdb.h"
#include "tools/tools_util.h"
#include "tools/sss_sync_ops.h"
#include "util/crypto/sss_crypto.h"


static void PyErr_SetSssErrorWithMessage(int ret, const char *message)
{
    PyObject *exc = Py_BuildValue(discard_const_p(char, "(is)"),
                                  ret, message);

    PyErr_SetObject(PyExc_IOError, exc);
    Py_XDECREF(exc);
}

static void PyErr_SetSssError(int ret)
{
    PyErr_SetSssErrorWithMessage(ret, strerror(ret));
}

/* ==================== obfuscation python wrappers ========================*/

/*
 * The sss.password object
 */
typedef struct {
    PyObject_HEAD

    int aes_256;
} PySssPasswordObject;

PyDoc_STRVAR(py_sss_encrypt__doc__,
"Obfuscate a password\n\n"
":param password: The password to obfuscate\n\n"
":param method: The obfuscation method\n\n");

static PyObject *py_sss_encrypt(PySssPasswordObject *self,
                                PyObject *args)
{
    char *password = NULL;
    int plen; /* may contain NULL bytes */
    char *obfpwd = NULL;
    TALLOC_CTX *tctx = NULL;
    int ret;
    int mode;
    PyObject *retval = NULL;

    /* parse arguments */
    if (!PyArg_ParseTuple(args, discard_const_p(char, "s#i"),
                          &password, &plen, &mode)) {
        return NULL;
    }

    tctx = talloc_new(NULL);
    if (!tctx) {
        PyErr_NoMemory();
        return NULL;
    }

    ret = sss_password_encrypt(tctx, password, plen+1,
                               mode, &obfpwd);
    if (ret != EOK) {
        PyErr_SetSssError(ret);
        goto fail;
    }

    retval = Py_BuildValue(sss_py_const_p(char, "s"), obfpwd);
    if (retval == NULL) {
        goto fail;
    }

fail:
    talloc_zfree(tctx);
    return retval;
}

#if 0
PyDoc_STRVAR(py_sss_decrypt__doc__,
"Deobfuscate a password\n\n"
":param obfpwd: The password to convert back to clear text\n\n");

static PyObject *py_sss_decrypt(PySssPasswordObject *self,
                                PyObject *args,
                                PyObject *kwds)
{
    char *password = NULL;
    char *obfpwd = NULL;
    TALLOC_CTX *tctx = NULL;
    int ret;
    PyObject *retval = NULL;

    /* parse arguments */
    if (!PyArg_ParseTuple(args, discard_const_p(char, "s"),
                          &obfpwd)) {
        return NULL;
    }

    tctx = talloc_new(NULL);
    if (!tctx) {
        PyErr_NoMemory();
        return NULL;
    }

    ret = sss_password_decrypt(tctx, obfpwd, &password);
    if (ret != EOK) {
        PyErr_SetSssError(ret);
        goto fail;
    }

    retval = Py_BuildValue("s", password);
    if (retval == NULL) {
        goto fail;
    }

fail:
    talloc_zfree(tctx);
    return retval;
}
#endif

/*
 * The sss.password destructor
 */
static void PySssPasswordObject_dealloc(PySssPasswordObject *self)
{
    Py_TYPE(self)->tp_free((PyObject*) self);
}

/*
 * The sss.password constructor
 */
static PyObject *PySssPasswordObject_new(PyTypeObject *type,
                                         PyObject *args,
                                         PyObject *kwds)
{
    PySssPasswordObject *self;

    self = (PySssPasswordObject *) type->tp_alloc(type, 0);
    if (self == NULL) {
        PyErr_NoMemory();
        return NULL;
    }

    self->aes_256 = AES_256;

    return (PyObject *) self;
}

/*
 * sss.password object methods
 */
static PyMethodDef sss_password_methods[] = {
    { sss_py_const_p(char, "encrypt"), (PyCFunction) py_sss_encrypt,
      METH_VARARGS | METH_STATIC, py_sss_encrypt__doc__
    },
#if 0
    { "decrypt", (PyCFunction) py_sss_decrypt,
      METH_VARARGS | METH_STATIC, py_sss_decrypt__doc__
    },
#endif
    {NULL, NULL, 0, NULL}        /* Sentinel */
};

/*
 * sss.password object members
 */
static PyMemberDef sss_password_members[] = {
    { discard_const_p(char, "AES_256"), T_INT,
      offsetof(PySssPasswordObject, aes_256), READONLY, NULL},
    {NULL, 0, 0, 0, NULL} /* Sentinel */
};

/*
 * sss.password object properties
 */
static PyTypeObject pysss_password_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = sss_py_const_p(char, "sss.password"),
    .tp_basicsize = sizeof(PySssPasswordObject),
    .tp_new = PySssPasswordObject_new,
    .tp_dealloc = (destructor) PySssPasswordObject_dealloc,
    .tp_methods = sss_password_methods,
    .tp_members = sss_password_members,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_doc   = sss_py_const_p(char, "SSS password obfuscation"),
};

/* ==================== the sss module initialization =======================*/

/*
 * Module methods
 */
static PyMethodDef module_methods[] = {
        {NULL, NULL, 0, NULL}  /* Sentinel */
};

/*
 * Module initialization
 */
#ifdef IS_PY3K
static struct PyModuleDef pysssdef = {
    PyModuleDef_HEAD_INIT,
    "pysss",
    NULL,
    -1,
    module_methods,
    NULL,
    NULL,
    NULL,
    NULL
};

PyMODINIT_FUNC
PyInit_pysss(void)
#else
PyMODINIT_FUNC
initpysss(void)
#endif
{
    PyObject *m;

    if (PyType_Ready(&pysss_password_type) < 0)
        MODINITERROR;

#ifdef IS_PY3K
    m = PyModule_Create(&pysssdef);
#else
    m = Py_InitModule(discard_const_p(char, "pysss"), module_methods);
#endif
    if (m == NULL)
        MODINITERROR;

    Py_INCREF(&pysss_password_type);
    PyModule_AddObject(m, discard_const_p(char, "password"), (PyObject *)&pysss_password_type);

#ifdef IS_PY3K
    return m;
#endif
}

