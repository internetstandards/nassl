
#include <Python.h>

#ifdef LEGACY_OPENSSL
// Include internal headers so we can access EDH and ECDH parameter
// They need to be included before including winsock.h otherwise we get a bunch of errors on Windows
// http://stackoverflow.com/questions/11726958/cant-include-winsock2-h-in-msvc-2010
/* crappy solution, ssl_locl and e_os are normally not exported by openssl but we need them to read non exported structures.
   Plus CERT is defined by ssl_locl so we have to undefine it before including it... */
#undef CERT
#include "openssl-internal/ssl_locl.h"
#endif


// Fix symbol clashing on Windows
// https://bugs.launchpad.net/pyopenssl/+bug/570101
#ifdef _WIN32
#include "winsock.h"
#endif

#include <openssl/ssl.h>
#include <openssl/ocsp.h>


#include "nassl_errors.h"
#include "nassl_SSL.h"
#include "nassl_BIO.h"
#include "nassl_X509.h"
#include "nassl_SSL_SESSION.h"
#include "nassl_OCSP_RESPONSE.h"
#include "openssl_utils.h"


// nassl.SSL.new()
static PyObject* nassl_SSL_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    nassl_SSL_Object *self;
    nassl_SSL_CTX_Object *sslCtx_Object;
    SSL *ssl;

    self = (nassl_SSL_Object *)type->tp_alloc(type, 0);
    if (self == NULL)
    {
        return NULL;
    }

    self->ssl = NULL;
    self->sslCtx_Object = NULL;
    self->networkBio_Object = NULL;

    // Recover and store the corresponding ssl_ctx
    if (!PyArg_ParseTuple(args, "O!", &nassl_SSL_CTX_Type, &sslCtx_Object))
    {
        Py_DECREF(self);
        return NULL;
    }

    if (sslCtx_Object == NULL)
    {
        PyErr_SetString(PyExc_RuntimeError, "Received a NULL SSL_CTX object");
        Py_DECREF(self);
        return NULL;
    }
    Py_INCREF(sslCtx_Object);

    ssl = SSL_new(sslCtx_Object->sslCtx);
    if (ssl == NULL)
    {
        Py_DECREF(self);
        return raise_OpenSSL_error();
    }

    self->sslCtx_Object = sslCtx_Object;
    self->ssl = ssl;

    return (PyObject *)self;
}


static void nassl_SSL_dealloc(nassl_SSL_Object *self)
{
    if (self->networkBio_Object != NULL)
    {
        if (self->networkBio_Object->bio != NULL)
        {
            // Manually free the network BIO; it's the only place where we know that it is not needed anymore
            // If a reference to the BIO Python object is kept, the object will not be usable
            BIO_vfree(self->networkBio_Object->bio);
            self->networkBio_Object->bio = NULL;
        }
        Py_DECREF(self->networkBio_Object);
        self->networkBio_Object = NULL;
    }

    if (self->ssl != NULL)
    {
        // This will also free the internal BIO
        SSL_free(self->ssl);
        self->ssl = NULL;
    }

    if (self->sslCtx_Object != NULL)
    {
        Py_DECREF(self->sslCtx_Object);
    }
    Py_TYPE(self)->tp_free((PyObject*)self);
}


static PyObject* nassl_SSL_set_bio(nassl_SSL_Object *self, PyObject *args)
{
    nassl_BIO_Object* internalBioObject;
    if (!PyArg_ParseTuple(args, "O!", &nassl_BIO_Type, &internalBioObject))
    {
        return NULL;
    }
    SSL_set_bio(self->ssl, internalBioObject->bio, internalBioObject->bio);
    Py_RETURN_NONE;
}


static PyObject* nassl_SSL_set_network_bio_to_free_when_dealloc(nassl_SSL_Object *self, PyObject *args)
{
    // The network BIO is only needed here so we properly free it when the SSL object gets freed
    // Other than that it's never used
    nassl_BIO_Object* networkBioObject;

    if (!PyArg_ParseTuple(args, "O!", &nassl_BIO_Type, &networkBioObject))
    {
        return NULL;
    }
    Py_INCREF(networkBioObject);
    self->networkBio_Object = networkBioObject;
    Py_RETURN_NONE;
}


static PyObject* nassl_SSL_set_connect_state(nassl_SSL_Object *self, PyObject *args)
{
    SSL_set_connect_state(self->ssl);
    Py_RETURN_NONE;
}


static PyObject* nassl_SSL_set_mode(nassl_SSL_Object *self, PyObject *args)
{
    long mode;
    if (!PyArg_ParseTuple(args, "l", &mode))
    {
        return NULL;
    }

    SSL_set_mode(self->ssl, mode);
    Py_RETURN_NONE;
}


static PyObject* nassl_SSL_do_handshake(nassl_SSL_Object *self, PyObject *args)
{
    int result = SSL_do_handshake(self->ssl);
    if (result != 1)
    {
        return raise_OpenSSL_ssl_error(self->ssl, result);
    }
    Py_RETURN_NONE;
}


