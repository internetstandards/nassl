#!/usr/bin/env python
# -*- coding: utf-8 -*-
from __future__ import absolute_import
from __future__ import unicode_literals
import unittest
from nassl import _nassl
from nassl import _nassl_legacy
from nassl.ssl_client import SslClient, OpenSslVersionEnum, OpenSslVerifyEnum


class Common_SSL_Tests(unittest.TestCase):

    # To be set in subclasses
    _NASSL_MODULE = None

    @classmethod
    def setUpClass(cls):
        if cls is Common_SSL_Tests:
            raise unittest.SkipTest("Skip Common_SSL_Tests tests, it's a base class")
        super(Common_SSL_Tests, cls).setUpClass()

    def test_new(self):
        self.assertTrue(self._NASSL_MODULE.SSL(self._NASSL_MODULE.SSL_CTX(OpenSslVersionEnum.SSLV23.value)))

    def test_new_bad(self):
        # Invalid None SSL_CTX
        self.assertRaises(TypeError, self._NASSL_MODULE.SSL, (None))

    def test_set_verify(self):
        test_ssl = self._NASSL_MODULE.SSL(self._NASSL_MODULE.SSL_CTX(OpenSslVersionEnum.SSLV23.value))
        self.assertIsNone(test_ssl.set_verify(OpenSslVerifyEnum.PEER.value))

    def test_set_verify_bad(self):
        # Invalid verify constant
        test_ssl = self._NASSL_MODULE.SSL(self._NASSL_MODULE.SSL_CTX(OpenSslVersionEnum.SSLV23.value))
        self.assertRaises(ValueError, test_ssl.set_verify, (1235))

    def test_set_bio(self):
        test_ssl = self._NASSL_MODULE.SSL(self._NASSL_MODULE.SSL_CTX(OpenSslVersionEnum.SSLV23.value))
        test_bio = self._NASSL_MODULE.BIO()
        self.assertIsNone(test_ssl.set_bio(test_bio))

    def test_set_bio_bad(self):
        # Invalid None BIO
        test_ssl = self._NASSL_MODULE.SSL(self._NASSL_MODULE.SSL_CTX(OpenSslVersionEnum.SSLV23.value))
        self.assertRaises(TypeError, test_ssl.set_bio, (None))

    def test_set_connect_state(self):
        test_ssl = self._NASSL_MODULE.SSL(self._NASSL_MODULE.SSL_CTX(OpenSslVersionEnum.SSLV23.value))
        self.assertIsNone(test_ssl.set_connect_state())

    # Can't really unittest a full handshake, read or write
    def test_do_handshake_bad(self):
        # Connection type not set
        test_ssl = self._NASSL_MODULE.SSL(self._NASSL_MODULE.SSL_CTX(OpenSslVersionEnum.SSLV23.value))
        self.assertRaisesRegexp(_nassl.OpenSSLError, 'connection type not set', test_ssl.do_handshake)

    def test_pending(self):
        # No BIO attached to the SSL object
        test_ssl = self._NASSL_MODULE.SSL(self._NASSL_MODULE.SSL_CTX(OpenSslVersionEnum.SSLV23.value))
        self.assertGreaterEqual(test_ssl.pending(), 0)

    def test_get_secure_renegotiation_support(self):
        test_ssl = self._NASSL_MODULE.SSL(self._NASSL_MODULE.SSL_CTX(OpenSslVersionEnum.SSLV23.value))
        self.assertFalse(test_ssl.get_secure_renegotiation_support())

    def test_get_current_compression_method(self):
        test_ssl = self._NASSL_MODULE.SSL(self._NASSL_MODULE.SSL_CTX(OpenSslVersionEnum.SSLV23.value))
        self.assertIsNone(test_ssl.get_current_compression_method())

    def test_get_available_compression_methods_has_zlib(self):
        test_ssl = self._NASSL_MODULE.SSL(self._NASSL_MODULE.SSL_CTX(OpenSslVersionEnum.SSLV23.value))
        self.assertEqual(['zlib compression'], test_ssl.get_available_compression_methods())

    def test_set_tlsext_host_name(self):
        test_ssl = self._NASSL_MODULE.SSL(self._NASSL_MODULE.SSL_CTX(OpenSslVersionEnum.SSLV23.value))
        self.assertIsNone(test_ssl.set_tlsext_host_name('tests'))

    def test_set_tlsext_host_name_bad(self):
        test_ssl = self._NASSL_MODULE.SSL(self._NASSL_MODULE.SSL_CTX(OpenSslVersionEnum.SSLV23.value))
        self.assertRaises(TypeError, test_ssl.set_tlsext_host_name, (None))

    def test_get_peer_certificate_bad(self):
        test_ssl = self._NASSL_MODULE.SSL(self._NASSL_MODULE.SSL_CTX(OpenSslVersionEnum.SSLV23.value))
        self.assertIsNone(test_ssl.get_peer_certificate())

    def test_set_cipher_list(self):
        test_ssl = self._NASSL_MODULE.SSL(self._NASSL_MODULE.SSL_CTX(OpenSslVersionEnum.SSLV23.value))
        self.assertIsNone(test_ssl.set_cipher_list('HIGH'))

    def test_set_cipher_list_bad(self):
        # Invalid cipher string
        test_ssl = self._NASSL_MODULE.SSL(self._NASSL_MODULE.SSL_CTX(OpenSslVersionEnum.SSLV23.value))
        self.assertRaises(_nassl.OpenSSLError, test_ssl.set_cipher_list, 'badcipherstring')

    def test_shutdown_bad(self):
        test_ssl = self._NASSL_MODULE.SSL(self._NASSL_MODULE.SSL_CTX(OpenSslVersionEnum.SSLV23.value))
        self.assertRaisesRegexp(_nassl.OpenSSLError, 'uninitialized', test_ssl.shutdown)

    def test_get_cipher_list(self):
        test_ssl = self._NASSL_MODULE.SSL(self._NASSL_MODULE.SSL_CTX(OpenSslVersionEnum.SSLV23.value))
        self.assertIsNotNone(test_ssl.get_cipher_list())

    def test_get_cipher_name(self):
        test_ssl = self._NASSL_MODULE.SSL(self._NASSL_MODULE.SSL_CTX(OpenSslVersionEnum.SSLV23.value))
        self.assertIsNone(test_ssl.get_cipher_name())

    def test_get_cipher_bits(self):
        test_ssl = self._NASSL_MODULE.SSL(self._NASSL_MODULE.SSL_CTX(OpenSslVersionEnum.SSLV23.value))
        self.assertEqual(test_ssl.get_cipher_bits(), 0)

    def test_get_client_CA_list_bad(self):
        test_ssl = self._NASSL_MODULE.SSL(self._NASSL_MODULE.SSL_CTX(OpenSslVersionEnum.SSLV23.value))
        self.assertEqual([],test_ssl.get_client_CA_list())

    def test_get_verify_result(self):
        test_ssl = self._NASSL_MODULE.SSL(self._NASSL_MODULE.SSL_CTX(OpenSslVersionEnum.SSLV23.value))
        self.assertEqual(0, test_ssl.get_verify_result())

    def test_renegotiate(self):
        test_ssl = self._NASSL_MODULE.SSL(self._NASSL_MODULE.SSL_CTX(OpenSslVersionEnum.SSLV23.value))
        self.assertIsNone(test_ssl.renegotiate())

    def test_get_session(self):
        test_ssl = self._NASSL_MODULE.SSL(self._NASSL_MODULE.SSL_CTX(OpenSslVersionEnum.SSLV23.value))
        self.assertIsNone(test_ssl.get_session())

    def test_set_session_bad(self):
        test_ssl = self._NASSL_MODULE.SSL(self._NASSL_MODULE.SSL_CTX(OpenSslVersionEnum.SSLV23.value))
        self.assertRaises(TypeError, test_ssl.set_session, None)

    def test_set_options_bad(self):
        test_ssl = self._NASSL_MODULE.SSL(self._NASSL_MODULE.SSL_CTX(OpenSslVersionEnum.SSLV23.value))
        self.assertGreaterEqual(test_ssl.set_options(123), 0)

    def test_set_tlsext_status_type(self):
        test_ssl = self._NASSL_MODULE.SSL(self._NASSL_MODULE.SSL_CTX(OpenSslVersionEnum.SSLV23.value))
        self.assertIsNone(test_ssl.set_tlsext_status_type(SslClient._TLSEXT_STATUSTYPE_ocsp))

    def test_get_tlsext_status_type(self):
        test_ssl = self._NASSL_MODULE.SSL(self._NASSL_MODULE.SSL_CTX(OpenSslVersionEnum.SSLV23.value))
        self.assertIsNone(test_ssl.get_tlsext_status_ocsp_resp())


