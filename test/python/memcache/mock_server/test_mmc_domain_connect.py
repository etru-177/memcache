#!/usr/bin/env python
# coding=utf-8
# Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
# MemCache_Hybrid is licensed under Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#          http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
# EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
# MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.

"""Domain-URL end-to-end coverage for the DNS-failover connect path.

Covers two regressions that only surface when the meta service URL is a domain
rather than a literal IP:

1. Server side: ``MmcMetaServiceProcess::LoadConfig`` must call
   ``ResolveAllUrlDomains`` so the domain in the config file is resolved to an
   IP before the net/config-store listeners bind. Without it the server fails
   with ``verify NetEngineOptions failed, ip is empty`` or
   ``Failed to start config store server`` and never comes up.

2. Client side: ``MetaNetClient::ResolveAndConnect`` must register the resolved
   ip:port with memfabric's ``SocketAddressParserMgr`` so
   ``AccTcpServer::ConnectToPeerServer``'s ``GetParser(port)`` lookup succeeds.
   Without it the client asserts ``Assert parser != nullptr`` and ``init``
   returns non-zero.

Both regressions are cross-process (the client process never registered the
meta service port), so they cannot be reproduced by the in-process
``meta_net_client_dns_failover_test`` gtest. This test starts the meta service
in a separate process from the client to reproduce them faithfully.

``localhost`` is used as the domain: it is a non-literal-IP host resolved via
``getaddrinfo`` (``inet_pton`` fails on it), so it exercises the same
resolution path as any real domain while staying portable.
"""

import logging
import multiprocessing
import os
import socket
import tempfile
import time
import unittest

import acl

from memcache_hybrid import DistributedObjectStore, LocalConfig, MetaService

DOMAIN = "localhost"
PORT_META = 5821
PORT_STORE = 5822
PORT_HTTP = 5823
DEVICE_ID = 0
META_READY_TIMEOUT_S = 20

META_CONFIG_TEMPLATE = """\
ock.mmc.meta_service_url = tcp://{domain}:{port_meta}
ock.mmc.meta_service.config_store_url = tcp://{domain}:{port_store}
ock.mmc.meta_service.metrics_url = http://{domain}:{port_http}
ock.mmc.log_level = info
ock.mmc.log_output_target = screen
"""


def start_meta_service():
    """Run the meta service in a child process.

    ``MetaService.main`` -> ``MainForPython`` -> ``LoadConfig`` (because no
    ``setup`` was called), exercising the server-side LoadConfig URL resolution.

    ``MMC_META_CONFIG_PATH`` must be set in the parent env *before* spawning so
    the child reads it at .so load time (it is captured by a static initializer
    in ``mmc_env.cpp``); setting it here would be too late.
    """
    try:
        MetaService.main()
    except Exception:  # noqa: BLE001 - surface failure for diagnosis
        logging.exception("MetaService failed")


def wait_port_ready(port, timeout_s):
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        try:
            with socket.create_connection(("localhost", port), timeout=1):
                return True
        except OSError:
            time.sleep(0.5)
    return False


def write_meta_config():
    fd, path = tempfile.mkstemp(prefix="mmc-meta-domain-", suffix=".conf")
    try:
        with os.fdopen(fd, "w") as f:
            f.write(
                META_CONFIG_TEMPLATE.format(
                    domain=DOMAIN, port_meta=PORT_META, port_store=PORT_STORE, port_http=PORT_HTTP
                )
            )
    except Exception:
        os.remove(path)
        raise
    return path


class TestMmcDomainConnect(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if acl.init() != 0:
            raise unittest.SkipTest("acl.init failed; NPU/CANN not available")
        count, ret = acl.rt.get_device_count()
        if ret != 0 or count == 0:
            raise unittest.SkipTest("no NPU device available")
        cls._config_path = write_meta_config()
        # Set before spawning: the child reads MMC_META_CONFIG_PATH at .so load
        # time (static initializer), so it must already be in the env it inherits.
        os.environ["MMC_META_CONFIG_PATH"] = cls._config_path
        cls._ctx = multiprocessing.get_context("spawn")
        cls._meta = cls._ctx.Process(target=start_meta_service)
        cls._meta.start()
        if not wait_port_ready(PORT_META, META_READY_TIMEOUT_S):
            cls._meta.terminate()
            cls._meta.join()
            raise AssertionError(f"meta service did not come up on port {PORT_META}")

    @classmethod
    def tearDownClass(cls):
        if getattr(cls, "_meta", None) is not None:
            if cls._meta.is_alive():
                cls._meta.terminate()
                cls._meta.join()
        path = getattr(cls, "_config_path", None)
        if path and os.path.exists(path):
            os.remove(path)
        os.environ.pop("MMC_META_CONFIG_PATH", None)

    def test_local_service_connects_via_domain(self):
        ret = acl.rt.set_device(DEVICE_ID)
        self.assertEqual(ret, 0, f"set_device {DEVICE_ID} failed, ret={ret}")

        cfg = LocalConfig()
        cfg.meta_service_url = f"tcp://{DOMAIN}:{PORT_META}"
        cfg.config_store_url = f"tcp://{DOMAIN}:{PORT_STORE}"
        cfg.protocol = "device_rdma"
        cfg.dram_size = "1GB"
        cfg.max_dram_size = "1GB"

        store = DistributedObjectStore()
        self.assertEqual(store.setup(cfg), 0, "local service setup failed")
        # init triggers MetaNetClient::Connect to the meta service via the domain
        # URL. Before the fix this asserted "Assert parser != nullptr" and
        # returned non-zero; it must now complete the handshake and return 0.
        self.assertEqual(store.init(DEVICE_ID), 0, "local service init via domain failed")
        store.close()


if __name__ == "__main__":
    unittest.main()
