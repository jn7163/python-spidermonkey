#include "spidermonkey.h"

/*
    This is a fairly unsafe operation in so much as
    I'm relying on JavaScript to never call one of
    our callbacks on an object we didn't create.

    Also, of note, we're not incref'ing the Python
    object.
*/
PyObject*
get_py_obj(JSContext* cx, JSObject* obj)
{
    jsval priv;

    if(!JS_GetReservedSlot(cx, obj, 0, &priv))
    {
        PyErr_SetString(PyExc_RuntimeError, "Failed to get slot data.");
        return NULL;
    }

    return (PyObject*) JSVAL_TO_PRIVATE(priv);
}

JSBool
js_add_prop(JSContext* jscx, JSObject* jsobj, jsval key, jsval* val)
{
    return JS_TRUE;
}

JSBool
js_del_prop(JSContext* jscx, JSObject* jsobj, jsval key, jsval* val)
{
    Context* pycx = NULL;
    PyObject* pyobj = NULL;
    PyObject* pykey = NULL;
    JSBool ret = JS_FALSE;
    
    pycx = (Context*) JS_GetContextPrivate(jscx);
    if(pycx == NULL)
    {
        PyErr_SetString(PyExc_RuntimeError, "Failed to get JS Context.");
        goto error;
    }
    
    pyobj = get_py_obj(jscx, jsobj);
    if(pyobj == NULL) goto error;
    
    pykey = js2py(pycx, key);
    if(pykey == NULL) goto error;

    if(PyObject_DelItem(pyobj, pykey) < 0)
    {
        PyErr_Clear();
        if(PyObject_DelAttr(pyobj, pykey) < 0)
        {
            PyErr_Clear();
            *val = JSVAL_FALSE;
        }
    }
   
    ret = JS_TRUE;
    goto success;
    
error:
success:
    Py_XDECREF(pykey);
    return ret;
}

JSBool
js_get_prop(JSContext* jscx, JSObject* jsobj, jsval key, jsval* val)
{
    Context* pycx = NULL;
    PyObject* pyobj = NULL;
    PyObject* pykey = NULL;
    PyObject* pyval = NULL;
    JSBool ret = JS_FALSE;
   
    pycx = (Context*) JS_GetContextPrivate(jscx);
    if(pycx == NULL)
    {
        PyErr_SetString(PyExc_RuntimeError, "Failed to get JS Context.");
        goto error;
    }
    
    pyobj = get_py_obj(jscx, jsobj);
    if(pyobj == NULL) goto error;
    
    pykey = js2py(pycx, key);
    if(pykey == NULL) goto error;

    pyval = PyObject_GetItem(pyobj, pykey);
    if(pyval == NULL)
    {
        PyErr_Clear();
        pyval = PyObject_GetAttr(pyobj, pykey);
        if(pyval == NULL)
        {
            PyErr_Clear();
            ret = JS_TRUE;
            *val = JSVAL_VOID;
            goto success;
        }
    }

    *val = py2js(pycx, pyval);
    if(*val == JSVAL_VOID) goto error;
    ret = JS_TRUE;
    goto success;

error:
success:
    Py_XDECREF(pykey);
    Py_XDECREF(pyval);
    
    return ret;
}

JSBool
js_set_prop(JSContext* jscx, JSObject* jsobj, jsval key, jsval* val)
{
    Context* pycx = NULL;
    PyObject* pyobj = NULL;
    PyObject* pykey = NULL;
    PyObject* pyval = NULL;
    JSBool ret = JS_FALSE;
    
    pycx = (Context*) JS_GetContextPrivate(jscx);
    if(pycx == NULL)
    {
        JS_ReportError(jscx, "Failed to find a Python Context.");
        goto error;
    }
    
    pyobj = get_py_obj(jscx, jsobj);
    if(pyobj == NULL)
    {
        JS_ReportError(jscx, "Failed to find a Python object.");
        goto error;
    }
    
    pykey = js2py(pycx, key);
    if(pykey == NULL)
    {
        JS_ReportError(jscx, "Failed to convert key to Python.");
        goto error;
    }

    pyval = js2py(pycx, *val);
    if(pyval == NULL)
    {
        JS_ReportError(jscx, "Failed to convert value to Python.");
        goto error;
    }

    if(PyObject_SetItem(pyobj, pykey, pyval) < 0)
    {
        PyErr_Clear();
        if(PyObject_SetAttr(pyobj, pykey, pyval) < 0) goto error;
    }

    ret = JS_TRUE;
    goto success;
    
error:
success:
    Py_XDECREF(pykey);
    Py_XDECREF(pyval);
    return ret;
}

