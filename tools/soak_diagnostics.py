"""Classify diagnostic UART lines without retaining identifiers or secrets."""
import re


def classify_diagnostic(line):
    if re.search(r'stack overflow',line,re.IGNORECASE): return ('fatal','stack_overflow')
    if re.search(r'Guru Meditation|panic_abort|(?:^|\s)rst:|Rebooting\.\.\.',line,re.IGNORECASE):
        return ('fatal','device_reset')
    if 'wifi_esp32' in line and 'Disconnected' in line:
        match=re.search(r"reason='([A-Za-z0-9 _-]{1,64})'",line)
        return ('wifi_disconnect',match.group(1) if match else 'unknown')
    if 'wifi:' in line and 'Connected' in line: return ('wifi_connected','connected')
    return None
