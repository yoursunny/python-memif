#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <assert.h>
#include <libmemif.h>
#include <stdbool.h>

#define PYMEMIF_THROW_MEMIF_ERR(func)                                                              \
  PyErr_Format(PyExc_ConnectionError, #func " %d %s", err, memif_strerror(err))

#define PYMEMIF_RX_BURST 16
#define PYMEMIF_TX_SEGS 8

typedef struct {
  PyObject_HEAD
  memif_socket_handle_t sock;
  memif_conn_handle_t conn;
  PyObject* rx;
  uint32_t dataroom;
  bool isUp;
} NativeMemif;

int
NativeMemif_handleConnect(memif_conn_handle_t conn, void* self0) {
  NativeMemif* self = self0;
  assert(self->conn == conn);
  self->isUp = true;

  int err = memif_refill_queue(conn, 0, -1, 0);
  if (err != MEMIF_ERR_SUCCESS) {
    PYMEMIF_THROW_MEMIF_ERR(memif_refill_queue);
  }
  return 0;
}

int
NativeMemif_handleDisconnect(memif_conn_handle_t conn, void* self0) {
  NativeMemif* self = self0;
  assert(self->conn == conn);
  self->isUp = false;
  return 0;
};

int
NativeMemif_handleInterrupt(memif_conn_handle_t conn, void* self0, uint16_t qid) {
  NativeMemif* self = self0;
  assert(self->conn == conn);

  memif_buffer_t burst[PYMEMIF_RX_BURST];
  uint16_t nRx = 0;
  int err = memif_rx_burst(conn, qid, burst, PYMEMIF_RX_BURST, &nRx);
  if (err != MEMIF_ERR_SUCCESS) {
    PYMEMIF_THROW_MEMIF_ERR(memif_rx_burst);
    return 0;
  }

  for (uint16_t i = 0; i < nRx; ++i) {
    const memif_buffer_t* b = &burst[i];
    bool hasNext = (b->flags & MEMIF_BUFFER_FLAG_NEXT) != 0;
    PyObject* args = Py_BuildValue("(y#O)", b->data, b->len, PyBool_FromLong(hasNext));
    PyObject_CallObject(self->rx, args);
    Py_DECREF(args);
  }

  err = memif_refill_queue(conn, qid, nRx, 0);
  if (err != MEMIF_ERR_SUCCESS) {
    PYMEMIF_THROW_MEMIF_ERR(memif_refill_queue);
  }
  return 0;
}

void
NativeMemif_doStop(NativeMemif* self) {
  self->isUp = false;

  if (self->conn != NULL) {
    int err = memif_delete(&self->conn);
    if (err != MEMIF_ERR_SUCCESS) {
      PYMEMIF_THROW_MEMIF_ERR(memif_delete);
    } else {
      self->conn = NULL;
    }
  }

  if (self->sock != NULL) {
    int err = memif_delete_socket(&self->sock);
    if (err != MEMIF_ERR_SUCCESS) {
      PYMEMIF_THROW_MEMIF_ERR(memif_delete_socket);
    } else {
      self->sock = NULL;
    }
  }
}

static PyObject*
NativeMemif_new(PyTypeObject* type, PyObject* args, PyObject* kwds) {
  NativeMemif* self = (NativeMemif*)type->tp_alloc(type, 0);
  if (self == NULL) {
    return NULL;
  }
  return (PyObject*)self;
}

static int
NativeMemif_init(NativeMemif* self, PyObject* args, PyObject* kwds) {
  const char* socketName = NULL;
  Py_ssize_t socketNameLen = 0;
  static_assert(sizeof(unsigned int) == sizeof(uint32_t), "");
  uint32_t id = 0;
  int isServer = 0;
  self->dataroom = 2048;
  uint32_t ringSizeLog2 = 10;
  static const char* keywords[] = {
    "socket_name", "id", "rx", "is_server", "dataroom", "ring_size_log2",
  };
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "s#IO|$pII", (char**)keywords, &socketName,
                                   &socketNameLen, &id, &self->rx, &isServer, &self->dataroom,
                                   &ringSizeLog2)) {
    return -1;
  }
  if (!PyCallable_Check(self->rx)) {
    PyErr_SetString(PyExc_TypeError, "rx must be callable");
    return -1;
  }

  memif_socket_args_t sa = {0};
  strncpy(sa.path, socketName, sizeof(sa.path) - 1);
  strncpy(sa.app_name, "python-memif", sizeof(sa.app_name) - 1);
  int err = memif_create_socket(&self->sock, &sa, self);
  if (err != MEMIF_ERR_SUCCESS) {
    PYMEMIF_THROW_MEMIF_ERR(memif_create_socket);
    return -1;
  }

  memif_conn_args_t ca = {0};
  ca.is_master = isServer;
  ca.socket = self->sock;
  ca.interface_id = id;
  ca.buffer_size = self->dataroom;
  ca.log2_ring_size = ringSizeLog2;
  err = memif_create(&self->conn, &ca, NativeMemif_handleConnect, NativeMemif_handleDisconnect,
                     NativeMemif_handleInterrupt, self);
  if (err != MEMIF_ERR_SUCCESS) {
    memif_delete_socket(&self->sock);
    PYMEMIF_THROW_MEMIF_ERR(memif_create);
    return -1;
  }

  Py_INCREF(self->rx);
  return 0;
}