// This is currently broken. Will come back to it.
JSBool
js_enumerate(JSContext* jscx, JSObject* jsobj,
                            JSIterateOp op, jsval* st, jsid* id)
{
    Context* pycx = NULL;
    PyObject* pyobj = NULL;
    PyObject* keys = NULL;
    jsval next = JSVAL_VOID;

    pycx = (Context*) JS_GetContextPrivate(jscx);
    if(pycx == NULL)
    {
        fprintf(stderr, "no context\n");
        JS_ReportError(jscx, "Failed to get Python context.");
        return JS_FALSE;
    }

    if(op == JSENUMERATE_INIT)
    {
        pyobj = get_py_obj(jscx, jsobj);
        if(PyMapping_Check(pyobj))
        {
            keys = PyMapping_Keys(pyobj);
        }
        else
        {
            keys = PyObject_Dir(pyobj);
        }

        if(keys == NULL)
        {
            fprintf(stderr, "no keys\n");
            JS_ReportError(jscx, "Failed to get iterable items.");
            return JS_FALSE;
        }

        *st = PRIVATE_TO_JSVAL(keys);
        *id = INT_TO_JSVAL(PySequence_Length(keys));
        return JS_TRUE;
    }
    else if(op == JSENUMERATE_NEXT)
    {
        keys = JSVAL_TO_PRIVATE(*st);
        if(PySequence_Length(keys) < 1)
        {
            Py_DECREF(keys);
            *st = JSVAL_NULL;
            return JS_TRUE;
        }

        pyobj = PySequence_GetItem(keys, 0);
        if(pyobj == NULL)
        {
            fprintf(stderr, "no next\n");
            JS_ReportError(jscx, "Failed to get next key.");
            PyErr_Clear();
            return JS_FALSE;
        }
        
        next = py2js(pycx, pyobj);
        if(next == JSVAL_VOID)
        {
            fprintf(stderr, "no py2js\n");
            JS_ReportError(jscx, "Failed to convert value to JS.");
            Py_DECREF(pyobj);
            PyErr_Clear();
            return JS_FALSE;
        }

        Py_DECREF(pyobj);
        
        if(PySequence_DelItem(keys, 0) < 0)
        {
            fprintf(stderr, "no delitem\n");
            JS_ReportError(jscx, "Failed to remove key from list.");
            PyErr_Clear();
            return JS_FALSE;
        }

        if(!JS_ValueToId(jscx, next, id))
        {
            fprintf(stderr, "no val to id\n");
            JS_ReportError(jscx, "Failed to convert jsval to a jsid.");
            return JS_FALSE;
        }

        return JS_TRUE;
    }
    else if(op == JSENUMERATE_DESTROY)
    {
        keys = (PyObject*) JSVAL_TO_PRIVATE(*st);
        Py_DECREF(keys);
        return JS_TRUE;
    }

    fprintf(stderr, "no op\n");
    JS_ReportError(jscx, "Unkown enumerate operation.");
    return JS_FALSE;
}

// I am not at all sure what I should have here.
JSBool
js_resolve(JSContext* cx, JSObject* obj, jsval key)
{
    return JS_TRUE;
}