class Modern_SSL_Tests(Common_SSL_Tests):
    _NASSL_MODULE = _nassl


class Legacy_SSL_Tests(Common_SSL_Tests):
    _NASSL_MODULE = _nassl_legacy

    # The following tests don't pass with modern OpenSSL - the API might have changed
    def test_do_handshake_bad_eof(self):
        # No BIO attached to the SSL object
        test_ssl = self._NASSL_MODULE.SSL(self._NASSL_MODULE.SSL_CTX(OpenSslVersionEnum.SSLV23.value))
        test_ssl.set_connect_state()
        self.assertRaisesRegexp(_nassl.SslError, 'An EOF was observed that violates the protocol',
                                test_ssl.do_handshake)

    def test_read_bad(self):
        # No BIO attached to the SSL object
        test_ssl = self._NASSL_MODULE.SSL(self._NASSL_MODULE.SSL_CTX(OpenSslVersionEnum.SSLV23.value))
        test_ssl.set_connect_state()
        self.assertRaisesRegexp(_nassl.OpenSSLError, 'ssl handshake failure', test_ssl.read, (128))

    def test_write_bad(self):
        # No BIO attached to the SSL object
        test_ssl = self._NASSL_MODULE.SSL(self._NASSL_MODULE.SSL_CTX(OpenSslVersionEnum.SSLV23.value))
        test_ssl.set_connect_state()
        self.assertRaisesRegexp(_nassl.OpenSSLError, 'ssl handshake failure', test_ssl.write, 'tests')


def main():
    unittest.main()

if __name__ == '__main__':
    main()