static PyObject* nassl_SSL_read(nassl_SSL_Object *self, PyObject *args)
{
    int returnValue, readSize;
    char *readBuffer;
    PyObject *res = NULL;

    if (!PyArg_ParseTuple(args, "I", &readSize))
    {
        return NULL;
    }

    readBuffer = (char *) PyMem_Malloc(readSize);
    if (readBuffer == NULL)
    {
        return PyErr_NoMemory();
    }

    returnValue = SSL_read(self->ssl, readBuffer, readSize);
    if (returnValue > 0)
    {
        // Read OK
        res = PyBytes_FromStringAndSize(readBuffer, returnValue);
    }
    else
    {
        // Read failed
        raise_OpenSSL_ssl_error(self->ssl, returnValue);
    }

    PyMem_Free(readBuffer);
    return res;
}


static PyObject* nassl_SSL_write(nassl_SSL_Object *self, PyObject *args)
{
    int returnValue, writeSize;
    char *writeBuffer;
    PyObject *res = NULL;

    if (!PyArg_ParseTuple(args, "s#", &writeBuffer, &writeSize))
    {
        return NULL;
    }

    returnValue = SSL_write(self->ssl, writeBuffer, writeSize);
    if (returnValue > 0)
    {
        // Write OK
        res = Py_BuildValue("I", returnValue);
    }
    else
    {
        // Write failed
        raise_OpenSSL_ssl_error(self->ssl, returnValue);
    }
    return res;
}

#ifndef LEGACY_OPENSSL
static PyObject* nassl_SSL_write_early_data(nassl_SSL_Object *self, PyObject *args)
{
    int returnValue, writeSize, writtenDataSize;
    char *writeBuffer;
    PyObject *res = NULL;

    if (!PyArg_ParseTuple(args, "s#", &writeBuffer, &writeSize))
    {
        return NULL;
    }

    returnValue = SSL_write_early_data(self->ssl, writeBuffer, writeSize, &writtenDataSize);
    if (returnValue > 0)
    {
        // Write OK
        res = Py_BuildValue("I", writtenDataSize);
    }
    else
    {
        // Write failed
        raise_OpenSSL_ssl_error(self->ssl, returnValue);
    }
    return res;
}

static PyObject* nassl_SSL_get_early_data_status(nassl_SSL_Object *self, PyObject *args)
{
    int returnValue = SSL_get_early_data_status(self->ssl);

    return Py_BuildValue("I", returnValue);
}

static PyObject* nassl_SSL_get_max_early_data(nassl_SSL_Object *self, PyObject *args)
{
    int returnValue = SSL_get_max_early_data(self->ssl);

    return Py_BuildValue("I", returnValue);
}
#endif

static PyObject* nassl_SSL_shutdown(nassl_SSL_Object *self, PyObject *args)
{
    int returnValue = SSL_shutdown(self->ssl);
    PyObject *res = NULL;

    if (returnValue >= 0)
    {
        res = Py_BuildValue("I", returnValue);
    }
    else
    {
        raise_OpenSSL_ssl_error(self->ssl, returnValue);
    }
    return res;
}


static PyObject* nassl_SSL_pending(nassl_SSL_Object *self, PyObject *args)
{
    int returnValue = SSL_pending(self->ssl);
    return Py_BuildValue("I", returnValue);
}


static PyObject* nassl_SSL_get_secure_renegotiation_support(nassl_SSL_Object *self, PyObject *args)
{
    if (SSL_get_secure_renegotiation_support(self->ssl))
    {
        Py_RETURN_TRUE;
    }
    else
    {
        Py_RETURN_FALSE;
    }
}

static PyObject* nassl_SSL_get_available_compression_methods(nassl_SSL_Object *self, PyObject *args)
{
    PyObject* compMethodPyList = NULL;
    int i, compMethodsCount = 0;
    STACK_OF(SSL_COMP) *compMethods = SSL_COMP_get_compression_methods();

    // We'll return a Python list containing the name of each compression method
    compMethodsCount = sk_SSL_COMP_num(compMethods);
    compMethodPyList = PyList_New(compMethodsCount);
    if (compMethodPyList == NULL)
    {
        return PyErr_NoMemory();
    }

    for (i=0;i<compMethodsCount;i++)
    {
        PyObject *methodPyString = NULL;

        const SSL_COMP *method = sk_SSL_COMP_value(compMethods, i);
        if (method == NULL)
        {
            Py_DECREF(compMethodPyList);
            PyErr_SetString(PyExc_ValueError, "Could not extract a compression method. Should not happen ?");
            return NULL;
        }

#ifdef LEGACY_OPENSSL
        methodPyString = PyUnicode_FromString(method->name);
#else
        methodPyString = PyUnicode_FromString(SSL_COMP_get0_name(method));
#endif
        if (methodPyString == NULL)
        {
            Py_DECREF(compMethodPyList);
            return PyErr_NoMemory();
        }

        PyList_SET_ITEM(compMethodPyList, i,  methodPyString);
    }

    return compMethodPyList;
}


static PyObject* nassl_SSL_get_current_compression_method(nassl_SSL_Object *self, PyObject *args)
{
    const COMP_METHOD *compMethod;
    compMethod = SSL_get_current_compression(self->ssl);
    if (compMethod == NULL)
    {
        Py_RETURN_NONE;
    }
    return PyUnicode_FromString(SSL_COMP_get_name(compMethod));
}


