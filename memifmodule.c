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

#define PYMEMIF_DATAROOM 2048
#define PYMEMIF_RING_CAPACITY_LOG2 10

typedef struct
{
  PyObject_HEAD memif_socket_handle_t sock;
  memif_conn_handle_t conn;
  bool isUp;
} NativeMemif;

int
NativeMemif_handleConnect(memif_conn_handle_t conn, void* self0)
{
  NativeMemif* self = self0;
  PYMEMIF_LOG("handleConnnect");
  return 0;
}

int
NativeMemif_handleDisconnect(memif_conn_handle_t conn, void* self0)
{
  NativeMemif* self = self0;
  PYMEMIF_LOG("handleDisconnnect");
  return 0;
};

int
NativeMemif_handleInterrupt(memif_conn_handle_t conn, void* self0, uint16_t qid)
{
  NativeMemif* self = self0;
  PYMEMIF_LOG("handleInterrupt %" PRIu16, qid);
  return 0;
}

static PyObject*
NativeMemif_new(PyTypeObject* type, PyObject* args, PyObject* kwds)
{
  NativeMemif* self = (NativeMemif*)type->tp_alloc(type, 0);
  if (self == NULL) {
    return NULL;
  }
  PYMEMIF_LOG("new");
  return (PyObject*)self;
}

static int
NativeMemif_init(NativeMemif* self, PyObject* args, PyObject* kwds)
{
  const char* socketName = NULL;
  Py_ssize_t socketNameLen = 0;
  static_assert(sizeof(unsigned int) == sizeof(uint32_t), "");
  uint32_t id = 0;
  int role = 0;
  PyArg_ParseTuple(args, "s#Ip", &socketName, &socketNameLen, &id, &role);
  PYMEMIF_LOG("init %s %ld %" PRIu32 " %d", socketName, socketNameLen, id, role);

  memif_socket_args_t sa = { 0 };
  strncpy(sa.path, socketName, sizeof(sa.path) - 1);
  strncpy(sa.app_name, "python-memif", sizeof(sa.app_name) - 1);
  int err = memif_create_socket(&self->sock, &sa, self);
  if (err != MEMIF_ERR_SUCCESS) {
    PYMEMIF_LOG_ERR(memif_create_socket);
    return -1;
  }

  memif_conn_args_t ca = { 0 };
  ca.is_master = role;
  ca.socket = self->sock;
  ca.interface_id = id;
  ca.buffer_size = PYMEMIF_DATAROOM;
  ca.log2_ring_size = PYMEMIF_RING_CAPACITY_LOG2;
  err = memif_create(&self->conn, &ca, NativeMemif_handleConnect, NativeMemif_handleDisconnect,
                     NativeMemif_handleInterrupt, self);
  if (err != MEMIF_ERR_SUCCESS) {
    PYMEMIF_LOG_ERR(memif_create);
    return -1;
  }

  return 0;
}

static void
NativeMemif_dealloc(NativeMemif* self)
{
  PYMEMIF_LOG("dealloc");
  if (self->conn != NULL) {
    int err = memif_delete(&self->conn);
    if (err != MEMIF_ERR_SUCCESS) {
      PYMEMIF_LOG_ERR(memif_delete);
      return;
    }
  }

  if (self->sock != NULL) {
    int err = memif_delete_socket(&self->sock);
    if (err != MEMIF_ERR_SUCCESS) {
      PYMEMIF_LOG_ERR(memif_delete_socket);
      return;
    }
  }

  Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject*
NativeMemif_poll(NativeMemif* self, PyObject* Py_UNUSED(ignored))
{
  if (self->sock == NULL) {
    return Py_None;
  }

  int err = memif_poll_event(self->sock, 0);
  if (err != MEMIF_ERR_SUCCESS) {
    PYMEMIF_LOG_ERR(memif_poll_event);
  }
  return Py_None;
}

static PyMethodDef NativeMemif_methods[] = {
  { "poll", (PyCFunction)NativeMemif_poll, METH_NOARGS, "" },
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
PyInit_memif()
{
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
