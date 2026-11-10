"""Synthetic validation of the soak evidence parser; no hardware access."""
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from analyze_soak import analyze
from soak_diagnostics import classify_diagnostic


def fixture():
    lines = ['limits: 2700 s; sample gap <= 90 s; warmup=5 samples; '
             'free and largest drop <= 12288 bytes; largest >= 16384 bytes; '
             'HTTP stack minimum >= 512 bytes, >= 20 samples']
    lines += [f'sample {i+1} elapsed={25+i*60} uptime={60000+i*60000} free=100000 largest=50000' for i in range(45)]
    lines += [f'http ok elapsed={1+i*60}' for i in range(45)]
    lines += [f'http_stack elapsed={2+i*120} min_free=800' for i in range(23)]
    lines += ['PASS samples=45 stack_samples=23 min_http_stack=800 elapsed=2700']
    return '\n'.join(lines)+'\n'


class EvidenceTests(unittest.TestCase):
    def test_valid_log(self):
        report = analyze(fixture())
        self.assertEqual((report['duration_s'],report['samples'],report['min_stack_bytes']),(2700,45,800))

    def test_partial_log_is_not_a_pass(self):
        with self.assertRaises(ValueError): analyze(fixture().replace('PASS samples=45 stack_samples=23 min_http_stack=800 elapsed=2700\n',''))

    def test_stack_limit_enforced(self):
        with self.assertRaises(ValueError): analyze(fixture().replace('min_free=800','min_free=300',1).replace('min_http_stack=800','min_http_stack=300'))

    def test_memory_loss_enforced(self):
        with self.assertRaises(ValueError): analyze(fixture().replace('uptime=420000 free=100000','uptime=420000 free=1000',1))

    def test_uptime_reset_enforced(self):
        with self.assertRaises(ValueError): analyze(fixture().replace('uptime=600000','uptime=10',1))

    def test_reset_before_first_sample_enforced(self):
        with self.assertRaises(ValueError): analyze(fixture().replace('uptime=60000 free=','uptime=10000 free=',1))

    def test_stale_stack_samples_enforced(self):
        with self.assertRaises(ValueError):
            analyze(fixture().replace('http_stack elapsed=2642 min_free=800', 'http_stack elapsed=2000 min_free=800'))

    def test_poll_after_end_rejected(self):
        with self.assertRaises(ValueError):
            analyze(fixture().replace('PASS samples=', 'http ok elapsed=2701\nPASS samples='))

    def test_redacted_uart_diagnostics(self):
        self.assertEqual(classify_diagnostic("[W][wifi_esp32:123]: Disconnected ssid='private' bssid=aa:bb reason='Association Expired'"),
                         ('wifi_disconnect','Association Expired'))
        self.assertEqual(classify_diagnostic('***ERROR*** A stack overflow in task httpd has been detected.'),
                         ('fatal','stack_overflow'))
        self.assertEqual(classify_diagnostic('rst:0xc (SW_CPU_RESET),boot:0x13'),('fatal','device_reset'))
        self.assertIsNone(classify_diagnostic("ssid='private' password='private'"))
        self.assertIsNone(classify_diagnostic('first: unrelated observation'))


if __name__ == '__main__': unittest.main()