static PyObject* nassl_SSL_set_verify(nassl_SSL_Object *self, PyObject *args)
{
    int verifyMode;
    if (!PyArg_ParseTuple(args, "I", &verifyMode))
    {
        return NULL;
    }

    switch (verifyMode)
    {
        case SSL_VERIFY_NONE:
        case SSL_VERIFY_PEER:
        case SSL_VERIFY_FAIL_IF_NO_PEER_CERT:
        case SSL_VERIFY_CLIENT_ONCE:
            SSL_set_verify(self->ssl, verifyMode, NULL);
            break;
        default:
            PyErr_SetString(PyExc_ValueError, "Invalid value for verification mode");
            return NULL;
    }

    Py_RETURN_NONE;
}


static PyObject* nassl_SSL_set_tlsext_host_name(nassl_SSL_Object *self, PyObject *args)
{
    char *nameIndication;
    if (!PyArg_ParseTuple(args, "s", &nameIndication))
    {
        return NULL;
    }

    if (!SSL_set_tlsext_host_name(self->ssl, nameIndication))
    {
        PyErr_SetString(PyExc_ValueError, "Error setting the SNI extension. Using SSL 2 ?");
        return NULL;
    }

    Py_RETURN_NONE;
}


static PyObject* nassl_SSL_get_peer_certificate(nassl_SSL_Object *self, PyObject *args)
{
    X509 *cert;
    cert = SSL_get_peer_certificate(self->ssl);
    if (cert == NULL)
    {
        // Anonymous cipher suite ?
        Py_RETURN_NONE;
    }
    else
    {
        // Return an _nassl.X509 object
        nassl_X509_Object *x509_Object;
        x509_Object = (nassl_X509_Object *)nassl_X509_Type.tp_alloc(&nassl_X509_Type, 0);
        if (x509_Object == NULL)
        {
            return PyErr_NoMemory();
        }

        x509_Object->x509 = cert;
        return (PyObject *) x509_Object;
    }
}


static PyObject* nassl_SSL_set_cipher_list(nassl_SSL_Object *self, PyObject *args)
{
    char *cipherList;
    if (!PyArg_ParseTuple(args, "s", &cipherList))
    {
        return NULL;
    }

    if (!SSL_set_cipher_list(self->ssl, cipherList))
    {
        return raise_OpenSSL_error();
    }

    Py_RETURN_NONE;
}


#ifndef LEGACY_OPENSSL
static PyObject* nassl_SSL_set_ciphersuites(nassl_SSL_Object *self, PyObject *args)
{
    char *cipherSuites;
    if (!PyArg_ParseTuple(args, "s", &cipherSuites))
    {
        return NULL;
    }

    if (!SSL_set_ciphersuites(self->ssl, cipherSuites))
    {
        return raise_OpenSSL_error();
    }

    Py_RETURN_NONE;
}
#endif


static PyObject* nassl_SSL_get_cipher_list(nassl_SSL_Object *self, PyObject *args)
{
    unsigned int priority = 0;
    PyObject* ciphersPyList = NULL;
    if (SSL_get_cipher_list(self->ssl, 0) == NULL)
    {
        Py_RETURN_NONE;
    }

    // Return a list of cipher strings
    ciphersPyList = PyList_New(0);
    if (ciphersPyList == NULL)
    {
        return PyErr_NoMemory();
    }

   do
   {
        // Extract each cipher name
        PyObject *cipherPyString = NULL;
        const char *cipherName = SSL_get_cipher_list(self->ssl, priority);

        cipherPyString = PyUnicode_FromString(cipherName);
        if (cipherPyString == NULL)
        {
            Py_DECREF(ciphersPyList);
            return PyErr_NoMemory();
        }

        if (PyList_Append(ciphersPyList, cipherPyString) == -1)
        {
            Py_DECREF(ciphersPyList);
            Py_DECREF(cipherPyString);
            return NULL; // PyList_Append() sets an exception
        }
        Py_DECREF(cipherPyString);

        priority++;
    } while (SSL_get_cipher_list(self->ssl, priority) != NULL) ;

    return ciphersPyList;
}


static PyObject* nassl_SSL_get_cipher_description(nassl_SSL_Object *self, PyObject *args)
{
    char *wantedCipherName;
    if (!PyArg_ParseTuple(args, "s", &wantedCipherName))
    {
        return NULL;
    }

    const SSL_CIPHER *found_cipher = NULL;

    // Based on: https://github.com/openssl/openssl/blob/master/apps/ciphers.c
    STACK_OF(SSL_CIPHER) *sk = SSL_get_ciphers(self->ssl);
    for (int i = 0; found_cipher == NULL && i < sk_SSL_CIPHER_num(sk); i++) {
        const SSL_CIPHER *c = sk_SSL_CIPHER_value(sk, i);
        const char *thisCipherName = SSL_CIPHER_get_name(c);
        if (thisCipherName && strcmp(thisCipherName, wantedCipherName) == 0) {
            found_cipher = c;
        }
    }

    if (found_cipher == NULL) {
        Py_RETURN_NONE;
    } else {
        char cipherDesc[128];
        return PyUnicode_FromString(SSL_CIPHER_description(found_cipher, cipherDesc, 128));
    }
}


