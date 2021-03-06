#!/usr/bin/env python
# -*- coding: utf-8 -*-
from __future__ import absolute_import
from __future__ import unicode_literals

import unittest
import socket

from nassl.legacy_ssl_client import LegacySslClient
from nassl.ssl_client import SslClient, OpenSslVerifyEnum
from nassl import _nassl
from nassl import _nassl_legacy


class Common_X509_Tests(unittest.TestCase):

    # To be set in subclasses
    _NASSL_MODULE = None

    @classmethod
    def setUpClass(cls):
        if cls is Common_X509_Tests:
            raise unittest.SkipTest("Skip tests, it's a base class")
        super(Common_X509_Tests, cls).setUpClass()

    def setUp(self):
        pem_cert = """
-----BEGIN CERTIFICATE-----
MIIDdTCCAl2gAwIBAgILBAAAAAABFUtaw5QwDQYJKoZIhvcNAQEFBQAwVzELMAkGA1UEBhMCQkUx
GTAXBgNVBAoTEEdsb2JhbFNpZ24gbnYtc2ExEDAOBgNVBAsTB1Jvb3QgQ0ExGzAZBgNVBAMTEkds
b2JhbFNpZ24gUm9vdCBDQTAeFw05ODA5MDExMjAwMDBaFw0yODAxMjgxMjAwMDBaMFcxCzAJBgNV
BAYTAkJFMRkwFwYDVQQKExBHbG9iYWxTaWduIG52LXNhMRAwDgYDVQQLEwdSb290IENBMRswGQYD
VQQDExJHbG9iYWxTaWduIFJvb3QgQ0EwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQDa
DuaZjc6j40+Kfvvxi4Mla+pIH/EqsLmVEQS98GPR4mdmzxzdzxtIK+6NiY6arymAZavpxy0Sy6sc
THAHoT0KMM0VjU/43dSMUBUc71DuxC73/OlS8pF94G3VNTCOXkNz8kHp1Wrjsok6Vjk4bwY8iGlb
Kk3Fp1S4bInMm/k8yuX9ifUSPJJ4ltbcdG6TRGHRjcdGsnUOhugZitVtbNV4FpWi6cgKOOvyJBNP
c1STE4U6G7weNLWLBYy5d4ux2x8gkasJU26Qzns3dLlwR5EiUWMWea6xrkEmCMgZK9FGqkjWZCrX
gzT/LCrBbBlDSgeF59N89iFo7+ryUp9/k5DPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBRge2YaRQ2XyolQL30EzTSo//z9SzANBgkqhkiG9w0BAQUF
AAOCAQEA1nPnfE920I2/7LqivjTFKDK1fPxsnCwrvQmeU79rXqoRSLblCKOzyj1hTdNGCbM+w6Dj
Y1Ub8rrvrTnhQ7k4o+YviiY776BQVvnGCv04zcQLcFGUl5gE38NflNUVyRRBnMRddWQVDf9VMOyG
j/8N7yy5Y0b2qvzfvGn9LhJIZJrglfCm7ymPAbEVtQwdpf5pLGkkeB6zpxxxYu7KyJesF12KwvhH
hm4qxFYxldBniYUr+WymXUadDKqC5JlR3XC321Y9YeRq4VzW9v493kHMB65jUr9TU/Qr6cf9tveC
X4XSQRjbgbMEHMUfpIBvFSDJ3gyICh3WZlXi/EjJKSZp4A==
-----END CERTIFICATE-----"""

        self.cert = self._NASSL_MODULE.X509(pem_cert)

    def test_from_pem(self):
        self.assertIsNotNone(self.cert.as_text())

    def test_from_pem_bad(self):
        pem_cert = '123123'
        with self.assertRaises(ValueError):
            cert = _nassl.X509(pem_cert)

    def test_get_version(self):
        self.assertIsNotNone(self.cert.get_version())

    def test_get_notBefore(self):
        self.assertIsNotNone(self.cert.get_notBefore())

    def test_get_notAfter(self):
        self.assertIsNotNone(self.cert.get_notAfter())

    def test_digest(self):
        self.assertIsNotNone(self.cert.digest())

    def test_as_pem(self):
        self.assertIsNotNone(self.cert.as_pem())

    def test_get_extensions(self):
        self.assertIsNotNone(self.cert.get_extensions())

    def test_get_issuer_name_entries(self):
        self.assertIsNotNone(self.cert.get_issuer_name_entries())

    def test_get_subject_name_entries(self):
        self.assertIsNotNone(self.cert.get_subject_name_entries())

    def test_get_spki_bytes(self):
        self.assertIsNotNone(self.cert.get_spki_bytes())


class Legacy_X509_Tests(Common_X509_Tests):
    _NASSL_MODULE = _nassl_legacy


class Modern_X509_Tests(Common_X509_Tests):
    _NASSL_MODULE = _nassl


class Common_X509_Tests_Online(unittest.TestCase):

    # To be set in subclasses
    _SSL_CLIENT_CLS = None

    @classmethod
    def setUpClass(cls):
        if cls is Common_X509_Tests_Online:
            raise unittest.SkipTest("Skip tests, it's a base class")
        super(Common_X509_Tests_Online, cls).setUpClass()

    def test(self):
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(5)
        sock.connect(('www.google.com', 443))

        ssl_client = self._SSL_CLIENT_CLS(underlying_socket=sock, ssl_verify=OpenSslVerifyEnum.NONE)
        ssl_client.do_handshake()
        cert = ssl_client.get_peer_certificate()
        ssl_client.shutdown()
        sock.close()

        self.assertIsNotNone(cert.as_text())

    def test_verify_cert_error_string(self):
        self.assertEqual('unspecified certificate verification error', _nassl.X509.verify_cert_error_string(1))


class Legacy_X509_Tests_Online(Common_X509_Tests_Online):
    _SSL_CLIENT_CLS = LegacySslClient


class Modern_X509_Tests_Online(Common_X509_Tests_Online):
    _SSL_CLIENT_CLS = SslClient


def main():
    unittest.main()

if __name__ == '__main__':
    main()
