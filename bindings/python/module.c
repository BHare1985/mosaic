/* Python extension over the Mosaic C ABI.
 *
 * This file contains no Mosaic internals: it calls mosaic_c.h and nothing else, so the binding
 * cannot drift from the contract the C API promises. Ergonomic wrapping (iterators, context
 * managers) lives in mosaic/__init__.py.
 */

#define PY_SSIZE_T_CLEAN
#include <mosaic/mosaic_c.h>

#include <Python.h>

typedef struct {
    PyObject_HEAD mosaic_keys* keys;
    mosaic_store* store;
    mosaic_transport* transport;
    mosaic_node* node;
    /* Records handed to the callback are borrowed, so each one is cloned into this queue for the
     * caller. */
    mosaic_record_owned** queue;
    Py_ssize_t queued;
    Py_ssize_t capacity;
    int closed;
} NodeObject;

static void queue_clear(NodeObject* self) {
    for (Py_ssize_t i = 0; i < self->queued; ++i)
        mosaic_record_owned_free(&self->queue[i]);
    self->queued = 0;
}

static void on_record(void* user, const mosaic_record* record) {
    NodeObject* self = (NodeObject*)user;
    if (self->queued == self->capacity) {
        const Py_ssize_t grown = self->capacity == 0 ? 64 : self->capacity * 2;
        mosaic_record_owned** next = PyMem_Realloc(self->queue, (size_t)grown * sizeof(*next));
        if (next == NULL)
            return; /* out of memory inside a callback: drop rather than abort the C caller */
        self->queue = next;
        self->capacity = grown;
    }
    mosaic_record_owned* copy = NULL;
    if (mosaic_record_clone(record, &copy) == MOSAIC_OK)
        self->queue[self->queued++] = copy;
}

static PyObject* status_error(mosaic_status status) {
    const char* detail = mosaic_last_error();
    PyErr_Format(PyExc_RuntimeError, "mosaic error %d: %s", (int)status,
                 detail != NULL ? detail : "");
    return NULL;
}

static void Node_dealloc(NodeObject* self) {
    queue_clear(self);
    PyMem_Free(self->queue);
    self->queue = NULL;
    mosaic_node_free(&self->node);
    mosaic_transport_free(&self->transport);
    mosaic_store_free(&self->store);
    mosaic_keys_free(&self->keys);
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static int Node_init(NodeObject* self, PyObject* args, PyObject* kwargs) {
    /* Order matters twice: it is the keyword table and the positional format below. */
    static char* keywords[] = {"node_id",     "keys_path", "store_path", "pub_bind",
                               "router_bind", "peers",     "plaintext",  NULL};
    const char* node_id = NULL;
    const char* pub_bind = "tcp://127.0.0.1:*";
    const char* router_bind = "tcp://127.0.0.1:*";
    const char* keys_path = NULL;
    const char* store_path = NULL;
    PyObject* peers = NULL;
    int plaintext = 0; /* links are encrypted unless the caller says otherwise */

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "sss|ssOp", keywords, &node_id, &keys_path,
                                     &store_path, &pub_bind, &router_bind, &peers, &plaintext))
        return -1;

    self->node = NULL;
    self->keys = NULL;
    self->store = NULL;
    self->transport = NULL;
    self->queue = NULL;
    self->queued = 0;
    self->capacity = 0;
    self->closed = 0;

    mosaic_node_config config;
    PY_LONG_LONG zero = 0;
    (void)zero;
    config.id = node_id;
    config.head_interval_ms = 200;
    config.fetch_batch = 0;
    config.fetch_window = 0;
    config.fetch_timeout_ms = 0;
    config.max_serve = 0;
    config.min_replicas = 0;
    config.max_record_bytes = 0;

    if (mosaic_keys_load(keys_path, &self->keys) != MOSAIC_OK) {
        status_error(MOSAIC_ERR_CRYPTO);
        return -1;
    }
    if (mosaic_store_open(store_path, 1, &self->store) != MOSAIC_OK) {
        status_error(MOSAIC_ERR_IO);
        return -1;
    }
    /* The keystore is already loaded for signing; its curve25519 pair is what encrypts the links,
     * so there is no reason for a Python node to run plaintext. Plaintext is opt-in and named. */
    {
        char* curve_public = NULL;
        char* curve_secret = NULL;
        mosaic_status status;
        if (plaintext) {
            status = mosaic_transport_new_plaintext(node_id, pub_bind, router_bind, NULL, 0,
                                                    &self->transport);
        } else {
            status = mosaic_keys_curve(self->keys, &curve_public, &curve_secret);
            if (status == MOSAIC_OK)
                status = mosaic_transport_new(node_id, pub_bind, router_bind, NULL, 0, curve_public,
                                              curve_secret, &self->transport);
            mosaic_free_string(&curve_public);
            mosaic_free_string(&curve_secret);
        }
        if (status != MOSAIC_OK) {
            status_error(status);
            return -1;
        }
    }
    if (peers != NULL && PyList_Check(peers)) {
        const Py_ssize_t count = PyList_Size(peers);
        for (Py_ssize_t i = 0; i < count; ++i) {
            PyObject* item = PyList_GetItem(peers, i); /* borrowed */
            if (!PyDict_Check(item))
                continue;
            mosaic_peer peer;
            PyObject* id = PyDict_GetItemString(item, "id");
            PyObject* pub = PyDict_GetItemString(item, "pub");
            PyObject* router = PyDict_GetItemString(item, "router");
            peer.id = id != NULL ? PyUnicode_AsUTF8(id) : NULL;
            peer.pub_endpoint = pub != NULL ? PyUnicode_AsUTF8(pub) : NULL;
            peer.router_endpoint = router != NULL ? PyUnicode_AsUTF8(router) : NULL;
            /* The peer's curve public key (z85), from `mosaic_keys_curve`. Without it a CURVE
             * client cannot authenticate the server, so the link is not encrypted however it was
             * asked for. */
            PyObject* curve = PyDict_GetItemString(item, "curve");
            peer.curve_public = curve != NULL ? PyUnicode_AsUTF8(curve) : NULL;
            if (mosaic_transport_add_peer(self->transport, &peer) != MOSAIC_OK) {
                status_error(MOSAIC_ERR_INVALID_ARGUMENT);
                return -1;
            }
        }
    }
    if (mosaic_node_new(&config, self->transport, self->store, self->keys, &self->node) !=
        MOSAIC_OK) {
        status_error(MOSAIC_ERR_INVALID_ARGUMENT);
        return -1;
    }
    if (mosaic_node_on_record(self->node, on_record, self) != MOSAIC_OK) {
        status_error(MOSAIC_ERR_INVALID_ARGUMENT);
        return -1;
    }
    return 0;
}