// Used to retrieve the cipher earlier in the connection
// https://github.com/nabla-c0d3/nassl/pull/15
static const SSL_CIPHER* get_tmp_new_cipher(nassl_SSL_Object *self)
{
#ifdef LEGACY_OPENSSL
    // TODO: Rewrite this without accessing private members (for example, use get_cipher())
    if (self->ssl == NULL || self->ssl->s3 == NULL)
    {
        return NULL;
    }
    return self->ssl->s3->tmp.new_cipher;
#else
    return NULL;
#endif
}


static PyObject* nassl_SSL_get_cipher_bits(nassl_SSL_Object *self, PyObject *args)
{
    const SSL_CIPHER *cipher = get_tmp_new_cipher(self);
    int returnValue = cipher ? SSL_CIPHER_get_bits(cipher, NULL) : SSL_get_cipher_bits(self->ssl, NULL);

    return Py_BuildValue("I", returnValue);
}


static PyObject* nassl_SSL_get_cipher_name(nassl_SSL_Object *self, PyObject *args)
{
    const SSL_CIPHER *cipher = get_tmp_new_cipher(self);
    const char *cipherName = cipher ? SSL_CIPHER_get_name(cipher) : SSL_get_cipher_name(self->ssl);

    if (strcmp(cipherName, "(NONE)") == 0)
    {
        Py_RETURN_NONE;
    }

    return PyUnicode_FromString(cipherName);
}


static PyObject* nassl_SSL_get_cipher_protocol_id(nassl_SSL_Object *self, PyObject *args)
{
    const SSL_CIPHER *cipher = get_tmp_new_cipher(self);
    if (!cipher) {
        Py_RETURN_NONE;
    }
#ifdef LEGACY_OPENSSL
    unsigned short id = (unsigned short) (cipher->id & 0x0000FFFF);
#else
    unsigned short id = SSL_CIPHER_get_protocol_id(self->ssl);
#endif
    return Py_BuildValue("H", id);
}


static PyObject* nassl_SSL_get_client_CA_list(nassl_SSL_Object *self, PyObject *args)
{
    PyObject* namesPyList = NULL;
    int x509NamesNum = 0;
    int i = 0;
    STACK_OF(X509_NAME) *x509Names = NULL;

    // Return a list of X509 names
    namesPyList = PyList_New(0);
    if (namesPyList == NULL)
    {
        return PyErr_NoMemory();
    }

    x509Names = SSL_get_client_CA_list(self->ssl); // freed by SSL_free()
    x509NamesNum = sk_X509_NAME_num(x509Names);

    // Extract each X509_NAME and store their string representation
    for (i=0; i<x509NamesNum; i++)
    {
        char *nameStr = NULL;
        PyObject *namePyString = NULL;

        X509_NAME *name = sk_X509_NAME_pop(x509Names);
        if (name == NULL)
        {
            Py_DECREF(namesPyList);
            PyErr_SetString(PyExc_ValueError, "Could not extract an X509_NAME from the client CA list. Should not happen ?");
            return NULL;
        }

        // The use of X509_NAME_oneline is "is strongly discouraged in new applications"
        // But that's all we need for now
        nameStr = X509_NAME_oneline(name, NULL, 0);
        namePyString = PyUnicode_FromString(nameStr);
        if (namePyString == NULL)
        {
            Py_DECREF(namesPyList);
            return PyErr_NoMemory();
        }

        if (PyList_Append(namesPyList, namePyString) == -1)
        {
            Py_DECREF(namesPyList);
            Py_DECREF(namePyString);
            return NULL; // PyList_Append() sets an exception
        }
        Py_DECREF(namePyString);
    }
    return namesPyList;
}


static PyObject* nassl_SSL_get_verify_result(nassl_SSL_Object *self, PyObject *args)
{
    long returnValue = SSL_get_verify_result(self->ssl);
    return Py_BuildValue("I", returnValue);
}


static PyObject* nassl_SSL_renegotiate(nassl_SSL_Object *self, PyObject *args)
{
    SSL_renegotiate(self->ssl);
    Py_RETURN_NONE;
}


static PyObject* nassl_SSL_get_session(nassl_SSL_Object *self, PyObject *args)
{
    SSL_SESSION *sslSession = SSL_get1_session(self->ssl);
    if (sslSession == NULL)
    {
        Py_RETURN_NONE;
    }
    else
    {
        // Return an _nassl.SSL_SESSION object
        nassl_SSL_SESSION_Object *sslSession_PyObject;
        sslSession_PyObject = (nassl_SSL_SESSION_Object *)nassl_SSL_SESSION_Type.tp_alloc(&nassl_SSL_SESSION_Type, 0);
        if (sslSession_PyObject == NULL)
        {
            return PyErr_NoMemory();
        }

        sslSession_PyObject->sslSession = sslSession;
        return (PyObject *) sslSession_PyObject;
    }
}


