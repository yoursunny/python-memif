#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <assert.h>
#include <libmemif.h>
#include <stdbool.h>

#define PYMEMIF_LOG(fmt, ...)                                                                      \
  do {                                                                                             \
    fprintf(stderr, "PyMemif(%p) " fmt "\n", self, ##__VA_ARGS__);                                 \
  } while (false)

#define PYMEMIF_LOG_ERR(func) PYMEMIF_LOG(#func " %d %s", err, memif_strerror(err))

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
  PYMEMIF_LOG("handleConnnect");
  assert(self->conn == conn);
  self->isUp = true;

  int err = memif_refill_queue(conn, 0, -1, 0);
  if (err != MEMIF_ERR_SUCCESS) {
    PYMEMIF_LOG_ERR(memif_refill_queue);
  }
  return 0;
}

int
NativeMemif_handleDisconnect(memif_conn_handle_t conn, void* self0) {
  NativeMemif* self = self0;
  PYMEMIF_LOG("handleDisconnnect");
  assert(self->conn == conn);
  self->isUp = false;
  return 0;
};

int
NativeMemif_handleInterrupt(memif_conn_handle_t conn, void* self0, uint16_t qid) {
  NativeMemif* self = self0;
  PYMEMIF_LOG("handleInterrupt %" PRIu16, qid);
  assert(self->conn == conn);

  memif_buffer_t burst[PYMEMIF_RX_BURST];
  uint16_t nRx = 0;
  int err = memif_rx_burst(conn, qid, burst, PYMEMIF_RX_BURST, &nRx);
  if (err != MEMIF_ERR_SUCCESS) {
    PYMEMIF_LOG_ERR(memif_rx_burst);
    return 0;
  }

  for (uint16_t i = 0; i < nRx; ++i) {
    const memif_buffer_t* b = &burst[i];
    PYMEMIF_LOG("RX %" PRIu32, b->len);
    bool hasNext = (b->flags & MEMIF_BUFFER_FLAG_NEXT) != 0;
    PyObject* args = Py_BuildValue("(y#O)", b->data, b->len, hasNext ? Py_True : Py_False);
    PyObject_CallObject(self->rx, args);
    Py_DECREF(args);
  }

  err = memif_refill_queue(conn, qid, nRx, 0);
  if (err != MEMIF_ERR_SUCCESS) {
    PYMEMIF_LOG_ERR(memif_rx_burst);
  }
  return 0;
}

void
NativeMemif_doStop(NativeMemif* self) {
  self->isUp = false;

  if (self->conn != NULL) {
    int err = memif_delete(&self->conn);
    if (err != MEMIF_ERR_SUCCESS) {
      PYMEMIF_LOG_ERR(memif_delete);
    } else {
      self->conn = NULL;
    }
  }

  if (self->sock != NULL) {
    int err = memif_delete_socket(&self->sock);
    if (err != MEMIF_ERR_SUCCESS) {
      PYMEMIF_LOG_ERR(memif_delete_socket);
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
  PYMEMIF_LOG("new");
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
  PYMEMIF_LOG("init %s %ld %" PRIu32 " %d", socketName, socketNameLen, id, isServer);

  memif_socket_args_t sa = {0};
  strncpy(sa.path, socketName, sizeof(sa.path) - 1);
  strncpy(sa.app_name, "python-memif", sizeof(sa.app_name) - 1);
  int err = memif_create_socket(&self->sock, &sa, self);
  if (err != MEMIF_ERR_SUCCESS) {
    PYMEMIF_LOG_ERR(memif_create_socket);
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
    PYMEMIF_LOG_ERR(memif_create);
    return -1;
  }

  Py_INCREF(self->rx);
  return 0;
}

static void
NativeMemif_dealloc(NativeMemif* self) {
  PYMEMIF_LOG("dealloc");
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
    PYMEMIF_LOG_ERR(memif_poll_event);
  }
  Py_RETURN_NONE;
}

static PyObject*
NativeMemif_send(NativeMemif* self, PyObject* args) {
  if (!self->isUp) {
    Py_RETURN_FALSE;
  }

  const uint8_t* pkt = NULL;
  Py_ssize_t pktLen = 0;
  if (!PyArg_ParseTuple(args, "y#", &pkt, &pktLen)) {
    return NULL;
  }
  if (pktLen >= PYMEMIF_TX_SEGS * self->dataroom) {
    PYMEMIF_LOG("TX %ld too-long", pktLen);
    Py_RETURN_FALSE;
  }
  PYMEMIF_LOG("TX %ld", pktLen);

  memif_buffer_t b[PYMEMIF_TX_SEGS];
  uint16_t nAlloc = 0;
  int err = memif_buffer_alloc(self->conn, 0, b, 1, &nAlloc, pktLen);
  if (err != MEMIF_ERR_SUCCESS) {
    PYMEMIF_LOG_ERR(memif_buffer_alloc);
    Py_RETURN_FALSE;
  }

  for (uint16_t i = 0; i < nAlloc; ++i) {
    memcpy(b[i].data, pkt, b[i].len);
    pkt += b[i].len;
  }

  uint16_t nTx = 0;
  err = memif_tx_burst(self->conn, 0, b, nAlloc, &nTx);
  if (err != MEMIF_ERR_SUCCESS || nTx != 1) {
    PYMEMIF_LOG_ERR(memif_tx_burst);
    Py_RETURN_FALSE;
  }
  Py_RETURN_TRUE;
}

static PyObject*
NativeMemif_close(NativeMemif* self, PyObject* Py_UNUSED(ignored)) {
  PYMEMIF_LOG("close");
  NativeMemif_doStop(self);
  Py_RETURN_NONE;
}

static PyMethodDef NativeMemif_methods[] = {
  {"poll", (PyCFunction)NativeMemif_poll, METH_NOARGS, ""},
  {"send", (PyCFunction)NativeMemif_send, METH_VARARGS, ""},
  {"close", (PyCFunction)NativeMemif_close, METH_NOARGS, ""},
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