static PyObject* Node_follow(NodeObject* self, PyObject* args) {
    const char* pattern = NULL;
    if (!PyArg_ParseTuple(args, "s", &pattern))
        return NULL;
    const mosaic_status status = mosaic_node_follow(self->node, pattern);
    return status == MOSAIC_OK ? Py_NewRef(Py_None) : status_error(status);
}

static PyObject* Node_publish(NodeObject* self, PyObject* args) {
    const char* channel = NULL;
    Py_buffer payload;
    unsigned long long now_ms = 0;
    const char* content_type = "application/octet-stream";

    if (!PyArg_ParseTuple(args, "sy*|Ks", &channel, &payload, &now_ms, &content_type))
        return NULL;
    uint64_t seq = 0;
    const mosaic_status status =
        mosaic_node_publish(self->node, channel, content_type, (const uint8_t*)payload.buf,
                            (size_t)payload.len, now_ms, &seq);
    PyBuffer_Release(&payload);
    if (status != MOSAIC_OK)
        return status_error(status);
    return PyLong_FromUnsignedLongLong(seq);
}

static PyObject* Node_pump(NodeObject* self, PyObject* args) {
    unsigned long long now_ms = 0;
    if (!PyArg_ParseTuple(args, "|K", &now_ms))
        return NULL;
    mosaic_node_pump(self->node, now_ms);
    mosaic_node_tick(self->node, now_ms);
    return Py_NewRef(Py_None);
}

/* drain() returns the records that arrived since the last call, oldest first. */
static PyObject* Node_drain(NodeObject* self, PyObject* unused) {
    (void)unused;
    PyObject* out = PyList_New(self->queued);
    if (out == NULL)
        return NULL;
    for (Py_ssize_t i = 0; i < self->queued; ++i) {
        mosaic_record_owned* owned = self->queue[i];
        mosaic_record view;
        PyObject* item = PyDict_New();
        if (item == NULL || mosaic_record_owned_get(owned, &view) != MOSAIC_OK) {
            Py_XDECREF(item);
            Py_DECREF(out);
            return NULL;
        }
        PyDict_SetItemString(item, "writer", PyUnicode_FromString(view.writer));
        PyDict_SetItemString(item, "channel", PyUnicode_FromString(view.channel));
        PyDict_SetItemString(item, "seq", PyLong_FromUnsignedLongLong(view.seq));
        PyDict_SetItemString(item, "timestamp_ms", PyLong_FromUnsignedLongLong(view.timestamp_ms));
        PyDict_SetItemString(item, "content_type", PyUnicode_FromString(view.content_type));
        PyDict_SetItemString(
            item, "payload",
            PyBytes_FromStringAndSize((const char*)view.payload, (Py_ssize_t)view.payload_len));
        PyList_SET_ITEM(out, i, item);
    }
    queue_clear(self);
    return out;
}

static PyObject* Node_endpoints(NodeObject* self, PyObject* unused) {
    (void)unused;
    char* pub = NULL;
    char* router = NULL;
    if (mosaic_transport_endpoints(self->transport, &pub, &router) != MOSAIC_OK)
        return status_error(MOSAIC_ERR_IO);
    PyObject* out = Py_BuildValue("(ss)", pub != NULL ? pub : "", router != NULL ? router : "");
    mosaic_free_string(&pub);
    mosaic_free_string(&router);
    return out;
}

