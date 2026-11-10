"""Connectivity watch logic with fakes; never opens a device or a socket."""
import sys
import tempfile
from pathlib import Path
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2]/'tools'))
import bench_connectivity_watch as watcher


class WatchTests(unittest.IsolatedAsyncioTestCase):
    async def test_requires_three_consecutive_successes(self):
        calls=[]
        async def identify(device, expected):
            calls.append(expected)
            if len(calls)==2: raise OSError('synthetic failure')
        async def no_wait(seconds): pass
        with tempfile.TemporaryDirectory() as folder, \
             patch.object(watcher,'LOCAL',Path(folder)), \
             patch.object(watcher,'manifest',return_value={'authorized_actions':['network_test']}), \
             patch.object(watcher,'identify_network',side_effect=identify), \
             patch.object(watcher.asyncio,'sleep',side_effect=no_wait):
            await watcher.watch('synthetic',60)
            log=(Path(folder)/'connectivity.log').read_text()
        self.assertEqual(len(calls),5)
        self.assertIn('unavailable=OSError',log)
        self.assertIn('authenticated=3/3',log)

    async def test_rejects_unauthorized_network_access(self):
        with patch.object(watcher,'manifest',return_value={'authorized_actions':[]}):
            with self.assertRaises(RuntimeError): await watcher.watch('synthetic',60)


if __name__=='__main__': unittest.main()