static PyObject* nassl_SSL_set_session(nassl_SSL_Object *self, PyObject *args)
{
    nassl_SSL_SESSION_Object *sslSession_PyObject = NULL;
    if (!PyArg_ParseTuple(args, "O!", &nassl_SSL_SESSION_Type, &sslSession_PyObject))
    {
        return NULL;
    }

    if (SSL_set_session(self->ssl, sslSession_PyObject->sslSession) == 0)
    {
        return raise_OpenSSL_error();
    }

    Py_RETURN_NONE;
}


static PyObject* nassl_SSL_set_options(nassl_SSL_Object *self, PyObject *args)
{
    long sslOption = 0;
    if (!PyArg_ParseTuple(args, "l", &sslOption))
    {
        return NULL;
    }
    return Py_BuildValue("I", SSL_set_options(self->ssl, sslOption));
}


static PyObject* nassl_SSL_set_tlsext_status_type(nassl_SSL_Object *self, PyObject *args)
{
    int statusType = 0;
    if (!PyArg_ParseTuple(args, "I", &statusType)) {
        return NULL;
    }

    SSL_set_tlsext_status_type(self->ssl, statusType);
    Py_RETURN_NONE;
}


// Based on OpenSSL 1.1.1 ssl_print_sigalgs() which is responsible for the
// "Peer signing digest: xxx" output when using openssl s_client to connect to
// an SSL/TLS server.
// See: https://github.com/openssl/openssl/blob/OpenSSL_1_1_1-stable/apps/s_cb.c#L301
static PyObject* nassl_SSL_get_peer_signature_digest(nassl_SSL_Object *self, PyObject *args)
{
    int nid;
    if (SSL_get_peer_signature_nid(self->ssl, &nid) && nid != NID_undef)
    {
        return PyUnicode_FromString(OBJ_nid2sn(nid));
    }
    else
    {
        Py_RETURN_NONE;
    }
}


#ifndef LEGACY_OPENSSL
// Based on OpenSSL 1.1.1 ssl_print_sigalgs() which is responsible for the
// "Peer signature type: xxx" output when using openssl s_client to connect to
// an SSL/TLS server.
// See: https://github.com/openssl/openssl/blob/OpenSSL_1_1_1-stable/apps/s_cb.c#L303
static PyObject* nassl_SSL_get_peer_signature_type(nassl_SSL_Object *self, PyObject *args)
{
    int nid;
    if (SSL_get_peer_signature_type_nid(self->ssl, &nid) && nid != NID_undef)
    {
        return PyUnicode_FromString(OBJ_nid2sn(nid));
    }
    else
    {
        Py_RETURN_NONE;
    }
}
#endif


static PyObject* nassl_SSL_get_tlsext_status_ocsp_resp(nassl_SSL_Object *self, PyObject *args)
{
    OCSP_RESPONSE *ocspResp = NULL;
    nassl_OCSP_RESPONSE_Object *ocspResp_PyObject;
    long ocspRespLen = 0;
    const unsigned char *ocspBuf = NULL;
    STACK_OF(X509) *certChain = NULL, *certChainCpy = NULL;

    // Get the OCSP response
    ocspRespLen = SSL_get_tlsext_status_ocsp_resp(self->ssl, &ocspBuf);
    if (ocspBuf == NULL)
    {
        Py_RETURN_NONE;
    }

    // Try to parse it
    ocspResp = d2i_OCSP_RESPONSE(NULL, &ocspBuf, ocspRespLen);
    if (ocspResp == NULL)
    {
        PyErr_SetString(PyExc_ValueError, "Error parsing the OCSP response. Should not happen ?");
        return NULL;
    }

    // Get the peer's certificate chain
    certChain = SSL_get_peer_cert_chain(self->ssl); // automatically freed
    if (certChain == NULL)
    {
        PyErr_SetString(PyExc_ValueError, "Error getting the peer's certificate chain.");
        return NULL;
    }

    { // Copy each cert of the chain
        int i = 0, certNum = 0;

        certChainCpy = sk_X509_new_null();
        if (certChainCpy == NULL)
        {
            return raise_OpenSSL_error();
        }

        certNum = sk_X509_num(certChain);
        for(i=0; i<certNum; i++)
        {
            X509 *cert = sk_X509_value(certChain, i);
            sk_X509_push(certChainCpy, X509_dup(cert));
        }
    }

    // Return an _nassl.OCSP_RESPONSE object
    ocspResp_PyObject = (nassl_OCSP_RESPONSE_Object *)nassl_OCSP_RESPONSE_Type.tp_alloc(&nassl_OCSP_RESPONSE_Type, 0);
    if (ocspResp_PyObject == NULL)
    {
        return PyErr_NoMemory();
    }

    ocspResp_PyObject->ocspResp = ocspResp;
    ocspResp_PyObject->peerCertChain = certChainCpy;
    return (PyObject *) ocspResp_PyObject;
}


#ifdef LEGACY_OPENSSL
static PyObject* nassl_SSL_state_string_long(nassl_SSL_Object *self, PyObject *args)
{
    // This is only used for fixing SSLv2 connections when connecting to IIS7 (like in the 90s)
    // See SslClient.py for more information
    const char *stateString = SSL_state_string_long(self->ssl);
    return PyUnicode_FromString(stateString);
}