static PyObject* Node_close(NodeObject* self, PyObject* unused) {
    (void)unused;
    if (!self->closed) {
        self->closed = 1;
        mosaic_node_free(&self->node);
        mosaic_transport_free(&self->transport);
        mosaic_store_free(&self->store);
        mosaic_keys_free(&self->keys);
    }
    return Py_NewRef(Py_None);
}

static PyMethodDef Node_methods[] = {
    {"follow", (PyCFunction)Node_follow, METH_VARARGS, "Subscribe to a channel pattern."},
    {"publish", (PyCFunction)Node_publish, METH_VARARGS,
     "publish(channel, payload, now_ms) -> sequence number"},
    {"pump", (PyCFunction)Node_pump, METH_VARARGS,
     "pump(now_ms) - handle everything waiting, then run timers"},
    {"drain", (PyCFunction)Node_drain, METH_NOARGS, "Return records received since the last call."},
    {"endpoints", (PyCFunction)Node_endpoints, METH_NOARGS,
     "(pub, router) endpoints actually bound"},
    {"close", (PyCFunction)Node_close, METH_NOARGS,
     "Release the handles now rather than at collection."},
    {NULL, NULL, 0, NULL}};

static PyTypeObject NodeType = {
    PyVarObject_HEAD_INIT(NULL, 0) "mosaic._mosaic.Node", /* tp_name */
    sizeof(NodeObject),                                   /* tp_basicsize */
};

static PyObject* module_version(PyObject* module, PyObject* unused) {
    (void)module;
    (void)unused;
    return PyUnicode_FromString(mosaic_version());
}

static PyObject* module_generate_keys(PyObject* module, PyObject* args) {
    (void)module;
    const char* path = NULL;
    if (!PyArg_ParseTuple(args, "s", &path))
        return NULL;
    char* identity = NULL;
    if (mosaic_keys_generate(path, &identity) != MOSAIC_OK)
        return status_error(MOSAIC_ERR_CRYPTO);
    PyObject* out = PyUnicode_FromString(identity != NULL ? identity : "");
    mosaic_free_string(&identity);
    return out;
}

/* The curve public key of a keystore: what you give a peer so it can authenticate your links. */
static PyObject* module_curve_public(PyObject* self, PyObject* args) {
    (void)self;
    const char* keys_path = NULL;
    if (!PyArg_ParseTuple(args, "s", &keys_path))
        return NULL;
    mosaic_keys* keys = NULL;
    if (mosaic_keys_load(keys_path, &keys) != MOSAIC_OK) {
        PyErr_SetString(PyExc_RuntimeError, mosaic_last_error());
        return NULL;
    }
    char* public_z85 = NULL;
    char* secret_z85 = NULL;
    const mosaic_status status = mosaic_keys_curve(keys, &public_z85, &secret_z85);
    mosaic_free_string(&secret_z85); /* only the public half leaves this function */
    mosaic_keys_free(&keys);
    if (status != MOSAIC_OK || public_z85 == NULL) {
        mosaic_free_string(&public_z85);
        PyErr_SetString(PyExc_RuntimeError, mosaic_last_error());
        return NULL;
    }
    PyObject* result = PyUnicode_FromString(public_z85);
    mosaic_free_string(&public_z85);
    return result;
}

static PyMethodDef module_methods[] = {
    {"version", module_version, METH_NOARGS, "The library version."},
    {"curve_public", module_curve_public, METH_VARARGS,
     "curve_public(keys_path) -> z85 curve key to give a peer"},
    {"generate_keys", module_generate_keys, METH_VARARGS, "generate_keys(path) -> writer identity"},
    {NULL, NULL, 0, NULL}};

static struct PyModuleDef module_def = {PyModuleDef_HEAD_INIT, "_mosaic", "Mosaic over its C ABI.",
                                        -1, module_methods};

PyMODINIT_FUNC PyInit__mosaic(void) {
    NodeType.tp_dealloc = (destructor)Node_dealloc;
    NodeType.tp_init = (initproc)Node_init;
    NodeType.tp_new = PyType_GenericNew;
    NodeType.tp_methods = Node_methods;
    NodeType.tp_flags = Py_TPFLAGS_DEFAULT;
    NodeType.tp_doc = "A Mosaic node: publish, follow, pump.";
    if (PyType_Ready(&NodeType) < 0)
        return NULL;

    PyObject* module = PyModule_Create(&module_def);
    if (module == NULL)
        return NULL;
    Py_INCREF(&NodeType);
    if (PyModule_AddObject(module, "Node", (PyObject*)&NodeType) < 0) {
        Py_DECREF(&NodeType);
        Py_DECREF(module);
        return NULL;
    }
    return module;
}