static void
NativeMemif_dealloc(NativeMemif* self) {
  NativeMemif_doStop(self);
  Py_DECREF(self->rx);
  Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject*
NativeMemif_poll(NativeMemif* self, PyObject* Py_UNUSED(ignored)) {
  if (self->sock == NULL) {
    Py_RETURN_NONE;
  }

  int err = memif_poll_event(self->sock, 0);
  if (err != MEMIF_ERR_SUCCESS) {
    PYMEMIF_THROW_MEMIF_ERR(memif_poll_event);
  }

  if (self->conn != NULL && !self->isUp) {
    memif_request_connection(self->conn);
  }

  Py_RETURN_NONE;
}

static PyObject*
NativeMemif_send(NativeMemif* self, PyObject* const* args, Py_ssize_t nargs) {
  if (!self->isUp) {
    return PyErr_Format(PyExc_BrokenPipeError, "interface is not up");
  }

  const uint8_t* pkt = NULL;
  Py_ssize_t pktLen = 0;
  if (nargs != 1 || PyBytes_AsStringAndSize(args[0], (char**)&pkt, &pktLen) < 0) {
    return PyErr_Format(PyExc_TypeError, "expect bytes argument");
  }
  if (pktLen >= PYMEMIF_TX_SEGS * self->dataroom) {
    return PyErr_Format(PyExc_OverflowError, "packet too long");
  }

  memif_buffer_t b[PYMEMIF_TX_SEGS];
  uint16_t nAlloc = 0;
  int err = memif_buffer_alloc(self->conn, 0, b, 1, &nAlloc, pktLen);
  if (err != MEMIF_ERR_SUCCESS) {
    return PYMEMIF_THROW_MEMIF_ERR(memif_buffer_alloc);
  }

  for (uint16_t i = 0; i < nAlloc; ++i) {
    memcpy(b[i].data, pkt, b[i].len);
    pkt += b[i].len;
  }

  uint16_t nTx = 0;
  err = memif_tx_burst(self->conn, 0, b, nAlloc, &nTx);
  if (err != MEMIF_ERR_SUCCESS) {
    return PYMEMIF_THROW_MEMIF_ERR(memif_tx_burst);
  }
  Py_RETURN_NONE;
}

static PyObject*
NativeMemif_close(NativeMemif* self, PyObject* Py_UNUSED(ignored)) {
  NativeMemif_doStop(self);
  Py_RETURN_NONE;
}

static PyObject*
NativeMemif_isUp(NativeMemif* self, void* Py_UNUSED(ignored)) {
  return PyBool_FromLong(self->isUp);
}

static PyMethodDef NativeMemif_methods[] = {
  {"poll", (PyCFunction)NativeMemif_poll, METH_NOARGS, ""},
  {"send", (PyCFunction)NativeMemif_send, METH_FASTCALL, ""},
  {"close", (PyCFunction)NativeMemif_close, METH_NOARGS, ""},
  {0},
};

static PyGetSetDef NativeMemif_getset[] = {
  {"up", (getter)NativeMemif_isUp, NULL, "", NULL},
  {0},
};

static PyTypeObject NativeMemifType = {
  PyVarObject_HEAD_INIT(NULL, 0) //
    .tp_name = "memif.NativeMemif",
  .tp_basicsize = sizeof(NativeMemif),
  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_new = NativeMemif_new,
  .tp_init = (initproc)NativeMemif_init,
  .tp_dealloc = (destructor)NativeMemif_dealloc,
  .tp_methods = NativeMemif_methods,
  .tp_getset = NativeMemif_getset,
};

static PyModuleDef memif = {
  PyModuleDef_HEAD_INIT,
  .m_name = "memif",
  .m_size = -1,
};

PyMODINIT_FUNC
PyInit_memif() {
  if (PyType_Ready(&NativeMemifType) < 0) {
    return NULL;
  }

  PyObject* m = PyModule_Create(&memif);
  if (m == NULL) {
    return NULL;
  }

  Py_INCREF(&NativeMemifType);
  if (PyModule_AddObject(m, "NativeMemif", (PyObject*)&NativeMemifType) < 0) {
    Py_DECREF(&NativeMemifType);
    Py_DECREF(m);
    return NULL;
  }

  return m;
}