static PyObject* nassl_SSL_get_dh_param(nassl_SSL_Object *self)
{
    DH *dh_srvr;
    SSL_SESSION* session;
    const SSL_CIPHER *cipher = get_tmp_new_cipher(self);
    long alg_k;

    if (cipher == NULL)
    {
        PyErr_SetString(PyExc_TypeError, "Invalid session (unable to get master key derivation algorithm)");
        return NULL;
    }
    alg_k = cipher->algorithm_mkey;
    session = SSL_get1_session(self->ssl);

    if (!(alg_k & (SSL_kEDH|SSL_kDHr|SSL_kDHd)))
    {
        PyErr_SetString(PyExc_TypeError, "Diffie-Hellman is not used in this session");
        return NULL;
    }

    if (session == NULL)
    {
        PyErr_SetString(PyExc_TypeError, "Unable to get Diffie-Hellman parameters");
        return NULL;
    }

    if ((session->sess_cert == NULL) || (session->sess_cert->peer_dh_tmp == NULL))
    {
        PyErr_SetString(PyExc_TypeError, "Unable to get Diffie-Hellman parameters");
        return NULL;
    }
    dh_srvr = session->sess_cert->peer_dh_tmp;

    if ((dh_srvr->p == NULL) ||(dh_srvr->g == NULL) ||(dh_srvr->pub_key == NULL))
    {
        PyErr_SetString(PyExc_TypeError, "Unable to get Diffie-Hellman parameters");
        return NULL;
    }

    return generic_print_to_string((int (*)(BIO *, const void *)) &DHparams_print, dh_srvr);
}


/* mostly ripped from OpenSSL's s3_clnt.c */
static PyObject* nassl_SSL_get_ecdh_param(nassl_SSL_Object *self)
{
    EC_KEY *ec_key;
    SSL_SESSION* session;
    const SSL_CIPHER *cipher = get_tmp_new_cipher(self);
    long alg_k;
    EVP_PKEY *srvr_pub_pkey = NULL;

    if (cipher == NULL)
    {
        PyErr_SetString(PyExc_TypeError, "Invalid session (unable to get master key derivation algorithm)");
        return NULL;
    }
    alg_k = cipher->algorithm_mkey;
    session = SSL_get1_session(self->ssl);

    if (!(alg_k & (SSL_kECDHr|SSL_kECDHe|SSL_kEECDH)))
    {
        PyErr_SetString(PyExc_TypeError, "Elliptic curve Diffie-Hellman is not used in this session");
        return NULL;
    }

    if ((session == NULL) || (session->sess_cert == NULL))
    {
        PyErr_SetString(PyExc_TypeError, "Unable to get ECDH parameters - Invalid session");
        return NULL;
    }


    if (session->sess_cert->peer_ecdh_tmp != NULL)
    {
        ec_key = session->sess_cert->peer_ecdh_tmp;
    }
    else
    {
        /* Get the Server Public Key from Cert */
        srvr_pub_pkey = X509_get_pubkey(session-> \
            sess_cert->peer_pkeys[SSL_PKEY_ECC].x509);
        if ((srvr_pub_pkey == NULL) ||
            (srvr_pub_pkey->type != EVP_PKEY_EC) ||
            (srvr_pub_pkey->pkey.ec == NULL))
        {
            if (srvr_pub_pkey)
                EVP_PKEY_free(srvr_pub_pkey);
            PyErr_SetString(PyExc_TypeError, "Unable to get server public key.");
            return NULL;
        }
        ec_key = srvr_pub_pkey->pkey.ec;
    }

    return generic_print_to_string((int (*)(BIO *, const void *)) &ECParameters_print, ec_key);
}
#endif

static PyObject* nassl_SSL_get_peer_cert_chain(nassl_SSL_Object *self, PyObject *args)
{
    STACK_OF(X509) *certChain = NULL;
    PyObject* certChainPyList = NULL;
    int certChainCount = 0, i = 0;

    // Get the peer's certificate chain
    certChain = SSL_get_peer_cert_chain(self->ssl); // automatically freed
    if (certChain == NULL)
    {
        PyErr_SetString(PyExc_ValueError, "Error getting the peer's certificate chain.");
        return NULL;
    }

    // We'll return a Python list containing each certificate
    certChainCount = sk_X509_num(certChain);
    certChainPyList = PyList_New(certChainCount);
    if (certChainPyList == NULL)
    {
        return PyErr_NoMemory();
    }

    for (i=0; i<certChainCount; i++)
    {
        nassl_X509_Object *x509_Object = NULL;
        // Copy the certificate as the cert chain is freed automatically
        X509 *cert = X509_dup(sk_X509_value(certChain, i));
        if (cert == NULL)
        {
            Py_DECREF(certChainPyList);
            PyErr_SetString(PyExc_ValueError, "Could not extract a certificate. Should not happen ?");
            return NULL;
        }

        // Store the cert in an _nassl.X509 object
        x509_Object = (nassl_X509_Object *)nassl_X509_Type.tp_alloc(&nassl_X509_Type, 0);
        if (x509_Object == NULL)
        {
            Py_DECREF(certChainPyList);
            return PyErr_NoMemory();
        }
        x509_Object->x509 = cert;

        // Add the X509 object to the final list
        PyList_SET_ITEM(certChainPyList, i,  (PyObject *)x509_Object);
    }

    return certChainPyList;
}

