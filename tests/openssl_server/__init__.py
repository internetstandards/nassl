import os
import shlex

import subprocess
from enum import Enum

import logging
import time
from queue import Queue, Empty
from threading import Thread
from typing import Optional

from build_tasks import ModernOpenSslBuildConfig, LegacyOpenSslBuildConfig, CURRENT_PLATFORM, SupportedPlatformEnum


class ClientAuthConfigEnum(Enum):
    """Whether the server asked for client authentication.
    """
    DISABLED = 1
    OPTIONAL = 2
    REQUIRED = 3


class OpenSslServerVersion(Enum):
    LEGACY = 1
    MODERN = 2


class _OpenSslServerIOManager:
    """Thread to log all output from s_server and reply to incoming connections.
    """

    def __init__(self, s_server_stdout, s_server_stdin):
        self.s_server_stdout = s_server_stdout
        self.s_server_stdin = s_server_stdin
        self.is_server_ready = False

        def read_and_log_and_reply():
            while True:
                s_server_out = self.s_server_stdout.readline()
                if s_server_out:
                    logging.warning(f's_server output: {s_server_out}')

                    if b'ACCEPT' in s_server_out:
                        # S_server is ready to receive connections
                        self.is_server_ready = True

                    if OpenSslServer.HELLO_MSG in s_server_out:
                        # When receiving the special message, we want s_server to reply
                        self.s_server_stdin.write(b'Hey there')
                        self.s_server_stdin.flush()
                else:
                    break

        self.thread = Thread(target=read_and_log_and_reply, args=())
        self.thread.daemon = True
        self.thread.start()

    def close(self):
        pass
        # TODO(AD): This hangs on Linux; figure it out
        #self.s_server_stdout.close()
        #self.s_server_stdin.close()
        #self.thread.join()


class OpenSslServer:
    """A wrapper around OpenSSL's s_server CLI.
    """

    _SERVER_CERT_PATH = os.path.join(os.path.dirname(__file__), 'server-self-signed-cert.pem')
    _SERVER_KEY_PATH = os.path.join(os.path.dirname(__file__), 'server-self-signed-key.pem')

    _AVAILABLE_LOCAL_PORTS = set(range(8110, 8150))

    _S_SERVER_CMD = '{openssl} s_server -cert {server_cert} -key {server_key} -accept {port} {extra_args}'
    _S_SERVER_WITH_OPTIONAL_CLIENT_AUTH_CMD = _S_SERVER_CMD + ' -verify {client_ca}'
    _S_SERVER_WITH_REQUIRED_CLIENT_AUTH_CMD = _S_SERVER_CMD + ' -Verify {client_ca}'

    # Client authentication - files generated using https://gist.github.com/nabla-c0d3/c2c5799a84a4867e5cbae42a5c43f89a
    _CLIENT_CA_PATH = os.path.join(os.path.dirname(__file__), 'client-ca.pem')
    _CLIENT_CERT_PATH = os.path.join(os.path.dirname(__file__), 'client-cert.pem')
    _CLIENT_KEY_PATH = os.path.join(os.path.dirname(__file__), 'client-key.pem')

    # A special message clients can send to get a reply from s_server
    HELLO_MSG = b'Hello\r\n'

    @classmethod
    def get_client_certificate_path(cls) -> str:
        return cls._CLIENT_CERT_PATH

    @classmethod
    def get_client_key_path(cls) -> str:
        return cls._CLIENT_KEY_PATH

    def __init__(
            self,
            server_version: OpenSslServerVersion,
            client_auth_config: ClientAuthConfigEnum = ClientAuthConfigEnum.DISABLED,
            max_early_data: Optional[int] = None,

    ) -> None:
        # Get the path to the OpenSSL executable from the build tasks
        if server_version == OpenSslServerVersion.MODERN:
            openssl_path = ModernOpenSslBuildConfig(CURRENT_PLATFORM).exe_path
            extra_args = '-early_data'
            if max_early_data is not None:
                extra_args += f' -max_early_data {max_early_data}'

        else:
            openssl_path = LegacyOpenSslBuildConfig(CURRENT_PLATFORM).exe_path
            extra_args = ''

            if max_early_data:
                raise ValueError('Cannot enable early data with legacy OpenSSL')

        self.hostname = 'localhost'
        self.ip_address = '127.0.0.1'

        # Retrieve one of the available local ports; set.pop() is thread safe
        self.port = self._AVAILABLE_LOCAL_PORTS.pop()
        self._process = None

        if client_auth_config == ClientAuthConfigEnum.DISABLED:
            self._command_line = self._S_SERVER_CMD.format(
                openssl=openssl_path,
                server_key=self._SERVER_KEY_PATH,
                server_cert=self._SERVER_CERT_PATH,
                port=self.port,
                extra_args=extra_args,
            )
        elif client_auth_config == ClientAuthConfigEnum.OPTIONAL:
            self._command_line = self._S_SERVER_WITH_OPTIONAL_CLIENT_AUTH_CMD.format(
                openssl=openssl_path,
                server_key=self._SERVER_KEY_PATH,
                server_cert=self._SERVER_CERT_PATH,
                port=self.port,
                client_ca=self._CLIENT_CA_PATH,
                extra_args=extra_args,
            )
        elif client_auth_config == ClientAuthConfigEnum.REQUIRED:
            self._command_line = self._S_SERVER_WITH_REQUIRED_CLIENT_AUTH_CMD.format(
                openssl=openssl_path,
                server_key=self._SERVER_KEY_PATH,
                server_cert=self._SERVER_CERT_PATH,
                port=self.port,
                client_ca=self._CLIENT_CA_PATH,
                extra_args=extra_args,
            )

    def __enter__(self):
        logging.warning('Running s_server: "{}"'.format(self._command_line))
        if CURRENT_PLATFORM in [SupportedPlatformEnum.WINDOWS_64, SupportedPlatformEnum.WINDOWS_32]:
            args = self._command_line
        else:
            args = shlex.split(self._command_line)
        self._process = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self._server_io_manager = _OpenSslServerIOManager(self._process.stdout, self._process.stdin)

        # Block until s_server is ready to accept requests
        while not self._server_io_manager.is_server_ready:
            time.sleep(1)
            if self._process.poll() is not None:
                # s_server has terminated early
                raise RuntimeError('Could not start s_server')

        return self

    def __exit__(self, *args):
        self._server_io_manager.close()
        if self._process and self._process.poll() is None:
            self._process.terminate()
            self._process.wait()
        self._process = None

        # Free the port that was used; not thread safe but should be fine
        self._AVAILABLE_LOCAL_PORTS.add(self.port)
        return False