void
js_finalize(JSContext* jscx, JSObject* jsobj)
{
    Context* pycx = (Context*) JS_GetContextPrivate(jscx);
    PyObject* pyobj = NULL;
    
    
    if(pycx == NULL)
    {
        // Not much else we can do but yell.
        fprintf(stderr, "*** NO PYTHON CONTEXT ***\n");
        return;
    }

    pyobj = get_py_obj(jscx, jsobj);
    Py_DECREF(pyobj);

    // Technically, this could turn out to be nasty. If
    // this is the last object keeping the python cx
    // alive, then this call could be deleting the cx
    // we're about to return to.
    Py_DECREF(pycx);
}

PyObject*
mk_args_tuple(Context* pycx, JSContext* jscx, uintN argc, jsval* argv)
{
    PyObject* tpl = NULL;
    PyObject* tmp = NULL;
    int idx;
    
    tpl = PyTuple_New(argc);
    if(tpl == NULL)
    {
        JS_ReportError(jscx, "Failed to build args value.");
        goto error;
    }
    
    for(idx = 0; idx < argc; idx++)
    {
        tmp = js2py(pycx, argv[idx]);
        if(tmp == NULL) goto error;
        if(PyTuple_SetItem(tpl, idx, tmp) != 0)
        {
            Py_DECREF(tmp);
            goto error;
        }
    }

    goto success;

error:
    Py_XDECREF(tpl);
success:
    return tpl;
}

JSBool
js_call(JSContext* jscx, JSObject* jsobj, uintN argc, jsval* argv, jsval* rval)
{
    Context* pycx = NULL;
    PyObject* pyobj = NULL;
    PyObject* tpl = NULL;
    PyObject* ret = NULL;
    JSBool jsret = JS_FALSE;
    
    pycx = (Context*) JS_GetContextPrivate(jscx);
    if(pycx == NULL)
    {
        JS_ReportError(jscx, "Failed to get Python context.");
        goto error;
    }
    
    pyobj = get_py_obj(jscx, JSVAL_TO_OBJECT(argv[-2]));
    
    if(!PyCallable_Check(pyobj))
    {
        JS_ReportError(jscx, "Object is not callable.");
        goto error;
    }

    tpl = mk_args_tuple(pycx, jscx, argc, argv);
    if(tpl == NULL) goto error;
    
    ret = PyObject_Call(pyobj, tpl, NULL);
    if(ret == NULL)
    {
        JS_ReportError(jscx, "Failed to call object.");
        goto error;
    }
    
    *rval = py2js(pycx, ret);
    if(*rval == JSVAL_VOID)
    {
        JS_ReportError(jscx, "Failed to convert Python return value.");
        goto error;
    }

    jsret = JS_TRUE;
    goto success;

error:
success:
    Py_XDECREF(tpl);
    Py_XDECREF(ret);
    return jsret;
}

JSBool
js_ctor(JSContext* jscx, JSObject* jsobj, uintN argc, jsval* argv, jsval* rval)
{
    Context* pycx = NULL;
    PyObject* pyobj = NULL;
    PyObject* tpl = NULL;
    PyObject* ret = NULL;
    JSBool jsret = JS_FALSE;
    
    pycx = (Context*) JS_GetContextPrivate(jscx);
    if(pycx == NULL)
    {
        JS_ReportError(jscx, "Failed to get Python context.");
        goto error;
    }
    
    pyobj = get_py_obj(jscx, JSVAL_TO_OBJECT(argv[-2]));
    
    if(!PyCallable_Check(pyobj))
    {
        JS_ReportError(jscx, "Object is not callable.");
        goto error;
    }

    if(!PyType_Check(pyobj))
    {
        PyErr_SetString(PyExc_TypeError, "Object is not a Type object.");
        goto error;
    }

    tpl = mk_args_tuple(pycx, jscx, argc, argv);
    if(tpl == NULL) goto error;
    
    ret = PyObject_CallObject(pyobj, tpl);
    if(ret == NULL)
    {
        JS_ReportError(jscx, "Failed to construct object.");
        goto error;
    }
    Py_INCREF(ret);
    
    *rval = py2js(pycx, ret);
    if(*rval == JSVAL_VOID)
    {
        JS_ReportError(jscx, "Failed to convert Python return value.");
        goto error;
    }

    jsret = JS_TRUE;
    goto success;

error:
success:
    Py_XDECREF(tpl);
    Py_XDECREF(ret);
    return jsret;
}