static PyObject* nassl_SSL_get_version(nassl_SSL_Object *self, PyObject *args)
{
    const char *version = SSL_get_version(self->ssl);
    return PyUnicode_FromString(version);
}

static PyObject* nassl_SSL_version(nassl_SSL_Object *self, PyObject *args)
{
    return Py_BuildValue("I", SSL_version(self->ssl));
}


static PyMethodDef nassl_SSL_Object_methods[] =
{
    {"set_bio", (PyCFunction)nassl_SSL_set_bio, METH_VARARGS,
     "OpenSSL's SSL_set_bio() on the internal BIO of an _nassl.BIO_Pair object."
    },
    {"set_network_bio_to_free_when_dealloc", (PyCFunction)nassl_SSL_set_network_bio_to_free_when_dealloc, METH_VARARGS,
     "Supply the network BIO paired with the internal BIO in order to have it freed when it's not needed anymore and to avoid memory leaks."
    },
    {"do_handshake", (PyCFunction)nassl_SSL_do_handshake, METH_NOARGS,
     "OpenSSL's SSL_do_handshake()."
    },
    {"set_connect_state", (PyCFunction)nassl_SSL_set_connect_state, METH_NOARGS,
     "OpenSSL's SSL_set_connect_state()."
    },
    {"set_mode", (PyCFunction)nassl_SSL_set_mode, METH_VARARGS,
     "OpenSSL's SSL_set_mode()."
    },
    {"read", (PyCFunction)nassl_SSL_read, METH_VARARGS,
     "OpenSSL's SSL_read()."
    },
    {"write", (PyCFunction)nassl_SSL_write, METH_VARARGS,
     "OpenSSL's SSL_write()."
    },
#ifndef LEGACY_OPENSSL
    {"write_early_data", (PyCFunction)nassl_SSL_write_early_data, METH_VARARGS,
     "OpenSSL's SSL_write_early_data()."
    },
    {"get_early_data_status", (PyCFunction)nassl_SSL_get_early_data_status, METH_VARARGS,
     "OpenSSL's SSL_get_early_data_status()."
    },
    {"get_max_early_data", (PyCFunction)nassl_SSL_get_max_early_data, METH_VARARGS,
     "OpenSSL's SSL_get_max_early_data()."
    },
#endif
    {"pending", (PyCFunction)nassl_SSL_pending, METH_NOARGS,
     "OpenSSL's SSL_pending()."
    },
    {"shutdown", (PyCFunction)nassl_SSL_shutdown, METH_NOARGS,
     "OpenSSL's SSL_shutdown()."
    },
    {"get_secure_renegotiation_support", (PyCFunction)nassl_SSL_get_secure_renegotiation_support, METH_NOARGS,
     "OpenSSL's SSL_get_secure_renegotiation_support()."
    },
    {"get_available_compression_methods", (PyCFunction)nassl_SSL_get_available_compression_methods, METH_NOARGS | METH_STATIC,
     "Recovers the list of all available compression methods by calling SSL_get_compression_methods()."
    },
    {"get_current_compression_method", (PyCFunction)nassl_SSL_get_current_compression_method, METH_NOARGS,
     "Recovers the name of the compression method being used by calling SSL_get_current_compression()."
    },
    {"set_verify", (PyCFunction)nassl_SSL_set_verify, METH_VARARGS,
     "OpenSSL's SSL_set_verify() with a NULL verify_callback."
    },
    {"set_tlsext_host_name", (PyCFunction)nassl_SSL_set_tlsext_host_name, METH_VARARGS,
     "OpenSSL's SSL_set_tlsext_host_name()."
    },
#ifndef LEGACY_OPENSSL
    {"get_peer_signature_digest", (PyCFunction)nassl_SSL_get_peer_signature_digest, METH_NOARGS,
    "Wraps OpenSSL's SSL_get_peer_signature_nid() returning the short name of the digest algorithm used by the peer to sign TLS messages."
    },
    {"get_peer_signature_type", (PyCFunction)nassl_SSL_get_peer_signature_type, METH_NOARGS,
    "Wraps OpenSSL's SSL_get_peer_signature_type_nid() returning the short name of the signature algorithm used by the peer to sign TLS messages."
    },
#endif
    {"get_peer_certificate", (PyCFunction)nassl_SSL_get_peer_certificate, METH_NOARGS,
     "OpenSSL's SSL_get_peer_certificate(). Returns an _nassl.X509 object."
    },
    {"set_cipher_list", (PyCFunction)nassl_SSL_set_cipher_list, METH_VARARGS,
     "OpenSSL's SSL_set_cipher_list()."
    },
#ifndef LEGACY_OPENSSL
    {"set_ciphersuites", (PyCFunction)nassl_SSL_set_ciphersuites, METH_VARARGS,
     "OpenSSL's SSL_set_ciphersuites()."
    },
#endif
    {"get_cipher_list", (PyCFunction)nassl_SSL_get_cipher_list, METH_NOARGS,
     "Returns a list of cipher strings using OpenSSL's SSL_get_cipher_list()."
    },
    {
    "get_cipher_description", (PyCFunction)nassl_SSL_get_cipher_description, METH_VARARGS,
    "Returns the cipher description using OpenSSL's SSL_CIPHER_description(),"
    },
    {"get_cipher_bits", (PyCFunction)nassl_SSL_get_cipher_bits, METH_NOARGS,
     "OpenSSL's SSL_get_cipher_bits()."
    },
    {"get_cipher_name", (PyCFunction)nassl_SSL_get_cipher_name, METH_NOARGS,
     "OpenSSL's SSL_get_cipher_name()."
    },
    {"get_cipher_protocol_id", (PyCFunction)nassl_SSL_get_cipher_protocol_id, METH_NOARGS,
     "OpenSSL's SSL_CIPHER_get_protocol_id()."
    },
    {"get_client_CA_list", (PyCFunction)nassl_SSL_get_client_CA_list, METH_NOARGS,
     "Returns a list of name strings using OpenSSL's SSL_get_client_CA_list() and X509_NAME_oneline()."
    },
    {"get_verify_result", (PyCFunction)nassl_SSL_get_verify_result, METH_NOARGS,
     "OpenSSL's SSL_get_verify_result()."
    },
    {"renegotiate", (PyCFunction)nassl_SSL_renegotiate, METH_NOARGS,
     "OpenSSL's SSL_renegotiate()."
    },
    {"get_session", (PyCFunction)nassl_SSL_get_session, METH_NOARGS,
     "OpenSSL's SSL_get_session(). Returns an _nassl.SSL_SESSION object."
    },
    {"set_session", (PyCFunction)nassl_SSL_set_session, METH_VARARGS,
     "OpenSSL's SSL_set_session(). Argument is an _nassl.SSL_SESSION object."
    },
    {"set_options", (PyCFunction)nassl_SSL_set_options, METH_VARARGS,
     "OpenSSL's SSL_set_options()."
    },
    {"set_tlsext_status_type", (PyCFunction)nassl_SSL_set_tlsext_status_type, METH_VARARGS,
     "OpenSSL's SSL_set_tlsext_status_type()."
    },
    {"get_tlsext_status_ocsp_resp", (PyCFunction)nassl_SSL_get_tlsext_status_ocsp_resp, METH_NOARGS,
     "OpenSSL's SSL_get_tlsext_status_ocsp_resp(). Returns an _nassl.OCSP_RESPONSE object."
    },
#ifdef LEGACY_OPENSSL
    {"state_string_long", (PyCFunction)nassl_SSL_state_string_long, METH_NOARGS,
     "OpenSSL's SSL_state_string_long()."
    },
    {"get_dh_param", (PyCFunction)nassl_SSL_get_dh_param, METH_NOARGS,
     "return Diffie-Hellman parameters as a string."
    },
    {"get_ecdh_param", (PyCFunction)nassl_SSL_get_ecdh_param, METH_NOARGS,
     "return elliptic curve Diffie-Hellman parameters as a string."
    },
#endif
    {"get_peer_cert_chain", (PyCFunction)nassl_SSL_get_peer_cert_chain, METH_NOARGS,
     "OpenSSL's SSL_get_peer_cert_chain(). Returns an array of _nassl.X509 objects."
    },
    {"get_ssl_version_string", (PyCFunction)nassl_SSL_get_version, METH_NOARGS,
     "OpenSSL's SSL_get_version()."
    },
    {"get_ssl_version", (PyCFunction)nassl_SSL_version, METH_NOARGS,
     "OpenSSL's SSL_version()."
    },
    {NULL}  // Sentinel
};
/*

static PyMemberDef nassl_SSL_Object_members[] = {
    {NULL}  // Sentinel
};
*/

static PyTypeObject nassl_SSL_Type =
{
    PyVarObject_HEAD_INIT(NULL, 0)
    "_nassl.SSL",             /*tp_name*/
    sizeof(nassl_SSL_Object),             /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)nassl_SSL_dealloc, /*tp_dealloc*/
    0,                         /*tp_print*/
    0,                         /*tp_getattr*/
    0,                         /*tp_setattr*/
    0,                         /*tp_compare*/
    0,                         /*tp_repr*/
    0,                         /*tp_as_number*/
    0,                         /*tp_as_sequence*/
    0,                         /*tp_as_mapping*/
    0,                         /*tp_hash */
    0,                         /*tp_call*/
    0,                         /*tp_str*/
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /*tp_flags*/
    "SSL objects",           /* tp_doc */
    0,                     /* tp_traverse */
    0,                     /* tp_clear */
    0,                     /* tp_richcompare */
    0,                     /* tp_weaklistoffset */
    0,                     /* tp_iter */
    0,                     /* tp_iternext */
    nassl_SSL_Object_methods,             /* tp_methods */
    0,             /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    0,      /* tp_init */
    0,                         /* tp_alloc */
    nassl_SSL_new,                 /* tp_new */
};



void module_add_SSL(PyObject* m)
{
    nassl_SSL_Type.tp_new = nassl_SSL_new;
    if (PyType_Ready(&nassl_SSL_Type) < 0)
    {
        return;
    }

    Py_INCREF(&nassl_SSL_Type);
    PyModule_AddObject(m, "SSL", (PyObject *)&nassl_SSL_Type);

    // TODO: Add constants
}