JSClass*
create_class(Context* cx, PyTypeObject* type)
{
    PyObject* curr = NULL;
    JSClass* jsclass = NULL;
    JSClass* ret = NULL;
    char* classname = NULL;
    
    curr = Context_get_class(cx, type->tp_name);
    if(curr != NULL) return (JSClass*) HashCObj_AsVoidPtr(curr);

    jsclass = (JSClass*) malloc(sizeof(JSClass));
    if(jsclass == NULL)
    {
        PyErr_NoMemory();
        goto error;
    }
   
    classname = (char*) malloc(strlen(type->tp_name)*sizeof(char));
    if(classname == NULL)
    {
        PyErr_NoMemory();
        goto error;
    }
    
    strcpy((char*) classname, type->tp_name);
    jsclass->name = classname;
    
    jsclass->flags = JSCLASS_HAS_RESERVED_SLOTS(1) | JSCLASS_NEW_ENUMERATE;
    jsclass->addProperty = js_add_prop;
    jsclass->delProperty = js_del_prop;
    jsclass->getProperty = js_get_prop;
    jsclass->setProperty = js_set_prop;
    jsclass->enumerate = (JSEnumerateOp) js_enumerate;
    jsclass->resolve = js_resolve;
    jsclass->convert = JS_ConvertStub;
    jsclass->finalize = js_finalize;
    jsclass->getObjectOps = NULL;
    jsclass->checkAccess = NULL;
    jsclass->call = js_call;
    //jsclass->construct = NULL;
    jsclass->construct = js_ctor;
    jsclass->xdrObject = NULL;
    jsclass->hasInstance = NULL;
    jsclass->mark = NULL;
    jsclass->reserveSlots = NULL;
    
    curr = HashCObj_FromVoidPtr(jsclass);
    if(curr == NULL) goto error;
    if(Context_add_class(cx, type->tp_name, curr) < 0) goto error;

    ret = jsclass;
    goto success;

error:
    if(jsclass != NULL) free(jsclass);
    if(classname != NULL) free(classname);
success:
    return ret;
}

jsval
py2js_object(Context* cx, PyObject* pyobj)
{
    PyObject* hashable = NULL;
    PyObject* attached = NULL;
    JSClass* klass = NULL;
    JSObject* jsobj = NULL;
    jsval pyval;
    jsval ret = JSVAL_VOID;
   
    klass = create_class(cx, pyobj->ob_type);
    if(klass == NULL) goto error;

    jsobj = JS_NewObject(cx->cx, klass, NULL, NULL);
    if(jsobj == NULL)
    {
        PyErr_SetString(PyExc_RuntimeError, "Failed to create JS object.");
        goto error;
    }
    
    attached = pyobj;
    Py_INCREF(attached);
    pyval = PRIVATE_TO_JSVAL(attached);
    if(!JS_SetReservedSlot(cx->cx, jsobj, 0, pyval))
    {
        PyErr_SetString(PyExc_RuntimeError, "Failed to store ref'ed object.");
        goto error;
    }

    hashable = HashCObj_FromVoidPtr(attached);
    if(hashable == NULL)
    {
        PyErr_SetString(PyExc_RuntimeError, "Failed to make hashable pointer.");
        goto error;
    }

    if(Context_add_object(cx, hashable) < 0)
    {
        PyErr_SetString(PyExc_RuntimeError, "Failed to store reference.");
        goto error;
    }

    /*
        As noted in Context_new, here we must ref the Python context
        to make sure it stays alive while a Python object may be
        referenced in the JS VM.
    */
    Py_INCREF(cx);
    ret = OBJECT_TO_JSVAL(jsobj);
    goto success;

error:
success:
    Py_XDECREF(attached);
    return ret;
}


